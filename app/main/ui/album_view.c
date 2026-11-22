// SPDX-License-Identifier: LGPL-2.1-or-later
/* Slideshow view: one full-screen photo from the album cache, the next one fetched ahead from the card, a short
 * fade between them. No decoding here: frames are ready-made 360x360 RGB565 (album_svc). */
#include "album_view.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "pc/pc_album.h"
#include "pc/pc_lowmem.h"
#include "album_svc.h"
#include "cfg.h"
#include "font.h"
#include "lowmem.h"
#include "ui.h"
#include "ui_kit.h"

#define SLOTS 2
#define FADE_MS 500
typedef struct {
    bool alive;
    lv_obj_t *root, *img[2];
    lv_image_dsc_t dsc[2];
    uint16_t *px[2], *next_px;
    int cur;                                 /* image on show, -1 none yet */
    uint32_t req;                            /* pending load, 0 none */
    int pending_idx;
    lv_timer_t *t;
    pc_album_seq_t seq;
    uint16_t *order;
    int seq_n;
    bool seq_shuffle;
    int64_t shown_ms;
    bool skip;                               /* album_view_next: advance at once */
} view_t;
static view_t s_v[SLOTS];
static uint32_t s_shown, s_fades;

/* ---- settings ---- */
bool album_now_mode(void) { return !strcmp(cfg_str("now_view", "cover"), "album"); }
bool now_view_viz(void) { return !strcmp(cfg_str("now_view", "cover"), "viz"); }
void album_now_mode_step(int d)                 /* cover -> album -> viz -> cover; any other value counts as cover */
{
    static const char *const V[3] = { "cover", "album", "viz" };
    int i = album_now_mode() ? 1 : now_view_viz() ? 2 : 0;
    cfg_set_str("now_view", V[((i + d) % 3 + 3) % 3]);
}
const char *album_now_label(void) { return album_now_mode() ? T(S_PHOTOS) /* Photos */ : now_view_viz() ? T(S_SPECTRUM) /* Spectrum */ : T(S_COVER) /* Cover */; }
static const int SECS[] = { 10, 30, 60, 300 };
int album_interval_s(void)
{
    int s = cfg_int("album_sec", 30);
    for (int i = 0; i < 4; i++) if (SECS[i] == s) return s;
    return 30;
}
bool album_shuffle(void) { return strcmp(cfg_str("album_order", "shuffle"), "order"); }
void album_order_next(void) { cfg_set_str("album_order", album_shuffle() ? "order" : "shuffle"); }
const char *album_order_label(void) { return album_shuffle() ? T(S_SHUFFLE) /* Shuffle */ : T(S_IN_ORDER_2) /* In order */; }

/* ---- showing ---- */
static void fade_cb(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }
static void fade_done(lv_anim_t *a)
{
    view_t *v = lv_anim_get_user_data(a);
    if (!v->alive) return;
    lv_obj_add_flag(v->img[1 - v->cur], LV_OBJ_FLAG_HIDDEN);   /* the old photo, fully covered now */
}
static void show(view_t *v, uint16_t *px)
{
    int k = v->cur < 0 ? 0 : 1 - v->cur;
    lv_obj_t *im = v->img[k];
    lv_anim_delete(v->img[0], fade_cb);
    lv_anim_delete(v->img[1], fade_cb);
    lv_obj_set_style_opa(v->img[1 - k], LV_OPA_COVER, 0);      /* a fade cut short: what shows now is the old one */
    if (v->px[k]) { lv_image_cache_drop(&v->dsc[k]); heap_caps_free(v->px[k]); }
    v->px[k] = px;
    v->dsc[k] = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = ALBUM_PX, .h = ALBUM_PX, .stride = ALBUM_PX * 2 },
                                  .data_size = ALBUM_PX * ALBUM_PX * 2, .data = (const uint8_t *)px };
    lv_image_set_src(im, &v->dsc[k]);
    lv_obj_move_foreground(im);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_HIDDEN);
    bool first = v->cur < 0;
    v->cur = k;
    v->shown_ms = esp_timer_get_time() / 1000;
    s_shown++;
    if (first || !cfg_int("album_fade", 1) || lowmem_level() >= PC_LOWMEM_STILL) {   /* a cut */
        lv_obj_set_style_opa(im, LV_OPA_COVER, 0);
        lv_obj_add_flag(v->img[1 - k], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    s_fades++;
    lv_obj_set_style_opa(im, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, im);
    lv_anim_set_exec_cb(&a, fade_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_user_data(&a, v);
    lv_anim_set_completed_cb(&a, fade_done);
    lv_anim_start(&a);
}

static view_t *by_req(uint32_t req)
{
    for (int i = 0; i < SLOTS; i++) if (s_v[i].alive && s_v[i].req == req) return &s_v[i];
    return NULL;
}
static void loaded(uint32_t req, uint16_t *px, void *u)
{
    (void)u;
    view_t *v = by_req(req);
    if (!v) { heap_caps_free(px); return; }   /* the view went away meanwhile */
    v->req = 0;
    if (!px) return;                          /* unreadable: the next tick asks for another */
    if (v->cur < 0) show(v, px);              /* the first photo: at once */
    else { heap_caps_free(v->next_px); v->next_px = px; }
}

static int next_index(view_t *v)
{
    int n = album_svc_ready();
    bool sh = album_shuffle();
    if (n != v->seq_n || sh != v->seq_shuffle) {          /* more photos ready, or the order changed */
        heap_caps_free(v->order);
        v->order = heap_caps_malloc((size_t)(n ? n : 1) * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!v->order) { v->seq_n = 0; return -1; }
        pc_album_seq_init(&v->seq, v->order, n, sh, esp_random());
        v->seq_n = n;
        v->seq_shuffle = sh;
    }
    return pc_album_seq_next(&v->seq);
}

static bool parent_visible(view_t *v)
{
    for (lv_obj_t *o = v->root; o; o = lv_obj_get_parent(o)) if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}

static void tick(lv_timer_t *t)
{
    view_t *v = lv_timer_get_user_data(t);
    if (!v->alive || ui_visual_paused(NULL) || !parent_visible(v)) return;   /* dark, a popup ring over the photos or covered: nothing loads, no photo change */
    if (lowmem_level() >= PC_LOWMEM_STILL && v->cur >= 0) return;     /* low memory: the photo stays */
    if (album_svc_ready() <= 0) return;
    int64_t now = esp_timer_get_time() / 1000;
    bool due = v->cur < 0 || v->skip || now - v->shown_ms >= (int64_t)album_interval_s() * 1000;
    if (due && v->next_px) {
        uint16_t *px = v->next_px;
        v->next_px = NULL;
        v->skip = false;
        show(v, px);
    }
    if (!v->req && !v->next_px) {                         /* fetch the next one ahead */
        int idx = next_index(v);
        if (idx >= 0) { v->pending_idx = idx; v->req = album_svc_load(idx, loaded, NULL); }
    }
}

static void deleted(lv_event_t *e)
{
    view_t *v = lv_event_get_user_data(e);
    v->alive = false;
    lv_anim_delete(v->img[0], fade_cb);
    lv_anim_delete(v->img[1], fade_cb);
    for (int i = 0; i < 2; i++) { if (v->px[i]) { lv_image_cache_drop(&v->dsc[i]); heap_caps_free(v->px[i]); } }
    heap_caps_free(v->next_px);
    heap_caps_free(v->order);
    memset(v, 0, sizeof *v);
}

lv_obj_t *album_view_create(lv_obj_t *parent)
{
    view_t *v = NULL;
    for (int i = 0; i < SLOTS && !v; i++) if (!s_v[i].alive) v = &s_v[i];
    if (!v) return NULL;
    memset(v, 0, sizeof *v);
    v->alive = true;
    v->cur = -1;
    v->root = lv_obj_create(parent);
    lv_obj_remove_style_all(v->root);
    lv_obj_set_size(v->root, ALBUM_PX, ALBUM_PX);
    lv_obj_center(v->root);
    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 2; i++) {
        v->img[i] = lv_image_create(v->root);
        lv_obj_set_pos(v->img[i], 0, 0);
        lv_obj_remove_flag(v->img[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(v->img[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_event_cb(v->root, deleted, LV_EVENT_DELETE, v);
    v->t = ui_timer_bind(v->root, tick, 500, v);        /* deleted with v->root, after deleted() above */
    album_svc_wake();                                     /* look for new photos, build what is missing */
    if (v->t) tick(v->t);                                 /* NULL: no timer, nothing will ever load (see ui_kit.h) */
    return v->root;
}

bool album_view_showing(lv_obj_t *view)
{
    for (int i = 0; i < SLOTS; i++) if (s_v[i].alive && s_v[i].root == view) return s_v[i].cur >= 0;
    return false;
}

void album_view_next(void) { for (int i = 0; i < SLOTS; i++) if (s_v[i].alive && s_v[i].t) { s_v[i].skip = true; tick(s_v[i].t); } }

void album_view_stat(void)
{
    printf("APP albumview shown=%lu fades=%lu views=%d%d\n", (unsigned long)s_shown, (unsigned long)s_fades, s_v[0].alive, s_v[1].alive);
}
