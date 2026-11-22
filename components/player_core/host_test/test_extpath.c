// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extpath.h"
#include "pc/pc_volume.h"
#include "pc_test.h"
#include <stdint.h>

static uint8_t ring[PC_EXTSTREAM_RING_BYTES];
static pc_extstream_t S;
static pc_extuac_t U;
static float buf[2 * 3840];
static uint8_t blk8[8 * 3840];
static void *xalloc(size_t n) { return calloc(1, n); }
static void xfree(void *p) { free(p); }
static const uint32_t R[] = { 44100, 48000 };

/* the host sends 10 ms of a full-scale square wave (+-1.0 at 32 bit) per block */
static void host_block(int frames)
{
    for (int i = 0; i < frames; i++) {
        uint32_t v = (i / 16) & 1 ? 0x7FFFFFFFu : 0x80000000u;
        memcpy(blk8 + 8 * i, &v, 4); memcpy(blk8 + 8 * i + 4, &v, 4);
    }
    pc_extstream_write(&S, blk8, (uint32_t)frames * 8);
}

/* the whole path as audio_svc runs it: idle blocks (the hook alone), then the mixer's blocks: render, hook, then the device volume. Every sample that would reach the I2S is checked. */
static void run_rate(uint32_t rate, int vol, bool unity_bug, int idle_blocks)
{
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extuac_init(&U, R, 2);
    pc_extpath_t *p = pc_extpath_create(xalloc, &S, &U);
    CHECK(p != NULL);
    int frames = (int)(rate / 100);
    for (int i = 0; i < idle_blocks; i++) { memset(buf, 0, sizeof buf); pc_extpath_post(buf, frames, rate, -1, p); for (int k = 0; k < 2 * frames; k++) CHECK(buf[k] == 0.0f); }   /* idle: never a sound */
    pc_extstream_set_format(&S, rate, 4);
    float vg = pc_volume_gain(vol);
    float peak_wire = 0, peak_hook = 0;
    int first_sound = -1, silent_before_ready = 0;
    for (int b = 0; b < 200; b++) {
        host_block(frames);
        host_block(frames);                                                 /* two blocks in, one read: the ring sits at its high mark (writes beyond it are dropped) */
        CHECK_EQ_INT(pc_extpath_render(buf, frames, p), frames);           /* ALWAYS the full count */
        float pre = 0;
        for (int k = 0; k < 2 * frames; k++) { CHECK(buf[k] <= 1.0f && buf[k] >= -1.0f); if (fabsf(buf[k]) > pre) pre = fabsf(buf[k]); }
        if (first_sound < 0 && pre > 0) { first_sound = b; CHECK(pc_extpath_ready(p) || b > 0); }
        if (first_sound < 0) silent_before_ready++;
        pc_extpath_post(buf, frames, rate, 0, p);
        for (int k = 0; k < 2 * frames; k++) {
            float a = fabsf(buf[k]);
            if (a > peak_hook) peak_hook = a;
            float wire = unity_bug ? a : a * vg;                           /* pc_rv_block: the device volume (no sink in this mode: never unity) */
            if (wire > peak_wire) peak_wire = wire;
        }
        if (b >= 20) { CHECK(pc_extpath_ready(p)); }
    }
    CHECK(first_sound >= 0 && first_sound <= 12);                           /* the sound starts within the first dozen blocks (about 100 ms), after the limiter is in */
    CHECK(silent_before_ready >= (idle_blocks ? PC_EXTPATH_GATE_BLOCKS : PC_EXTPATH_GATE_BLOCKS + 1));                                        /* never in the first blocks: the limiter is installed and warmed first */
    CHECK(peak_hook <= PC_EXTPATH_CEIL);                                    /* the hook's output: at the ceiling at most (full-scale input) */
    CHECK(peak_hook > 0.8f);                                                /* ... and it does play (not silent by accident) */
    CHECK(peak_wire <= PC_EXTPATH_CEIL * vg + 1e-6f);
    CHECK_EQ_INT(pc_extpath_limited(p), 0);                                 /* the limiter itself did it: the final clamp never had to cut */
    pc_extpath_destroy(p, xfree);
}
static void test_path_rates(void)
{
    static const uint32_t rates[] = { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        pc_extuac_init(&U, rates, 8);
        run_rate(rates[i], 20, false, 5);                                   /* the hook ran idle blocks before the source: the limiter is in already, three silent blocks remain */
        run_rate(rates[i], 100, false, 0);                                  /* no idle block: the first blocks of the source install and warm it, silence meanwhile */
    }
}
static void test_gate_without_limiter(void)                                /* the limiter not in place (its SWAP not yet taken): the stream is silence, the frames still returned */
{
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extuac_init(&U, R, 2);
    pc_extpath_t *p = pc_extpath_create(xalloc, &S, &U);
    pc_extstream_set_format(&S, 48000, 4);
    for (int b = 0; b < 2; b++) {                                          /* no post ran: the chain has not taken the mailbox */
        host_block(480); host_block(480);
        CHECK_EQ_INT(pc_extpath_render(buf, 480, p), 480);
        for (int k = 0; k < 960; k++) CHECK(buf[k] == 0.0f);
    }
    CHECK(!pc_extpath_ready(p));
    pc_extpath_destroy(p, xfree);
}
static void test_volume_pre_gain(void)                                      /* the feature unit: never above 0 dB, mute = silence */
{
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extuac_init(&U, R, 2);
    const uint8_t up[2] = { 0x00, 0x7F }, m6[2] = { 0x00, 0xFA }, mu[1] = { 1 };
    pc_extuac_set(&U, 3, 2, 0, up, 2);                                      /* +127 dB asked */
    CHECK(pc_extuac_gain(&U, 0) <= 1.0f);
    pc_extpath_t *p = pc_extpath_create(xalloc, &S, &U);
    pc_extstream_set_format(&S, 48000, 4);
    for (int b = 0; b < 12; b++) { memset(buf, 0, sizeof buf); host_block(480); host_block(480); pc_extpath_render(buf, 480, p); pc_extpath_post(buf, 480, 48000, 0, p); }
    float pk = 0;
    for (int b = 0; b < 4; b++) { host_block(480); host_block(480); pc_extpath_render(buf, 480, p); for (int k = 0; k < 960; k++) if (fabsf(buf[k]) > pk) pk = fabsf(buf[k]); pc_extpath_post(buf, 480, 48000, 0, p); }
    CHECK(pk <= 1.0f && pk > 0.99f);                                        /* 0 dB: the stream as it is, the limiter and the device volume decide the rest */
    pc_extuac_set(&U, 3, 2, 0, m6, 2);
    for (int b = 0; b < 3; b++) { host_block(480); host_block(480); pc_extpath_render(buf, 480, p); pc_extpath_post(buf, 480, 48000, 0, p); }
    pk = 0;
    host_block(480); host_block(480); pc_extpath_render(buf, 480, p);
    for (int k = 0; k < 960; k++) if (fabsf(buf[k]) > pk) pk = fabsf(buf[k]);
    CHECK_NEAR(pk, 0.5012, 0.002);                                          /* -6 dB */
    pc_extuac_set(&U, 3, 1, 0, mu, 1);                                      /* mute */
    for (int b = 0; b < 3; b++) { host_block(480); host_block(480); pc_extpath_render(buf, 480, p); }
    for (int k = 0; k < 960; k++) CHECK(buf[k] == 0.0f);
    pc_extpath_destroy(p, xfree);
}
static void test_start_volume_and_unity_guard(void)
{
    CHECK(pc_volume_gain(pc_extstream_start_volume(100)) < 0.2f);           /* the start volume 20: -48 dB on the -60..0 dB curve */
    CHECK(pc_volume_gain(pc_extstream_start_volume(55)) == pc_volume_gain(20));   /* whatever was saved, the mode starts at 20 at most */
}
int main(void) { RUN(test_path_rates); RUN(test_gate_without_limiter); RUN(test_volume_pre_gain); RUN(test_start_volume_and_unity_guard); return 0; }
