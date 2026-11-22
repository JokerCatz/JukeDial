// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Chip bench core (CHIPEXP Phase 0): decode a file for N seconds through a pc_backend in audio-task blocks (rate / 100 frames =
 * 10 ms at 44.1 kHz), time every block, and report the distribution. Pure (no ESP-IDF, no I/O): the clock, the per-block hook and the
 * small-allocation counter are hooks, so the same core runs on the host (the internal chip bench) and on the board (console `chipbench`).
 * It only measures the backend's render(): no fx, no gain, no sink. */
#include <stdint.h>
#include "pc/pc_backend.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PC_CHIPBENCH_BUDGET_US 10000      /* one block's time budget at a 10 ms block */

typedef struct {
    uint64_t (*now_us)(void);                 /* monotonic microseconds (required) */
    void (*block_hook)(void *u);              /* optional: called after each block, outside the timed part (board: samples internal RAM) */
    void *hook_u;
    const char *ext;                          /* optional: the file extension with its dot (pc_reader_t.ext; AdPlug needs it) */
    uint32_t (*alloc_small)(void);            /* optional: cumulative count of allocations <= 1 KiB (they land in internal RAM); NULL = not counted */
} pc_chipbench_env_t;

typedef struct {
    int      rc;                  /* 0 ok; -1 open failed; -2 start_track failed; -3 bad argument; -4 out of memory */
    uint32_t rate;                /* source rate */
    int      block_frames;
    int      blocks;              /* blocks rendered */
    int      ended;               /* the track ended before `secs` (blocks is then short) */
    int      render_err;          /* render() returned < 0 */
    uint32_t avg_us, p50_us, p99_us, max_us;
    uint32_t rt_pct10;            /* sum of block times / audio time, in tenths of a percent */
    int      over_budget;         /* blocks > PC_CHIPBENCH_BUDGET_US (scaled to the block length) */
    int      max_run_over;        /* longest run of consecutive over-budget blocks (the queue covers about 2) */
    int      silent_blocks;       /* all-zero blocks: a sanity check that it plays */
    uint32_t checksum;            /* FNV-1a over the int16 PCM (host and board must agree) */
    int32_t  alloc_open, alloc_play;   /* allocations <= 1 KiB during open / during the timed blocks; -1 = not counted */
} pc_chipbench_t;

/* data/size: the whole file in memory (it must stay alive). sub: sub-track (start_track when > 0). secs: audio seconds to render (1..3600). */
void pc_chipbench_run(const pc_backend_t *be, const void *data, uint32_t size, int sub, int secs,
                      const pc_chipbench_env_t *env, pc_chipbench_t *out);
/* one line, the same on the host and the board: "blocks= avg_us= p50_us= p99_us= max_us= rt_pct= over10= maxrun= silent= ended= alloc_open= alloc_play= sum=" */
int pc_chipbench_fmt(const pc_chipbench_t *r, char *buf, int n);

#ifdef __cplusplus
}
#endif
