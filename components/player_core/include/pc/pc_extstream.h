// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T5: the sound card's stream as pure code (spec 2026-11-15-external-mode.md section 3): the PSRAM ring between the USB pump (writer) and the audio task
 * (reader), the conversion of what the computer sent (16 / 24 / 32 bit little-endian stereo) to float, and the feedback controller. No TinyUSB, no RTOS.
 *
 * AUDIO SAFETY (the review focus): this code never produces a sample above full scale times the gain it is handed, and it clamps that gain to [0, 1] itself:
 * a gain above 1 (or NaN) is not a thing it can be asked for. Everything it renders goes to audio_svc_play_mixer's render callback only: the device volume, the
 * fixed limiter and pc_rv_block are behind it, in audio_svc.c, unchanged.
 *
 * Threads: one writer (the pump task: set_format, write, flush), one reader (the audio task: render). The format and the fill are 32-bit words moved with
 * acquire / release atomics; the reader owns the read index and also does a flush (it skips what is in the ring when it sees the format change). */
#define PC_EXTSTREAM_TARGET_MS 20      /* the water level the feedback holds (ring + endpoint FIFO) */
#define PC_EXTSTREAM_HIGH_MS   35      /* a write that would take the ring above this drops what does not fit (counted as an overrun) */
#define PC_EXTSTREAM_RING_MS   40      /* the ring's size at the largest format */
#define PC_EXTSTREAM_RING_BYTES 122880 /* 384000 Hz * 40 ms * 2 channels * 4 bytes: fixed, no re-allocation at a rate change (a multiple of 24: every frame size divides it, but after a format change the indexes are not aligned: reads wrap per byte) */
#define PC_EXTSTREAM_START_VOL 20      /* the device volume the mode starts at (never above the saved one) */

int pc_extstream_start_volume(int saved);   /* min(saved, PC_EXTSTREAM_START_VOL), at least 0: set BEFORE the first block */

typedef struct {
    uint8_t *buf;
    uint32_t cap;                      /* bytes in use: buf size rounded down to a multiple of 24 */
    volatile uint32_t cfg;             /* (rate << 4) | bytes per sample (2, 3, 4); 0 = no stream */
    volatile uint32_t fill;            /* bytes in the ring */
    volatile uint32_t epoch;           /* +1 at every flush request */
    volatile uint32_t flush_to;        /* the writer's byte count (wcount) at the flush request: the reader skips what was written up to there, not what came after */
    uint32_t wcount, rcount;           /* bytes written / read in total (wrapping), writer / reader only */
    uint32_t wr, rd;                   /* byte indexes, writer / reader only */
    /* reader only */
    uint32_t seen_cfg, seen_epoch;
    volatile uint32_t primed;          /* the reader has seen the ring at the water level and plays (the writer reads it: no overrun is counted while nothing plays) */
    float gl, gr;                      /* the last block's gains: a change ramps over the block */
    volatile uint32_t underruns;       /* the audio task found less than a block (a break in the sound) */
    volatile uint32_t overruns;        /* the pump dropped samples because the ring was above the high mark */
    volatile uint32_t drops;           /* frames dropped by overruns (the pump counts them) */
} pc_extstream_t;

void pc_extstream_init(pc_extstream_t *s, void *buf, uint32_t cap);
/* writer: rate 1..384000 and bytes per sample 2 / 3 / 4 (stereo), or 0 / 0 = no stream. A format that is not one of those is "no stream". Flushes too. */
void pc_extstream_set_format(pc_extstream_t *s, uint32_t rate, int bytes);
void pc_extstream_flush(pc_extstream_t *s);   /* writer: empty the ring (the reader skips what is in it at its next block) and play again only from the water level */
uint32_t pc_extstream_rate(const pc_extstream_t *s);
int pc_extstream_bytes(const pc_extstream_t *s);
/* writer: whole frames only (bytes is rounded down to the frame size). Returns the bytes taken; the rest is dropped (overruns +1) when the ring would pass the high mark. */
uint32_t pc_extstream_write(pc_extstream_t *s, const uint8_t *data, uint32_t bytes);
bool pc_extstream_playing(const pc_extstream_t *s);           /* the reader has primed and plays (the feedback integrates only then) */
void pc_extstream_discard(pc_extstream_t *s);                  /* reader: drop everything in the ring and wait for the water level again (the limiter gate calls it every block) */
uint32_t pc_extstream_fill_frames(const pc_extstream_t *s);   /* frames in the ring */
uint32_t pc_extstream_target_frames(const pc_extstream_t *s); /* the water level in frames at the current rate (0: no stream) */
/* reader: exactly `frames` stereo float frames into out, always. While the ring is filling (the first PC_EXTSTREAM_TARGET_MS after a start, a format change, an underrun) or with no
 * stream: silence. Every sample is the stored one times min(max(gain, 0), 1) (a ramp from the last block's gain over the block), never larger than 1.0 in magnitude. */
int pc_extstream_render(pc_extstream_t *s, float *out, int frames, float gain_l, float gain_r);

/* ---- the feedback value (16.16 samples per high-speed micro-frame) ---- */
typedef struct {
    uint32_t rate, nominal, value;
    uint32_t target;                   /* frames */
    float integ;                       /* frames * seconds */
    uint64_t t0;                       /* window start, us (0 = none) */
    uint64_t t_last;
    float last_fill, area;             /* the window's integral of the fill, frames * seconds */
} pc_extfb_t;
#define PC_EXTFB_WINDOW_US 10000       /* one audio block: the sawtooth of 10 ms reads averages out over exactly one window */
void pc_extfb_init(pc_extfb_t *f, uint32_t rate);                 /* the nominal value for rate; the integrator cleared */
uint32_t pc_extfb_nominal(uint32_t rate);                         /* rate / 8000 in 16.16, rounded */
/* called by the pump about every millisecond with the time (us) and the fill (frames: ring + endpoint FIFO); returns the value to send. Clamped to nominal +- 1 sample (Windows FMT-2.0). */
uint32_t pc_extfb_update(pc_extfb_t *f, uint64_t now_us, uint32_t fill_frames, bool playing);   /* playing = pc_extstream_playing: while the ring is only filling (start, after an underrun, a stalled host) the I part is held, so no wind-up */

#ifdef __cplusplus
}
#endif
