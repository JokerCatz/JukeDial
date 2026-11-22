// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_mix.h"

void pc_mix_add(float *acc, const float *src, int samples, float gain)
{
    for (int i = 0; i < samples; i++) acc[i] += src[i] * gain;
}

void pc_gain(float *buf, int samples, float gain)
{
    for (int i = 0; i < samples; i++) buf[i] *= gain;
}

void pc_f32_to_s16(const float *src, int16_t *dst, int samples)
{
    for (int i = 0; i < samples; i++) {
        float v = src[i] * 32768.0f;
        v += v >= 0 ? 0.5f : -0.5f;
        dst[i] = v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : (int16_t)v;
    }
}

void pc_rs_init(pc_rs_t *rs, uint32_t in_rate, uint32_t out_rate)
{
    rs->pos = 1.0f;          /* first output waits for the first input frame */
    rs->step = (float)in_rate / (float)out_rate;
    rs->last[0] = rs->last[1] = 0.0f;
}

int pc_rs_process(pc_rs_t *rs, const float *in, int in_frames, float *out, int out_cap, int *consumed)
{
    int c = 0, n = 0;
    while (n < out_cap) {
        while (rs->pos >= 1.0f) {
            if (c >= in_frames) goto done;
            rs->last[0] = in[2 * c];
            rs->last[1] = in[2 * c + 1];
            c++;
            rs->pos -= 1.0f;
        }
        if (c >= in_frames) break;
        float a = rs->pos;
        out[2 * n] = rs->last[0] + (in[2 * c] - rs->last[0]) * a;
        out[2 * n + 1] = rs->last[1] + (in[2 * c + 1] - rs->last[1]) * a;
        n++;
        rs->pos += rs->step;
    }
done:
    *consumed = c;
    return n;
}

void pc_f32_to_s32(const float *src, int32_t *dst, int samples)
{
    for (int i = 0; i < samples; i++) {
        float v = src[i] * 8388608.0f;
        v += v >= 0 ? 0.5f : -0.5f;
        int32_t q = v >= 8388607.0f ? 8388607 : v <= -8388608.0f ? -8388608 : (int32_t)v;
        dst[i] = q * 256;            /* not << 8: a negative left shift is undefined */
    }
}

void pc_dither_init(pc_dither_t *d, uint32_t seed) { d->s = seed ? seed : 0x9E3779B9u; }

static inline uint32_t dither_next(pc_dither_t *d)
{
    uint32_t x = d->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    d->s = x;
    return x;
}

void pc_f32_to_s16_tpdf(pc_dither_t *d, const float *src, int16_t *dst, int samples)
{
    int silent = 1;
    for (int i = 0; i < samples; i++) if (src[i] != 0.0f) { silent = 0; break; }
    if (silent) {
        for (int i = 0; i < samples; i++) dst[i] = 0;
        return;
    }
    for (int i = 0; i < samples; i++) {
        float u1 = (float)(dither_next(d) >> 8) * (1.0f / 16777216.0f);   /* 24 bits: exact in float, never reaches 1.0 */
        float u2 = (float)(dither_next(d) >> 8) * (1.0f / 16777216.0f);
        float v = src[i] * 32767.0f + (u1 - u2);
        v += v >= 0 ? 0.5f : -0.5f;
        dst[i] = v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : (int16_t)v;
    }
}
