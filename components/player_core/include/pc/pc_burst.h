// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Decode-ahead reservoir (the audio task's finished blocks -> a PSRAM FIFO -> a writer task -> the I2S write). Pure logic only: the mode decision and the
 * slot arithmetic. The memory, the lock and the tasks are the BSP's (bsp_audio.c). Why: a FLAC frame of 16384 samples at 176.4/192 kHz decodes in one
 * call of 42-56 ms, longer than the I2S DMA ring (8 x 511 frames = 21 ms at 192 kHz); the audio task writes nothing meanwhile. */
#define PC_BURST_ABOVE_RATE 96000      /* the reservoir runs for a wired native rate above this (176.4 / 192 / 352.8 / 384 kHz); 96 kHz and below are today's path, byte for byte */
#define PC_BURST_SLOTS 25              /* 25 slots of 10 ms = 250 ms of audio */
#define PC_BURST_MS (PC_BURST_SLOTS * 10)

bool     pc_burst_wanted(uint32_t rate);                 /* rate > PC_BURST_ABOVE_RATE */
uint32_t pc_burst_slot_frames(uint32_t rate);            /* frames one slot holds: a 10 ms block (rate / 100, at least 1) */
uint32_t pc_burst_slots_for(uint32_t frames, uint32_t slot_frames);   /* slots a push of `frames` frames takes (a longer block is split across slots) */

/* The FIFO's counters. Indices 0 .. 2*depth-1: fill = (wr - rd) mod 2*depth. One producer, one consumer; the BSP holds the lock around every call. A slot being
 * written to the I2S still counts as full (it is released by pc_bq_done), so the producer never overwrites what the DMA is being fed from. */
typedef struct {
    uint32_t wr, rd;
    uint32_t gen;                  /* bumped by a flush: a take in flight from before it is not released afterwards */
    uint16_t depth;
    uint16_t min_fill;             /* lowest fill seen at a take in this window (slots, the one taken included); depth when none */
    bool     primed;               /* the FIFO has been full once since the last start / flush: only then are the minimum fills meaningful (the first 250 ms of a track fill it) */
} pc_bq_t;

void     pc_bq_init(pc_bq_t *q, uint16_t depth);
uint32_t pc_bq_fill(const pc_bq_t *q);
uint32_t pc_bq_space(const pc_bq_t *q);
uint32_t pc_bq_wr_slot(const pc_bq_t *q);                /* index of the next slot to fill (needs space > 0) */
void     pc_bq_commit(pc_bq_t *q);                       /* the slot at wr_slot is filled */
bool     pc_bq_take(pc_bq_t *q, uint32_t *slot, uint32_t *gen);   /* false: empty. The slot stays counted as full until pc_bq_done; the minimum fill is noted here */
void     pc_bq_done(pc_bq_t *q, uint32_t gen);           /* the taken slot is written out: released, unless a flush came in between */
void     pc_bq_flush(pc_bq_t *q);                        /* drop everything queued (rd = wr), bump gen, unprime */
uint32_t pc_bq_min_ms(const pc_bq_t *q, bool *valid);   /* the window's minimum fill in ms (slots x 10); valid false when the FIFO never filled in it */
void     pc_bq_window_reset(pc_bq_t *q);

/* The start gap (night3 C). After a channel enable, a flush, or a new source the I2S DMA runs (auto_clear: silence) while the first block is still being decoded
 * (a 16384 / 32768-sample FLAC frame, tens of ms: longer than the 21-42 ms ring), so every track start of a high rate counted tens of "underruns" that are silence,
 * not lost audio. The counter is split honestly, nothing hidden: underruns while ARMED are start_n (`start_underruns=` in the log), the ones after the first data
 * reached the writer are the steady count. Inline: the I2S ISR (IRAM) calls pc_gap_underrun. */
typedef struct { volatile uint8_t armed; volatile uint32_t start_n; } pc_gap_t;
static inline void pc_gap_arm(pc_gap_t *g) { g->armed = 1; }
static inline void pc_gap_data(pc_gap_t *g) { g->armed = 0; }                   /* the first data of the start is queued */
static inline int  pc_gap_underrun(pc_gap_t *g) { if (!g->armed) return 0; g->start_n++; return 1; }   /* 1: a start-gap underrun (not steady) */

#ifdef __cplusplus
}
#endif
