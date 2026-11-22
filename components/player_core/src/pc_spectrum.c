// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_spectrum.h"
#include <math.h>
#include <stdlib.h>

#define NMAX 1024
#define PI_F 3.14159265f

void pc_spectrum_band(int bar, float *lo, float *hi)
{
    const float f0 = 40.0f, f1 = 16000.0f;
    float r = logf(f1 / f0) / PC_SPECTRUM_BARS;
    *lo = f0 * expf(r * (float)bar);
    *hi = f0 * expf(r * (float)(bar + 1));
}

static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {          /* bit reversal */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
/* pc_spectrum_bars guarantees n is a power of two in 2..1024 and fills re/im[0..n-1] first; the analyzer cannot bound the bit-reversed index j < n or the butterfly index b < n */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
#pragma GCC diagnostic pop
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * PI_F / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
                float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
#pragma GCC diagnostic pop
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

void pc_spectrum_bars(const float *mono, int n, uint32_t rate, float bars[PC_SPECTRUM_BARS])
{
    pc_spectrum_bars_db(mono, n, rate, -60.0f, bars);
}

void pc_spectrum_bars_db(const float *mono, int n, uint32_t rate, float floor_db, float bars[PC_SPECTRUM_BARS])
{
    if (n < 2 || (n & (n - 1))) { for (int b = 0; b < PC_SPECTRUM_BARS; b++) bars[b] = 0.0f; return; }   /* contract: n a power of two (header) */
    if (!(floor_db >= -90.0f)) floor_db = -90.0f;   /* also catches NaN */
    if (floor_db > -12.0f) floor_db = -12.0f;
    static float *re, *im;   /* heap, not .bss: 8 KB must not land in internal RAM on the device */
    if (!re) { re = malloc(NMAX * sizeof *re); im = malloc(NMAX * sizeof *im); }
    if (!re || !im) { for (int b = 0; b < PC_SPECTRUM_BARS; b++) bars[b] = 0.0f; return; }
    if (n > NMAX) n = NMAX;
    for (int i = 0; i < n; i++) {
        float w = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)(n - 1));
        re[i] = mono[i] * w;
        im[i] = 0.0f;
    }
    fft(re, im, n);
    float bin_hz = (float)rate / (float)n;
    float norm = 4.0f / (float)n;                 /* Hann coherent gain 0.5, one-sided x2 */
    for (int b = 0; b < PC_SPECTRUM_BARS; b++) {
        float lo, hi;
        pc_spectrum_band(b, &lo, &hi);
        int k0 = (int)(lo / bin_hz), k1 = (int)(hi / bin_hz);
        if (k1 <= k0) k1 = k0 + 1;                /* narrow low bands share a bin */
        if (k1 > n / 2) k1 = n / 2;
        float peak = 0.0f;
        for (int k = k0; k < k1; k++) {
            float m = sqrtf(re[k] * re[k] + im[k] * im[k]) * norm;
            if (m > peak) peak = m;
        }
        float db = peak > 1e-9f ? 20.0f * log10f(peak) : -120.0f;
        float v = (db - floor_db) / -floor_db;
        bars[b] = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    }
}
