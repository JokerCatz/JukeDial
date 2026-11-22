// SPDX-License-Identifier: LGPL-2.1-or-later
/* The instrument's drum pads (header inst_pads.h). Moved from inst_ui.c (T11): the masks, the draw, the press hit test and the fade are the old ones. */
#include "inst_pads.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "font.h"
#include "inst_bars.h"
#include "misc/lv_area_private.h"
#include "pc/pc_ledfx.h"
#include "pc/pc_ledring.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

/* radii scale with the design circle (360: 62..158, the sectors of pc_pad_hit) */
#define PAD_RI    (UI_DESIGN_D * 62 / 360)
#define PAD_RO    (UI_DESIGN_D * 158 / 360)
#define PAD_LBL_R (UI_DESIGN_D * 124 / 360)  /* label centres: out where a sector is widest (110 put the two lines of neighbouring labels in one row, 20 px apart: "hat" | "open") */
#define PAD_LBL_W UI_PADS_LBL_W                         /* longest line: about the arc of a sector at the label radius; longer text is cut with dots */
#if UI_ROUND
#define PAD_HUB_D (PAD_RI * 2 - 8)
#else
#define PAD_HUB_D UI_PADS_HUB_D
#endif
#define PAD_GAP   1.5f                       /* degrees each side */
#define PAD_C     (UI_DESIGN_D / 2)

static lv_image_dsc_t s_img[PC_PADS];        /* the masks: made once, kept for the mode */
static lv_area_t s_area[PC_PADS];
static uint32_t s_rgb[PC_PADS], s_col[PC_PADS];
static uint8_t s_lvl[PC_PADS], s_drawn[PC_PADS];
static lv_obj_t *s_obj, *s_lbl[PC_PADS];
static void (*s_down)(int), (*s_up)(int);
static uint8_t s_held;                        /* pads down now (bit per pad) */
static int8_t s_inject = -1;                  /* the test hook's pad while its event runs */

#define PAD_BOX (UI_ROUND ? UI_DESIGN_D : UI_CONTENT_D)   /* the pads object: the design circle (round) or the whole panel (square) */
static uint32_t pad_color(int p, int q) { return pc_fx_mix(ui_theme()->panel2, s_rgb[p], (q ? q + 63 : 40) / 255.0f); }

#if !UI_ROUND
/* The square panel's pads: a 3 x 3 grid of rounded cells, the hub in the middle cell, the 8 pads clockwise round it from the lower left (p0 BL, p1 L, p2 TL, p3 T,
 * p4 TR, p5 R, p6 BR, p7 B: the same neighbours as the round ring's sectors). */
#define SQ_M    UI_PADS_M
#define SQ_GAP  UI_PADS_GAP
#define SQ_CELL ((UI_CONTENT_D - 2 * SQ_M - 2 * SQ_GAP) / 3)
#define SQ_R    UI_RGRID_RADIUS
#define SQ_RC   (UI_RIM_SQ_INSET + UI_RIM_SQ_RR - SQ_M)     /* a corner pad's outer corner: concentric with the LED border's corner arc, so the pad stays inside it (SQ2) */
static const int8_t SQ_COL[PC_PADS] = { 0, 0, 0, 1, 2, 2, 2, 1 }, SQ_ROW[PC_PADS] = { 2, 1, 0, 0, 0, 1, 2, 2 };
static void sq_cell(int p, lv_area_t *a)
{
    a->x1 = SQ_M + SQ_COL[p] * (SQ_CELL + SQ_GAP); a->y1 = SQ_M + SQ_ROW[p] * (SQ_CELL + SQ_GAP);
    a->x2 = a->x1 + SQ_CELL - 1; a->y2 = a->y1 + SQ_CELL - 1;
}
static int sq_hit(int x, int y)
{
    for (int p = 0; p < PC_PADS; p++) {
        lv_area_t a;
        sq_cell(p, &a);
        if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return p;
    }
    return -1;
}
static void pad_mask(int p)
{
    lv_area_t *a = &s_area[p];
    sq_cell(p, a);
    int w = lv_area_get_width(a), h = lv_area_get_height(a);
    uint8_t *m = heap_caps_calloc(1, (size_t)(w * h), MALLOC_CAP_SPIRAM);
    if (!m) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const bool left = x < w / 2, top = y < h / 2, outer = (SQ_COL[p] == 1 ? -1 : SQ_COL[p] == 0) == left && (SQ_ROW[p] == 1 ? -1 : SQ_ROW[p] == 0) == top;   /* the corner nearest the panel's corner */
            const float rr = outer ? SQ_RC : SQ_R;
            float qx = fabsf((float)x + 0.5f - w / 2.0f) - (w / 2.0f - rr), qy = fabsf((float)y + 0.5f - h / 2.0f) - (h / 2.0f - rr);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, d = sqrtf(ox * ox + oy * oy) + LV_MIN(LV_MAX(qx, qy), 0.0f) - rr;
            float cov = 0.5f - d;
            m[y * w + x] = (uint8_t)(cov <= 0 ? 0 : cov >= 1 ? 255 : (int)(cov * 255.0f));
        }
    lv_image_dsc_t *im = &s_img[p];
    memset(im, 0, sizeof *im);
    im->header.magic = LV_IMAGE_HEADER_MAGIC;
    im->header.cf = LV_COLOR_FORMAT_A8;
    im->header.w = (uint32_t)w;
    im->header.h = (uint32_t)h;
    im->header.stride = (uint32_t)w;
    im->data = m;
    im->data_size = (uint32_t)(w * h);
}
#else
static void pad_mask(int p)
{
    const float deg = 3.1415927f / 180.0f, a0 = (float)(pc_pad_angle(p) - PC_PAD_EDGE / PC_PADS) + PAD_GAP,
                a1 = (float)(pc_pad_angle(p) + PC_PAD_EDGE / PC_PADS) - PAD_GAP;
    int x1 = UI_DESIGN_D, y1 = UI_DESIGN_D, x2 = 0, y2 = 0;
    for (int i = 0; i <= 16; i++) {
        float t = (a0 + (a1 - a0) * (float)i / 16.0f) * deg;
        for (int r = PAD_RI; r <= PAD_RO; r += PAD_RO - PAD_RI) {
            int x = PAD_C + (int)lroundf((float)r * sinf(t)), y = PAD_C - (int)lroundf((float)r * cosf(t));
            x1 = LV_MIN(x1, x); y1 = LV_MIN(y1, y); x2 = LV_MAX(x2, x); y2 = LV_MAX(y2, y);
        }
    }
    lv_area_t *a = &s_area[p];
    a->x1 = x1 - 1; a->y1 = y1 - 1; a->x2 = x2 + 1; a->y2 = y2 + 1;
    int w = lv_area_get_width(a), h = lv_area_get_height(a);
    uint8_t *m = heap_caps_calloc(1, (size_t)(w * h), MALLOC_CAP_SPIRAM);
    if (!m) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = (float)(a->x1 + x) + 0.5f - (float)PAD_C, dy = (float)(a->y1 + y) + 0.5f - (float)PAD_C;
            float r = sqrtf(dx * dx + dy * dy), t = atan2f(dx, -dy) / deg;
            float dr = LV_MAX((float)PAD_RI - r, r - (float)PAD_RO), da = LV_MAX(a0 - t, t - a1) * deg * r;
            float cov = 0.5f - LV_MAX(dr, da);
            m[y * w + x] = (uint8_t)(cov <= 0 ? 0 : cov >= 1 ? 255 : (int)(cov * 255.0f));
        }
    lv_image_dsc_t *im = &s_img[p];
    memset(im, 0, sizeof *im);
    im->header.magic = LV_IMAGE_HEADER_MAGIC;
    im->header.cf = LV_COLOR_FORMAT_A8;
    im->header.w = (uint32_t)w;
    im->header.h = (uint32_t)h;
    im->header.stride = (uint32_t)w;
    im->data = m;
    im->data_size = (uint32_t)(w * h);
}

#endif

static void draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t tmp, at, oc;
    lv_draw_image_dsc_t d;
    lv_obj_get_coords(lv_event_get_current_target_obj(e), &oc);
    for (int p = 0; p < PC_PADS; p++) {
        if (!s_img[p].data) continue;
        at = s_area[p];
        lv_area_move(&at, oc.x1, oc.y1);
        if (!lv_area_intersect(&tmp, &layer->_clip_area, &at)) continue;
        lv_draw_image_dsc_init(&d);
        d.src = &s_img[p];
        d.recolor = lv_color_hex(s_col[p]);
        d.recolor_opa = LV_OPA_COVER;
        lv_draw_image(layer, &d, &at);
    }
}

static void release_pad(int p)
{
    if (p < 0 || p >= PC_PADS || !(s_held >> p & 1)) return;
    s_held &= (uint8_t)~(1u << p);
    if (s_up) s_up(p);
}

/* the touch: PRESSED on a sector = that pad down (audio first: down() posts the note); RELEASED / PRESS_LOST = the pad that went down, up */
static int8_t s_touch = -1;                   /* the pad the pointer pressed */
static int hit_at(int x, int y)
{
#if UI_ROUND
    return pc_pad_hit(x, y);
#else
    return sq_hit(x, y);
#endif
}
static bool pad_veto(lv_point_t pt)           /* I3: a press on a pad is a (sustained) note: no hold-home, no flick; the hub, the gaps and the page around the pads still have both */
{
    if (!s_obj) return false;
    lv_area_t oc;
    lv_obj_get_coords(s_obj, &oc);
    return hit_at(pt.x - oc.x1, pt.y - oc.y1) >= 0;
}
static void press(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    int p = s_inject;
    if (p < 0 && c == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_active(), &pt);
        lv_area_t oc;
        lv_obj_get_coords(lv_event_get_current_target_obj(e), &oc);
        p = hit_at(pt.x - oc.x1, pt.y - oc.y1);
    }
    if (c == LV_EVENT_PRESSED) {
        if (p < 0 || p >= PC_PADS || (s_held >> p & 1)) return;
        s_held |= (uint8_t)(1u << p);
        if (s_inject < 0) s_touch = (int8_t)p;
        if (s_down) s_down(p);
        return;
    }
    if (s_inject < 0) { p = s_touch; s_touch = -1; }
    release_pad(p);
}

static void deleted(lv_event_t *e)
{
    (void)e;
    for (int p = 0; p < PC_PADS; p++) release_pad(p);   /* a held pad gets its note off */
    s_obj = NULL;
    s_touch = -1;
    ui_gesture_veto(NULL);
    memset(s_lbl, 0, sizeof s_lbl);
}

lv_obj_t *inst_pads_create(lv_obj_t *page, void (*down)(int pad), void (*up)(int pad), void (*hub)(void))
{
    s_down = down;
    s_up = up;
    s_held = 0;
    s_touch = -1;
    for (int p = 0; p < PC_PADS; p++) {
        if (!s_img[p].data) pad_mask(p);   /* ~0.1 s, the first visit only */
        s_rgb[p] = inst_voice_rgb(3 + p);
        s_lvl[p] = s_drawn[p] = 0;
        s_col[p] = pad_color(p, 0);
    }
    s_obj = lv_obj_create(page);
    lv_obj_remove_style_all(s_obj);
    lv_obj_set_size(s_obj, PAD_BOX, PAD_BOX);
    lv_obj_center(s_obj);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_obj, draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_obj, press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_obj, press, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_obj, press, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_obj, deleted, LV_EVENT_DELETE, NULL);
    ui_gesture_veto(pad_veto);
    for (int p = 0; p < PC_PADS; p++) {
        double r = pc_pad_angle(p) * M_PI / 180.0;
        lv_obj_t *l = lv_label_create(s_obj);
        lv_obj_set_style_text_font(l, font_main(), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);   /* the text is made to fit by inst_pads_label (a word per line, dots); the box is the text itself (tight) */
        lv_obj_set_width(l, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(l, "");
#if UI_ROUND
        lv_obj_align(l, LV_ALIGN_CENTER, (int)lround(PAD_LBL_R * sin(r)), (int)lround(-PAD_LBL_R * cos(r)));
#else
        lv_area_t ca;
        sq_cell(p, &ca);
        lv_obj_align(l, LV_ALIGN_CENTER, (ca.x1 + ca.x2 + 1) / 2 - PAD_BOX / 2, (ca.y1 + ca.y2 + 1) / 2 - PAD_BOX / 2);
        (void)r;
#endif
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        s_lbl[p] = l;
    }
    lv_obj_t *h = ui_hub(s_obj, PAD_HUB_D, hub);
    lv_obj_t *hl = lv_obj_get_child(h, 0);
    lv_obj_set_width(hl, PAD_HUB_D - 16);
    lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(hl, LV_TEXT_ALIGN_CENTER, 0);
    return h;
}

static int line_w(const char *t)
{
    lv_point_t sz;
    lv_text_get_size(&sz, t, font_main(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* "hat closed" -> "hat" / "closed": one word per line, so no label is wider than its longest word, and a word wider than PAD_LBL_W ends in ".." */
static void label_lines(char *out, size_t cap, const char *text)
{
    size_t n = 0;
    for (const char *w = text; *w && n + 1 < cap; ) {
        size_t len = strcspn(w, " ");
        char word[24];
        snprintf(word, sizeof word, "%.*s", (int)len, w);
        if (len && line_w(word) > PAD_LBL_W) {
            for (size_t k = strlen(word); k > 1; ) {
                do k--; while (k > 1 && (word[k] & 0xC0) == 0x80);            /* back to the start of a UTF-8 character */
                snprintf(word + k, sizeof word - k, "..");
                if (line_w(word) <= PAD_LBL_W) break;
            }
        }
        if (len) n += (size_t)snprintf(out + n, cap - n, "%s%s", n ? "\n" : "", word);
        w += len;
        while (*w == ' ') w++;
    }
    if (!n) out[0] = 0;
}

void inst_pads_label(int pad, const char *text)
{
    if (pad < 0 || pad >= PC_PADS || !s_lbl[pad] || !text) return;
    char b[48];
    label_lines(b, sizeof b, text);
    if (!strcmp(lv_label_get_text(s_lbl[pad]), b)) return;
    lv_label_set_text(s_lbl[pad], b);
}

void inst_pads_flash(int pad, uint32_t rgb)
{
    if (pad < 0 || pad >= PC_PADS) return;
    s_rgb[pad] = rgb;
    s_lvl[pad] = 255;
}

void inst_pads_tick(int ms)
{
    if (!s_obj) return;
    int d = ms * 255 / 200;
    for (int p = 0; p < PC_PADS; p++) {
        s_lvl[p] = (uint8_t)(s_lvl[p] > d ? s_lvl[p] - d : 0);
        uint8_t q = (uint8_t)(s_lvl[p] & 0xC0);    /* 4 steps: fewer redraws of the big sectors */
        if (q == s_drawn[p]) continue;
        s_drawn[p] = q;
        s_col[p] = pad_color(p, q);
        lv_area_t a = s_area[p], oc;
        lv_obj_get_coords(s_obj, &oc);
        lv_area_move(&a, oc.x1, oc.y1);
        lv_obj_invalidate_area(s_obj, &a);         /* that sector's box only */
    }
}

void inst_pads_inject(int pad, bool down)
{
    if (!s_obj || pad < 0 || pad >= PC_PADS) return;
    s_inject = (int8_t)pad;
    lv_obj_send_event(s_obj, down ? LV_EVENT_PRESSED : LV_EVENT_RELEASED, NULL);
    s_inject = -1;
}
