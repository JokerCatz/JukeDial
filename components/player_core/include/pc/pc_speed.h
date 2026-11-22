// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Speed playback with the pitch kept (Track B): WSOLA (waveform-similarity overlap-add) on interleaved stereo float.
 * Pull-based: pc_speed_run asks `feed` for the source frames it needs, so the audio task keeps one block of output per 10 ms whatever the
 * speed. Pipeline rates up to PC_SPEED_MAX_RATE only (the buffers are sized for it; the caller turns the stage off above that).
 * Per rate R: overlap 10 ms, grain 40 ms, search +-12 ms; the search runs on a 4:1 decimated mono copy, then refines at full rate.
 * The struct is ~50 KB and is meant to live in PSRAM (EXT_RAM_BSS_ATTR static); pc_speed_init touches only its header. */
#define PC_SPEED_MAX_RATE 48000
#define PC_SPEED_MIN 0.5f
#define PC_SPEED_MAX 2.0f
#define PC_SPEED_OV_MAX (PC_SPEED_MAX_RATE / 100)                         /* 10 ms */
#define PC_SPEED_SEQ_MAX (PC_SPEED_MAX_RATE * 4 / 100)                    /* 40 ms */
#define PC_SPEED_SEEK_MAX (PC_SPEED_MAX_RATE * 12 / 1000)                 /* 12 ms */
#define PC_SPEED_IN_MAX (2 * PC_SPEED_SEEK_MAX + PC_SPEED_SEQ_MAX + 64)
#define PC_SPEED_OUT_MAX (PC_SPEED_IN_MAX + PC_SPEED_OV_MAX + 640)       /* a flush adds the tail and what is left of in[] */
#define PC_SPEED_DEC 4
#define PC_SPEED_RUN_MAX (PC_SPEED_OUT_MAX - PC_SPEED_IN_MAX)             /* the most frames one pc_speed_run call may ask for (1120 at 48 kHz: 23 ms) */
typedef struct {
    int ov, seq, seek;          /* frames at the current rate */
    float speed;
    int in_n;                   /* frames in in[] */
    int skip;                   /* source frames still to read and drop (the speed jumped past the buffer) */
    double nom;                 /* the nominal search centre (frames from the start of in[]) */
    bool have_tail, eos, done;
    int out_n, out_r;
    float win[PC_SPEED_OV_MAX]; /* amplitude-complementary crossfade ramp 0 -> 1 */
    float tail[2 * PC_SPEED_OV_MAX];
    float dref[PC_SPEED_OV_MAX / PC_SPEED_DEC + 1];
    float din[(2 * PC_SPEED_SEEK_MAX + PC_SPEED_OV_MAX) / PC_SPEED_DEC + 2];
    float in[2 * PC_SPEED_IN_MAX];
    float out[2 * PC_SPEED_OUT_MAX];
} pc_speed_t;
/* feed(u, dst, n): fills up to n stereo frames; fewer than n means the source ended (or failed) */
typedef int (*pc_speed_feed_fn)(void *u, float *dst, int n);
/* (re)start: rate <= PC_SPEED_MAX_RATE, speed clamped to PC_SPEED_MIN..PC_SPEED_MAX. Drops everything buffered. */
void pc_speed_init(pc_speed_t *s, uint32_t rate, float speed);
/* writes up to `frames` frames (clamped to PC_SPEED_RUN_MAX: before an iterate out[] holds under `frames`, a flush adds at most PC_SPEED_IN_MAX); fewer only when the source has ended and everything is out (*ended set). */
int pc_speed_run(pc_speed_t *s, float *out, int frames, pc_speed_feed_fn feed, void *u, bool *ended);

/* ---- the settings around it: the values, the rule that turns it off, the crossfade between tracks ---- */
#define PC_SPEED_STEPS 5
#define PC_XFADE_STEPS 4
int pc_speed_norm(int pct);                    /* any stored value -> one of 75 / 100 / 125 / 150 / 200 (100 when it is none of them) */
int pc_speed_step(int pct, int d);             /* signed, wraps */
int pc_xfade_norm(int sec);                    /* -> 0 / 2 / 4 / 6 (0 when it is none of them) */
int pc_xfade_step(int sec, int d);             /* signed, wraps */
/* Too heavy next to the decoders and the encoders: aptX / LDAC encoding, the broadcast, a pipeline above 48 kHz (fx mode bits, pc_fxapp.h:
 * PC_FXM_APTX 2, LDAC 4, CAST 8, HIRES 16, HIRES2 32). Both speed and the crossfade (it runs a second decoder) follow it. */
#define PC_SPEED_OFF_MODES (2 | 4 | 8 | 16 | 32)
bool pc_speed_ok(uint8_t fx_mode_bits);
/* Equal-power crossfade of a block: a[i] = a[i] * cos(th) + b[i] * sin(th), th running from p0 * pi/2 to p1 * pi/2 across the block (p in 0..1;
 * one sin/cos pair per block, the rest is a rotation). The sum of the two powers stays 1; a soft knee above 0.8 keeps a correlated sum (up to +3 dB) from clipping. */
void pc_xfade_mix(float *a, const float *b, int frames, float p0, float p1);
/* the part of a block after the old track ended: the new one alone, its gain going from sin(p * pi/2) to 1 across the block */
void pc_xfade_finish(float *b, int frames, float p);
/* where the crossfade is: 0 at its start, 1 at the end of the old track (left and total in frames; total 0 gives 1) */
float pc_xfade_progress(int64_t left, int64_t total);
#ifdef __cplusplus
}
#endif
