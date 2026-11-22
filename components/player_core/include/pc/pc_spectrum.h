// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 64-bar spectrum for the now-playing ring: Hann window, n-point FFT (n = 1024, power of two),
 * log-spaced bands 40 Hz .. 16 kHz, each bar = dB mapped from -60..0 dBFS to 0..1. */
#define PC_SPECTRUM_BARS 64
void pc_spectrum_bars(const float *mono, int n, uint32_t rate, float bars[PC_SPECTRUM_BARS]);
/* same with the dB floor of the bottom of the scale: floor_db clamped to -90..-12 (pc_spectrum_bars = -60) */
void pc_spectrum_bars_db(const float *mono, int n, uint32_t rate, float floor_db, float bars[PC_SPECTRUM_BARS]);
void pc_spectrum_band(int bar, float *lo_hz, float *hi_hz);
#ifdef __cplusplus
}
#endif
