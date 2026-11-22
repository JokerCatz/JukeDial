// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "pc/pc_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Gapless joins (ideas 2026-11-10 #1): the audio task fills each block from the current source and,
 * when it ends cleanly, carries on from the pre-opened next one in the same block. */
typedef struct { const pc_backend_t *be; void *h; float gain; } pc_play_src_t;   /* gain: ReplayGain, linear */
typedef struct {
    int frames;        /* written into the block (the caller pads the rest with silence) */
    int cur_rc;        /* cur: 1 still playing, 0 ended, -1 error */
    int next_frames;   /* >= 0: cur ended and next took over with this many frames (the caller swaps); -1 not used */
} pc_fill_t;
/* Fills up to `frames` stereo frames from cur (asking again after short reads), then from next (may be NULL).
 * Each source's frames are multiplied by its gain. */
pc_fill_t pc_gapless_fill(pc_play_src_t *cur, pc_play_src_t *next, float *stereo, int frames);
/* ms left in a source: from its length when known, else the bytes after read_end (the furthest byte read, of size)
 * at the rate bytes_read (all bytes read, skips not counted) took pos_ms; -1 when it cannot tell (yet: the
 * decoder's read-ahead dominates the first seconds) */
#define PC_GAPLESS_MIN_EST_MS 3000
int32_t pc_gapless_remaining_ms(int32_t len_ms, int32_t pos_ms, uint32_t size, uint32_t read_end, uint32_t bytes_read);
#ifdef __cplusplus
}
#endif
