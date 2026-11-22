// SPDX-License-Identifier: LGPL-2.1-or-later
/* The event-driven YM2149 model (chip_sample) against the plain tick-by-tick one (chip_sample_ref): both are driven by the same random register
 * writes (periods from 0 up, noise, envelope shapes, mixer bits, volumes) and must produce bit-identical samples. */
#define PC_YM_REFERENCE 1
#include "../src/pc_backend_ym.c"
#include "pc_test.h"

static uint32_t rs = 1;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static void test_equal(void)
{
    for (int round = 0; round < 40; round++) {
        ym_t *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b);
        a->clock = b->clock = (round & 1) ? 2000000u : 1789773u;
        chip_reset(a);
        chip_reset(b);
        rs = (uint32_t)round * 7919u + 3;
        for (int step = 0; step < 400; step++) {
            for (int w = 0; w < 6; w++) {                           /* a few register writes, both chips */
                int r = (int)(rnd() % 14);
                uint32_t big = rnd() % 8;
                uint8_t v = (uint8_t)(big < 3 ? rnd() % 4 : big < 6 ? rnd() % 40 : rnd());   /* many tiny periods */
                if (r == 13 && rnd() % 3) continue;
                wr(a, r, v);
                wr_ref(b, r, v);
            }
            if (rnd() % 5 == 0) { a->mute = b->mute = rnd() & 7; a->dirty = true; }
            for (int i = 0; i < 90; i++) {
                float x = chip_sample(a), y = chip_sample_ref(b);
                CHECK(x == y);
            }
        }
        free(a);
        free(b);
    }
}

int main(void)
{
    RUN(test_equal);
    return 0;
}
