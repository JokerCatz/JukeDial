// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_dec2.h"
#include "pc/pc_halve.h"

#define HDR 16                                   /* header bytes, keeps the stages 8-aligned */

struct pc_halve { uint8_t halvings; };

size_t pc_halve_size(void) { return HDR + 3 * pc_dec2_size(); }

static pc_dec2_t *stage(pc_halve_t *h, int i) { return (pc_dec2_t *)((uint8_t *)h + HDR + (size_t)i * pc_dec2_size()); }

void pc_halve_init(pc_halve_t *h) { memset(h, 0, pc_halve_size()); }

pc_pipe_t pc_halve_set(pc_halve_t *h, uint32_t src, uint32_t out_max, uint32_t burst, uint8_t limit_why)
{
    pc_pipe_t p = pc_pipe_plan(src, out_max, burst, limit_why);
    uint8_t n = p.pipe ? p.halvings : 0;
    for (int i = h->halvings; i < n; i++) pc_dec2_init(stage(h, i));   /* newly on: cleared history */
    h->halvings = n;
    return p;
}

int pc_halve_in_frames(const pc_halve_t *h, int pipe_frames) { return pipe_frames < 0 ? 0 : pipe_frames << h->halvings; }

int pc_halve_run(pc_halve_t *h, float *buf, int in_frames)
{
    int n = in_frames;
    for (int i = 0; i < h->halvings; i++) n = pc_dec2_run(stage(h, i), buf, n, buf);
    return n;
}
