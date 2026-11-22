// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
/* While the screen is off the knob only wakes it (spec 4.7), and for PC_WAKE_LOCKOUT_MS after that the rest of
 * the same turn is swallowed too (review minor, 2026-11-09). */
#define PC_WAKE_LOCKOUT_MS 400
typedef struct { bool screen_on; int64_t woke_ms; } pc_wake_t;

int  pc_wake_on_knob(pc_wake_t *w, int detents, int64_t now_ms);   /* detents to apply to the volume */
void pc_wake_screen_off(pc_wake_t *w);

#ifdef __cplusplus
}
#endif
