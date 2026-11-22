// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rate.h"
#include "pc_test.h"

/* the audio task's block buffers hold PC_AUDIO_MAX_BLOCK frames: a source above PC_AUDIO_MAX_RATE would write twice that (hi-res survey) */
static void test_rate_ok(void)
{
    CHECK_EQ_INT(PC_AUDIO_MAX_RATE, 384000);            /* hires T13: the pipeline (DAC) runs up to 384k, the APLL tiers */
    CHECK_EQ_INT(PC_SRC_MAX_RATE, 384000);              /* sources up to 384k are accepted (halved on the audio task) */
    CHECK_EQ_INT(PC_AUDIO_MAX_BLOCK, 3840);
    CHECK(pc_rate_ok(44100));
    CHECK(pc_rate_ok(48000));
    CHECK(pc_rate_ok(8000));
    CHECK(pc_rate_ok(88200));
    CHECK(pc_rate_ok(96000));
    CHECK(!pc_rate_ok(0));
    CHECK(pc_rate_ok(96001));
    CHECK(pc_rate_ok(176400));
    CHECK(pc_rate_ok(192000));
    CHECK(pc_rate_ok(192001));
    CHECK(pc_rate_ok(352800));
    CHECK(pc_rate_ok(384000));
    CHECK(!pc_rate_ok(384001));
    CHECK(!pc_rate_ok(0xFFFFFFFFu));
}

/* a block is never longer than the buffers, whatever the rate says */
static void test_block_frames(void)
{
    CHECK_EQ_INT(pc_block_frames(44100), 441);
    CHECK_EQ_INT(pc_block_frames(48000), 480);
    CHECK_EQ_INT(pc_block_frames(96000), 960);
    CHECK_EQ_INT(pc_block_frames(192000), 1920);
    CHECK_EQ_INT(pc_block_frames(384000), 3840);
    CHECK_EQ_INT(pc_block_frames(768000), 3840);        /* never longer than the buffers */
    CHECK_EQ_INT(pc_block_frames(0xFFFFFFFFu), 3840);
    CHECK_EQ_INT(pc_block_frames(0), 1);                /* never 0: the caller divides by the block length */
    CHECK_EQ_INT(pc_block_frames(50), 1);
}

static uint32_t s_r, s_calls;
static uint32_t rate_of(void *h) { (void)h; s_calls++; return s_r; }
static uint32_t rate_via(uint32_t r, bool null_fn)
{
    pc_backend_t be = { .rate = null_fn ? NULL : rate_of };
    uint32_t raw = 1;
    s_r = r;
    s_calls = 0;
    uint32_t got = pc_src_rate(&be, NULL, &raw);
    CHECK_EQ_INT(s_calls, null_fn ? 0 : 1);             /* the backend is asked once */
    CHECK_EQ_INT(raw, null_fn ? 0 : r);                 /* and the log gets what it said */
    return got;
}

/* where a source enters: the backend's rate, or 0 = refused (also a backend without rate()) */
static void test_src_rate(void)
{
    CHECK_EQ_INT(rate_via(44100, false), 44100);
    CHECK_EQ_INT(rate_via(48000, false), 48000);
    CHECK_EQ_INT(rate_via(96000, false), 96000);
    CHECK_EQ_INT(rate_via(192000, false), 192000);      /* the raw source rate up to PC_SRC_MAX_RATE: the audio task halves it */
    CHECK_EQ_INT(rate_via(192001, false), 192001);
    CHECK_EQ_INT(rate_via(384000, false), 384000);
    CHECK_EQ_INT(rate_via(384001, false), 0);
    CHECK_EQ_INT(rate_via(768000, false), 0);
    CHECK_EQ_INT(rate_via(0, false), 0);
    CHECK_EQ_INT(rate_via(44100, true), 0);
}

static void test_out_max_rate(void)
{
    CHECK_EQ_INT(pc_out_max_rate(PC_OUT_WIRED, 384000), PC_AUDIO_MAX_RATE);
    CHECK_EQ_INT(pc_out_max_rate(PC_OUT_WIRED, 48000), 48000);
    CHECK_EQ_INT(pc_out_max_rate(PC_OUT_SINK, 384000), 48000);
}

/* H19: every tier has a row the I2S driver accepts */
static void test_clk_table(void)
{
    static const uint32_t rates[] = { 8000, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    for (unsigned i = 0; i < sizeof rates / sizeof *rates; i++) {
        pc_i2s_clk_t c;
        uint32_t r = rates[i];
        CHECK(pc_i2s_clk_for(r, &c));
        CHECK_EQ_INT(c.rate, r);
        CHECK(c.mclk_mult >= 128 && c.mclk_mult % 64 == 0);
        if (c.src == PC_CLK_XTAL) CHECK(40e6 > 1.99 * r * c.mclk_mult);
        CHECK(c.frames <= 511 && c.frames * 8 <= 4092);
        if (r == 44100 || r == 48000) { CHECK_EQ_INT(c.src, PC_CLK_XTAL); CHECK_EQ_INT(c.mclk_mult, 256); CHECK_EQ_INT(c.desc, 6); CHECK_EQ_INT(c.frames, 480); }
        if (r == 88200) { CHECK_EQ_INT(c.src, PC_CLK_XTAL); CHECK_EQ_INT(c.mclk_mult, 128); CHECK_EQ_INT(c.desc, 8); CHECK_EQ_INT(c.frames, 480); }   /* hires T8b: 8 x 480 above 48k */
        if (r == 176400 || r == 352800) { CHECK_EQ_INT(c.src, PC_CLK_APLL); CHECK_EQ_INT(c.mclk_mult, 128); CHECK_EQ_INT(c.desc, 8); CHECK_EQ_INT(c.frames, 511); }
        if (r == 96000) { CHECK_EQ_INT(c.src, PC_CLK_XTAL); CHECK_EQ_INT(c.mclk_mult, 128); CHECK_EQ_INT(c.desc, 8); CHECK_EQ_INT(c.frames, 480); }
        if (r <= 48000) { CHECK_EQ_INT(c.desc, 6); CHECK_EQ_INT(c.frames, 480); }   /* today's shape: 0 B more internal RAM at <= 48k */
        if (r == 192000 || r == 384000) { CHECK_EQ_INT(c.src, PC_CLK_APLL); CHECK_EQ_INT(c.mclk_mult, 128); CHECK_EQ_INT(c.desc, 8); CHECK_EQ_INT(c.frames, 511); }
    }
    pc_i2s_clk_t c;
    CHECK(!pc_i2s_clk_for(100000, &c));
}

/* hires T12 (Review Focus 2): every rate up to the build's PC_AUDIO_MAX_RATE that has a row runs BCK = 64 fs (2 x 32-bit slots) from an integer
 * divider of its MCLK (mclk_mult / 64); <= 96k stays on the crystal (H21); the first rate above the build is refused by pc_rate_ok */
static void test_clk_table_bck64(void)
{
    static const uint32_t rates[] = { 8000, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600 };
    for (unsigned i = 0; i < sizeof rates / sizeof *rates; i++) {
        pc_i2s_clk_t c;
        uint32_t r = rates[i];
        if (r > PC_AUDIO_MAX_RATE) { CHECK(!pc_rate_ok(r)); continue; }
        CHECK(pc_rate_ok(r));
        CHECK(pc_i2s_clk_for(r, &c));
        CHECK_EQ_INT(c.mclk_mult % 64, 0);                  /* BCK = MCLK / (mult / 64) = 64 fs: an integer divider */
        CHECK(pc_i2s_rate_ok(r, r * 64, 32));
        CHECK_EQ_INT(c.src == PC_CLK_APLL, r > 96000);
    }
    CHECK(!pc_rate_ok(PC_AUDIO_MAX_RATE + 1));
}

/* hires T8b: a channel on one of the table's DMA shapes follows the table on a rate change (reshape); any other shape is the caller's own
 * (the instrument's 3 x 64) and is kept */
static void test_i2s_shape_follows(void)
{
    CHECK(pc_i2s_shape_follows(6, 480));
    CHECK(pc_i2s_shape_follows(8, 480));
    CHECK(pc_i2s_shape_follows(8, 511));
    CHECK(!pc_i2s_shape_follows(3, 64));
    CHECK(!pc_i2s_shape_follows(6, 511));
    CHECK(!pc_i2s_shape_follows(0, 0));
    /* the RAM of the 96k tier: 8 x 480 x 8 B = 30,720 B, +7,680 B over 6 x 480 x 8 = 23,040 B; DMA time 40 ms at 96k (30 ms before) */
    pc_i2s_clk_t lo, hi;
    CHECK(pc_i2s_clk_for(48000, &lo) && pc_i2s_clk_for(96000, &hi));
    CHECK_EQ_INT((int)hi.desc * hi.frames * 8 - (int)lo.desc * lo.frames * 8, 7680);
    CHECK_EQ_INT((int)((uint64_t)hi.desc * hi.frames * 1000 / 96000), 40);
}

static void test_i2s_rate_ok(void)
{
    CHECK(pc_i2s_rate_ok(96000, 6144000, 32));
    CHECK(pc_i2s_rate_ok(44100, 2822402, 32));
    CHECK(!pc_i2s_rate_ok(192000, 6144000, 32));
    CHECK(!pc_i2s_rate_ok(192000, 12349440, 32));
    CHECK(!pc_i2s_rate_ok(192000, 0, 32));
    CHECK(pc_i2s_rate_ok(48000, 1536000, 16));
    CHECK(!pc_i2s_rate_ok(48000, 1536000, 32));
}

static void test_tap_decim(void)
{
    CHECK_EQ_INT(pc_tap_decim(44100), 1);
    CHECK_EQ_INT(pc_tap_decim(48000), 1);
    CHECK_EQ_INT(pc_tap_decim(88200), 2);
    CHECK_EQ_INT(pc_tap_decim(96000), 2);
    CHECK_EQ_INT(pc_tap_decim(192000), 4);
    CHECK_EQ_INT(pc_tap_decim(384000), 8);
}

/* hires T5 fix 1 (M5): a mixer is never halved, so its own rate must be a pipeline rate the DAC has a clock row for */
static void test_mixer_rate_ok(void)
{
    CHECK(pc_mixer_rate_ok(8000));
    CHECK(pc_mixer_rate_ok(32000));
    CHECK(pc_mixer_rate_ok(44100));
    CHECK(pc_mixer_rate_ok(48000));
    CHECK(pc_mixer_rate_ok(88200));
    CHECK(pc_mixer_rate_ok(96000));
    CHECK(!pc_mixer_rate_ok(0));
    CHECK(!pc_mixer_rate_ok(50000));                    /* <= PC_AUDIO_MAX_RATE but no clock row */
    CHECK(!pc_mixer_rate_ok(64000));
    CHECK(!pc_mixer_rate_ok(96001));
    CHECK(pc_mixer_rate_ok(176400));                    /* hires T12: the APLL rows are pipeline rates */
    CHECK(pc_mixer_rate_ok(192000));
    CHECK(pc_mixer_rate_ok(384000));                    /* hires T13: the top APLL row is a pipeline rate */
    CHECK(!pc_mixer_rate_ok(768000));
}

/* the words of APP pipe / audio info (one table for the board and the simulator) */
static void test_pipe_why_name(void)
{
    CHECK_STR(pc_pipe_why_name(PC_PIPE_NATIVE), "native");
    CHECK_STR(pc_pipe_why_name(PC_PIPE_CAP), "cap");
    CHECK_STR(pc_pipe_why_name(PC_PIPE_SINK), "sink");
    CHECK_STR(pc_pipe_why_name(PC_PIPE_BURST), "burst");
    CHECK_STR(pc_pipe_why_name(PC_PIPE_CLOCK), "clock");
    CHECK_STR(pc_pipe_why_name(5), "?");
    CHECK_STR(pc_pipe_why_name(255), "?");
}

int main(void)
{
    RUN(test_mixer_rate_ok);
    RUN(test_pipe_why_name);
    RUN(test_out_max_rate);
    RUN(test_clk_table);
    RUN(test_clk_table_bck64);
    RUN(test_i2s_shape_follows);
    RUN(test_i2s_rate_ok);
    RUN(test_tap_decim);
    RUN(test_rate_ok);
    RUN(test_block_frames);
    RUN(test_src_rate);
    return 0;
}
