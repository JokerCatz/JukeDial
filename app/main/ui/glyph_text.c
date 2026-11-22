// SPDX-License-Identifier: LGPL-2.1-or-later
#include "glyph_text.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "misc/cache/instance/lv_image_cache.h"   /* not in lvgl.h; harmless with LV_CACHE_DEF_SIZE 0 */
#include "psram_alloc.h"
#include "ui_theme.h"

/* ---------- packs ---------- */
struct gt_pack {
    struct gt_pack *next;   /* the open packs: uploads must not replace or delete a file in use (FATFS has no locks here) */
    char path[128];
    FILE *f;
    pc_reader_t r;      /* pc_gp keeps a pointer to it: it lives here, in PSRAM, until close */
    pc_gp_t *gp;
};

static const pc_gp_alloc_t PSRAM = { psram_malloc, psram_free };
static gt_pack_t *s_open;

bool gt_pack_in_use(const char *path)
{
    for (const gt_pack_t *p = s_open; p; p = p->next)
        if (!strcmp(p->path, path)) return true;
    return false;
}

int gt_pack_open_count(void)
{
    int n = 0;
    for (const gt_pack_t *p = s_open; p; p = p->next) n++;
    return n;
}

gt_pack_t *gt_pack_open(const char *path)
{
    gt_pack_t *p = psram_calloc(1, sizeof *p);
    if (!p) return NULL;
    int rc = -ENOENT;
    if (strlen(path) >= sizeof p->path) { psram_free(p); return NULL; }
    strcpy(p->path, path);
    p->f = fopen(path, "rb");
    if (p->f) {
        setvbuf(p->f, NULL, _IONBF, 0);              /* no stdio buffer in internal RAM; reads are whole bitmaps */
        rc = pc_reader_file(&p->r, p->f) ? -EIO : pc_gp_open(&p->r, &PSRAM, &p->gp);
    }
    if (rc) {
        printf("APP gt open %s rc=%d\n", path, rc);
        if (p->f) fclose(p->f);
        psram_free(p);
        return NULL;
    }
    p->next = s_open;
    s_open = p;
    printf("APP gt open %s glyphs=%u nodes=%u index=%u B\n", path, (unsigned)pc_gp_glyph_count(p->gp), (unsigned)pc_gp_node_count(p->gp), (unsigned)pc_gp_block_bytes(p->gp));
    return p;
}

gt_pack_t *gt_pack_open_mem(const char *name, const void *data, uint32_t len)
{
    gt_pack_t *p = psram_calloc(1, sizeof *p);
    if (!p) return NULL;
    snprintf(p->path, sizeof p->path, "%s", name);
    pc_reader_mem(&p->r, data, len);
    int rc = pc_gp_open(&p->r, &PSRAM, &p->gp);
    if (rc) { printf("APP gt open %s rc=%d\n", name, rc); psram_free(p); return NULL; }
    p->next = s_open;
    s_open = p;
    printf("APP gt open %s glyphs=%u nodes=%u index=%u B\n", name, (unsigned)pc_gp_glyph_count(p->gp), (unsigned)pc_gp_node_count(p->gp), (unsigned)pc_gp_block_bytes(p->gp));
    return p;
}


void gt_pack_close(gt_pack_t *p)
{
    if (!p) return;
    for (gt_pack_t **q = &s_open; *q; q = &(*q)->next)
        if (*q == p) { *q = p->next; break; }
    pc_gp_close(p->gp);
    if (p->f) fclose(p->f);
    psram_free(p);
}

const pc_gp_t *gt_pack_gp(const gt_pack_t *p) { return p ? p->gp : NULL; }

/* ---------- text widget ---------- */
enum { MAX_LINES = 8 };
typedef struct { lv_image_dsc_t dsc; int16_t x, y; } ent_t;
typedef struct {
    gt_pack_t *pack;
    lv_color_t color;
    int px, max_lines;
    int32_t laid_w, widest;
    uint32_t nids, nent;
    uint16_t *ids;      /* a copy, to lay out again when the width changes */
    ent_t *ent;         /* then the bitmaps; all in this one PSRAM block */
} st_t;

static void drop(st_t *s)
{
    if (!s) return;
    for (uint32_t i = 0; i < s->nent; i++) lv_image_cache_drop(&s->ent[i].dsc);
    psram_free(s);
}

static void draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    st_t *s = lv_obj_get_user_data(obj);
    if (!s || !s->nent) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_content_coords(obj, &c);
    lv_opa_t opa = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    for (uint32_t i = 0; i < s->nent; i++) {
        const ent_t *g = &s->ent[i];
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = &g->dsc;
        d.opa = opa;
        if (g->dsc.header.cf == LV_COLOR_FORMAT_A8) { d.recolor = s->color; d.recolor_opa = LV_OPA_COVER; }   /* A8: the mask takes this colour */
        lv_area_t a = { c.x1 + g->x, c.y1 + g->y, c.x1 + g->x + (int32_t)g->dsc.header.w - 1, c.y1 + g->y + (int32_t)g->dsc.header.h - 1 };
        lv_draw_image(layer, &d, &a);
    }
}

static void on_event(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    st_t *s = lv_obj_get_user_data(obj);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DRAW_MAIN) draw(e);
    else if (code == LV_EVENT_DELETE) { drop(s); lv_obj_set_user_data(obj, NULL); }
    else if (code == LV_EVENT_SIZE_CHANGED && s && s->pack && lv_obj_get_content_width(obj) != s->laid_w)
        glyph_text_set(obj, s->pack, s->ids, s->nids, s->px, s->max_lines);   /* copies ids before freeing the old block */
}

lv_obj_t *glyph_text_create(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o, on_event, LV_EVENT_ALL, NULL);
    lv_obj_set_size(o, lv_pct(100), 0);
    st_t *s = psram_calloc(1, sizeof *s);
    if (s) s->color = UI_FG;
    lv_obj_set_user_data(o, s);
    return o;
}

static lv_color_format_t cf_of(uint8_t fmt) { return fmt == PC_GP_FMT_A8 ? LV_COLOR_FORMAT_A8 : LV_COLOR_FORMAT_RGB565A8; }

static void fill_dsc(lv_image_dsc_t *d, uint8_t fmt, uint32_t w, uint32_t h, const uint8_t *px, uint32_t bytes)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = cf_of(fmt);
    d->header.w = w;
    d->header.h = h;
    d->header.stride = fmt == PC_GP_FMT_A8 ? w : w * 2;   /* RGB565A8: the stride of the colour plane */
    d->data_size = bytes;
    d->data = px;
}

int glyph_text_set(lv_obj_t *obj, gt_pack_t *pack, const uint16_t *ids, uint32_t n, int px, int max_lines)
{
    st_t *old = lv_obj_get_user_data(obj);
    lv_color_t color = old ? old->color : UI_FG;
    if (!pack || (n && !ids) || max_lines < 1) return -EINVAL;
    if (max_lines > MAX_LINES) max_lines = MAX_LINES;
    const pc_gp_t *gp = pack->gp;
    int size = pc_gp_size_for_px(gp, px);
    pc_gp_size_t sz;
    pc_gp_size(gp, size, &sz);
    int32_t width = lv_obj_get_content_width(obj);
    pc_gp_line_t L[MAX_LINES];
    int nl = pc_gp_layout(gp, size, ids, n, width, max_lines, L);
    if (nl < 0) return nl;
    uint16_t ell = pc_gp_ellipsis(gp);
    /* pass 1: how many bitmaps and bytes */
    uint32_t nent = 0, bytes = 0;
    for (int k = 0; k < nl; k++)
        for (uint32_t i = 0; i <= L[k].len; i++) {
            uint16_t id = i < L[k].len ? ids[L[k].start + i] : (L[k].ellipsis ? ell : PC_GP_NONE);
            pc_gp_glyph_t g;
            if (pc_gp_glyph(gp, size, id, &g) || !g.bytes) continue;
            nent++;
            bytes += (g.bytes + 3) & ~3u;
        }
    size_t ids_b = ((size_t)n * 2 + 7) & ~(size_t)7, ent_b = (size_t)nent * sizeof(ent_t);
    st_t *s = psram_calloc(1, sizeof *s + ids_b + ent_b + bytes);
    if (!s) return -ENOMEM;
    *s = (st_t){ .pack = pack, .color = color, .px = px, .max_lines = max_lines, .laid_w = width, .nids = n };
    s->ids = (uint16_t *)(s + 1);
    s->ent = (ent_t *)((uint8_t *)s->ids + ids_b);
    if (n) memcpy(s->ids, ids, (size_t)n * 2);
    uint8_t *pix = (uint8_t *)(s->ent + nent);
    /* pass 2: positions and bitmaps */
    int rc = 0;
    for (int k = 0; k < nl && !rc; k++) {
        int32_t pen = (width - L[k].width) / 2, top = k * sz.line_h;
        if (L[k].width > s->widest) s->widest = L[k].width;
        for (uint32_t i = 0; i <= L[k].len && !rc; i++) {
            uint16_t id = i < L[k].len ? s->ids[L[k].start + i] : (L[k].ellipsis ? ell : PC_GP_NONE);
            pc_gp_glyph_t g;
            if (pc_gp_glyph(gp, size, id, &g)) continue;
            if (g.bytes) {
                ent_t *e = &s->ent[s->nent];
                int got = pc_gp_glyph_read(gp, size, id, pix, g.bytes);
                if (got != (int)g.bytes) { rc = got < 0 ? got : -EIO; break; }
                fill_dsc(&e->dsc, g.fmt, g.w, g.h, pix, g.bytes);
                e->x = (int16_t)(pen + g.x);
                e->y = (int16_t)(top + g.y);
                pix += (g.bytes + 3) & ~3u;
                s->nent++;
            }
            pen += g.adv;
        }
    }
    if (rc) { s->nent = 0; s->nids = 0; s->pack = NULL; }
    drop(old);
    lv_obj_set_user_data(obj, s);
    lv_obj_set_height(obj, (rc ? 0 : nl * sz.line_h) + lv_obj_get_style_pad_top(obj, LV_PART_MAIN) + lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN));
    lv_obj_invalidate(obj);
    return rc;
}

int glyph_text_set_node(lv_obj_t *obj, gt_pack_t *pack, uint32_t node, int px, int max_lines)
{
    pc_gp_node_t nd;
    if (!pack || pc_gp_node(pack->gp, node, &nd)) return -EINVAL;
    return glyph_text_set(obj, pack, nd.text, nd.len, px, max_lines);
}

void glyph_text_set_color(lv_obj_t *obj, lv_color_t c)
{
    st_t *s = lv_obj_get_user_data(obj);
    if (s) { s->color = c; lv_obj_invalidate(obj); }
}

void glyph_text_clear(lv_obj_t *obj)
{
    st_t *s = lv_obj_get_user_data(obj);
    if (!s) return;
    for (uint32_t i = 0; i < s->nent; i++) lv_image_cache_drop(&s->ent[i].dsc);
    s->nent = s->nids = 0;
    s->widest = 0;
    s->pack = NULL;                                     /* nothing to lay out again */
    lv_obj_set_height(obj, lv_obj_get_style_pad_top(obj, LV_PART_MAIN) + lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN));
    lv_obj_invalidate(obj);
}


