// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_math.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void bsp_math_fill_sine_s16_stereo(int16_t *buf, size_t frames, uint32_t fs, uint32_t hz,
                                   int16_t amp, float *phase_io)
{
    const double step = (double)hz / (double)fs;
    double ph = phase_io ? *phase_io : 0.0;
    for (size_t i = 0; i < frames; i++) {
        double p = ph + step * (double)i;
        int16_t v = (int16_t)lrint(sin(2.0 * M_PI * p) * amp);
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    if (phase_io) {
        *phase_io = (float)(ph + step * (double)frames);
    }
}

void bsp_math_pcm_level_s16(const int16_t *buf, size_t n, int16_t *peak_abs, uint16_t *rms)
{
    int32_t pk = 0;
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = buf[i];
        int32_t a = v < 0 ? -v : v;
        if (a > pk) pk = a;
        acc += (uint64_t)((int64_t)v * v);
    }
    if (pk > 32767) pk = 32767;
    if (peak_abs) *peak_abs = (int16_t)pk;
    if (rms) *rms = n ? (uint16_t)lrint(sqrt((double)acc / (double)n)) : 0;
}

int bsp_math_rail_mv_from_pin_mv(int pin_mv)
{
    return pin_mv * 5 / 2;
}

int bsp_math_format_scan(char *out, size_t n, const uint8_t *addrs, int count)
{
    if (!out || n == 0) return 0;
    out[0] = '\0';
    if (count <= 0) {
        snprintf(out, n, "none");
        return (int)strlen(out);
    }
    size_t len = 0;
    for (int i = 0; i < count && len + 1 < n; i++) {
        int w = snprintf(out + len, n - len, i ? ",0x%02X" : "0x%02X", addrs[i]);
        if (w < 0) break;
        len += (size_t)w;
        if (len >= n) { len = n - 1; break; }
    }
    out[len] = '\0';
    return (int)len;
}

int32_t bsp_math_floor_div(int32_t a, int32_t b)
{
    int32_t q = a / b;
    if ((a % b != 0) && (a < 0)) q--;
    return q;
}

bool bsp_math_has_addr(const uint8_t *addrs, int count, uint8_t addr)
{
    for (int i = 0; i < count; i++) {
        if (addrs[i] == addr) return true;
    }
    return false;
}
