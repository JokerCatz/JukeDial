// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_ledring.h"

static void test_segments(void)
{
    /* 12 semitones on 12 of the 16 rim segments, rising clockwise from lower left; the bottom 4 stay free */
    CHECK_EQ_INT(pc_led_note_segment(60), 10);          /* C */
    CHECK_EQ_INT(pc_led_note_segment(66), 0);           /* F# at the top */
    CHECK_EQ_INT(pc_led_note_segment(71), 5);           /* B */
    CHECK_EQ_INT(pc_led_note_segment(48), 10);          /* octave does not matter */
    int used[16] = { 0 };
    for (int n = 0; n < 12; n++) used[pc_led_note_segment(n)]++;
    for (int k = 0; k < 16; k++) CHECK_EQ_INT(used[k], k >= 6 && k <= 9 ? 0 : 1);
    CHECK_EQ_INT(pc_led_note_segment(-1), -1);
}

static void test_hit_decay(void)
{
    pc_ledring_t r;
    pc_led_init(&r);
    pc_led_hit(&r, 62, 1, 127);
    int k = pc_led_note_segment(62);
    CHECK_EQ_INT(r.level[k], 255);
    CHECK_EQ_INT(r.voice[k], 1);
    pc_led_hit(&r, 74, 2, 20);                          /* same segment, softer: louder level stays, voice follows */
    CHECK_EQ_INT(r.level[k], 255);
    CHECK_EQ_INT(r.voice[k], 2);
    pc_led_decay(&r, 250);
    CHECK(r.level[k] > 100 && r.level[k] < 160);
    pc_led_decay(&r, 1000);
    CHECK_EQ_INT(r.level[k], 0);
    pc_led_hit(&r, 60, 0, 1);                           /* quiet notes still show */
    CHECK(r.level[10] >= 64);
    CHECK_EQ_INT(pc_led_active(&r), 1);
}

static void test_beats(void)
{
    CHECK_EQ_INT(pc_led_beat_segment(0, 4), 11);
    CHECK_EQ_INT(pc_led_beat_segment(1, 4), 14);
    CHECK_EQ_INT(pc_led_beat_segment(2, 4), 1);
    CHECK_EQ_INT(pc_led_beat_segment(3, 4), 4);
    CHECK_EQ_INT(pc_led_beat_segment(4, 4), 11);       /* wraps with the bar */
    CHECK_EQ_INT(pc_led_beat_segment(1, 3), 0);        /* 3/4: the middle beat at the top */
    for (int n = 2; n <= 7; n++) {                     /* every beat on its own visible segment */
        int used[16] = { 0 };
        for (int i = 0; i < n; i++) {
            int k = pc_led_beat_segment(i, n);
            CHECK(k < 6 || k > 9);
            CHECK_EQ_INT(used[k]++, 0);
        }
    }
    pc_ledring_t r;
    pc_led_init(&r);
    pc_led_set(&r, 3, 1, 300);
    CHECK_EQ_INT(r.level[3], 255);
    pc_led_set(&r, 16, 1, 100);                        /* out of range: ignored */
}

static void test_pads(void)
{
    CHECK_EQ_INT(pc_pad_hit(180, 180), -1);            /* centre: the kit name, not a pad */
    CHECK_EQ_INT(pc_pad_hit(180, 340), -1);            /* bottom: the page icons */
    CHECK_EQ_INT(pc_pad_hit(170, 60), 3);              /* just left of the top */
    CHECK_EQ_INT(pc_pad_hit(190, 60), 4);              /* just right */
    CHECK_EQ_INT(pc_pad_hit(70, 250), 0);              /* lower left */
    CHECK_EQ_INT(pc_pad_hit(290, 250), 7);             /* lower right */
    CHECK_EQ_INT(pc_pad_hit(5, 5), -1);                /* outside the circle */
    int used[16] = { 0 };
    for (int p = 0; p < PC_PADS; p++) {
        int k = pc_pad_segment(p);
        CHECK(k < 6 || k > 9);
        CHECK_EQ_INT(used[k]++, 0);
        double a = pc_pad_angle(p) * M_PI / 180;      /* the pad's own centre hits it */
        CHECK_EQ_INT(pc_pad_hit(180 + (int)(110 * sin(a)), 180 - (int)(110 * cos(a))), p);
    }
    CHECK_EQ_INT(pc_pad_segment(0), 11);
    CHECK_EQ_INT(pc_pad_segment(7), 4);
    for (int p = 0; p < PC_PADS; p++)                  /* mirror pads, mirror segments (visible index v + v' = 11) */
        CHECK_EQ_INT((pc_pad_segment(p) + 6) % 16 + (pc_pad_segment(7 - p) + 6) % 16, 11);
    CHECK_EQ_INT(pc_pad_hit(73, 270), -1);             /* -130 degrees: the page icons' corner, not a pad */
    CHECK_EQ_INT(pc_pad_hit(287, 270), -1);
}

int main(void)
{
    RUN(test_beats);
    RUN(test_pads);
    RUN(test_segments);
    RUN(test_hit_decay);
    return 0;
}
