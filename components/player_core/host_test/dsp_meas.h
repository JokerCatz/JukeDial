// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Test-only measurement helpers (host tests; reused by the 192k/384k tests). Header-only, double precision.
 * Both take interleaved stereo float and look at the LEFT channel only. The caller skips the filter settle time itself
 * (pass lr + 2 * settle_frames) -- nothing here discards samples.
 *
 * meas_tone_db: level in dBFS of the sine at `hz` (0 dB = amplitude 1.0). Single-bin DFT over all `frames`, Hann-windowed
 *   (normalised by the window sum, so a pure sine of amplitude A reads 20 log10 A; the window keeps the neighbouring
 *   tones/leakage of a non-integer number of cycles ~ 100 dB down at 20 bins).
 * meas_thdn_db: THD+N of a sine at `hz`: the best-fit sine+cosine at `hz` (least squares over all frames) is subtracted and
 *   the RMS of what is left is compared with the RMS of the fitted tone, in dB (full band, no window, no weighting). */
#include <math.h>
#include <stdint.h>

static inline double meas_tone_db(const float *lr, int frames, uint32_t rate, double hz)
{
    double re = 0, im = 0, ws = 0, w0 = 2 * M_PI * hz / rate;
    for (int i = 0; i < frames; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / frames);
        double x = lr[2 * (size_t)i] * w;
        re += x * cos(w0 * i);
        im += x * sin(w0 * i);
        ws += w;
    }
    double a = 2 * sqrt(re * re + im * im) / ws;
    return 20 * log10(a > 1e-30 ? a : 1e-30);
}

static inline double meas_thdn_db(const float *lr, int frames, uint32_t rate, double hz)
{
    double w0 = 2 * M_PI * hz / rate, scc = 0, sss = 0, scs = 0, sxc = 0, sxs = 0;
    for (int i = 0; i < frames; i++) {
        double c = cos(w0 * i), s = sin(w0 * i), x = lr[2 * (size_t)i];
        scc += c * c; sss += s * s; scs += c * s; sxc += x * c; sxs += x * s;
    }
    double det = scc * sss - scs * scs, a = (sxc * sss - sxs * scs) / det, b = (sxs * scc - sxc * scs) / det, res = 0;
    for (int i = 0; i < frames; i++) {
        double e = lr[2 * (size_t)i] - a * cos(w0 * i) - b * sin(w0 * i);
        res += e * e;
    }
    double tone = (a * a + b * b) / 2, noise = res / frames;
    return 10 * log10((noise > 1e-60 ? noise : 1e-60) / tone);
}
