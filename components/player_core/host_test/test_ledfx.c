// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_ledfx.h"
#include "pc/pc_ledring.h"

#define UNLIT 0x202020u
static const pc_theme_t *th(void) { return pc_theme_find("neon"); }
static int brightest(const uint32_t *o)
{
    int best = -1, bv = -1;
    for (int k = 0; k < PC_FX_SEGS; k++) {
        int v = (int)(o[k] >> 16 & 255) + (int)(o[k] >> 8 & 255) + (int)(o[k] & 255);
        if (v > bv) { bv = v; best = k; }
    }
    return best;
}
static void render(const pc_ledfx_t *f, uint32_t *o) { pc_fx_render(f, UNLIT, PC_RAMP_THEME, th(), o); }

static void test_names(void)
{
    for (int m = 0; m < PC_FX_COUNT; m++) CHECK_EQ_INT(pc_fx_parse(pc_fx_name((pc_fx_mode_t)m)), m);
    CHECK_EQ_INT(pc_fx_parse("nope"), PC_FX_PITCH);
    CHECK_EQ_INT(pc_fx_parse(NULL), PC_FX_PITCH);
    CHECK_STR(pc_fx_name(PC_FX_SPECTRUM), "spectrum");
}

static void test_geometry(void)
{
    CHECK_EQ_INT(pc_fx_note_seg(60), 0);                /* C lower left */
    CHECK_EQ_INT(pc_fx_note_seg(71), 11);               /* B lower right */
    CHECK_EQ_INT(pc_fx_note_seg(-1), -1);
    for (int n = 0; n < 12; n++) CHECK_EQ_INT((10 + pc_fx_note_seg(n)) % PC_LED_N, pc_led_note_segment(n));   /* same ring */
    CHECK_EQ_INT(pc_fx_mix(0x000000, 0xffffff, 0.0f), 0x000000);
    CHECK_EQ_INT(pc_fx_mix(0x000000, 0xffffff, 1.0f), 0xffffff);
    CHECK_EQ_INT(pc_fx_mix(0x000000, 0xff0000, 2.0f), 0xff0000);   /* clamped */
    uint32_t c = pc_fx_pc_color(60);
    CHECK((c >> 16 & 255) > 200 && (c & 255) < 120);  /* C: red */
    for (int a = 0; a < 12; a++)
        for (int b = a + 1; b < 12; b++) CHECK(pc_fx_pc_color(a) != pc_fx_pc_color(b));
}

static void test_pitch(void)
{
    pc_ledfx_t f;
    uint32_t o[PC_FX_SEGS];
    pc_fx_init(&f, PC_FX_PITCH);
    CHECK(!pc_fx_busy(&f));
    render(&f, o);
    for (int k = 0; k < PC_FX_SEGS; k++) CHECK_EQ_INT(o[k], UNLIT);
    pc_fx_note(&f, 64, 127, 0x00ff00);                  /* E */
    render(&f, o);
    CHECK_EQ_INT(o[4], 0x00ff00);
    CHECK_EQ_INT(o[3], UNLIT);
    CHECK(pc_fx_busy(&f));
    pc_fx_tick(&f, 250);
    render(&f, o);
    CHECK(o[4] != 0x00ff00 && o[4] != UNLIT);
    pc_fx_tick(&f, 400);
    render(&f, o);
    CHECK_EQ_INT(o[4], UNLIT);
    CHECK(!pc_fx_busy(&f));
    pc_fx_note(&f, 60, 1, 0xff0000);                    /* a soft note still shows */
    CHECK(f.lvl[0] >= 0.25f);
}

static void test_rainbow(void)
{
    pc_ledfx_t f;
    uint32_t o[PC_FX_SEGS];
    pc_fx_init(&f, PC_FX_RAINBOW);
    pc_fx_note(&f, 67, 127, 0x123456);                  /* G: its own hue, not the voice colour */
    render(&f, o);
    CHECK_EQ_INT(o[7], pc_fx_pc_color(67));
    CHECK(f.lvl[6] > 0 && f.lvl[6] < f.lvl[7]);          /* soft glow on the neighbours */
    CHECK(f.lvl[8] > 0 && f.lvl[8] < f.lvl[7]);
    CHECK_EQ_INT(f.lvl[2], 0);
}

static void test_ripple(void)
{
    pc_ledfx_t f;
    uint32_t o[PC_FX_SEGS];
    pc_fx_init(&f, PC_FX_RIPPLE);
    pc_fx_note(&f, 65, 127, 0x0000ff);                  /* F: segment 5 */
    render(&f, o);
    CHECK_EQ_INT(brightest(o), 5);
    pc_fx_tick(&f, 250);                                /* the ring spreads both ways */
    render(&f, o);
    CHECK(o[2] != UNLIT && o[8] != UNLIT);
    CHECK(o[5] == UNLIT || (o[5] & 255) < (o[8] & 255));
    for (int i = 0; i < 30; i++) pc_fx_note(&f, 60 + i, 100, 0x0000ff);   /* more than the pool: no harm */
    for (int i = 0; i < 40; i++) pc_fx_tick(&f, 50);
    CHECK(!pc_fx_busy(&f));
}

static void test_comet(void)
{
    pc_ledfx_t f;
    uint32_t o[PC_FX_SEGS];
    pc_fx_init(&f, PC_FX_COMET);
    pc_fx_note(&f, 62, 127, 0xff00ff);                  /* D: 2, first note goes clockwise */
    pc_fx_tick(&f, 200);
    render(&f, o);
    CHECK(brightest(o) > 2);
    CHECK(o[2] != UNLIT);                                /* the tail */
    pc_fx_init(&f, PC_FX_COMET);
    pc_fx_note(&f, 69, 127, 0xff00ff);                  /* A, then F below it: counter-clockwise */
    pc_fx_tick(&f, 3000);
    pc_fx_note(&f, 65, 127, 0xff00ff);
    pc_fx_tick(&f, 200);
    render(&f, o);
    CHECK(brightest(o) < 5);
    for (int i = 0; i < 60; i++) pc_fx_tick(&f, 50);
    CHECK(!pc_fx_busy(&f));
}

static void test_spectrum_off_marks(void)
{
    pc_ledfx_t f;
    uint32_t o[PC_FX_SEGS];
    float bars[64];
    for (int i = 0; i < 64; i++) bars[i] = i / 63.0f;
    pc_fx_init(&f, PC_FX_SPECTRUM);
    pc_fx_note(&f, 60, 127, 0xffffff);                  /* notes do nothing here */
    CHECK_EQ_INT(f.lvl[0], 0);
    for (int i = 0; i < 20; i++) pc_fx_spectrum(&f, bars, 64);
    CHECK(f.band[0] < f.band[11]);
    CHECK(f.band[11] > 0.8f);
    render(&f, o);
    CHECK(o[11] != UNLIT);
    pc_fx_set_mode(&f, PC_FX_OFF);
    render(&f, o);
    for (int k = 0; k < PC_FX_SEGS; k++) CHECK_EQ_INT(o[k], UNLIT);
    pc_fx_note(&f, 60, 127, 0xffffff);
    CHECK(!pc_fx_busy(&f));
    pc_fx_mark(&f, 3, 0xff8000, 1.0f);                  /* beats show in every mode */
    pc_fx_mark(&f, 12, 0xff8000, 1.0f);
    pc_fx_mark(&f, -1, 0xff8000, 1.0f);
    render(&f, o);
    CHECK_EQ_INT(o[3], 0xff8000);
    pc_fx_tick(&f, 600);
    CHECK(!pc_fx_busy(&f));
    pc_fx_set_mode(&f, PC_FX_PITCH);                    /* a brighter note wins over a dim mark */
    pc_fx_mark(&f, 4, 0xff8000, 0.3f);
    pc_fx_note(&f, 64, 127, 0x00ff00);
    render(&f, o);
    CHECK_EQ_INT(o[4], 0x00ff00);
}

int main(void)
{
    RUN(test_names);
    RUN(test_geometry);
    RUN(test_pitch);
    RUN(test_rainbow);
    RUN(test_ripple);
    RUN(test_comet);
    RUN(test_spectrum_off_marks);
    return 0;
}
