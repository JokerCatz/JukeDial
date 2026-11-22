// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/moon.h"

uint32_t moon_age_s(int64_t utc_s)
{
    int64_t age = (utc_s - MOON_EPOCH_UTC_S) % MOON_SYNODIC_S;
    if (age < 0) {
        age += MOON_SYNODIC_S;
    }
    return (uint32_t)age;
}

uint8_t moon_phase8(int64_t utc_s)
{
    int64_t age = (int64_t)moon_age_s(utc_s);
    return (uint8_t)(((age * 8 + MOON_SYNODIC_S / 2) / MOON_SYNODIC_S) % 8);
}

const char *moon_phase_name(uint8_t phase8)
{
    static const char *const k_names[8] = {
        "New",           "Waxing crescent", "First quarter", "Waxing gibbous",
        "Full",          "Waning gibbous",  "Last quarter",  "Waning crescent",
    };
    if (phase8 > 7) {
        return "?";
    }
    return k_names[phase8];
}

/* cos(2*pi * num/den) in Q10 (-1024..1024), from a 17-entry quarter-wave
 * table with linear interpolation -- error under 1/500 of full scale,
 * which moves the drawn terminator by well under a pixel at this disc
 * size. Integer throughout. */
static int32_t cos_q10_turns(uint32_t num, uint32_t den)
{
    static const int16_t k_quarter[17] = {
        1024, 1019, 1004, 980, 946, 903, 851, 792, 724,
        650,  569,  483,  392, 297, 200, 100, 0,
    };
    /* Angle as a 0..4095 fraction of a full turn. */
    uint32_t a = (uint32_t)(((uint64_t)num * 4096) / den) & 4095;
    uint32_t quadrant = a >> 10;    /* 0..3 */
    uint32_t within = a & 1023;     /* position inside the quadrant */
    if (quadrant == 1 || quadrant == 3) {
        within = 1023 - within; /* falling quadrants mirror the table */
    }
    uint32_t idx = within >> 6;    /* 0..15 */
    uint32_t frac = within & 63;   /* interpolation fraction /64 */
    int32_t lo = k_quarter[idx];
    int32_t hi = k_quarter[idx + 1];
    int32_t v = lo + ((hi - lo) * (int32_t)frac) / 64;
    return (quadrant == 1 || quadrant == 2) ? -v : v;
}

/* floor(sqrt(v)) for small v, by counting up -- v <= ~121 here. */
static int32_t isqrt_small(int32_t v)
{
    int32_t r = 0;
    while ((r + 1) * (r + 1) <= v) {
        r++;
    }
    return r;
}

uint8_t moon_lit_pct(int64_t utc_s)
{
    int32_t c = cos_q10_turns(moon_age_s(utc_s), (uint32_t)MOON_SYNODIC_S);   /* 1024 new .. -1024 full */
    return (uint8_t)((1024 - c) * 100 / 2048 + ((1024 - c) * 100 % 2048 >= 1024));
}

uint32_t moon_next_event_s(int64_t utc_s, int *is_full)
{
    uint32_t age = moon_age_s(utc_s);
    uint32_t half = (uint32_t)(MOON_SYNODIC_S / 2);
    int full = age < half;                                   /* before full: full is next; after: new is next */
    if (is_full) *is_full = full;
    return (full ? half : (uint32_t)MOON_SYNODIC_S) - age;
}

void moon_disc(int64_t utc_s, uint32_t rows[MOON_DISC_PX])
{
    const int32_t r = MOON_DISC_PX / 2; /* 10; center pixel at (r, r) */
    uint32_t age = moon_age_s(utc_s);
    int32_t c = cos_q10_turns(age, (uint32_t)MOON_SYNODIC_S); /* cos(phase angle) */
    int waxing = age < (uint32_t)(MOON_SYNODIC_S / 2);

    for (int32_t y = -r; y <= r; y++) {
        uint32_t bits = 0;
        for (int32_t x = -r; x <= r; x++) {
            int32_t d2 = x * x + y * y;
            if (d2 > r * r) {
                continue; /* outside the disc */
            }
            int lit;
            if (d2 > (r - 1) * (r - 1)) {
                lit = 1; /* the limb ring, always drawn -- a new moon is an empty circle */
            } else {
                /* Interior: lit between the terminator and the bright
                 * limb. The terminator is the half-ellipse x = c * w(y)
                 * where w(y) is the inner disc's half-chord; the bright
                 * limb is on the right while waxing, the left while
                 * waning. */
                int32_t w = isqrt_small((r - 1) * (r - 1) - y * y);
                int32_t xt = (c * w) / 1024;
                if (c >= 1024) {
                    /* Exactly new (either side of the fold): the
                     * terminator coincides with the bright limb, and the
                     * integer xt would otherwise leave a 1px sliver lit. */
                    lit = 0;
                } else {
                    lit = waxing ? (x >= xt) : (x <= -xt);
                }
            }
            if (lit) {
                bits |= 1u << (x + r);
            }
        }
        rows[y + r] = bits;
    }
}

/* JC player: Traditional Chinese names */
const char *moon_phase_name_zh(uint8_t phase8)
{
    static const char *const k_zh[8] = { "新月", "眉月", "上弦月", "盈凸月", "滿月", "虧凸月", "下弦月", "殘月" };
    return phase8 > 7 ? "?" : k_zh[phase8];
}
