// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_chipseq.h"
#include "pc_test.h"

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static void test_next_subtrack_both_modes(void)
{
    pc_chippos_t p = { 0, 0 };
    pc_chippos_t a = pc_chip_next(p, 3, 5, PC_SEQ_LOOP, 99), b = pc_chip_next(p, 3, 5, PC_SEQ_RANDOM, 99);
    CHECK_EQ_INT(a.file, 0); CHECK_EQ_INT(a.sub, 1);
    CHECK_EQ_INT(b.file, 0); CHECK_EQ_INT(b.sub, 1);
}

static void test_loop_after_last_sub(void)
{
    pc_chippos_t a = pc_chip_next((pc_chippos_t){ 0, 2 }, 3, 5, PC_SEQ_LOOP, 0);
    CHECK_EQ_INT(a.file, 1); CHECK_EQ_INT(a.sub, 0);
    pc_chippos_t w = pc_chip_next((pc_chippos_t){ 4, 0 }, 1, 5, PC_SEQ_LOOP, 0);
    CHECK_EQ_INT(w.file, 0); CHECK_EQ_INT(w.sub, 0);
}

static void test_random_other_file_uniform(void)
{
    int hits[5] = { 0 };
    uint32_t s = 5;
    for (int i = 0; i < 10000; i++) {
        pc_chippos_t n = pc_chip_next((pc_chippos_t){ 2, 3 }, 4, 5, PC_SEQ_RANDOM, lcg(&s));
        CHECK(n.file != 2);
        CHECK_EQ_INT(n.sub, 0);
        CHECK(n.file >= 0 && n.file < 5);
        hits[n.file]++;
    }
    for (int f = 0; f < 5; f++) if (f != 2) CHECK_NEAR(hits[f] / 10000.0, 0.25, 0.02);
}

static void test_random_single_file(void)
{
    pc_chippos_t n = pc_chip_next((pc_chippos_t){ 0, 0 }, 1, 1, PC_SEQ_RANDOM, 77);
    CHECK_EQ_INT(n.file, 0); CHECK_EQ_INT(n.sub, 0);
}

static void test_length(void)
{
    CHECK_EQ_INT(pc_chip_length_ms(-1), 180000);
    CHECK_EQ_INT(pc_chip_length_ms(150000), 150000);
    CHECK_EQ_INT(pc_chip_length_ms(0), 0);
}

static void test_ext(void)
{
    CHECK(pc_chip_supported_ext("a/B.NSF"));
    CHECK(pc_chip_supported_ext("x.nsfe"));
    CHECK(pc_chip_supported_ext("song.vgz"));
    CHECK(pc_chip_supported_ext("Zelda.gbs"));
    CHECK(pc_chip_supported_ext("Song.AHX"));
    CHECK(pc_chip_supported_ext("x.hvl"));
    CHECK(!pc_chip_supported_ext("x.mid"));
    CHECK(!pc_chip_supported_ext("readme.txt"));
    CHECK(!pc_chip_supported_ext("nsf"));
}

int main(void)
{
    RUN(test_next_subtrack_both_modes);
    RUN(test_loop_after_last_sub);
    RUN(test_random_other_file_uniform);
    RUN(test_random_single_file);
    RUN(test_length);
    RUN(test_ext);
    return 0;
}
