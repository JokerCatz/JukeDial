// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_btcodec.h"
#include "pc_test.h"
#include "ldacBT.h"
#include "freeaptx.h"

static void test_pick_and_order(void)
{
    CHECK_EQ_INT(pc_btc_pick(0), PC_BTC_NONE);
    CHECK_EQ_INT(pc_btc_pick(1u << PC_BTC_SBC), PC_BTC_SBC);
    CHECK_EQ_INT(pc_btc_pick((1u << PC_BTC_SBC) | (1u << PC_BTC_AAC)), PC_BTC_AAC);
    CHECK_EQ_INT(pc_btc_pick((1u << PC_BTC_SBC) | (1u << PC_BTC_APTX) | (1u << PC_BTC_AAC)), PC_BTC_APTX);
    CHECK_EQ_INT(pc_btc_pick((1u << PC_BTC_APTX) | (1u << PC_BTC_APTX_HD)), PC_BTC_APTX_HD);
    CHECK_EQ_INT(pc_btc_pick(0x1F), PC_BTC_LDAC);
    pc_btc_t o[PC_BTC_N];
    CHECK_EQ_INT(pc_btc_order(0x1F, o), 5);
    CHECK_EQ_INT(o[0], PC_BTC_LDAC); CHECK_EQ_INT(o[1], PC_BTC_APTX_HD); CHECK_EQ_INT(o[2], PC_BTC_APTX);
    CHECK_EQ_INT(o[3], PC_BTC_AAC); CHECK_EQ_INT(o[4], PC_BTC_SBC);
    CHECK_EQ_INT(pc_btc_order((1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), o), 2);   /* the normal build */
    CHECK_EQ_INT(o[0], PC_BTC_AAC);
    CHECK_STR(pc_btc_name(PC_BTC_APTX_HD), "aptX HD");
}

static void test_ldac_levels(void)
{
    /* the stage-1 notes: 330/660/990 -> 110/220/330-octet frames at 6/3/2 per packet; 492 in between */
    const int kbps[4] = { 990, 660, 492, 330 }, fb[4] = { 330, 220, 164, 110 }, fpp[4] = { 2, 3, 4, 6 };
    for (int i = 0; i < PC_LDAC_LEVELS; i++) {
        const pc_ldac_level_t *l = pc_ldac_level(i);
        CHECK_EQ_INT(l->kbps, kbps[i]);
        CHECK_EQ_INT(l->frame_bytes, fb[i]);
        CHECK_EQ_INT(l->frames_per_packet, fpp[i]);
        CHECK_EQ_INT(pc_ldac_kbps(fb[i], 48000), kbps[i]);
        CHECK_EQ_INT(pc_ldac_frames_per_packet(fb[i], 995), fpp[i]);    /* the link MTU measured in stage 1 */
    }
    CHECK_EQ_INT(pc_ldac_kbps(330, 44100), 909);
    CHECK_EQ_INT(pc_ldac_kbps(110, 44100), 303);
    CHECK_EQ_INT(pc_ldac_frames_per_packet(330, 500), 1);              /* a small MTU: fewer frames */
    CHECK_EQ_INT(pc_ldac_frames_per_packet(110, 500), 4);
    CHECK(pc_ldac_level(4) == NULL);
    CHECK_EQ_INT(pc_ldac_level_for_kbps(990), 0);
    CHECK_EQ_INT(pc_ldac_level_for_kbps(800), 1);
    CHECK_EQ_INT(pc_ldac_level_for_kbps(660), 1);
    CHECK_EQ_INT(pc_ldac_level_for_kbps(330), 3);
    CHECK_EQ_INT(pc_ldac_level_for_kbps(100), 3);
}

static void test_abr_cap(void)
{
    CHECK_EQ_INT(pc_ldac_abr_cap(false, 990), 0);
    CHECK_EQ_INT(pc_ldac_abr_cap(true, 990), 1);       /* Wi-Fi on: 660 at most */
    CHECK_EQ_INT(pc_ldac_abr_cap(true, 330), 3);
    pc_ldac_abr_t a;
    pc_ldac_abr_init(&a, true, 990);
    CHECK_EQ_INT(a.level, 1);
    /* quiet for minutes with Wi-Fi on: never 990 */
    for (uint32_t t = 0; t < 300000; t += 100) CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 0, true, 990), 1);
    /* Wi-Fi off after a long quiet spell: 990 at once */
    uint32_t t = 300000;
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 0, false, 990), 0);
    /* Wi-Fi off but the link was busy just now: 990 only after a quiet spell */
    pc_ldac_abr_init(&a, true, 990);
    pc_ldac_abr_update(&a, t, 4, 0, true, 990);
    t += 100;
    for (; t < 300000 + PC_ABR_UP_MS; t += 100) CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 0, false, 990), 1);
    for (; t < 300000 + PC_ABR_UP_MS + 300; t += 100) pc_ldac_abr_update(&a, t, 0, 0, false, 990);
    CHECK_EQ_INT(a.level, 0);
    /* Wi-Fi on again: down to 660 at once, without counting it as a loss */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 0, true, 990), 1);
    CHECK_EQ_INT(a.downs, 0);
}

static void test_abr_steps(void)
{
    pc_ldac_abr_t a;
    pc_ldac_abr_init(&a, true, 990);                     /* 660 with Wi-Fi */
    uint32_t t = 1000;
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 1, true, 990), 2);        /* a lost packet: 492 at once */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 200, 0, 1, true, 990), 2);  /* not again within a second */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 1100, 0, 1, true, 990), 3); /* still losing: 330 */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 2500, 20, 3, true, 990), 3);/* the bottom */
    /* a deep queue must last before it counts */
    pc_ldac_abr_init(&a, false, 990);
    t = 0;
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t, PC_ABR_QUEUE_DEEP, 0, false, 990), 0);
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 100, PC_ABR_QUEUE_DEEP, 0, false, 990), 0);
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 200, 1, 0, false, 990), 0);              /* drained: reset */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 300, PC_ABR_QUEUE_DEEP, 0, false, 990), 0);
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t + 300 + PC_ABR_DEEP_MS, PC_ABR_QUEUE_DEEP, 0, false, 990), 1);
    /* a critical queue: at once */
    pc_ldac_abr_init(&a, false, 990);
    CHECK_EQ_INT(pc_ldac_abr_update(&a, 5000, PC_ABR_QUEUE_CRITICAL, 0, false, 990), 1);
}

static void test_abr_backoff(void)
{
    pc_ldac_abr_t a;
    pc_ldac_abr_init(&a, false, 990);
    uint32_t t = 0;
    pc_ldac_abr_update(&a, t, 0, 1, false, 990);         /* 660 */
    CHECK_EQ_INT(a.level, 1);
    t += 100;
    while (a.level == 1) { pc_ldac_abr_update(&a, t, 0, 0, false, 990); t += 100; }
    CHECK(t >= PC_ABR_UP_MS);                            /* up after the first quiet spell */
    pc_ldac_abr_update(&a, t, 0, 2, false, 990);         /* fails right away */
    CHECK_EQ_INT(a.level, 1);
    CHECK_EQ_INT(a.up_wait_ms, 2 * PC_ABR_UP_MS);
    uint32_t t1 = t;
    t += 100;
    while (a.level == 1) { pc_ldac_abr_update(&a, t, 0, 0, false, 990); t += 100; }
    CHECK(t - t1 >= 2 * PC_ABR_UP_MS);
    for (int i = 0; i < 6; i++) {                        /* keeps failing: the wait stops growing */
        pc_ldac_abr_update(&a, t, 0, 1, false, 990);
        t += 100;
        while (a.level == 1) { pc_ldac_abr_update(&a, t, 0, 0, false, 990); t += 100; }
    }
    CHECK_EQ_INT(a.up_wait_ms, PC_ABR_UP_MAX_MS);
    /* an up-step that holds halves the wait again (down to the first one), so one bad spell does not slow every
     * later recovery for the rest of the stream */
    CHECK_EQ_INT(a.level, 0);
    for (uint32_t e = t + PC_ABR_UP_FAIL_MS + 100; t < e; t += 100) pc_ldac_abr_update(&a, t, 0, 0, false, 990);
    CHECK_EQ_INT(a.up_wait_ms, PC_ABR_UP_MAX_MS / 2);
    for (uint32_t e = t + 30000; t < e; t += 100) pc_ldac_abr_update(&a, t, 0, 0, false, 990);
    CHECK_EQ_INT(a.up_wait_ms, PC_ABR_UP_MAX_MS / 2);    /* once per up-step */
    pc_ldac_abr_update(&a, t, 0, 1, false, 990);         /* a loss long after the up-step: not a failed one */
    CHECK_EQ_INT(a.level, 1);
    CHECK_EQ_INT(a.up_wait_ms, PC_ABR_UP_MAX_MS / 2);
    uint32_t t2 = t;
    t += 100;
    while (a.level == 1) { pc_ldac_abr_update(&a, t, 0, 0, false, 990); t += 100; }
    CHECK(t - t2 >= PC_ABR_UP_MAX_MS / 2 && t - t2 < PC_ABR_UP_MAX_MS);
    /* the user's top rate wins over everything */
    CHECK_EQ_INT(pc_ldac_abr_update(&a, t, 0, 0, false, 330), 3);
}

/* down to 330 with a long wait, then a good link: each held up-step halves the next wait (80 + 40 + 20 s) */
static void test_abr_recovery(void)
{
    pc_ldac_abr_t a;
    pc_ldac_abr_init(&a, false, 990);
    a.up_wait_ms = PC_ABR_UP_MAX_MS;
    uint32_t t = 1000;
    for (int i = 0; i < 3; i++, t += PC_ABR_DOWN_GAP_MS) pc_ldac_abr_update(&a, t, 0, 1, false, 990);
    CHECK_EQ_INT(a.level, 3);
    uint32_t t0 = t, up[3];
    int n = 0;
    for (; t < t0 + 300000 && a.level > 0; t += 100) {
        int lv = a.level;
        if (pc_ldac_abr_update(&a, t, 0, 0, false, 990) != lv) up[n++] = t;
    }
    CHECK_EQ_INT(a.level, 0);
    CHECK_EQ_INT(n, 3);
    CHECK(up[0] - t0 >= PC_ABR_UP_MAX_MS - 1000 && up[0] - t0 <= PC_ABR_UP_MAX_MS + 1000);
    CHECK(up[1] - up[0] >= PC_ABR_UP_MAX_MS / 2 - 100 && up[1] - up[0] <= PC_ABR_UP_MAX_MS / 2 + 100);
    CHECK(up[2] - up[1] >= PC_ABR_UP_MAX_MS / 4 - 100 && up[2] - up[1] <= PC_ABR_UP_MAX_MS / 4 + 100);
}

/* one encoder task per codec family, and whether a codec can stream over a given media MTU */
static void test_encoder_and_mtu(void)
{
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_LDAC), PC_ENC_VENDOR);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_APTX_HD), PC_ENC_VENDOR);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_APTX), PC_ENC_VENDOR);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_AAC), PC_ENC_STD);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_SBC), PC_ENC_STD);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_NONE), PC_ENC_NONE);
    CHECK_EQ_INT(pc_btc_encoder(PC_BTC_N), PC_ENC_NONE);
    /* libldac always fills one 2-DH5 packet (679 octets), whatever the bit rate */
    CHECK(pc_btc_fits_mtu(PC_BTC_LDAC, 995));
    CHECK(pc_btc_fits_mtu(PC_BTC_LDAC, PC_LDAC_MTU_MIN));
    CHECK(!pc_btc_fits_mtu(PC_BTC_LDAC, PC_LDAC_MTU_MIN - 1));
    CHECK(!pc_btc_fits_mtu(PC_BTC_LDAC, 600));
    /* aptX shrinks its packets to the MTU; it needs room for one codeword (and the RTP header for HD) */
    CHECK(pc_btc_fits_mtu(PC_BTC_APTX, 600));
    CHECK(pc_btc_fits_mtu(PC_BTC_APTX_HD, 600));
    CHECK(pc_btc_fits_mtu(PC_BTC_APTX, 4));
    CHECK(!pc_btc_fits_mtu(PC_BTC_APTX, 3));
    CHECK(pc_btc_fits_mtu(PC_BTC_APTX_HD, 18));
    CHECK(!pc_btc_fits_mtu(PC_BTC_APTX_HD, 17));
    CHECK(pc_btc_fits_mtu(PC_BTC_AAC, 600));
    CHECK(pc_btc_fits_mtu(PC_BTC_SBC, 600));
    CHECK(!pc_btc_fits_mtu(PC_BTC_NONE, 995));
    /* a codec left out after it failed: the next best is offered first */
    pc_btc_t o[PC_BTC_N];
    unsigned all = (1u << PC_BTC_N) - 1;
    CHECK_EQ_INT(pc_btc_order(all & ~(1u << PC_BTC_LDAC), o), 4);
    CHECK_EQ_INT(o[0], PC_BTC_APTX_HD);
    CHECK_EQ_INT(o[3], PC_BTC_SBC);
}

/* real LDAC frames from libldac (fixed point, as on the board): the check passes them and catches damage */
static void test_ldac_check_real_frames(void)
{
    const int rates[2] = { 48000, 44100 };
    for (int r = 0; r < 2; r++) {
        for (int lv = 0; lv < PC_LDAC_LEVELS; lv++) {
            HANDLE_LDAC_BT h = ldacBT_get_handle();
            CHECK(h);
            CHECK_EQ_INT(ldacBT_init_handle_encode(h, 995 + 12, LDACBT_EQMID_SQ, LDACBT_CHANNEL_MODE_STEREO,
                                                   LDACBT_SMPL_FMT_S16, rates[r]), 0);
            if (lv == 0) CHECK_EQ_INT(ldacBT_set_eqmid(h, LDACBT_EQMID_HQ), 0);
            if (lv == 2) CHECK_EQ_INT(ldacBT_alter_eqmid_priority(h, LDACBT_EQMID_INC_CONNECTION), 0);   /* SQ -> Q0 */
            if (lv == 3) CHECK_EQ_INT(ldacBT_set_eqmid(h, LDACBT_EQMID_MQ), 0);
            const pc_ldac_level_t *L = pc_ldac_level(lv);
            static int16_t pcm[2 * LDACBT_ENC_LSU];
            static uint8_t out[1024];
            double ph = 0;
            int packets = 0, checked = 0;
            for (int k = 0; k < 400; k++) {
                for (int i = 0; i < LDACBT_ENC_LSU; i++) { pcm[2 * i] = (int16_t)(9000 * sin(ph)); pcm[2 * i + 1] = (int16_t)(4000 * sin(3 * ph)); ph += 0.07; }
                int used = 0, sz = 0, nf = 0;
                CHECK_EQ_INT(ldacBT_encode(h, pcm, &used, out, &sz, &nf), 0);
                if (sz <= 0) continue;
                packets++;
                if (packets < 4) continue;               /* the first packets may still carry the start size */
                pc_ldac_check_t c;
                pc_ldac_check(out, sz, nf, pc_ldac_rate_id((uint32_t)rates[r]), 2, &c);
                CHECK(c.ok);
                CHECK_EQ_INT(c.frames, L->frames_per_packet);
                CHECK_EQ_INT(c.frame_bytes, L->frame_bytes);
                CHECK_EQ_INT(sz, L->frames_per_packet * L->frame_bytes);
                checked++;
                /* damage: a lost frame-start, a wrong count, a wrong rate */
                uint8_t keep = out[L->frame_bytes];
                out[L->frame_bytes] = 0x55;
                pc_ldac_check(out, sz, nf, 1 - r, 2, &c);
                CHECK(!c.ok);
                out[L->frame_bytes] = keep;
                pc_ldac_check(out, sz, nf + 1, -1, -1, &c);
                CHECK(!c.ok);
                pc_ldac_check(out, sz, nf, r, 2, &c);    /* r is the other rate's id here */
                CHECK(!c.ok && c.bad_frames == nf);
                pc_ldac_check(out, sz - 1, nf, -1, -1, &c);
                CHECK(!c.ok);
            }
            CHECK(checked > 10);
            ldacBT_free_handle(h);
        }
    }
}

static void test_aptx_framing(void)
{
    CHECK_EQ_INT(pc_aptx_samples_per_packet(false, 48000, 995), 672);
    CHECK_EQ_INT(pc_aptx_packet_bytes(false, 672), 672);               /* AOSP: 672 octets, 14 ms */
    CHECK_EQ_INT(pc_aptx_samples_per_packet(false, 44100, 995), 660);
    CHECK_EQ_INT(pc_aptx_samples_per_packet(true, 48000, 995), 432);
    CHECK_EQ_INT(pc_aptx_packet_bytes(true, 432), 648);                /* AOSP: 648 octets, 9 ms */
    CHECK_EQ_INT(pc_aptx_samples_per_packet(true, 44100, 995), 396);
    CHECK_EQ_INT(pc_aptx_packet_bytes(true, 396), 594);
    CHECK_EQ_INT(pc_aptx_samples_per_packet(false, 48000, 600), 600);  /* no RTP header: up to the MTU */
    CHECK_EQ_INT(pc_aptx_samples_per_packet(true, 48000, 600), 392);   /* 588 + 12 */
}

/* aptX round trip through libfreeaptx, then the tone analyser on what comes out */
static void test_aptx_tone_roundtrip(void)
{
    for (int hd = 0; hd < 2; hd++) {
        struct aptx_context *e = aptx_init(hd), *d = aptx_init(hd);
        CHECK(e && d);
        enum { N = 48000 / 2 };
        static uint8_t in[N * 6], enc[N * 2], dec[N * 6 + 64];
        for (int i = 0; i < N; i++) {
            int32_t v = (int32_t)(0.5 * 8388607 * sin(2 * M_PI * 1000.0 * i / 48000));
            for (int c = 0; c < 2; c++) { uint8_t *p = in + 6 * i + 3 * c; p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); }
        }
        size_t w = 0, w2 = 0;
        aptx_encode(e, in, sizeof in, enc, sizeof enc, &w);
        int synced = 0;
        size_t dropped = 0;
        aptx_decode_sync(d, enc, w, dec, sizeof dec, &w2, &synced, &dropped);
        CHECK(synced);
        int n = (int)(w2 / 6);
        CHECK(n > N - 200);
        static float x[N];
        for (int i = 0; i < n; i++) { const uint8_t *p = dec + 6 * i; x[i] = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f; }
        pc_tone_t t;
        CHECK(pc_tone_analyse(x + 4096, 8192, 48000, &t));
        printf("  aptX%s 1 kHz: %.3f Hz %.2f dBFS SNR %.1f dB\n", hd ? " HD" : "", t.freq_hz, t.level_db, t.snr_db);
        CHECK_NEAR(t.freq_hz, 1000, 0.05);
        CHECK_NEAR(t.level_db, -6.02, 0.3);
        CHECK(t.snr_db > 60);
        aptx_finish(e);
        aptx_finish(d);
    }
}

static void test_tone(void)
{
    enum { N = 8192 };
    static float x[N];
    uint32_t seed = 1;
    for (int i = 0; i < N; i++) {
        seed = seed * 1664525u + 1013904223u;
        float noise = ((seed >> 8) / 16777216.0f - 0.5f) * 2 * 1.732f * 1e-4f;   /* rms 1e-4: -80 dB */
        x[i] = 0.1f + 0.7071f * sinf((float)(2 * M_PI * 997.3 * i / 48000 + 0.3)) + noise;
    }
    pc_tone_t t;
    CHECK(pc_tone_analyse(x, N, 48000, &t));
    CHECK_NEAR(t.freq_hz, 997.3, 0.05);
    CHECK_NEAR(t.level_db, -3.01, 0.05);
    CHECK_NEAR(t.snr_db, 20 * log10(0.5 / 1e-4) / 1.0 - 3.01 + 3.01, 1.5);  /* (0.5 power) / (1e-8) = 77 dB */
    for (int i = 0; i < N; i++) x[i] = 0.25f * sinf((float)(2 * M_PI * 6000.0 * i / 44100)) + 0.25f * sinf((float)(2 * M_PI * 6500.0 * i / 44100));
    CHECK(pc_tone_analyse(x, N, 44100, &t));             /* two tones: a poor SNR, not a pass */
    CHECK(t.snr_db < 10);
    memset(x, 0, sizeof x);
    CHECK(!pc_tone_analyse(x, N, 48000, &t));            /* silence */
}


/* fixed-rate codecs: the rungs to step down through when the link cannot carry the bit rate */
static void test_ladder(void)
{
    pc_btc_t o[PC_BTC_N];
    unsigned all = (1u << PC_BTC_N) - 1;
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_APTX_HD, all, o), 3);
    CHECK_EQ_INT(o[0], PC_BTC_APTX_HD); CHECK_EQ_INT(o[1], PC_BTC_APTX); CHECK_EQ_INT(o[2], PC_BTC_SBC);
    /* a sink without classic aptX: straight to SBC */
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_APTX_HD, (1u << PC_BTC_APTX_HD) | (1u << PC_BTC_SBC), o), 2);
    CHECK_EQ_INT(o[1], PC_BTC_SBC);
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_APTX, all, o), 2);
    CHECK_EQ_INT(o[0], PC_BTC_APTX); CHECK_EQ_INT(o[1], PC_BTC_SBC);
    /* LDAC has its own ABR, AAC and SBC are the bottom already: no ladder */
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_LDAC, all, o), 1);
    CHECK_EQ_INT(o[0], PC_BTC_LDAC);
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_AAC, all, o), 1);
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_SBC, all, o), 1);
    /* an unknown sink mask (not reported): only the negotiated codec and SBC, which every sink has */
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_APTX_HD, 0, o), 2);
    CHECK_EQ_INT(o[1], PC_BTC_SBC);
    CHECK_EQ_INT(pc_btc_ladder(PC_BTC_NONE, all, o), 0);
    /* nominal rates at 48 kHz: aptX HD 576, aptX 384, SBC on the low rung (bitpool PC_SBC_STEP_BITPOOL) */
    CHECK_EQ_INT(pc_btc_step_kbps(PC_BTC_APTX_HD, 48000), 576);
    CHECK_EQ_INT(pc_btc_step_kbps(PC_BTC_APTX, 48000), 384);
    CHECK_EQ_INT(pc_btc_step_kbps(PC_BTC_APTX, 44100), 352);
    CHECK_EQ_INT(pc_btc_step_kbps(PC_BTC_SBC, 48000), 249);   /* 83-octet frames, 375 frames/s */
}

/* feed one update every 100 ms for `ms`, `lost_per_s` packets lost each second */
static int cs_run(pc_cstep_t *s, uint32_t *t, uint32_t ms, int lost_per_s, bool wifi)
{
    int lv = s->level;
    for (uint32_t e = *t + ms; *t < e; *t += 100) lv = pc_cstep_update(s, *t, (*t % 1000) == 0 ? lost_per_s : 0, wifi);
    return lv;
}

/* sustained losses: PC_CS_DOWN_LOST spread over PC_CS_DOWN_SECS seconds; *t ends at the last one */
static int cs_burst(pc_cstep_t *s, uint32_t *t, bool wifi)
{
    int per = (PC_CS_DOWN_LOST + PC_CS_DOWN_SECS - 1) / PC_CS_DOWN_SECS, lv = s->level;
    for (int i = 0; i < PC_CS_DOWN_SECS; i++) {
        if (i) *t += 1000;
        lv = pc_cstep_update(s, *t, per, wifi);
    }
    return lv;
}

static void test_cstep_down(void)
{
    pc_cstep_t s;
    pc_cstep_init(&s, 3);
    uint32_t t = 0;
    /* one burst, however big (a stall when Wi-Fi starts, 2026-11-10: 12 packets at once), does not cost a step:
     * a step is a pause of about 0.7 s itself */
    CHECK_EQ_INT(pc_cstep_update(&s, t, 200, true), 0);
    CHECK_EQ_INT(cs_run(&s, &t, 30000, 0, true), 0);
    /* losses in PC_CS_DOWN_SECS different seconds but fewer than PC_CS_DOWN_LOST: no step either */
    for (int i = 0; i < PC_CS_DOWN_SECS + 2; i++, t += 1000) CHECK_EQ_INT(pc_cstep_update(&s, t, 2, true), 0);
    CHECK_EQ_INT(s.downs, 0);
    t += PC_CS_WIN_S * 1000;
    /* sustained: one rung down */
    CHECK_EQ_INT(cs_burst(&s, &t, true), 1);
    CHECK_EQ_INT(s.downs, 1);
    /* the window starts over after a step, and the next down-step waits PC_CS_MIN_GAP_MS */
    uint32_t sw = t;
    t += 100;
    CHECK_EQ_INT(cs_burst(&s, &t, true), 1);
    while (t < sw + PC_CS_MIN_GAP_MS - 100) { t += 100; CHECK_EQ_INT(pc_cstep_update(&s, t, 0, true), 1); }
    t = sw + PC_CS_MIN_GAP_MS;
    CHECK_EQ_INT(pc_cstep_update(&s, t, 0, true), 1);   /* those losses are out of the window by now */
    CHECK_EQ_INT(cs_burst(&s, &t, true), 2);
    /* the bottom rung stays */
    t += PC_CS_MIN_GAP_MS;
    CHECK_EQ_INT(cs_burst(&s, &t, true), 2);
    CHECK_EQ_INT(s.downs, 2);
    /* thin losses (one a second) never add up to a step; a steady 5 a second does */
    pc_cstep_init(&s, 3);
    t = 0;
    CHECK_EQ_INT(cs_run(&s, &t, 120000, 1, true), 0);
    CHECK_EQ_INT(cs_run(&s, &t, (PC_CS_WIN_S + 1) * 1000, 5, true), 1);
}

static void test_cstep_up(void)
{
    pc_cstep_t s;
    pc_cstep_init(&s, 3);
    uint32_t t = 1000;
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 1);
    uint32_t down = t;
    /* quiet: back up after PC_CS_UP_MS */
    uint32_t up = 0;
    for (t += 100; t < down + 3 * PC_CS_UP_MS && !up; t += 100)
        if (pc_cstep_update(&s, t, 0, true) == 0) up = t;
    CHECK(up >= down + PC_CS_UP_MS && up <= down + PC_CS_UP_MS + 200);
    /* it fails at once: down again, and the next wait is twice as long */
    t = up + 2000;
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 1);
    CHECK_EQ_INT(s.up_wait_ms, 2 * PC_CS_UP_MS);
    /* keeps failing: the wait stops at PC_CS_UP_MAX_MS */
    for (int i = 0; i < 8; i++) {
        while (s.level == 1) { t += 100; pc_cstep_update(&s, t, 0, true); }
        t += 1000;
        cs_burst(&s, &t, true);
        CHECK_EQ_INT(s.level, 1);
    }
    CHECK_EQ_INT(s.up_wait_ms, PC_CS_UP_MAX_MS);
    /* an up-step that holds PC_CS_UP_FAIL_MS halves the wait again */
    while (s.level == 1) { t += 100; pc_cstep_update(&s, t, 0, true); }
    cs_run(&s, &t, PC_CS_UP_FAIL_MS + 1000, 0, true);
    CHECK_EQ_INT(s.level, 0);
    CHECK_EQ_INT(s.up_wait_ms, PC_CS_UP_MAX_MS / 2);
    /* losses long after the up-step are not a failed up-step */
    cs_run(&s, &t, 60000, 0, true);
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 1);
    CHECK_EQ_INT(s.up_wait_ms, PC_CS_UP_MAX_MS / 2);
    /* one rung at a time on the way up too */
    pc_cstep_init(&s, 3);
    t = 0;
    cs_burst(&s, &t, true);
    t += PC_CS_MIN_GAP_MS;
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 2);
    uint32_t t0 = t;
    while (s.level == 2) { t += 100; pc_cstep_update(&s, t, 0, true); }
    CHECK_EQ_INT(s.level, 1);
    CHECK(t - t0 >= PC_CS_UP_MS);
}

/* Wi-Fi off: the link has room again, back up after a short quiet spell; a failed step (the sink refused the
 * reconfiguration) puts the rung back and stops stepping for this stream */
static void test_cstep_wifi_and_fail(void)
{
    pc_cstep_t s;
    pc_cstep_init(&s, 3);
    uint32_t t = 0;
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 1);
    uint32_t off = t + 1000, up = 0;
    for (t = off; t < off + PC_CS_UP_MS && !up; t += 100)
        if (pc_cstep_update(&s, t, 0, false) == 0) up = t;
    CHECK(up >= off + PC_CS_UP_NOWIFI_MS && up <= off + PC_CS_UP_NOWIFI_MS + 1200);
    pc_cstep_init(&s, 3);
    t = 0;
    cs_burst(&s, &t, true);
    CHECK_EQ_INT(s.level, 1);
    pc_cstep_failed(&s, 0);
    CHECK_EQ_INT(s.level, 0);
    t += 2 * PC_CS_MIN_GAP_MS;
    CHECK_EQ_INT(cs_burst(&s, &t, true), 0);
    CHECK(s.off);
    pc_cstep_init(&s, 1);                                /* one rung: nothing to do */
    CHECK_EQ_INT(pc_cstep_update(&s, 0, 1000, true), 0);
}

static void test_reconnect_backoff(void)
{
    CHECK_EQ_INT(pc_bt_reconnect_ms(0), 3000);      /* first try soon: headphones that blinked out come back fast */
    CHECK_EQ_INT(pc_bt_reconnect_ms(1), 6000);
    CHECK_EQ_INT(pc_bt_reconnect_ms(2), 12000);
    CHECK_EQ_INT(pc_bt_reconnect_ms(4), 48000);
    CHECK_EQ_INT(pc_bt_reconnect_ms(5), 60000);     /* then once a minute, forever (headphones switched off, then on) */
    CHECK_EQ_INT(pc_bt_reconnect_ms(40), 60000);
    CHECK_EQ_INT(pc_bt_reconnect_ms(-1), 3000);
}

int main(void)
{
    RUN(test_pick_and_order);
    RUN(test_reconnect_backoff);
    RUN(test_ldac_levels);
    RUN(test_abr_cap);
    RUN(test_abr_steps);
    RUN(test_abr_backoff);
    RUN(test_abr_recovery);
    RUN(test_encoder_and_mtu);
    RUN(test_ldac_check_real_frames);
    RUN(test_aptx_framing);
    RUN(test_aptx_tone_roundtrip);
    RUN(test_tone);
    RUN(test_ladder);
    RUN(test_cstep_down);
    RUN(test_cstep_up);
    RUN(test_cstep_wifi_and_fail);
    return 0;
}
