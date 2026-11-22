// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The content-first page frame (F1a2): a CONTENT page (ui_screen_t .content: every game, the toys, ambient, breath, counter, dice / slot / ideas / lunch /
 * people, the quiz, drink / habits) fills the panel with its content; the chrome (the title, the option pills such as medium speed / campfire / menu, Back) is ONE popup shared by all of them.
 * List / menu / settings / help / explain / card / fx pages have NO frame: on the square panel they keep their title and Back visible, on the round glass they are as they always were.
 * There is NO `...` bubble anywhere. The popup shows (no transition, ~4 s after the last touch / knob turn / borrowed axis step) by:
 *   - mode A (the content takes no taps; the page says ui_frame_mode(page, UI_FRAME_A) or its exit is mode A): a tap on the content;
 *   - a knob (or FakePod key) turn that falls to the volume: the volume changes exactly as before AND the popup shows. A borrowed knob is no volume and shows nothing;
 *     a board without a knob lends a content page its keys only for 4 s after the last touch / key it took (ui.c keys_idle), so "knob free" has the same 4 s timeout there;
 *   - mode B (ui_exit.h: knob borrowed, screen free): a tap PAUSES and shows the popup above Continue / Back; its options move by the knob / keys (a tap on one borrows it);
 *   - mode C (knob AND taps used): only after the 4 s borrow timeout returned the knob (then a turn shows it).
 * Long press anywhere on a content page goes home after cfg home_hold_ms (1500 / 2000 / 3000, default 2000; ui.c ui_hold_*); GS (2026-11-16): a hold anywhere on ANY page does (no Back pill or ring Back cell exists any more; the up flick goes back, ui.c).
 * What a page does (no geometry, no raw widgets):
 *   - the screen table says .content = true (ui.c makes the frame at new_root); ui_back_button* (ui_common.c) arms it: the Back pill becomes chrome; no Back call = no popup.
 *   - ui_list_titled's title is chrome by itself; an option control (ui_pill / ui_axis_button / ui_choice_row) is declared with ui_frame_option(obj); a status text that is a page's own
 *     (the Game of Life generation line) with ui_frame_chrome(obj); status the user needs while playing stays in the content (a hub text, a content label).
 *   - A page with the shared exit (ui_exit.h) is mode UI_FRAME_X: its exit's A pill is the popup's Back and its pause overlay shows the options above it.
 *   - 說明: a page whose options are a ring (ui_popring .help) has it as the ring's last cell, a list as its last row; the popup has no help pill of its own.
 * Objects: scrim (made when the popup first shows, hidden between; transparent, it only catches the tap beside the card) and the card (ONE solid rounded rectangle behind the items: the items are the page's own objects stacked in it while the popup is up, put back where the page had them when it hides; the content is not dimmed); the chrome objects themselves are the page's (hidden while the popup is down). State: a pool of frames (PSRAM statics,
 * this file is built with -msmall-data-limit=0), one per content page root.
 * Console `frame`: "" dumps "APP frame armed=<0|1> mode=<A|C|X> shown=<0|1> chrome=<n>"; "show" / "hide" / "hold" (shown, no 4 s) / "mode a|c" (tests: the page in that mode); "tap back|bg|opt <i> [<cell>]" (an option row:
 * its cell, 0 by default) (a tap on that object, then the dump; "bg" = a tap on the content, mode A); "at back|opt <i>" prints "APP frame at <name> x=<x>,y=<y>" (its screen centre). */
typedef enum { UI_FRAME_C = 0, UI_FRAME_A, UI_FRAME_X } ui_frame_mode_t;   /* X: a page with the shared exit (ui_exit.h): the exit decides when the overlay shows */

void ui_frame_enable(lv_obj_t *page);               /* ui.c, for the root of every page whose ui_screen_t has .content (both panels); no-op where the frame exists */
void ui_frame_mode(lv_obj_t *page, ui_frame_mode_t m);
void ui_frame_hint(lv_obj_t *label, bool hint);     /* GS: a game's status label is popup-only while it says an instruction (tap to serve, paused), in the content while it says the score */
void ui_frame_option(lv_obj_t *obj);                /* an option control of the page (an object whose parent is the page root): chrome */
void ui_frame_chrome(lv_obj_t *obj);                /* shared pieces: a title (ui_list_titled) */
void ui_frame_back(lv_obj_t *back);                 /* ui_common.c: the Back pill of a page; arms the frame of its page */
void ui_frame_band(lv_obj_t *page, int *top, int *bottom);   /* the band the board / cards / dial pieces fill: with a frame under the status line down to the panel edge, else HEAD row..Back */
void ui_frame_field(int *R, int *mid);              /* the round field of a frame page: radius and centre offset (under the status line, down to the margin) */
int ui_frame_field_centred(void);                  /* the radius of a field centred on the panel between a text line above (UI_SLOT_TITLE) and one below (UI_SLOT_BASE) */
int ui_frame_field_r(void);                         /* the radius of a round field of a frame page, centred on the panel */
bool ui_frame_active(lv_obj_t *page);               /* the page root has a frame (every square page; a round pilot page after ui_frame_enable) */
bool ui_frame_adopt_exit(lv_obj_t *page, lv_obj_t *back);   /* ui_exit.c: the exit's A pill becomes the Back of this page's overlay; false: no frame */
bool ui_frame_content_tap(lv_obj_t *page);          /* a tap on the content of a page that takes none (mode A): shows the overlay, true */
void ui_frame_show(lv_obj_t *page, bool hold, bool opts_only);   /* ui_exit.c: the overlay now (hold: no 4 s; opts_only: the options and 說明 above the exit's own pause overlay) */
void ui_frame_exit_items(lv_obj_t *page, lv_obj_t *resume, lv_obj_t *back);   /* ui_exit.c: Continue / Back of the pause overlay join the card's options (ONE card); NULL, NULL on resume */
void ui_frame_hide(lv_obj_t *page);
void ui_frame_volume_turn(void);                    /* ui.c: the knob fell to the volume on the top page */
bool ui_frame_axis_held(void);                      /* an option of the popup holds the borrowed knob (ui.c: it works under the pause overlay too) */
lv_obj_t *ui_frame_card(lv_obj_t *parent, int w, int h, int cy);   /* the popup card (solid, rounded, UI_POP_*) centred at offset cy, made first so its items draw above it; clickable: a tap on it closes nothing */
bool ui_frame_item_at(const char *w, int cell, int *x, int *y);   /* sim: the screen centre of "back" / "row" cell n of the shown popup (the card moves them, so tests tap them here) */
void ui_frame_cmd(const char *arg);
