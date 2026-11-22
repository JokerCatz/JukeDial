// SPDX-License-Identifier: LGPL-2.1-or-later
/* Drink reminder (ideas G7, clock family: the clock ring's Drink water): the day's cups on ui_sectors with a progress ring round it (one segment a cup of the goal, the hub says
 * "3/8"). Two sectors: Drank (+1 cup) and Later (the next reminder in 10 minutes); a tap on the hub is +1 too. The HEAD row's one cell opens the page's ring (ui_popring):
 * Reminder (the reminders on / off: default off), Interval (30 / 45 / 60 / 90 min), Start and End (active hours), Goal (cups a day), One less (armed: a cup counted by mistake),
 * 說明 last. The four axis cells take the knob (the usual 4 s). The title line says what happens next ("Next 10:30", "Reminder: off", "Goal reached today").
 * The rules are pc_habit's; the state, the saved keys (drink.*) and the reminder itself (haptic + the bank's "ok", never a wake or a page) are drink_svc.c.
 * Without a synced clock the page says so and counts nothing (a cup needs a day). The 1 s timer follows midnight: the cups go back to 0 on the new local day.
 * Statics are PSRAM .bss (built with -msmall-data-limit=0). Console `drink` (drink_svc.h): "tap <i>" and "ring" are this page's, the rest is the service's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clock_svc.h"
#include "drink_svc.h"
#include "font.h"
#include "gamesave_svc.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "ui_frame.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_sectors.h"

#define FLASH_MS 140
enum { D_ON, D_INTERVAL, D_START, D_END, D_GOAL, D_UNDO, D_N };      /* the first five are the DRINK_* axes, in order */

static lv_obj_t *s_page, *s_sec, *s_title, *s_cell;
static ui_popring_t s_pop;

static void refresh(void)
{
    if (!s_page) return;
    drink_today();
    const pc_drink_cfg_t *c = drink_cfg();
    const pc_drink_t *s = drink_state();
    static char b[40];
    int nx = drink_next_min();
    if (!clock_time_valid()) snprintf(b, sizeof b, "%s", T(S_NOT_SYNCED) /* Time not set yet */);
    else if (s->cups >= c->goal) snprintf(b, sizeof b, "%s", T(S_GOAL_REACHED) /* Goal reached today */);
    else if (!c->on) snprintf(b, sizeof b, "%s", T(S_REMINDERS_OFF) /* Reminder: off */);
    else if (nx < 0) snprintf(b, sizeof b, "%s", T(S_RESTING) /* Resting */);
    else snprintf(b, sizeof b, "%s %02d:%02d", T(S_NEXT_3) /* Next */, nx / 60, nx % 60);
    if (s_title && strcmp(lv_label_get_text(s_title), b)) lv_label_set_text(s_title, b);
    if (s_sec && !s_title) ui_sectors_set_status(s_sec, b);          /* a page with a frame: the status stays in the content */
    if (!s_sec) return;
    snprintf(b, sizeof b, "%d/%d", s->cups, c->goal);
    ui_sectors_set_hub(s_sec, b);
    ui_sectors_set_progress(s_sec, s->cups, c->goal);
}

static void do_cup(void)
{
    if (!clock_time_valid()) { ui_push_message(T(S_THE_TIME_IS_NOT_SET_YET) /* Time not set yet */); return; }
    drink_cup();
    sfx_play(drink_state()->cups == drink_cfg()->goal ? "win" : "tick");
    refresh();
}
static void tapped(int i, void *user)
{
    (void)user;
    if (i == 0) do_cup();
    else { drink_snooze(); refresh(); }
}
static bool hub_tap(void *user) { (void)user; if (!clock_time_valid()) return false; do_cup(); return true; }

/* ---- the ring ---- */
static const char *cell_label(int i)
{
    static char b[D_N][40];                                /* the ring asks for every cell before it copies any */
    const pc_drink_cfg_t *c = drink_cfg();
    if (i == D_ON) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_REMIND) /* Reminder */, c->on ? T(S_ON_2) /* On */ : T(S_OFF_2) /* Off */);
    else if (i == D_INTERVAL) snprintf(b[i], sizeof b[i], "%s\n%d %s", T(S_EVERY) /* Interval */, pc_drink_interval_min(c->interval), T(S_MIN_2) /* min */);
    else if (i == D_START) snprintf(b[i], sizeof b[i], "%s\n%02d:00", T(S_FROM) /* Start */, c->start_h);
    else if (i == D_END) snprintf(b[i], sizeof b[i], "%s\n%02d:00", T(S_UNTIL) /* End */, c->end_h);
    else if (i == D_GOAL) snprintf(b[i], sizeof b[i], "%s\n%d", T(S_GOAL) /* Goal */, c->goal);
    else snprintf(b[i], sizeof b[i], "%s", T(S_MINUS_ONE) /* One less */);
    return b[i];
}
static const char *armed_text(int i) { (void)i; return T(S_TAP_AGAIN_ONE_CUP_LESS) /* Tap again: count one less */; }
static bool ring_step(int axis, int d)
{
    if (axis < D_INTERVAL || axis > D_GOAL || !drink_step(axis, d)) return false;
    refresh();
    return true;
}
static void ring_pick(int i)
{
    if (i == D_ON) { drink_step(DRINK_ON, 1); ui_popring_relabel(); refresh(); }
    else if (i == D_UNDO) { ui_popring_close(); drink_undo(); refresh(); }
    else if (i < 0) ui_popring_close();
}
static void ring_open(void)
{
    s_pop = (ui_popring_t){ .n = D_N, .label = cell_label, .pick = ring_pick, .step = ring_step, .axis_mask = 1u << D_INTERVAL | 1u << D_START | 1u << D_END | 1u << D_GOAL,
                            .arm_mask = 1u << D_UNDO, .armed = armed_text, .help = &HELP_DRINK };
    ui_popring_open(s_page, &s_pop, 0);
}
static void head_tap(lv_event_t *e) { (void)e; ring_open(); }

/* ---- the page ---- */
static void timer_cb(lv_timer_t *t) { (void)t; refresh(); }
static bool on_knob(int d) { return ui_axis_knob(d) || (s_sec && ui_sectors_knob(s_sec, d)); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_page = ui_obj_guard(p, &s_page);
    s_sec = NULL;
    ui_frame_enable(p);                                  /* the round glass: a pilot page of the content-first frame (ui_frame.h) */
    s_title = NULL;
    if (!ui_frame_active(p)) s_title = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_title);
    ui_frame_option(ui_choice_row(p, UI_SLOT_HEAD, 1, head_tap, &s_cell));
    ui_obj_guard(s_cell, &s_cell);
    if (s_cell) ui_choice_set(s_cell, T(S_MENU) /* Menu */, UI_LOOK_NORMAL);
    const ui_sectors_def_t def = { .n = 2, .rot = 0, .hub_pct = 30, .tap_flash_ms = FLASH_MS, .tones = NULL, .progress = true, .frame = true };
    ui_obj_guard(ui_sectors_create(p, &def, tapped, hub_tap, NULL), &s_sec);
    if (s_sec) {
        lv_obj_move_to_index(s_sec, 0);                  /* behind the HEAD row, the title and Back */
        ui_sectors_set_text(s_sec, 0, T(S_DRANK) /* Drank */);
        ui_sectors_set_text(s_sec, 1, T(S_LATER) /* Later */);
    }
    ui_back_button(p);
    ui_timer_bind(p, timer_cb, 1000, NULL);              /* midnight, the next time, the clock arriving */
    refresh();
}
static void destroy(void) { gamesave_flush(); s_sec = NULL; s_cell = NULL; s_title = NULL; s_page = NULL; }

const ui_screen_t scr_drink = { .name = "clock_drink", .create = create, .destroy = destroy, .on_knob = on_knob, .content = true };

void drink_cmd(const char *a)
{
    int v;
    if (sscanf(a, "tap %d", &v) == 1) { if (ui_top() == &scr_drink && s_sec && v >= 0 && v < 2) ui_sectors_tap(s_sec, v); }
    else if (!strcmp(a, "ring")) { if (ui_top() == &scr_drink) ring_open(); }
    drink_cmd_state(!strncmp(a, "tap ", 4) || !strcmp(a, "ring") ? "" : a);
    refresh();
}
