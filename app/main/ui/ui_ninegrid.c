// SPDX-License-Identifier: LGPL-2.1-or-later
/* The nine-grid of invisible tap zones (header: ui_ninegrid.h). Zones are thirds of the page; the labels are made at the first show, deleted at the hide. */
#include "ui_ninegrid.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "audio_svc.h"
#include "font.h"
#include "lab.h"
#include "pc/pc_lab.h"
#include "player_ctl.h"
#include "sleep_timer.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

#if UI_ROUND
void ui_ninegrid_attach(lv_obj_t *page, const ui_ng_def_t *def, bool bands) { (void)page; (void)def; (void)bands; }
bool ui_ninegrid_shown(void) { return false; }
void ui_ninegrid_hide(void) {}
void ui_ninegrid_console(const char *arg) { (void)arg; printf("APP ninegrid none\n"); }
const ui_ng_set_t UI_NG_PLAYMODE = { 0 };
#else

#define Z 9
#define TICK_MS 250
enum { Z_SET, Z_MENU, Z_PAGE, Z_LEFT, Z_PLAY, Z_RIGHT, Z_PREV, Z_CLOSE, Z_NEXT };

/* ---- the shared set 播放模式 (the source's own ops: mode_kind, mode_step) ---- */
static bool mode_ok(const now_ops_t *o) { return o && o->mode_kind && o->mode_step && o->modes >= 2; }
static void mode_to(int want)                       /* step the play order (its own op) until it reads `want`; at most `modes` steps */
{
    const now_ops_t *o = player_ctl_now();
    if (!mode_ok(o)) return;
    for (int i = 0; i < o->modes && o->mode_kind() != want; i++) o->mode_step(1);
}
static const char *pm_name(void) { return T(S_NG_PLAYMODE) /* Play mode */; }
static const char *pm_shuffle(void)
{
    const now_ops_t *o = player_ctl_now();
    return mode_ok(o) ? (o->mode_kind() == NOW_MODE_SHUFFLE ? T(S_NG_SHUFFLE_ON) /* Shuffle: on */ : T(S_NG_SHUFFLE_OFF) /* Shuffle: off */) : NULL;
}
static void pm_shuffle_run(void)
{
    const now_ops_t *o = player_ctl_now();
    if (mode_ok(o)) mode_to(o->mode_kind() == NOW_MODE_SHUFFLE ? NOW_MODE_ORDER : NOW_MODE_SHUFFLE);
}
static const char *pm_repeat(void)
{
    const now_ops_t *o = player_ctl_now();
    return mode_ok(o) && o->modes >= 3 ? (o->mode_kind() == NOW_MODE_ONE ? T(S_NG_REPEAT_ONE) /* Repeat: one */ : T(S_NG_REPEAT_ALL) /* Repeat: all */) : NULL;
}
static void pm_repeat_run(void)
{
    const now_ops_t *o = player_ctl_now();
    if (mode_ok(o) && o->modes >= 3) mode_to(o->mode_kind() == NOW_MODE_ONE ? NOW_MODE_ORDER : NOW_MODE_ONE);
}
const ui_ng_set_t UI_NG_PLAYMODE = { .name = pm_name,
    .left = { pm_shuffle, pm_shuffle_run, UI_ICON_L_SHUFFLE }, .right = { pm_repeat, pm_repeat_run, UI_ICON_L_REPEAT } };

/* ---- the state: one block in PSRAM ---- */
#define TXT 96
typedef struct {
    const ui_ng_def_t *def;
    lv_obj_t *page, *grid, *cell[Z], *lab[Z], *band_t, *band_m, *lab_t, *lab_m;
    bool bands;
    uint32_t t_show;
    bool en[Z];                                       /* the zone is available now */
    char txt[Z][TXT], band[2][TXT];                   /* what the labels show now */
} ng_t;
EXT_RAM_BSS_ATTR static ng_t g[2];                    /* one per page (the spectrum page sits over the cover page: both stay attached); index = ui_ng_def_t.id */
EXT_RAM_BSS_ATTR static uint8_t g_set[2];             /* the chosen set per page, kept for the life of the boot */
EXT_RAM_BSS_ATTR static uint8_t s_i;                  /* the page being served now (set by every entry): an index, so nothing initialised sits in internal .data */
#define s (&g[s_i])

static void zone_geom(int z, int *cx, int *cy) { *cx = UI_W * (2 * (z % 3) + 1) / 6; *cy = UI_H * (2 * (z / 3) + 1) / 6; }

bool ui_ninegrid_shown(void) { return g[0].grid || g[1].grid; }

static void grid_gone(lv_event_t *e)                  /* the grid object was deleted (hide, or its page went): its children went with it */
{
    ng_t *x = (ng_t *)lv_event_get_user_data(e);
    x->grid = x->band_t = x->band_m = x->lab_t = x->lab_m = NULL;
    memset(x->cell, 0, sizeof x->cell);
    memset(x->lab, 0, sizeof x->lab);
}
static void page_gone(lv_event_t *e) { ng_t *x = (ng_t *)lv_event_get_user_data(e); x->def = NULL; x->page = NULL; }   /* the page went: the grid object (its child) went with it */

void ui_ninegrid_hide(void) { for (int i = 0; i < 2; i++) if (g[i].grid) lv_obj_delete(g[i].grid); }   /* the callers hold `s` too: grid_gone clears the slot */

static int cur_set(void) { const int i = s->def ? g_set[s->def->id & 1] : 0; return s->def && i < s->def->n_sets ? i : 0; }

static void fmt_time(char *b, size_t n, int32_t ms)
{
    const int t = ms < 0 ? 0 : ms / 1000;
    if (t >= 3600) snprintf(b, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(b, n, "%d:%02d", t / 60, t % 60);
}

/* the text of zone z now; NULL = not available */
static const char *zone_text(int z, char *buf, size_t n, bool *en)
{
    const now_ops_t *o = player_ctl_now();
    const ui_ng_set_t *st = s->def->sets[cur_set()];
    const char *t = NULL, *ic = NULL;
    *en = true;
    switch (z) {
    case Z_SET: snprintf(buf, n, "%s %d/%d", st->name(), cur_set() + 1, s->def->n_sets); return buf;
    case Z_MENU: t = T(S_MENU) /* Menu */; ic = UI_ICON_L_STYLE; break;
    case Z_PAGE: t = T(S_NG_NEXT_PAGE) /* Next page */; ic = LV_SYMBOL_RIGHT; *en = s->def->next_page != NULL; break;
    case Z_LEFT: case Z_RIGHT: {
        const ui_ng_cell_t *c = z == Z_LEFT ? &st->left : &st->right;
        t = c->label ? c->label() : NULL;
        ic = c->icon;
        if (!t) { t = "--"; *en = false; }
        break; }
    case Z_PLAY: { const bool p = audio_svc_paused(); t = p ? T(S_PLAY) /* Play */ : T(S_PAUSE) /* Pause */; ic = p ? UI_ICON_L_PLAY : UI_ICON_L_PAUSE; break; }
    case Z_PREV: t = T(S_PREV) /* Previous */; ic = LV_SYMBOL_PREV; *en = o && o->prev; break;
    case Z_CLOSE: t = T(S_CLOSE) /* Close */; ic = UI_ICON_L_CLOSE; break;
    case Z_NEXT: t = T(S_NEXT) /* Next */; ic = LV_SYMBOL_NEXT; *en = o && o->next; break;
    }
    snprintf(buf, n, "%s%s%s", ic ? ic : "", ic ? "\n" : "", t);   /* the icon on its own line: a long label keeps its width */
    return buf;
}

static void set_if(lv_obj_t *l, char *shown, const char *txt)
{
    if (!strcmp(shown, txt)) return;
    snprintf(shown, TXT, "%s", txt);
    if (l) lv_label_set_text(l, txt);
}

static void refresh(void)
{
    if (!s->grid) return;
    char b[TXT];
    for (int z = 0; z < Z; z++) {
        bool en;
        zone_text(z, b, sizeof b, &en);
        set_if(s->lab[z], s->txt[z], b);
        if (en != s->en[z]) { s->en[z] = en; lv_obj_set_style_text_color(s->lab[z], en ? UI_FG : UI_DIM, 0); }
    }
    if (s->bands) {
        const now_ops_t *o = player_ctl_now();
        char t[300];
        if (o && o->playing()) o->title(t, sizeof t); else snprintf(t, sizeof t, "%s", T(S_STOPPED_2) /* Stopped */);
        set_if(s->lab_t, s->band[0], t);
        aud_status_t st;
        audio_svc_status(&st);
        const bool live = st.state == AUD_PLAYING || st.state == AUD_PAUSED;
        char el[16], tot[16], m[TXT];
        fmt_time(el, sizeof el, live ? st.pos_ms : 0);
        if (live && st.len_ms > 0) { fmt_time(tot, sizeof tot, st.len_ms); snprintf(m, sizeof m, "%s / %s", el, tot); }
        else snprintf(m, sizeof m, "%s", el);
        const int left = sleep_timer_left_s();
        if (left >= 0) { char sl[16]; fmt_time(sl, sizeof sl, (int32_t)left * 1000); size_t n = strlen(m); snprintf(m + n, sizeof m - n, "    %s %s", UI_ICON_L_MOON, sl); }
        set_if(s->lab_m, s->band[1], m);
    }
}

static void tick(lv_timer_t *t)
{
    s_i = (uint8_t)((ng_t *)lv_timer_get_user_data(t) - g);
    if (!s->grid) return;
    if (!ui_screen_on() || ui_top_root() != s->page) { lv_obj_delete(s->grid); return; }   /* dark, or another page over this one */
    const int hide = lab_get(PC_LAB_NOWB_HIDE_MS);        /* LAB: 0 = never hide (UI_NG default 4000 ms) */
    if (hide && lv_tick_elaps(s->t_show) >= (uint32_t)hide) { lv_obj_delete(s->grid); return; }
    refresh();
}

static lv_obj_t *box(lv_obj_t *parent, int cx, int cy, int w, int h, lv_color_t c, lv_opa_t opa, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, cx - w / 2, cy - h / 2);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label_in(lv_obj_t *box_, int w, lv_label_long_mode_t mode)
{
    lv_obj_t *l = lv_label_create(box_);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, mode);
    lv_obj_set_style_text_font(l, font_main(), 0);
    lv_obj_set_style_text_color(l, UI_FG, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(l, "");
    lv_obj_center(l);
    return l;
}

static void show(void)
{
    s->t_show = lv_tick_get();
    if (s->grid) return;
    s->grid = box(s->page, UI_W / 2, UI_H / 2, UI_W, UI_H, UI_BG, s->bands ? LV_OPA_TRANSP : UI_NG_BACK_OPA, 0);   /* a page with its own text (封面＋資訊) is dimmed behind the cells */
    if (!s->grid) return;
    lv_obj_add_event_cb(s->grid, grid_gone, LV_EVENT_DELETE, s);
    if (s->bands) {
        s->band_t = box(s->grid, UI_W / 2, UI_H / 3, UI_W, UI_NG_BAND_H, UI_BG, UI_NG_BAND_OPA, 0);
        s->lab_t = label_in(s->band_t, UI_W - 2 * UI_NOW_M, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        s->band_m = box(s->grid, UI_W / 2, UI_H * 2 / 3, UI_W, UI_NG_BAND_H, UI_BG, UI_NG_BAND_OPA, 0);
        s->lab_m = label_in(s->band_m, UI_W - 2 * UI_NOW_M, LV_LABEL_LONG_MODE_CLIP);
    }
    for (int z = 0; z < Z; z++) {
        int cx, cy;
        zone_geom(z, &cx, &cy);
        s->cell[z] = box(s->grid, cx, cy, UI_NG_CELL_W, UI_NG_CELL_H, z == Z_PLAY ? UI_SEL : UI_PANEL, UI_NG_CELL_OPA, UI_NG_RADIUS);
        s->lab[z] = label_in(s->cell[z], UI_NG_CELL_W - 2 * UI_NG_PAD, LV_LABEL_LONG_MODE_WRAP);
        s->txt[z][0] = 0;
        s->en[z] = true;
    }
    s->band[0][0] = s->band[1][0] = 0;
    ui_timer_bind(s->grid, tick, TICK_MS, s);
    refresh();
}

/* ---- the tap: zone by position; hidden = show only ---- */
static void run_zone(int z)
{
    const now_ops_t *o = player_ctl_now();
    bool en;
    char b[TXT];
    zone_text(z, b, sizeof b, &en);
    s->t_show = lv_tick_get();                         /* a tap on a zone restarts the timer */
    switch (z) {
    case Z_SET: { const int n = s->def->n_sets; g_set[s->def->id & 1] = (uint8_t)((cur_set() + 1) % n); break; }
    case Z_MENU: ui_ninegrid_hide(); if (s->def->menu) s->def->menu(); return;
    case Z_PAGE: if (!en) return; ui_ninegrid_hide(); s->def->next_page(); return;
    case Z_LEFT: case Z_RIGHT: if (en) { const ui_ng_cell_t *c = z == Z_LEFT ? &s->def->sets[cur_set()]->left : &s->def->sets[cur_set()]->right; if (c->run) c->run(); } break;
    case Z_PLAY: audio_svc_pause(!audio_svc_paused()); break;
    case Z_PREV: if (en && o->prev) o->prev(); break;
    case Z_NEXT: if (en && o->next) o->next(); break;
    case Z_CLOSE: ui_ninegrid_hide(); return;
    }
    if (s->grid) refresh();                            /* a track change may have ended the source: the page may be going */
}

static void on_tap(int z)
{
    if (!s->def || !s->page) return;
    if (s->def->passthru && s->def->passthru()) { ui_ninegrid_hide(); if (s->def->menu) s->def->menu(); return; }
    if (!s->grid) { show(); return; }
    run_zone(z);
}

static void page_click(lv_event_t *e)
{
    s_i = (uint8_t)((ng_t *)lv_event_get_user_data(e) - g);
    if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e)) return;   /* a child control's tap bubbled up */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(s->page, &a);
    int cx = (p.x - a.x1) * 3 / UI_W, cy = (p.y - a.y1) * 3 / UI_H;
    cx = cx < 0 ? 0 : cx > 2 ? 2 : cx;
    cy = cy < 0 ? 0 : cy > 2 ? 2 : cy;
    on_tap(cy * 3 + cx);
}

void ui_ninegrid_attach(lv_obj_t *page, const ui_ng_def_t *def, bool bands)
{
    s_i = (uint8_t)(def->id & 1);
    memset(s, 0, sizeof *s);
    s->page = page;
    s->def = def;
    s->bands = bands;
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, page_click, LV_EVENT_CLICKED, s);
    lv_obj_add_event_cb(page, page_gone, LV_EVENT_DELETE, s);
}

void ui_ninegrid_console(const char *arg)
{
    int z;
    s_i = g[1].def && g[1].page == ui_top_root() ? 1 : g[0].def ? 0 : 1;   /* the top page's slot */
    if (!strncmp(arg, "tap ", 4) && sscanf(arg + 4, "%d", &z) == 1 && z >= 0 && z < Z) {
        if (!s->def) { printf("APP ninegrid none\n"); return; }
        on_tap(z);
        return;
    }
    if (!s->def) { printf("APP ninegrid none\n"); return; }
    printf("APP ninegrid shown=%d set=%d/%d name=%s", s->grid != NULL, cur_set() + 1, s->def->n_sets, s->def->sets[cur_set()]->name());
    for (int i = 0; i < Z; i++) {
        bool en;
        char b[TXT];
        zone_text(i, b, sizeof b, &en);
        const char *t = strrchr(b, '\n');                      /* the text after the icon's line: one console line, no private-use glyph */
        printf("%s%d=%s%s", i ? " | " : " cells: ", i, t ? t + 1 : b, en ? "" : " (off)");
    }
    printf("\n");
}
#endif
