// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Pure helpers with no ESP-IDF dependency (host-testable). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fill `frames` interleaved stereo frames with a sine of `hz` at `fs`.
 * *phase_io is the running phase in cycles; it is advanced by frames*hz/fs. */
void bsp_math_fill_sine_s16_stereo(int16_t *buf, size_t frames, uint32_t fs, uint32_t hz,
                                   int16_t amp, float *phase_io);

/* Peak absolute value (clamped to 32767) and RMS of n samples. */
void bsp_math_pcm_level_s16(const int16_t *buf, size_t n, int16_t *peak_abs, uint16_t *rms);

/* BATT_ADC divider R6 15k / R63 10k: rail = pin * 25 / 10. */
int bsp_math_rail_mv_from_pin_mv(int pin_mv);

/* "0x15,0x5A" or "none"; always NUL-terminated, truncates. Returns strlen(out). */
int bsp_math_format_scan(char *out, size_t n, const uint8_t *addrs, int count);

/* Floor division (rounds toward -inf), b > 0. Encoder edges -> detents without a double-wide
 * step around zero. */
int32_t bsp_math_floor_div(int32_t a, int32_t b);

/* true if addr is among the first count entries of addrs (count <= 0 -> false). */
bool bsp_math_has_addr(const uint8_t *addrs, int count, uint8_t addr);

#ifdef __cplusplus
}
#endif
