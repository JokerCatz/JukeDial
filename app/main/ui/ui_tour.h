// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "ui.h"
/* GS: the gesture tutorial (spec 2026-11-16-gestures.md section 4). One page, four steps, each a big drawn hint and one line of text; the user has to DO the gesture to go on:
 * flick up, flick down (the quick settings open, then a flick up closes them), flick left or right, hold 3 s (home_hold_ms). Skip (a button) or a hold at any step before the last ends it
 * too. It shows after the language pick and, until cfg tour_done is stored, at every boot (app_main only: safe, USB and external boots never push it; the simulator's boots do not either:
 * its console "tour" opens it). Finishing stores tour_done=1 and returns to the page below; Settings > Gesture tutorial plays it again. Console "tour dump": "APP tour step=<n> done=<0|1>". */
extern const ui_screen_t scr_tour;
bool tour_needed(void);          /* the card is mounted and cfg has no tour_done */
void tour_dump(void);
