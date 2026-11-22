// SPDX-License-Identifier: LGPL-2.1-or-later
/* The static now-playing page of the square panel (header: now_fp.h). Style A (封面＋資訊): cover + status icons + text block + time row, nothing to tap. Style B (全封面): the bare cover.
 * Taps come from the nine-grid (ui_ninegrid.c, attached by scr_now.c). The geometry is ui_geom.h's UI_NOW* names (square column); this file only places things by them.
 * No transition, no pressed look: everything is drawn once and changed only when a value changes. */
#include "now_fp.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "audio_svc.h"
#include "cfg.h"
#include "font.h"
#include "player_ctl.h"
#include "sleep_timer.h"
#include "pc/pc_nowinfo.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define TICK_MS 250

bool nowfp_available(void) { return !UI_ROUND; }
int nowfp_style(void)
{
    if (UI_ROUND) return NOWFP_GRID;
    int v = cfg_int("now_fp", NOWFP_FULL);
    return v >= 0 && v < NOWFP_N ? v : NOWFP_FULL;
}
const char *nowfp_style_label(void) { const int v = nowfp_style(); return v == NOWFP_FULL ? T(S_NOWFP_FULL) /* Full cover */ : v == NOWFP_GRID ? T(S_NOWFP_GRID) /* Menu grid */ : T(S_NOWFP_CARDS) /* Cover + info */; }
void nowfp_style_step(int d)
{
    if (UI_ROUND) return;
    cfg_set_int("now_fp", ((nowfp_style() + d) % NOWFP_N + NOWFP_N) % NOWFP_N);
}

#if UI_ROUND
lv_obj_t *nowfp_create(lv_obj_t *parent, int style) { (void)parent; (void)style; return NULL; }
void nowfp_set_title(const char *text) { (void)text; }
void nowfp_set_cover(const lv_image_dsc_t *dsc) { (void)dsc; }
void nowfp_set_note(const char *text) { (void)text; }
void nowfp_refresh(void) {}
bool nowfp_active(void) { return false; }
void nowfp_print(void) { printf("APP nowfp style=grid\n"); }
int nowfp_cover_size(void) { return 0; }
#else

enum { B_PLAY, B_SHUF, B_REP, B_N };          /* the passive status icons of style A: playing / paused, shuffle, repeat */
EXT_RAM_BSS_ATTR static struct {                  /* one instance; PSRAM (no internal RAM) */
    lv_obj_t *root, *cov, *img, *note;
    lv_obj_t *title, *line2, *line3, *t_el, *t_tot, *bar, *sl_ico, *sl_txt;   /* style A only (style B is the bare cover: no label) */
    lv_obj_t *ico[B_N];
    int style;
    bool icons;                                    /* the icon font loaded: glyphs; else LVGL's own symbols */
    char title_txt[300], artist[200], fmt[64];     /* the pieces of the text lines */
    char l2[200], l3[64], el[16], tot[16];         /* what the labels show now (a label is set only when its text changes) */
    int bar_v, sleep_on, cov_n;                    /* cov_n: the picture's pixels per side now (0: none) */
    char sl[16];
    int paused, kind, has_mode;                    /* the state the icons show now (-1: nothing drawn yet) */
} s;

static void fmt_time(char *b, size_t n, int32_t ms)
{
    if (ms < 0) { snprintf(b, n, "--:--"); return; }
    const int t = ms / 1000;
    if (t >= 3600) snprintf(b, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(b, n, "%d:%02d", t / 60, t % 60);
}

static void set_if(lv_obj_t *l, char *shown, size_t cap, const char *txt)
{
    if (!strcmp(shown, txt)) return;
    snprintf(shown, cap, "%s", txt);
    if (l) lv_label_set_text(l, txt);
}

/* artist when there is one, else the format takes the artist's line; the third line is the format then */
static void lines(void)
{
    if (s.artist[0]) { set_if(s.line2, s.l2, sizeof s.l2, s.artist); set_if(s.line3, s.l3, sizeof s.l3, s.fmt); }
    else { set_if(s.line2, s.l2, sizeof s.l2, s.fmt); set_if(s.line3, s.l3, sizeof s.l3, ""); }
}

void nowfp_set_title(const char *text)
{
    if (!s.root) return;
    const char *sep = NULL;
    for (const char *p = strstr(text, "  -  "); p; p = strstr(p + 1, "  -  ")) sep = p;   /* the last one: a title may hold " - " itself */
    if (sep) {
        snprintf(s.title_txt, sizeof s.title_txt, "%.*s", (int)(sep - text), text);
        snprintf(s.artist, sizeof s.artist, "%s", sep + 5);
    } else { snprintf(s.title_txt, sizeof s.title_txt, "%s", text); s.artist[0] = 0; }
    if (s.title) lv_label_set_text(s.title, s.title_txt);
    lines();
}

void nowfp_set_cover(const lv_image_dsc_t *dsc)
{
    if (!s.root) return;
    s.cov_n = dsc ? (int)dsc->header.w : 0;
    if (dsc) { lv_image_set_src(s.img, dsc); lv_obj_remove_flag(s.img, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(s.img, LV_OBJ_FLAG_HIDDEN);
}

void nowfp_set_note(const char *text)
{
    if (!s.root) return;
    if (text) { lv_label_set_text(s.note, text); lv_obj_remove_flag(s.note, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(s.note, LV_OBJ_FLAG_HIDDEN);
}

static const char *icon_of(int b, int paused, int kind)
{
    if (s.icons) {
        switch (b) {
        case B_PLAY: return paused ? UI_ICON_T_PLAY : UI_ICON_T_PAUSE;
        case B_SHUF: return UI_ICON_T_SHUFFLE;
        default: return kind == NOW_MODE_ONE ? UI_ICON_T_REPEAT1 : UI_ICON_T_REPEAT;
        }
    }
    switch (b) {                                   /* no icon file: LVGL's built-in symbols (the font chain's tail) */
    case B_PLAY: return paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    case B_SHUF: return LV_SYMBOL_SHUFFLE;
    default: return LV_SYMBOL_LOOP;
    }
}

static void show(lv_obj_t *o, bool on) { if (!o) return; if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void refresh_now(void)
{
    if (!s.root) return;
    const now_ops_t *o = player_ctl_now();
    aud_status_t st;
    audio_svc_status(&st);
    const int paused = audio_svc_paused() ? 1 : 0;
    const int kind = o && o->mode_kind ? o->mode_kind() : NOW_MODE_ORDER;
    const int hm = o && o->mode_label && o->mode_step;
    if (s.ico[B_PLAY] && paused != s.paused) lv_label_set_text(s.ico[B_PLAY], icon_of(B_PLAY, paused, kind));
    s.paused = paused;
    if (kind != s.kind || hm != s.has_mode) {      /* shuffle lit when the order is shuffle, repeat lit otherwise (it shows one / all); both only when the source has an order at all */
        if (s.ico[B_SHUF]) {
            lv_obj_set_style_text_color(s.ico[B_SHUF], kind == NOW_MODE_SHUFFLE ? UI_A1 : UI_DIM, 0);
            lv_obj_set_style_text_color(s.ico[B_REP], kind == NOW_MODE_SHUFFLE ? UI_DIM : UI_A1, 0);
            lv_label_set_text(s.ico[B_REP], icon_of(B_REP, paused, kind));
            show(s.ico[B_SHUF], hm);
            show(s.ico[B_REP], hm);
        }
        s.kind = kind;
        s.has_mode = hm;
    }
    char t[16];
    const bool live = st.state == AUD_PLAYING || st.state == AUD_PAUSED;
    fmt_time(t, sizeof t, live ? st.pos_ms : 0);
    set_if(s.t_el, s.el, sizeof s.el, t);
    fmt_time(t, sizeof t, live && st.len_ms > 0 ? st.len_ms : -1);
    set_if(s.t_tot, s.tot, sizeof s.tot, t);
    const int v = live && st.len_ms > 0 ? (int)((int64_t)st.pos_ms * 1000 / st.len_ms) : 0;
    if (v != s.bar_v) { s.bar_v = v; if (s.bar) lv_bar_set_value(s.bar, v, LV_ANIM_OFF); }
    aud_info_t a;                                  /* the format line: the format name, the depth when known, the source's own rate (the status page's words) */
    audio_svc_info(&a);
    char f[64] = "";
    if (a.fmt && a.fmt[0]) {
        size_t n = (size_t)snprintf(f, sizeof f, "%s", a.fmt);
        if (a.src_bits && n < sizeof f) n += (size_t)snprintf(f + n, sizeof f - n, " · %u bit", (unsigned)a.src_bits);
        if (a.src_rate && n < sizeof f) { char r[24]; pc_nowinfo_rate(a.src_rate, r, sizeof r); snprintf(f + n, sizeof f - n, " · %s", r); }
    }
    if (strcmp(f, s.fmt)) { snprintf(s.fmt, sizeof s.fmt, "%s", f); lines(); }
    const int left = sleep_timer_left_s();        /* the sleep timer's remaining time: the rim arc has no place here */
    if (left >= 0) { fmt_time(t, sizeof t, (int32_t)left * 1000); set_if(s.sl_txt, s.sl, sizeof s.sl, t); }
    if ((left >= 0) != (s.sleep_on > 0)) {
        s.sleep_on = left >= 0;
        show(s.sl_ico, s.sleep_on);
        show(s.sl_txt, s.sleep_on);
        if (s.line3) lv_obj_set_width(s.line3, s.sleep_on ? UI_NOWA_TEXT_W - 124 : UI_NOWA_TEXT_W);   /* the format line gives the timer its end of the row */
    }
}
void nowfp_refresh(void) { if (s.root) refresh_now(); }

static void tick(lv_timer_t *t)
{
    (void)t;
    if (!ui_screen_on()) return;
    refresh_now();
}

static lv_obj_t *label(int x, int y, int w, const lv_font_t *f, lv_color_t c, lv_text_align_t al, lv_label_long_mode_t mode)
{
    lv_obj_t *l = lv_label_create(s.root);
    lv_obj_set_size(l, w, lv_font_get_line_height(f));   /* one line: the dots / the marquee need a fixed height */
    lv_label_set_long_mode(l, mode);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, al, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c, lv_opa_t opa, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *the_bar(int x, int cy, int w)
{
    lv_obj_t *b = lv_bar_create(s.root);
    lv_obj_set_size(b, w, UI_NOW_BAR_H);
    lv_obj_set_pos(b, x, cy - UI_NOW_BAR_H / 2);
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, lv_color_mix(UI_FG, UI_BG, 90), LV_PART_MAIN);   /* a track that shows on the page and on a scrim */
    lv_obj_set_style_bg_color(b, UI_A1, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void gone(lv_event_t *e) { (void)e; memset(&s, 0, sizeof s); }   /* the box was deleted with its page: nothing to free, the children went with it */

lv_obj_t *nowfp_create(lv_obj_t *parent, int style)
{
    if (style != NOWFP_CARDS && style != NOWFP_FULL) return NULL;
    memset(&s, 0, sizeof s);
    s.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s.root);
    lv_obj_set_size(s.root, UI_W, UI_H);
    lv_obj_center(s.root);
    lv_obj_remove_flag(s.root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);   /* a tap goes to the page: the nine-grid takes it */
    lv_obj_add_event_cb(s.root, gone, LV_EVENT_DELETE, NULL);
    s.style = style;
    s.icons = font_has_icons();
    s.paused = s.kind = s.has_mode = -1;
    const bool full = style == NOWFP_FULL;
    const lv_font_t *fl = font_large(), *fm = font_main();

    /* the cover box (the picture is stretched to it; it clips its corners in style A) */
    const int cd = full ? UI_W : UI_NOWA_COVER_D;
    s.cov = box(s.root, full ? 0 : UI_NOW_M, full ? 0 : UI_NOWA_COVER_Y, cd, full ? UI_H : cd, UI_PANEL2, LV_OPA_COVER, full ? 0 : UI_NOWA_COVER_R);
    if (!full) lv_obj_set_style_clip_corner(s.cov, true, 0);
    s.img = lv_image_create(s.cov);
    lv_obj_set_size(s.img, cd, full ? UI_H : cd);
    lv_image_set_inner_align(s.img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_remove_flag(s.img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.img, LV_OBJ_FLAG_HIDDEN);
    s.note = lv_label_create(s.cov);
    lv_obj_set_width(s.note, cd - 2 * UI_NOW_M);
    lv_label_set_long_mode(s.note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s.note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s.note, fm, 0);
    lv_obj_set_style_text_color(s.note, UI_DIM, 0);
    lv_obj_align(s.note, LV_ALIGN_BOTTOM_MID, 0, full ? -UI_H / 3 : -UI_NOW_M);
    lv_obj_add_flag(s.note, LV_OBJ_FLAG_HIDDEN);

    s.sleep_on = -1;
    s.bar_v = -1;
    if (!full) {
        const int lm = lv_font_get_line_height(fm);
        const lv_font_t *fi = s.icons ? font_icon(24) : NULL;
        const int tw = UI_NOWA_TEXT_W;                 /* the two small buttons are gone: the text lines take the whole width */
        s.title = label(UI_NOW_M, UI_NOWA_TEXT_Y, tw, fl, UI_FG, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        s.line2 = label(UI_NOW_M, UI_NOWA_TEXT_Y + UI_NOW_TITLE_PITCH, tw, fm, UI_FG, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_DOTS);
        const int l3y = UI_NOWA_TEXT_Y + UI_NOW_TITLE_PITCH + UI_NOW_LINE_PITCH;
        s.line3 = label(UI_NOW_M, l3y, tw, fm, UI_DIM, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_DOTS);
        s.sl_txt = label(UI_NOW_M + tw - 92, l3y, 92, fm, UI_A2, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_CLIP);
        s.sl_ico = label(UI_NOW_M + tw - 92 - 28, l3y, 28, fi ? fi : fm, UI_A2, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_CLIP);
        const int cy = UI_NOWA_COVER_Y + UI_NOWA_COVER_D / 2;   /* the status icons: a row in the column right of the cover, where the buttons were */
        const int d = UI_NOWA_STATUS_D, x0 = UI_NOWA_COL_CX - (3 * d + 2 * UI_NOWA_STATUS_GAP) / 2;
        const lv_font_t *si = s.icons ? font_icon(d) : font_large();
        for (int b = 0; b < B_N; b++) {
            s.ico[b] = label(x0 + b * (d + UI_NOWA_STATUS_GAP), cy - d / 2, d, si ? si : fl, UI_FG, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
            lv_label_set_text(s.ico[b], icon_of(b, 0, NOW_MODE_ORDER));
        }
        const int tw2 = 84, ty = UI_NOWA_TIME_CY - lm / 2;
        s.t_el = label(UI_NOWA_TIME_X, ty, tw2, fm, UI_DIM, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_CLIP);
        s.t_tot = label(UI_W - UI_NOWA_TIME_X - tw2, ty, tw2, fm, UI_DIM, LV_TEXT_ALIGN_RIGHT, LV_LABEL_LONG_MODE_CLIP);
        s.bar = the_bar(UI_NOWA_TIME_X + tw2 + 6, UI_NOWA_TIME_CY, UI_W - 2 * (UI_NOWA_TIME_X + tw2 + 6));
        if (s.sl_ico && fi) lv_label_set_text(s.sl_ico, UI_ICON_L_MOON);
        else if (s.sl_ico) lv_label_set_text(s.sl_ico, "z");
        show(s.sl_ico, false);
        show(s.sl_txt, false);
    }
    ui_timer_bind(s.root, tick, TICK_MS, NULL);
    nowfp_refresh();
    return s.root;
}

bool nowfp_active(void) { return s.root != NULL; }
int nowfp_cover_size(void) { return !s.root ? 0 : s.style == NOWFP_FULL ? UI_W : UI_NOWA_COVER_D; }

void nowfp_print(void)
{
    if (!s.root) { printf("APP nowfp style=none\n"); return; }
    printf("APP nowfp style=%s paused=%d mode=%d title=%s artist=%s fmt=%s el=%s tot=%s bar=%d\n", s.style == NOWFP_FULL ? "full" : "info", s.paused,
           s.has_mode > 0 ? s.kind : -1, s.title_txt, s.artist, s.fmt, s.el, s.tot, s.bar_v);   /* the status icons are passive: there is nothing to press */
    printf("APP nowovl sleep=%s cov=%d\n", s.sleep_on > 0 ? s.sl : "-", s.cov_n);   /* the sleep timer's remaining time shown (style A), - when none; cov = the cover picture's pixels per side (240 thumbnail, 296 / 460 sharp) */
}
#endif
