// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Interleaved stereo float helpers (samples = frames * 2). */
void pc_mix_add(float *acc, const float *src, int samples, float gain);
void pc_f32_to_s16(const float *src, int16_t *dst, int samples);   /* rounds and clamps */
void pc_gain(float *buf, int samples, float gain);
/* 24-bit samples in the top of 32-bit I2S slots: round(v * 2^23), clamped to [-2^23, 2^23 - 1], times 256 (low byte 0) */
void pc_f32_to_s32(const float *src, int32_t *dst, int samples);
/* narrowing to 16 bit with TPDF dither (Bluetooth sink): v * 32767 + (u1 - u2), u1/u2 uniform in [0, 1) from xorshift32,
 * rounded half away from 0, clipped to int16. A call whose samples are all exactly 0.0f writes zeros and leaves the generator alone. */
typedef struct { uint32_t s; } pc_dither_t;
void pc_dither_init(pc_dither_t *d, uint32_t seed);   /* seed 0 -> 0x9E3779B9 */
void pc_f32_to_s16_tpdf(pc_dither_t *d, const float *src, int16_t *dst, int samples);

/* Linear resampler, float only. */
typedef struct { float pos, step; float last[2]; } pc_rs_t;
void pc_rs_init(pc_rs_t *rs, uint32_t in_rate, uint32_t out_rate);
/* returns output frames; *consumed = input frames used */
int  pc_rs_process(pc_rs_t *rs, const float *in, int in_frames, float *out, int out_cap, int *consumed);

#ifdef __cplusplus
}
#endif
