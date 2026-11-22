// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The FakePod's two keys as a knob (F1b): pure recogniser, no hardware. The top key (the PMU's power key) is "+" (volume up in the maker's own firmware), the bottom
 * key (GPIO61, also the BOOT strap) is "-". Feed it the two key levels at a steady poll (a few ms), with the clock.
 *   click      one detent for a press, given after PC_KEYS_COMBO_MS (so a second key joining is not mistaken for a click; a shorter click gives it on the release): top +1, bottom -1
 *   hold       from PC_KEYS_HOLD_MS one more detent every PC_KEYS_REPEAT_MS, up to PC_KEYS_CAP_MS of holding, then nothing: the top key's long press is the
 *              PMU's hard power-off (measured 4 s on this board, register 0x27), so the repeat has stopped long before it and a longer hold is never taken for a command
 *   both keys  one PC_KEYS_BOTH event when the second key goes down within PC_KEYS_COMBO_MS of the first (the screen on / off); no detent while both are down, and none
 *              after the first release, until both are up again. A key already down at the first sample (the BOOT strap held at start) is silent until released.
 * A level must hold for PC_KEYS_DEBOUNCE_MS before it counts. One event per call at most (a poll of a few ms never needs more). */
#define PC_KEYS_DEBOUNCE_MS 12
#define PC_KEYS_COMBO_MS    70
#define PC_KEYS_HOLD_MS     450
#define PC_KEYS_REPEAT_MS   140
#define PC_KEYS_CAP_MS      2500
enum { PC_KEYS_NONE = 0, PC_KEYS_UP = 1, PC_KEYS_DOWN = -1, PC_KEYS_BOTH = 2 };   /* UP / DOWN are the detents (+1 / -1) */
typedef struct {
    bool raw[2], st[2];              /* [0] top, [1] bottom: the last raw level, the debounced level */
    uint32_t raw_t[2];               /* when the raw level last changed */
    uint32_t down_t[2];              /* when the debounced level went down (the press, not the debounce) */
    bool init, pending, clicked, combo;   /* pending: a press waits out the combo window; clicked: its first detent is given; combo: silent until both are up */
    int who;                         /* the key of the press (0 top, 1 bottom) */
    uint32_t last_rep;               /* the last detent of the hold (ms) */
    uint16_t hold_ms, rep_ms;        /* LAB (pc_lab.h): the hold / repeat times in use, 0 = PC_KEYS_HOLD_MS / PC_KEYS_REPEAT_MS (a caller sets them per step; the host tests leave them 0) */
    bool rep;                        /* the detent of the last step came from a hold's repeat (F2: the blind layer's double click must not count repeats) */
} pc_keys_t;
void pc_keys_init(pc_keys_t *k);
int  pc_keys_step(pc_keys_t *k, bool top_down, bool bottom_down, uint32_t now_ms);   /* PC_KEYS_* */
bool pc_keys_was_repeat(const pc_keys_t *k);                                          /* the event of the last step was a hold's repeat, not the click */
#ifdef __cplusplus
}
#endif
