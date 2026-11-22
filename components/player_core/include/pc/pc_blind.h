// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The FakePod's blind key layer (F2), pure: the screen is off and the music plays, the two keys (pc_keys detents) mean
 *   top click      volume up            bottom click   volume down            (a hold repeats them at once)
 *   top x2         next track           bottom x2      play / pause           (two clicks within PC_BLIND_DBL_MS)
 * both keys together stay "screen on" (pc_keys). A lone click is therefore given PC_BLIND_DBL_MS late (the time a second one may still come); repeats of a hold are
 * volume steps at once, never a double click. One action per call; feed the click events and poll with the clock. */
#define PC_BLIND_DBL_MS 300
enum { PC_BLIND_NONE = 0, PC_BLIND_VOL_UP, PC_BLIND_VOL_DOWN, PC_BLIND_NEXT, PC_BLIND_PLAYPAUSE };
typedef struct { int8_t pend; uint32_t t; uint16_t dbl_ms; } pc_blind_t;   /* dbl_ms: LAB (pc_lab.h) the double-click window in use, 0 = PC_BLIND_DBL_MS; pend: +1 / -1 a click waits for its second, 0 none */
void pc_blind_init(pc_blind_t *b);
int  pc_blind_feed(pc_blind_t *b, int dir /* +1 top, -1 bottom */, bool repeat, uint32_t now_ms);
int  pc_blind_poll(pc_blind_t *b, uint32_t now_ms);       /* the waiting click, once its window has passed: a volume step */
#ifdef __cplusplus
}
#endif
