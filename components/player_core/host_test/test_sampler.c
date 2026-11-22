// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_sampler.h"

#define RATE 48000
static int16_t ramp[1000], stereo[2000], dc[48000];
static pc_sampler_t s;
static int32_t mix[2 * 256];

static void render(int frames) { memset(mix, 0, sizeof mix); pc_sampler_render(&s, mix, frames); }

static void test_unity_and_end(void)
{
    for (int i = 0; i < 1000; i++) ramp[i] = (int16_t)(i * 30);
    pc_sample_t smp = { ramp, 1000, RATE, 1 };
    pc_sampler_init(&s, RATE);
    CHECK_EQ_INT(pc_sampler_note_on(&s, &smp, 60, 60, 127, 9, true), 0);
    render(256);
    /* level 0.5 at full velocity, same sample in both channels, unity pitch: sample by sample */
    for (int f = 0; f < 256; f++) {
        CHECK_NEAR(mix[2 * f], ramp[f] * 0.5, 2);
        CHECK_EQ_INT(mix[2 * f], mix[2 * f + 1]);
    }
    for (int k = 0; k < 2; k++) render(256);
    CHECK_EQ_INT(pc_sampler_active(&s), 1);
    render(256);                                        /* 1024 frames rendered > 1000: done */
    CHECK_EQ_INT(pc_sampler_active(&s), 0);
    render(16);
    CHECK_EQ_INT(mix[0], 0);
}

static void test_pitch_and_rate(void)
{
    for (int i = 0; i < 1000; i++) ramp[i] = (int16_t)(i * 30);
    pc_sample_t smp = { ramp, 1000, RATE, 1 };
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 72, 60, 127, 0, true);     /* an octave up: twice as fast */
    render(100);
    CHECK_NEAR(mix[2 * 50], ramp[100] * 0.5, 3);
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 48, 60, 127, 0, true);     /* an octave down; 7 semitones up */
    render(101);
    CHECK_NEAR(mix[2 * 100], ramp[50] * 0.5, 3);
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 67, 60, 127, 0, true);
    render(101);
    CHECK_NEAR(mix[2 * 100], (ramp[149] + (ramp[150] - ramp[149]) * 0.83) * 0.5, 3);   /* 100 x 1.4983 */
    pc_sampler_init(&s, RATE);
    pc_sample_t slow = { ramp, 1000, 24000, 1 };           /* a 24 kHz sample at 48 kHz: half speed, interpolated */
    pc_sampler_note_on(&s, &slow, 60, 60, 127, 0, true);
    render(100);
    CHECK_NEAR(mix[2 * 51], (ramp[25] + ramp[26]) / 2 * 0.5, 3);
}

static void test_stereo_velocity(void)
{
    for (int i = 0; i < 1000; i++) { stereo[2 * i] = 10000; stereo[2 * i + 1] = -10000; }
    pc_sample_t smp = { stereo, 1000, RATE, 2 };
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 60, 60, 64, 0, true);
    render(10);
    double g = (64 / 127.0) * (64 / 127.0) * 0.5;
    CHECK_NEAR(mix[0], 10000 * g, 3);
    CHECK_NEAR(mix[1], -10000 * g, 3);
    CHECK_EQ_INT(pc_sampler_note_on(&s, &smp, 60, 60, 0, 0, true), -1);
}

static void test_release_and_retrigger(void)
{
    for (int i = 0; i < 48000; i++) dc[i] = 10000;
    pc_sample_t smp = { dc, 48000, RATE, 1 };
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 60, 60, 127, 0, true);
    pc_sampler_note_off(&s, 0, 60);                     /* one-shot: ignored */
    render(256);
    CHECK(mix[2 * 255] > 4900);
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 60, 60, 127, 0, false);
    pc_sampler_note_off(&s, 0, 60);                     /* sustained: 50 ms fade = 2400 frames */
    render(256);
    CHECK(mix[2 * 255] < 5000 && mix[2 * 255] > 4000);
    for (int k = 0; k < 10; k++) render(256);
    CHECK_EQ_INT(pc_sampler_active(&s), 0);
    /* retrigger: the old voice fades over 2 ms (96 frames) while the new one starts */
    pc_sampler_init(&s, RATE);
    pc_sampler_note_on(&s, &smp, 38, 38, 127, 9, true);
    render(64);
    pc_sampler_note_on(&s, &smp, 38, 38, 127, 9, true);
    CHECK_EQ_INT(pc_sampler_active(&s), 2);
    render(128);
    CHECK_EQ_INT(pc_sampler_active(&s), 1);
    CHECK_NEAR(mix[2 * 127], 5000, 2);
}

static void test_steal_and_all_off(void)
{
    for (int i = 0; i < 48000; i++) dc[i] = 1000;
    pc_sample_t smp = { dc, 48000, RATE, 1 };
    pc_sampler_init(&s, RATE);
    for (int n = 0; n < PC_SAMP_VOICES; n++) CHECK_EQ_INT(pc_sampler_note_on(&s, &smp, 40 + n, 60, 100, 0, true), n);
    CHECK_EQ_INT(pc_sampler_active(&s), PC_SAMP_VOICES);
    CHECK_EQ_INT(pc_sampler_note_on(&s, &smp, 90, 60, 100, 0, true), 0);   /* the oldest */
    CHECK_EQ_INT(s.v[0].note, 90);
    pc_sampler_all_off(&s);
    render(128);
    CHECK_EQ_INT(pc_sampler_active(&s), 0);
    pc_sampler_note_on(&s, &smp, 40, 60, 100, 0, true);
    pc_sampler_kill(&s);
    CHECK_EQ_INT(pc_sampler_active(&s), 0);
    render(8);
    CHECK_EQ_INT(mix[0], 0);
}

static void test_voice_limit(void)
{
    for (int i = 0; i < 48000; i++) dc[i] = 1000;
    pc_sample_t smp = { dc, 48000, RATE, 1 };
    pc_sampler_init(&s, RATE);
    CHECK_EQ_INT(pc_sampler_voices(&s), PC_SAMP_VOICES);
    for (int n = 0; n < 12; n++) pc_sampler_note_on(&s, &smp, 40 + n, 60, 100, 9, true);
    CHECK_EQ_INT(pc_sampler_active(&s), 12);
    pc_sampler_set_voices(&s, 8);                         /* the ones above the limit fade out (2 ms) */
    CHECK_EQ_INT(pc_sampler_voices(&s), 8);
    render(200);
    CHECK_EQ_INT(pc_sampler_active(&s), 8);
    for (int n = 0; n < 6; n++) pc_sampler_note_on(&s, &smp, 70 + n, 60, 100, 9, true);   /* steals within the 8 */
    CHECK_EQ_INT(pc_sampler_active(&s), 8);
    for (int i = 8; i < PC_SAMP_VOICES; i++) CHECK(!s.v[i].on);
    pc_sampler_set_voices(&s, 0);                         /* clamped */
    CHECK_EQ_INT(pc_sampler_voices(&s), 1);
    pc_sampler_set_voices(&s, 99);
    CHECK_EQ_INT(pc_sampler_voices(&s), PC_SAMP_VOICES);
}

int main(void)
{
    RUN(test_voice_limit);
    RUN(test_unity_and_end);
    RUN(test_pitch_and_rate);
    RUN(test_stereo_velocity);
    RUN(test_release_and_retrigger);
    RUN(test_steal_and_all_off);
    return 0;
}
