// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* test only: builds a FLAC file in memory (STREAMINFO + fixed-blocksize frames, VERBATIM subframes). lr = interleaved stereo float. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint8_t flac_gen_crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1); }
    return c;
}
static inline uint16_t flac_gen_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0;
    while (n--) { c ^= (uint16_t)(*p++ << 8); for (int i = 0; i < 8; i++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1); }
    return c;
}

/* returns the byte count, 0 if cap is too small */
static inline size_t flac_gen(uint8_t *out, size_t cap, uint32_t rate, int bits, int ch, int block, const float *lr, int frames)
{
    size_t n = 0;
    if (cap < 42) return 0;
    memcpy(out, "fLaC", 4);
    out[4] = 0x80; out[5] = 0; out[6] = 0; out[7] = 34;
    uint8_t *s = out + 8;
    memset(s, 0, 34);
    s[0] = (uint8_t)(block >> 8); s[1] = (uint8_t)block; s[2] = (uint8_t)(block >> 8); s[3] = (uint8_t)block;   /* min = max block */
    uint64_t pk = ((uint64_t)rate << 44) | ((uint64_t)(ch - 1) << 41) | ((uint64_t)(bits - 1) << 36) | (uint64_t)frames;
    for (int i = 0; i < 8; i++) s[10 + i] = (uint8_t)(pk >> (56 - 8 * i));
    n = 42;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return 0;   /* whole bytes only: 12 and 20 bit are not packed here */
    int code = bits == 8 ? 1 : bits == 16 ? 4 : bits == 24 ? 6 : 7;
    int64_t full = ((int64_t)1 << (bits - 1)) - 1;
    uint32_t fno = 0;
    for (int pos = 0; pos < frames; pos += block, fno++) {
        int len = frames - pos < block ? frames - pos : block;
        size_t need = 16 + (size_t)len * ch * (bits / 8 + 1) + 8;
        if (cap - n < need) return 0;
        uint8_t *f = out + n;
        size_t k = 0;
        f[k++] = 0xFF; f[k++] = 0xF8;
        f[k++] = 0x70;                                    /* block size: 16 bit value follows; rate: from STREAMINFO */
        f[k++] = (uint8_t)(((ch - 1) << 4) | (code << 1));
        if (fno < 0x80) f[k++] = (uint8_t)fno;            /* UTF-8 coded frame number */
        else if (fno < 0x800) { f[k++] = (uint8_t)(0xC0 | (fno >> 6)); f[k++] = (uint8_t)(0x80 | (fno & 63)); }
        else if (fno < 0x10000) { f[k++] = (uint8_t)(0xE0 | (fno >> 12)); f[k++] = (uint8_t)(0x80 | ((fno >> 6) & 63)); f[k++] = (uint8_t)(0x80 | (fno & 63)); }
        else { f[k++] = (uint8_t)(0xF0 | (fno >> 18)); f[k++] = (uint8_t)(0x80 | ((fno >> 12) & 63)); f[k++] = (uint8_t)(0x80 | ((fno >> 6) & 63)); f[k++] = (uint8_t)(0x80 | (fno & 63)); }
        f[k++] = (uint8_t)((len - 1) >> 8); f[k++] = (uint8_t)(len - 1);
        f[k] = flac_gen_crc8(f, k); k++;
        for (int c = 0; c < ch; c++) {
            f[k++] = 0x02;                                /* VERBATIM, no wasted bits */
            for (int i = 0; i < len; i++) {
                float v = lr[2 * (size_t)(pos + i) + c];
                v = v > 1 ? 1 : v < -1 ? -1 : v;
                int64_t q = (int64_t)(v * (float)full);
                for (int b = bits - 8; b >= 0; b -= 8) f[k++] = (uint8_t)(q >> b);
            }
        }
        uint16_t c16 = flac_gen_crc16(f, k);
        f[k++] = (uint8_t)(c16 >> 8); f[k++] = (uint8_t)c16;
        n += k;
    }
    return n;
}
