// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* In-field effects of the games (G0b game feel; short motion INSIDE the play field is game content, not a UI transition).
 * One object over a ui_board (its child, same box, not clickable: a touch still lands on the board, so an effect never blocks input), drawn by
 * one draw event; its timer is bound to the object (ui_timer_bind) and runs at 30 fps only while an effect moves, paused otherwise. A frame redraws
 * only each moving effect's box and the think dots' strip, never the whole board (fix round 1). Without a timer (no memory) effects are not made:
 * the board shows the end at once (a line is made at its end). One drop or fade per cell: a new one there replaces the one in flight. Every
 * effect lasts at most ~400 ms (12 frames). With the screen off nothing animates: the effects jump to their end. The state (one PSRAM block)
 * goes with the object, so a page close deletes every effect mid-flight. Colours come from the board's style (game_style.h).
 * Effects (cells are the board's indexes):
 *   drop    the mark of a cell falls `rows` pitches from above into it with a short bounce (the board does not draw the cell meanwhile)
 *   fade    a mark that just left a cell (消失版) shrinks and fades there
 *   line    a line drawn across cells (the win line), growing over 300 ms, then it STAYS until game_fx_clear (a new game); a step between two
 *           cells that are not neighbours (a line across the wrapped edge) is not drawn: the line breaks there
 *   glow    the cells pulse with the highlight colour twice (the four of 四子棋, a refused tap: `warn` = the warning colour, shorter)
 *   burst   a few particles fly out of a cell (a pop, a vanish)
 *   think   three dots pulse under the grid while the computer thinks (loops until off; not an effect of its own: the dots only)
 * Console (`games fx ...`): "" dumps ("APP game_fx n=<effects> think=<0|1> hold=<0|1> run=<0|1> frames=<n> inv_n=<areas> inv_px=<pixels> board_px=<n>": the
 * areas and pixels the last advance invalidated; one "APP game_fx e<i> kind=<k> frame=<f>/<d> cells=<a,b>" each);
 * "step <n>" advances n frames now (deterministic, for tests and pictures) and dumps; "hold 1|0" stops / restarts the timer's own advance. All calls from the LVGL task. */
lv_obj_t *game_fx_create(lv_obj_t *board);              /* NULL: no memory (the game plays without effects) */
void game_fx_drop(lv_obj_t *fx, int cell, int rows);
void game_fx_fade(lv_obj_t *fx, int cell, int mark, const char *text);
void game_fx_line(lv_obj_t *fx, const int *cells, int n);   /* n 2..4, in order along the line */
void game_fx_glow(lv_obj_t *fx, const int *cells, int n, bool warn);
void game_fx_burst(lv_obj_t *fx, int cell);
void game_fx_think(lv_obj_t *fx, bool on);
void game_fx_stop_cell(lv_obj_t *fx, int cell);         /* a drop or fade carrying `cell` ends now (a pop rebuilds its column) */
void game_fx_clear(lv_obj_t *fx);                        /* every effect and the line end now (a hidden cell shows again) */
void game_fx_cmd(const char *arg);
