// SPDX-License-Identifier: LGPL-2.1-or-later
/* The playing ring shared by the cover page and the spectrum page (header: now_menu.h). Moved out of scr_now.c; the dim layer, the 4 s rule and the rebuild
 * after a pick behave as they did there. The pause state lives here: the centre toggles it, pages ask now_menu_paused(). */
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "audio_svc.h"
#include "font.h"
#include "now_menu.h"
#include "player_ctl.h"
#include "screens.h"
#include "speed_svc.h"
#include "stats_svc.h"
#include "ui_axis.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_geom.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* the cells in ring order; each is present only when the source (and the host) offers it */
enum { C_FAV, C_PREV, C_LIST, C_HOST, C_ORDER, C_NEXT, C_INFO, C_STOP, C_SPEED, C_N };
static const char *const CELL_NAME[C_N] = { "fav", "prev", "list", "host", "order", "next", "info", "stop", "speed" };

static ui_popring_t s_def;                         /* the ring's definition: rebuilt at every open (the cells follow the source) */
static now_menu_host_t s_host;
static lv_obj_t *s_parent;
static void (*s_extra)(void), (*s_back)(void);
static uint8_t s_items[C_N];
static const char *s_lab[C_N];
static char s_order[48];
EXT_RAM_BSS_ATTR static char s_speed[48];   /* M4: PSRAM, 0 B internal */
static int s_n;
static const now_ops_t *s_owner;                   /* the owner the cells were built for */

bool now_menu_is_open(void) { return ui_popring_up(&s_def); }
void now_menu_raise(void) { if (now_menu_is_open()) ui_popring_raise(); }
bool now_menu_paused(void) { return audio_svc_paused(); }

static int order_cell(void) { for (int i = 0; i < s_n; i++) if (s_items[i] == C_ORDER) return i; return -1; }
static bool order_borrowed(void) { return s_parent && ui_axis_current_of(s_parent) >= 0; }   /* the play order or the speed borrowed the knob */
void now_menu_close(void)
{
    if (order_borrowed()) ui_axis_end();          /* the knob is volume again */
    if (now_menu_is_open()) ui_popring_close();
}

static bool is_fav(const now_ops_t *o)
{
    char id[300];
    return o->fav_id && o->fav_id(id, sizeof id) && pc_stats_is_fav(g_stats, id);
}

static void pick(int idx);
static bool order_step(int axis, int d);
static void back_cell(void)
{
    void (*b)(void) = s_back;
    now_menu_close();
    if (b) b();
}
static const char *cell_label(int i) { return s_lab[i]; }
static const char *centre_text(void) { return now_menu_paused() ? T(S_RESUME) /* Resume */ : T(S_PAUSE) /* Pause */; }

static bool open_at(int page)
{
    const now_ops_t *o = player_ctl_now();
    if (!o || !o->playing() || !s_parent) { now_menu_close(); return false; }
    s_owner = o;
    s_n = 0;
#define ADD(id, text) do { s_items[s_n] = id; s_lab[s_n++] = text; } while (0)
    if (o->fav_id) ADD(C_FAV, is_fav(o) ? T(S_UNFAVOURITE) /* Unfavourite */ : T(S_FAVOURITE) /* Favourite */);
    if (o->prev) ADD(C_PREV, T(S_PREV) /* Previous */);
    if (o->open_list) ADD(C_LIST, o->list_label ? o->list_label() : T(S_LIST) /* List */);
    if (s_extra) ADD(C_HOST, s_host == NOW_MENU_COVER ? T(S_SPECTRUM) /* Spectrum */ : T(S_STYLE_2) /* Style */);
    if (o->mode_label) { snprintf(s_order, sizeof s_order, "%s", o->mode_label()); ADD(C_ORDER, s_order); }
    if (o->next) ADD(C_NEXT, T(S_NEXT) /* Next */);
    if (o->open_info) ADD(C_INFO, T(S_DETAILS) /* Details */);
    if (s_host == NOW_MENU_COVER) ADD(C_STOP, T(S_STOP) /* Stop */);
    if (o->speed) { speed_svc_label(s_speed, sizeof s_speed); ADD(C_SPEED, s_speed); }   /* Track B: five values, always an axis (R21); the last cell, so the older cells keep their places */
#undef ADD
    s_def = (ui_popring_t){ .n = s_n, .label = cell_label, .pick = pick, .centre = centre_text, .back = back_cell,
                            .step = order_step, .knob_volume = true, .status = &HELP_STATUS,
                            .help = UI_ROUND ? NULL : &HELP_NINEGRID };   /* the knob is volume here, also with two pages (Ruling P1) */
    int oc = o->modes >= 3 ? order_cell() : -1;     /* the play order with three or more values is an axis (rule R21) */
    if (oc >= 0) s_def.axis_mask = 1u << oc;
    for (int i = 0; i < s_n; i++) if (s_items[i] == C_SPEED) s_def.axis_mask |= 1u << i;
    ui_popring_open(s_parent, &s_def, page);        /* no memory for the ring: its own no-memory Back shows (not when the 24 byte state block fails: then nothing opens) */
    return true;
}

static void rebuild(int page)                      /* labels changed (pause, favourite, order): the same page again, a fresh 4 s */
{
    if (!open_at(page)) back_cell();                /* the source ended meanwhile: nothing to control, the page decides */
}

/* the play order with three or more values is an axis (rule R21): the tap steps it, then the knob does for 4 s; the label follows at once */
static bool order_step(int axis, int d)
{
    const now_ops_t *o = player_ctl_now();
    if (o != s_owner) { now_menu_close(); return false; }   /* the cells were built for another owner */
    if (axis >= 0 && axis < s_n && s_items[axis] == C_SPEED) speed_svc_step(d);
    else if (!o || !o->mode_step) return false;
    else o->mode_step(d);
    if (now_menu_is_open()) rebuild(ui_ring_get_page(ui_popring_ring()));
    return true;
}

/* another owner took playback (the phone streams, the alarm, the web page starts the radio): the cells were built for the old one, the ring goes
 * (as at its timeout: the borrowed play order ends, the timer goes). The cover page calls it from its change hook. */
void now_menu_owner_check(void) { if (now_menu_is_open() && player_ctl_now() != s_owner) now_menu_close(); }

static void pick(int idx)
{
    const now_ops_t *o = player_ctl_now();
    if (!o || o != s_owner) { now_menu_close(); return; }
    int what = idx >= 0 && idx < s_n ? s_items[idx] : idx < 0 ? -1 : -2;
    int page = ui_ring_get_page(ui_popring_ring());   /* an order axis cell does not come here: the popring borrows the knob and steps it (order_step) */
    switch (what) {
    case -2: now_menu_close(); return;              /* not a cell */
    case -1: audio_svc_pause(!audio_svc_paused()); break;   /* the centre */
    case C_FAV: { char id[300]; if (o->fav_id && o->fav_id(id, sizeof id)) pc_stats_toggle_fav(g_stats, id); break; }
    case C_PREV: if (o->prev) o->prev(); break;
    case C_NEXT: if (o->next) o->next(); break;
    case C_ORDER: if (o->mode_step) o->mode_step(1); break;   /* two orders: a plain tap */
    case C_LIST: now_menu_close(); if (o->open_list) o->open_list(); return;
    case C_HOST: { void (*x)(void) = s_extra; now_menu_close(); if (x) x(); return; }
    case C_INFO: now_menu_close(); if (o->open_info) o->open_info(); return;
    case C_STOP: now_menu_close(); if (o->stop) o->stop(); return;
    }
    /* the step itself ended the ring (the source stopped or changed hands) or covered this page (the "cannot play" message after a failed next):
     * nothing is rebuilt and nothing pops, the message stays until it leaves by itself and the page decides then (R40) */
    if (!now_menu_is_open() || lv_obj_has_flag(s_parent, LV_OBJ_FLAG_HIDDEN)) { now_menu_close(); return; }
    rebuild(page);
}

bool now_menu_open(lv_obj_t *parent, now_menu_host_t host, void (*on_extra)(void), void (*on_back)(void))
{
    s_parent = parent;
    s_host = host;
    s_extra = on_extra;
    s_back = on_back;
    return open_at(0);
}

void now_menu_console(int p)
{
    if (p == -3) {
        aud_status_t st;
        audio_svc_status(&st);
        printf("APP now paused=%d audio=%s\n", audio_svc_paused(), st.state == AUD_PAUSED ? "paused" : st.state == AUD_PLAYING ? "playing" : "other");
    } else if (p == -5) {
        char names[96] = "";
        int per = ui_ring_per(ui_popring_ring()), k = 0;   /* M5: the ring's own page size */
        bool open = now_menu_is_open();
        for (int i = 0; open && i < s_n; i++)
            k += snprintf(names + k, sizeof names - (size_t)k, "%s%s", i && i % per == 0 ? "|" : i ? "," : "", CELL_NAME[s_items[i]]);
        printf("APP now menu=%d page=%d\n", open, ui_ring_get_page(ui_popring_ring()));
        printf("APP now cells=%d items=%s\n", open ? s_n : 0, open ? names : "-");
    } else if (p == -2) {
        if (ui_top() == &scr_now) scr_now_tap();
        else if (ui_top() == &scr_viz) scr_viz_tap();
    } else if (!now_menu_is_open()) return;
    else if (p == -4) ui_ring_page(ui_popring_ring(), 1);      /* "nowmenu page": the "next page" segment */
    else ui_popring_pick(p);
}
