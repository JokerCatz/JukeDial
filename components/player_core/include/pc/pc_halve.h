// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_rate.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Up to three 2:1 stages in a row (source rate -> pipeline rate, pc_pipe_plan decides how many). Works on 480-frame-style
 * float stereo blocks in place. The caller allocates pc_halve_size() bytes (8-aligned, malloc is fine). */
typedef struct pc_halve pc_halve_t;
size_t pc_halve_size(void);                      /* 3 * pc_dec2_size() + header */
/* The caller inits per track and on any source-rate change (T5): set() keeps the history of stages that stay on, it cannot see a new source. */
void   pc_halve_init(pc_halve_t *h);             /* no stage on (plan 0 halvings) */
/* re-plans (pc_pipe_plan). A stage that turns on starts from cleared history; stages that stay on keep theirs. pipe 0 = refused: no stage on, run() is then a
 * pass-through at the SOURCE rate, so the caller must stop on pipe == 0 and not play. */
pc_pipe_t pc_halve_set(pc_halve_t *h, uint32_t src, uint32_t out_max, uint32_t burst, uint8_t limit_why);
int    pc_halve_in_frames(const pc_halve_t *h, int pipe_frames);   /* source frames to read for pipe_frames out: pipe_frames << halvings (pipe_frames >= 0; a negative value is clamped to 0).
                                                                      Largest case: 3 halvings x 480 = 3840 frames = 7680 floats in buf */
int    pc_halve_run(pc_halve_t *h, float *buf, int in_frames);     /* in place; returns the pipeline frames */

#ifdef __cplusplus
}
#endif
