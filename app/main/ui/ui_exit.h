// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The shared exit (G4b): a full-screen field page (a toy, a game) has no permanent Back button and NO `...` bubble. Each page attaches ONE exit and declares how
 * the popup (ui_frame.h) is reached:
 *  A  taps unused: a tap on the field (ui_exit_tap, called first in the field's touch path) shows the popup, and a knob turn that falls to the volume shows it too.
 *  B  knob borrowed, taps unused while the game runs: a tap on the field PAUSES (def->running() permitting) and shows Continue / Back with the popup's options above; the options move by the
 *     knob / keys. No page is B today (`exit mode b` makes any page B for tests).
 *  C  knob AND taps both used (every game, the toys that poke / drop / pop): no tap trigger; the popup shows when a knob turn falls to the volume (after the 4 s borrow ran out).
 * Pause (B, `exit pause`) = def->pause() (the page's own) and then EVERY timer the page made is paused (ui_top_timers) so nothing moves under the overlay; Continue resumes those and calls def->resume().
 * The overlay is a scrim over the whole page that takes every touch; while it is up the knob and the keys are the volume only (ui.c knob_hook), except an option of the popup the user borrowed;
 * Back pops the page. The escape hatch of every content page is a hold anywhere (ui.c ui_hold_*, cfg home_hold_ms).
 * Objects: the A pill is made at attach (hidden) as a child of the page root and so die with it; the pause overlay is made when it opens and deleted when it closes. State: PSRAM statics.
 * Console `exit`: "" dumps "APP exit attached=<0|1> mode=<A|B|C> a=<0|1> pause=<0|1> frozen=<n> hold=<0|1>"; "mode a|b|c" the page runs in that mode (tests); "tap field" the field's touch path
 * ("APP exit field <0|1>"); "show" shows the A pill (and holds it); "hide"; "hold 1|0"; "pause" / "resume"; "tap back|resume" a tap on that button (each then dumps); "at back|resume" prints its screen
 * centre ("APP exit at <name> x=<x>,y=<y>"). */
typedef enum { UI_EXIT_A = 0, UI_EXIT_B, UI_EXIT_C } ui_exit_mode_t;
typedef struct {
    ui_exit_mode_t mode;
    bool (*running)(void *user);             /* B: the game is in flight (a tap pauses it); NULL: always */
    void (*pause)(void *user);               /* the page's own pause (stop its clip, an arena pause); may be NULL */
    void (*resume)(void *user, uint32_t paused_ms);   /* its undo, and the time it was paused (a page that times itself by lv_tick moves its start by it); may be NULL */
} ui_exit_def_t;

/* On the page root `page` (call last in create: the objects must be above the field); def must outlive the page. Returns the A pill. */
lv_obj_t *ui_exit_attach(lv_obj_t *page, const ui_exit_def_t *def, void *user);
bool ui_exit_tap(void);                      /* a touch on the field (CF2): A shows the popup, B pauses a running game + popup; true = consumed, the page does nothing else with it */
void ui_exit_volume_turn(void);              /* ui.c: the knob fell to the volume on the top page: show the A pill if the page has an exit */
bool ui_exit_in_flight(void);                /* CF2: the page's .running says a round is on (no knob: keys are the game's then) */
bool ui_exit_paused(void);                   /* the pause overlay is up */
void ui_exit_cmd(const char *arg);
