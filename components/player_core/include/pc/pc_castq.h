// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Auracast broadcast quality (cfg cast_q), three values in display order: 0 = 24 kHz stereo (24_2: 60 B per 10 ms), 1 = 48 kHz stereo
 * (48_4: 120 B, the default), 2 = 48 kHz stereo high (48_6: 155 B). Anything else reads as 1 (no migration of old stored values). */
/* the value when nothing is stored: 48 kHz stereo. Every reader of cfg cast_q (bt_svc, the 現況 line, the settings table) uses this. */
#define PC_CAST_Q_DEFAULT 1
int pc_cast_quality(int stored);
/* the quality one step (d, either sign) along 0 -> 1 -> 2, wrapping */
int pc_cast_q_step(int cur, int d);
int pc_cast_q_rate(int q);    /* Hz */
int pc_cast_q_nbyte(int q);   /* LC3 octets per channel per 10 ms frame (the BIS SDU size) */
const char *pc_cast_q_label(int q, bool zh);   /* "24k 立體聲" "48k 立體聲" "48k 高位元率" (en: 24k stereo, 48k stereo, 48k high); q is normalised first */

#ifdef __cplusplus
}
#endif
