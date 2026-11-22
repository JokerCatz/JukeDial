// SPDX-License-Identifier: LGPL-2.1-or-later
/* Chiptune browsing (spec 4.7): system list -> game list -> track list (three scrolling lists, ruling R23). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"
#include "chip_ctl.h"
#include "cfg.h"
#include "font.h"
#include "screens.h"
#include "ui_kit.h"

#define MAX_SYS 32
#define MAX_TRACKS 256
static char (*s_sys)[64];
static int s_nsys;
static lv_obj_t *s_list, *s_glist, *s_tlist;   /* one list per page: a page under another keeps its own */
static int s_game;
static char (*s_names)[64];

/* ---- tracks ---- */
static void track_cb(lv_event_t *e)
{
    chip_play(s_game, (int)(intptr_t)lv_event_get_user_data(e));
    char sys[64];
    int file, sub;
    if (!chip_now(sys, sizeof sys, &file, &sub)) return;   /* it failed at once (chip_play pushed "cannot play" over this list, which stays under it); what played before may go on */
    ui_pop();                           /* tracks */
    ui_pop();                           /* games */
    now_open();
}
static void tracks_create(lv_obj_t *p, void *arg)
{
    s_game = (int)(intptr_t)arg;
    s_names = calloc(MAX_TRACKS, 64);
    int n = s_names ? chip_track_names(s_game, s_names, MAX_TRACKS) : -1;
    ui_obj_guard(ui_list(p), &s_tlist);
    if (n <= 0) ui_list_row(s_tlist, T(S_CANNOT_READ) /* Cannot read */, UI_PANEL, track_cb, (void *)(intptr_t)0);
    for (int i = 0; i < n; i++) {
        char t[96];
        snprintf(t, sizeof t, "%d  %s", i + 1, s_names[i]);
        ui_list_row(s_tlist, t, UI_PANEL, track_cb, (void *)(intptr_t)i);
    }
    free(s_names);
    s_names = NULL;
    ui_back_button(p);
}
static bool tracks_knob(int d) { return ui_list_knob(s_tlist, d); }
static const ui_screen_t scr_chip_tracks = { .name = "chip_tracks", .create = tracks_create, .on_knob = tracks_knob };

/* ---- games ---- */
static void game_cb(lv_event_t *e) { ui_push(&scr_chip_tracks, lv_event_get_user_data(e)); }
static void games_create(lv_obj_t *p, void *arg)
{
    int n = chip_open_system((const char *)arg);
    printf("APP chip games system=%s n=%d\n", (const char *)arg, n);
    ui_obj_guard(ui_list(p), &s_glist);
    if (n <= 0) ui_list_note(s_glist, T(S_NOTHING_PLAYABLE) /* No playable files */);
    for (int i = 0; i < n; i++) {
        char t[96];
        chip_file_label(i, t, sizeof t);
        ui_list_row(s_glist, t, UI_PANEL, game_cb, (void *)(intptr_t)i);
    }
    ui_back_button(p);
}
static bool games_knob(int d) { return ui_list_knob(s_glist, d); }
const ui_screen_t scr_chip_games = { .name = "chip_games", .create = games_create, .on_knob = games_knob };

/* ---- systems (ruling R23): one row per system, the playing one marked and in view; the play order last, a system row (two values: a tap switches) ---- */
static lv_obj_t *s_order;
static void order_text(void)
{
    lv_label_set_text_fmt(lv_obj_get_child(s_order, 0), "%s: %s", T(S_PLAY_ORDER) /* Play order */, chip_mode() == PC_SEQ_LOOP ? T(S_IN_ORDER) /* Sequential */ : T(S_RANDOM) /* Shuffle */);
}
static void order_cb(lv_event_t *e) { (void)e; chip_set_mode(chip_mode() == PC_SEQ_LOOP ? PC_SEQ_RANDOM : PC_SEQ_LOOP); order_text(); }
static void sys_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    printf("APP pick chip %d\n", i);
    ui_push(&scr_chip_games, s_sys[i]);
}
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    cfg_set_str("last_screen", "chip");
    if (!s_sys) s_sys = calloc(MAX_SYS, 64);
    s_nsys = s_sys ? chip_list_systems(s_sys, MAX_SYS) : 0;
    char playing[64];
    int file, sub;
    if (!chip_now(playing, sizeof playing, &file, &sub)) playing[0] = 0;
    ui_obj_guard(ui_list(p), &s_list);
    lv_obj_t *cur = NULL;
    for (int i = 0; i < s_nsys; i++) {
        const bool on = playing[0] && !strcmp(playing, s_sys[i]);
        lv_obj_t *r = ui_list_row(s_list, s_sys[i], on ? UI_SEL : UI_PANEL, sys_cb, (void *)(intptr_t)i);
        if (on) cur = r;
    }
    if (s_nsys) { s_order = ui_list_row(s_list, "", UI_SYS, order_cb, NULL); order_text(); }
    else ui_list_note(s_list, T(S_NO_CHIPTUNE_ZIP) /* No /chiptune/ *.zip */);
    ui_back_button(p);
    lv_obj_update_layout(s_list);
    ui_list_show(s_list, cur);
}
static bool sys_knob(int d) { return ui_list_knob(s_list, d); }
const ui_screen_t scr_chip = { .name = "chip", .create = create, .on_knob = sys_knob };
