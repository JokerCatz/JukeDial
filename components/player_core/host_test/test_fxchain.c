// SPDX-License-Identifier: LGPL-2.1-or-later
/* fx chain (spec §3, §10.2): slots in the registry order, one-block fades (on/off, mode, rate), SWAP/OFF/SET/MIX through
 * the mailbox, the return ring (FREE/REBUILD), the storm and the full-ring cases, order probe, stat. Both sides run on
 * this one thread (host_test/CMakeLists.txt links no threads for any test: no concurrent test, see the T2 report). */
#include <stdlib.h>
#include <string.h>
#include "pc/pc_fxchain.h"
#include "fx_test.h"
#include "pc_test.h"

#define BLK PC_FX_BLOCK
#define PI 3.14159265358979

static int g_live, g_nfree;   /* every allocation of the test (chain included) */
static void *al(size_t n) { g_live++; return calloc(1, n); }
static void rel(void *p) { if (p) { g_live--; g_nfree++; free(p); } }
#define LIVE_BLOCKS() (g_live - 1)   /* minus the chain itself */

typedef struct {
    pc_fxchain_t *c;
    int rb[PC_FXAPP_MAX];     /* REBUILD(app) seen */
    int nfreed;               /* FREE taken (and freed) */
    double ph;
} W;

static void w_open(W *w) { memset(w, 0, sizeof *w); g_live = g_nfree = 0; w->c = pc_fxchain_create(al); CHECK(w->c != NULL); }
static void w_close(W *w) { pc_fxchain_destroy(w->c, rel); CHECK_EQ_INT(g_live, 0); }

/* the LVGL side: take everything, free what is handed back (take_all = false: leave the ring alone) */
static void pump(W *w)
{
    pc_fxret_t r;
    while (pc_fxchain_take(w->c, &r)) {
        if (r.op == PC_FXRET_FREE) { CHECK(r.ptr != NULL); rel(r.ptr); w->nfreed++; }
        else if (r.op == PC_FXRET_REBUILD) w->rb[r.app]++;
        else CHECK(0);
    }
}

static const pc_fxapp_t *app_of(int i) { const pc_fxapp_t *const *a; pc_fxapps(&a); return a[i]; }
static int app_id(const char *id) { int i = pc_fxapp_find(id); CHECK(i >= 0); return i; }
static int pidx(int app, const char *id)
{
    const pc_fxapp_t *ap = app_of(app);
    for (int p = 0; p < ap->nparam; p++)
        if (!strcmp(ap->param[p].id, id)) return p;
    CHECK(0);
    return -1;
}
static void defaults(int app, float *v)
{
    memset(v, 0, PC_FXP_MAX * sizeof *v);
    for (int p = 0; p < app_of(app)->nparam; p++) v[p] = app_of(app)->param[p].def;
}

static void *mk_block(int app, uint32_t rate, const float *v, float mix)
{
    size_t n = pc_fxchain_block_size(app, rate, v);
    void *b = al(n);
    CHECK(b != NULL);
    CHECK(pc_fxchain_block_init(b, app, rate, v, mix) == b);
    return b;
}
static bool post_swap(W *w, int app, void *blk)
{
    pc_fxmsg_t m = { .op = PC_FXOP_SWAP, .app = (uint8_t)app, .ptr = blk };
    return pc_fxchain_post(w->c, &m);
}
static void swap(W *w, int app, const float *v, uint32_t rate)
{
    void *b = mk_block(app, rate, v, 1.0f);
    CHECK(post_swap(w, app, b));
}
static void swap_def(W *w, int app, uint32_t rate) { float v[PC_FXP_MAX]; defaults(app, v); swap(w, app, v, rate); }
static void post_op(W *w, int op, int app, int param, float v)
{
    pc_fxmsg_t m = { .op = (uint8_t)op, .app = (uint8_t)app, .param = (uint8_t)param, .v = v };
    CHECK(pc_fxchain_post(w->c, &m));
}

static void sine(W *w, float *b, int frames, uint32_t rate)
{
    for (int i = 0; i < frames; i++) {
        b[2 * i] = b[2 * i + 1] = 0.5f * (float)sin(w->ph);
        w->ph += 2 * PI * 440.0 / rate;
    }
}
/* one audio block of the 0.5 sine into out[] (n blocks of BLK frames are laid down back to back) */
static void blk_sine(W *w, float *out, uint32_t rate, uint8_t mode)
{
    sine(w, out, BLK, rate);
    int before = g_nfree;
    pc_fxchain_process(w->c, out, BLK, rate, mode);
    CHECK_EQ_INT(g_nfree, before);   /* nothing freed on the audio side */
}
static float peak(const float *b, int frames)
{
    float m = 0;
    for (int i = 0; i < 2 * frames; i++) m = fmaxf(m, fabsf(b[i]));
    return m;
}
static float rms(const float *b, int frames)
{
    double s = 0;
    for (int i = 0; i < 2 * frames; i++) s += (double)b[i] * b[i];
    return (float)sqrt(s / (2 * frames));
}

/* ---- tests ---- */
static void test_fxchain_on_off_fade(void)
{
    W w; w_open(&w);
    static float y[12 * BLK * 2];
    int g = app_id("gain");
    float v[PC_FXP_MAX]; defaults(g, v);
    v[pidx(g, "gain_db")] = 6;
    for (int b = 0; b < 12; b++) {
        if (b == 2) swap(&w, g, v, 48000);
        if (b == 7) post_op(&w, PC_FXOP_OFF, g, 0, 0);
        blk_sine(&w, y + b * BLK * 2, 48000, 0);
        if (b == 7) CHECK_EQ_INT(w.nfreed, 0);
        if (b == 8) {                                  /* after OFF and one more block: exactly one FREE */
            pump(&w);
            CHECK_EQ_INT(w.nfreed, 1);
            CHECK_EQ_INT(LIVE_BLOCKS(), 0);
        }
    }
    float s_old = fx_maxstep(y, 2 * BLK), s_new = fx_maxstep(y + 3 * BLK * 2, 4 * BLK);
    CHECK(rms(y + 4 * BLK * 2, BLK) > 1.9f * rms(y, BLK));            /* the +6 dB is there */
    CHECK(fx_click_ok(fx_maxstep(y, 12 * BLK), s_old, s_new, FX_CLICK_SLACK_CHAIN));
    CHECK(fx_maxstep(y, 12 * BLK) > 1.2f * s_old);                     /* the bound is not trivially loose: the wet side has bigger steps */
    CHECK_NEAR(rms(y + 10 * BLK * 2, BLK), rms(y, BLK), 0.02f);        /* back to dry */
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 1);
    pc_fxchain_stat_t st; pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.blocks, 0);
    w_close(&w);
}

static void test_fxchain_mode_suspends_heavy(void)
{
    int h = fx_first_app_of(PC_FXC_HEAVY), g = app_id("gain");
    CHECK(h >= 0);
    CHECK(app_of(g)->cls == PC_FXC_LIGHT);
    float vg[PC_FXP_MAX]; defaults(g, vg);
    vg[pidx(g, "gain_db")] = 6;
    static float y[16 * BLK * 2];
    for (int ctl = 0; ctl < 2; ctl++) {      /* ctl = 1: no mode change, the tail control */
        W w; w_open(&w);
        swap(&w, g, vg, 48000);
        swap_def(&w, h, 48000);
        for (int b = 0; b < 16; b++) {
            uint8_t mode = (!ctl && b >= 6 && b < 9) ? PC_FXM_APTX : 0;
            float *o = y + b * BLK * 2;
            if (b < 7) blk_sine(&w, o, 48000, mode);
            else { memset(o, 0, BLK * 2 * sizeof *o); pc_fxchain_process(w.c, o, BLK, 48000, mode); }
        }
        if (ctl) {
            CHECK(peak(y + 9 * BLK * 2, BLK) > 1e-3f);                   /* the control: the tail is really there */
        } else {
            /* block 6 fades the reverb out; block 7 is silence in (tail gone: reverb skipped, gain only: silent) */
            CHECK(peak(y + 7 * BLK * 2, BLK) < 1e-6f);
            CHECK(peak(y + 9 * BLK * 2, BLK) < 1e-6f);                    /* mode back to 0: reset() was called, no old tail */
            CHECK(peak(y + 12 * BLK * 2, BLK) < 1e-6f);
            float s_old = fx_maxstep(y + 4 * BLK * 2, 2 * BLK);
            CHECK(fx_click_ok(fx_maxstep(y + 4 * BLK * 2, 3 * BLK), s_old, s_old, FX_CLICK_SLACK_CHAIN));
        }
        w_close(&w);
    }
    /* the light app stays: gain alone in APTX mode, sine in */
    W w; w_open(&w);
    swap(&w, g, vg, 48000);
    swap_def(&w, h, 48000);
    float in[BLK * 2], o[BLK * 2];
    for (int b = 0; b < 4; b++) blk_sine(&w, o, 48000, 0);
    blk_sine(&w, o, 48000, PC_FXM_APTX);
    sine(&w, in, BLK, 48000);
    memcpy(o, in, sizeof o);
    pc_fxchain_process(w.c, o, BLK, 48000, PC_FXM_APTX);
    for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], in[i] * 1.9953f, 1e-3);
    w_close(&w);
}

static void test_fxchain_mode_during_swap(void)
{
    W w; w_open(&w);
    int h = fx_first_app_of(PC_FXC_HEAVY);
    static float y[14 * BLK * 2];
    float in[BLK * 2];
    for (int b = 0; b < 14; b++) {
        uint8_t mode = b < 6 ? PC_FXM_APTX : 0;
        if (b == 0) swap_def(&w, h, 48000);                       /* SWAP and the mode in the same block */
        if (b == 10) post_op(&w, PC_FXOP_OFF, h, 0, 0);
        if (b < 3) {                                              /* suspended: dry, exactly */
            sine(&w, in, BLK, 48000);
            memcpy(y + b * BLK * 2, in, sizeof in);
            pc_fxchain_process(w.c, y + b * BLK * 2, BLK, 48000, mode);
            for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(y[b * BLK * 2 + i], in[i], 1e-7);
            CHECK_EQ_INT(LIVE_BLOCKS(), 1);                       /* installed once */
        } else blk_sine(&w, y + b * BLK * 2, 48000, mode);
        pump(&w);
        if (b < 10) CHECK_EQ_INT(w.nfreed, 0);
    }
    float s_old = fx_maxstep(y, 3 * BLK), s_new = fx_maxstep(y + 8 * BLK * 2, 2 * BLK);
    CHECK(fx_click_ok(fx_maxstep(y, 14 * BLK), s_old, s_new, FX_CLICK_SLACK_CHAIN));
    CHECK(peak(y + 8 * BLK * 2, BLK) > 0.0f);
    CHECK_EQ_INT(w.nfreed, 1);                                    /* returned once, after the OFF */
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);
    w_close(&w);
}

static void test_fxchain_rate_change_mid_fade(void)
{
    W w; w_open(&w);
    int l = app_id("limiter");
    static float y[16 * BLK * 2];
    float in[BLK * 2];
    swap_def(&w, l, 48000);                                       /* 5 ms look-ahead: block 0 warms up, block 1 fades in */
    for (int b = 0; b < 3; b++) blk_sine(&w, y + b * BLK * 2, 48000, 0);
    blk_sine(&w, y + 3 * BLK * 2, 44100, 0);                      /* fits: rate() in place, no REBUILD. rate() clears the line, so this block's step is */
    pump(&w);                                                     /* not bound-checked (spec M4: callers change rate at a track boundary) */
    CHECK_EQ_INT(w.rb[l], 0);
    CHECK_EQ_INT(w.nfreed, 0);
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    for (int b = 4; b < 6; b++) blk_sine(&w, y + b * BLK * 2, 44100, 0);   /* the line is full again: block 5 is steady and event-free */
    for (int b = 6; b < 9; b++) blk_sine(&w, y + b * BLK * 2, 96000, 0);   /* does not fit: fade to dry (block 6) + one REBUILD */
    pump(&w);
    CHECK_EQ_INT(w.rb[l], 1);
    float s_old = fx_maxstep(y + 5 * BLK * 2, BLK), s_new = fx_maxstep(y + 8 * BLK * 2, BLK);
    float hi = fx_maxstep(y + 5 * BLK * 2, 4 * BLK);               /* blocks 5..8: steady wet, the 96 kHz bypass, dry */
    CHECK(s_old > 0.02f);                                          /* a real steady reference, not an event */
    CHECK(fx_click_ok(hi, s_old, s_new, FX_CLICK_SLACK_CHAIN));
    sine(&w, in, BLK, 96000);
    memcpy(y + 9 * BLK * 2, in, sizeof in);
    pc_fxchain_process(w.c, y + 9 * BLK * 2, BLK, 96000, 0);       /* dry now, exactly */
    for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(y[9 * BLK * 2 + i], in[i], 1e-7);
    pump(&w);
    CHECK_EQ_INT(w.rb[l], 1);                                      /* once per rate */
    swap_def(&w, l, 96000);                                        /* the rebuilt block: two warm-up blocks (5 ms look-ahead = 481 frames at 96 kHz > 480), then it fades in */
    sine(&w, in, BLK, 96000);
    memcpy(y + 10 * BLK * 2, in, sizeof in);
    pc_fxchain_process(w.c, y + 10 * BLK * 2, BLK, 96000, 0);      /* warming: not heard */
    for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(y[10 * BLK * 2 + i], in[i], 1e-7);
    for (int b = 11; b < 15; b++) blk_sine(&w, y + b * BLK * 2, 96000, 0);   /* block 11 is the 2nd warm-up block, 12 the fade */
    sine(&w, in, BLK, 96000);
    memcpy(y + 15 * BLK * 2, in, sizeof in);
    pc_fxchain_process(w.c, y + 15 * BLK * 2, BLK, 96000, 0);
    float d = 0;
    for (int i = 0; i < 2 * BLK; i++) d = fmaxf(d, fabsf(y[15 * BLK * 2 + i] - in[i]));
    CHECK(d > 1e-3f);                                              /* the limiter's look-ahead delay: it runs */
    float r_old = fx_maxstep(y + 9 * BLK * 2, BLK), r_new = fx_maxstep(y + 14 * BLK * 2, 2 * BLK);
    float hi96 = fx_maxstep(y + 9 * BLK * 2, 7 * BLK);
    printf("I1 96 kHz rebuilt block faded in: step %.4f, bound %.4f\n", hi96, 2.0f * fmaxf(r_old, r_new) + FX_CLICK_SLACK_CHAIN);
    CHECK(fx_click_ok(hi96, r_old, r_new, FX_CLICK_SLACK_CHAIN));
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 1);                                     /* the old 48 kHz block */
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    w_close(&w);
}

static void test_fxchain_split(void)
{
    static float noise[1000 * 2], a[1000 * 2], b[1000 * 2];
    unsigned s = 12345;
    for (int i = 0; i < 2000; i++) { s = s * 1664525u + 1013904223u; noise[i] = ((float)(s >> 8) / (float)(1 << 24) - 0.5f) * 0.8f; }
    const char *const ids[] = { "gain", "compressor", "limiter" };
    W wa, wb;
    w_open(&wa);
    pc_fxchain_t *ca = wa.c;
    wb.c = pc_fxchain_create(al); wb.ph = 0; memset(wb.rb, 0, sizeof wb.rb); wb.nfreed = 0;
    CHECK(wb.c != NULL);
    for (int k = 0; k < 3; k++) {
        float v[PC_FXP_MAX];
        int ap = app_id(ids[k]);
        defaults(ap, v);
        if (k == 0) v[pidx(ap, "gain_db")] = 4;
        if (k == 1) v[pidx(ap, "threshold_db")] = -20;
        swap(&wa, ap, v, 48000);
        swap(&wb, ap, v, 48000);
    }
    for (int round = 0; round < 3; round++) {
        memcpy(a, noise, sizeof a); memcpy(b, noise, sizeof b);
        pc_fxchain_process(ca, a, 1000, 48000, 0);
        pc_fxchain_process(wb.c, b, 480, 48000, 0);
        pc_fxchain_process(wb.c, b + 960, 480, 48000, 0);
        pc_fxchain_process(wb.c, b + 1920, 40, 48000, 0);
        if (round == 0) CHECK(peak(a, 1000) > 0.0f);
        for (int i = 0; i < 2000; i++) CHECK_NEAR(a[i], b[i], 1e-6);
    }
    pc_fxchain_destroy(wb.c, rel);
    w_close(&wa);
}

/* Why 3 blocks: PC_FXMAIL_N + 1 swaps are applied (the helper's pending one goes in after the first pump). The first block
 * applies 1 + PC_FXRET_N: one crossfade-less retiree plus PC_FXRET_N straight FREEs that fill the return ring; the second
 * applies PC_FXRET_N - 1 (one ring slot is taken by the held retiree); the third applies the last one. Other ring sizes
 * need another count, hence the assert. */
_Static_assert(PC_FXMAIL_N == 32 && PC_FXRET_N == 16, "storm test: block count 3 is derived for 32 / 16");
static void test_fxchain_rebuild_storm(void)
{
    W w; w_open(&w);
    int l = app_id("limiter");
    int pl = pidx(l, "lookahead_ms");
    float v[PC_FXP_MAX]; defaults(l, v);
    swap(&w, l, v, 48000);
    float o[BLK * 2];
    blk_sine(&w, o, 48000, 0);
    pump(&w);
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    void *pend = NULL;      /* the helper's coalescing: only the newest unsent block is kept */
    int posted = 0, nfalse = 0;
    const pc_fxparam_t *pp = &app_of(l)->param[pl];
    for (int k = 0; k < 100; k++) {
        v[pl] = pp->lo + (pp->hi - pp->lo) * (float)(k % 7) / 6.0f;
        if (pend) { rel(pend); pend = NULL; }
        pend = mk_block(l, 48000, v, 1.0f);
        if (post_swap(&w, l, pend)) { pend = NULL; posted++; } else nfalse++;
    }
    CHECK_EQ_INT(posted, PC_FXMAIL_N);
    CHECK_EQ_INT(nfalse, 100 - PC_FXMAIL_N);
    for (int b = 0; b < 3; b++) {
        blk_sine(&w, o, 48000, 0);
        pump(&w);
        if (pend && post_swap(&w, l, pend)) pend = NULL;
    }
    CHECK(pend == NULL);
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    pc_fxchain_stat_t st; pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.mail_free, PC_FXMAIL_N);
    CHECK_EQ_INT(st.ret_free, PC_FXRET_N);
    CHECK_EQ_INT(st.blocks, 1);
    for (int i = 0; i < 2 * BLK; i++) CHECK(isfinite(o[i]));
    w_close(&w);
}

static void test_fxchain_ret_full(void)
{
    W w; w_open(&w);
    int g = app_id("gain");
    float in[BLK * 2], o[BLK * 2];
    pc_fxchain_stat_t st;
    for (int k = 0; k < PC_FXRET_N; k++) {          /* each cycle hands one block back; nobody takes */
        swap_def(&w, g, 48000);
        blk_sine(&w, o, 48000, 0);
        post_op(&w, PC_FXOP_OFF, g, 0, 0);
        blk_sine(&w, o, 48000, 0);
        blk_sine(&w, o, 48000, 0);
    }
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.ret_free, 0);
    CHECK_EQ_INT(st.blocks, 0);
    swap_def(&w, g, 48000);                          /* the 17th cycle */
    blk_sine(&w, o, 48000, 0);
    post_op(&w, PC_FXOP_OFF, g, 0, 0);
    for (int b = 0; b < 3; b++) {
        sine(&w, in, BLK, 48000);
        memcpy(o, in, sizeof o);
        pc_fxchain_process(w.c, o, BLK, 48000, 0);
        pc_fxchain_stat(w.c, &st);
        CHECK_EQ_INT(st.ret_free, 0);
        CHECK_EQ_INT(st.blocks, 1);                  /* kept, silent, retried */
        if (b >= 1) for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], in[i], 1e-7);
    }
    CHECK_EQ_INT(g_nfree, 0);                        /* nothing freed by the audio side */
    CHECK_EQ_INT(LIVE_BLOCKS(), PC_FXRET_N + 1);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, PC_FXRET_N);
    blk_sine(&w, o, 48000, 0);                       /* room again: the held block goes back */
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.blocks, 0);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, PC_FXRET_N + 1);
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);
    w_close(&w);
}

static int s_probe[32], s_nprobe;
static void probe_cb(int app, void *u) { (void)u; if (s_nprobe < 32) s_probe[s_nprobe++] = app; }

static void test_fxchain_order_runs(void)
{
    W w; w_open(&w);
    int n = pc_fxapps(NULL);
    int nheavy = 0;
    {   const pc_fxapp_t *const *ta; pc_fxapps(&ta); for (int i = 0; i < n; i++) nheavy += ta[i]->cls == PC_FXC_HEAVY; }
    pc_fxchain_set_probe(w.c, probe_cb, NULL);
    for (int i = n - 1; i >= 0; i--) swap_def(&w, i, 48000);   /* posted backwards: the order is the chain's, not the mailbox's */
    float o[BLK * 2];
    s_nprobe = 0;
    blk_sine(&w, o, 48000, 0);
    CHECK_EQ_INT(s_nprobe, n);
    for (int i = 0; i < n; i++) CHECK_EQ_INT(s_probe[i], i);
    s_nprobe = 0;
    blk_sine(&w, o, 48000, PC_FXM_APTX);                       /* the heavy one is still fading out here */
    CHECK_EQ_INT(s_nprobe, n);
    s_nprobe = 0;
    blk_sine(&w, o, 48000, PC_FXM_APTX);                       /* and gone now */
    CHECK_EQ_INT(s_nprobe, n - nheavy);
    {   const pc_fxapp_t *const *ta; pc_fxapps(&ta);
        for (int i = 0, k = 0; i < n; i++) if (ta[i]->cls != PC_FXC_HEAVY) CHECK_EQ_INT(s_probe[k++], i); }
    for (int i = 0; i < 2 * BLK; i++) CHECK(isfinite(o[i]));
    w_close(&w);
}

static void test_fxchain_stat(void)
{
    W w; w_open(&w);
    pc_fxchain_stat_t st;
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.mail_free, 32);
    CHECK_EQ_INT(st.ret_free, 16);
    CHECK_EQ_INT(st.blocks, 0);
    CHECK_EQ_INT(pc_fxchain_rate_seen(w.c), 0);
    float o[BLK * 2];
    blk_sine(&w, o, 48000, 0);
    CHECK_EQ_INT(pc_fxchain_rate_seen(w.c), 48000);
    swap_def(&w, 0, 48000);
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.mail_free, 31);                            /* posted, not drained yet */
    blk_sine(&w, o, 48000, 0);
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.mail_free, 32);
    CHECK_EQ_INT(st.blocks, 1);
    w_close(&w);                                               /* destroy frees the block left in the slot */
}

static void test_fxchain_set_mix(void)
{
    W w; w_open(&w);
    int g = app_id("gain");
    float o[BLK * 2];
    swap_def(&w, g, 48000);
    for (int b = 0; b < 2; b++) blk_sine(&w, o, 48000, 0);
    float r0 = rms(o, BLK);
    post_op(&w, PC_FXOP_SET, g, pidx(g, "gain_db"), 6);
    post_op(&w, PC_FXOP_SET, g, 99, 6);                        /* out-of-range param index: ignored */
    for (int b = 0; b < 40; b++) blk_sine(&w, o, 48000, 0);    /* the app slews its own value */
    CHECK(rms(o, BLK) > 1.9f * r0);
    post_op(&w, PC_FXOP_MIX, g, 0, 0.0f);
    blk_sine(&w, o, 48000, 0);
    blk_sine(&w, o, 48000, 0);
    CHECK_NEAR(rms(o, BLK), r0, 0.01f);
    post_op(&w, PC_FXOP_MIX, 200, 0, 0.5f);                    /* unknown app: ignored */
    blk_sine(&w, o, 48000, 0);
    w_close(&w);
}

/* ---- fix round 1: warm-up (I1), swap crossfade (I3), bit identity and `changed` bits (M5), REBUILD SETs ignored (X13) ---- */
static void lim_on_case(float ms)
{
    W w; w_open(&w);
    int l = app_id("limiter");
    float v[PC_FXP_MAX]; defaults(l, v);
    v[pidx(l, "lookahead_ms")] = ms;
    static float y[14 * BLK * 2];
    for (int b = 0; b < 14; b++) {
        if (b == 3) swap(&w, l, v, 48000);
        blk_sine(&w, y + b * BLK * 2, 48000, 0);
    }
    float s_old = fx_maxstep(y, 3 * BLK), s_new = fx_maxstep(y + 10 * BLK * 2, 3 * BLK), all = fx_maxstep(y, 14 * BLK);
    printf("I1 limiter on, look-ahead %.0f ms: step %.4f, bound %.4f\n", ms, all, 2.0f * fmaxf(s_old, s_new) + FX_CLICK_SLACK_CHAIN);
    CHECK(s_new > 0.02f);
    CHECK(fx_click_ok(all, s_old, s_new, FX_CLICK_SLACK_CHAIN));
    w_close(&w);
}

static void test_fxchain_warmup_limiter(void)
{
    lim_on_case(5);
    lim_on_case(20);
    /* look-ahead 5 -> 10 ms through a SWAP on a running, warmed-up limiter: old stays at full while the new one warms */
    W w; w_open(&w);
    int l = app_id("limiter");
    float v[PC_FXP_MAX]; defaults(l, v);
    static float y[16 * BLK * 2];
    swap(&w, l, v, 48000);
    for (int b = 0; b < 16; b++) {
        if (b == 6) { v[pidx(l, "lookahead_ms")] = 10; swap(&w, l, v, 48000); }
        blk_sine(&w, y + b * BLK * 2, 48000, 0);
        pump(&w);
    }
    float s_old = fx_maxstep(y + 3 * BLK * 2, 3 * BLK), s_new = fx_maxstep(y + 13 * BLK * 2, 3 * BLK), all = fx_maxstep(y + 3 * BLK * 2, 13 * BLK);
    printf("I1 limiter look-ahead 5 -> 10 ms swap: step %.4f, bound %.4f\n", all, 2.0f * fmaxf(s_old, s_new) + FX_CLICK_SLACK_CHAIN);
    CHECK(s_old > 0.02f);
    CHECK(fx_click_ok(all, s_old, s_new, FX_CLICK_SLACK_CHAIN));
    CHECK_EQ_INT(w.nfreed, 1);
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    w_close(&w);
}

/* a swap of a running gain block (+6 dB -> -6 dB): a crossfade, not a hard switch (dropping the old block's part fails this) */
static void test_fxchain_swap_crossfade(void)
{
    W w; w_open(&w);
    int g = app_id("gain");
    float v[PC_FXP_MAX]; defaults(g, v);
    v[pidx(g, "gain_db")] = 6;
    static float y[12 * BLK * 2];
    swap(&w, g, v, 48000);
    for (int b = 0; b < 12; b++) {
        if (b == 5) { v[pidx(g, "gain_db")] = -6; swap(&w, g, v, 48000); }
        blk_sine(&w, y + b * BLK * 2, 48000, 0);
    }
    float s_old = fx_maxstep(y + 2 * BLK * 2, 3 * BLK), s_new = fx_maxstep(y + 8 * BLK * 2, 3 * BLK), all = fx_maxstep(y + 2 * BLK * 2, 10 * BLK);
    CHECK(s_old > 1.5f * s_new);
    CHECK(fx_click_ok(all, s_old, s_new, FX_CLICK_SLACK_CHAIN));
    CHECK(rms(y + 8 * BLK * 2, BLK) < 0.6f * rms(y + 3 * BLK * 2, BLK));   /* it really changed */
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 1);
    w_close(&w);
}

/* the REBUILD path with a real line: velvet_widener length 25 -> 40 ms is a new block through the swap, the old one comes back once,
 * and the cross-fade does not click */
static void test_vnd_rebuild_swap(void)
{
    W w; w_open(&w);
    int a = app_id("velvet_widener");
    float v[PC_FXP_MAX]; defaults(a, v);
    v[pidx(a, "amount")] = 1;
    static float y[16 * BLK * 2];
    swap(&w, a, v, 48000);
    for (int b = 0; b < 16; b++) {
        if (b == 6) { v[pidx(a, "length_ms")] = 40; swap(&w, a, v, 48000); }
        blk_sine(&w, y + b * BLK * 2, 48000, 0);
        pump(&w);
        if (b < 6) CHECK_EQ_INT(w.nfreed, 0);
    }
    float s_old = fx_maxstep(y + 3 * BLK * 2, 3 * BLK), s_new = fx_maxstep(y + 12 * BLK * 2, 4 * BLK), all = fx_maxstep(y + 3 * BLK * 2, 13 * BLK);
    CHECK(s_old > 0.01f && s_new > 0.01f);                         /* steady wet runs, not events */
    CHECK(fx_click_ok(all, s_old, s_new, FX_CLICK_SLACK_CHAIN));
    CHECK_EQ_INT(w.nfreed, 1);                                     /* the 25 ms block, once */
    CHECK_EQ_INT(w.rb[a], 0);                                      /* a swap by the LVGL side, not a rate REBUILD */
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    w_close(&w);
}

/* a steady slot is exactly the app's own process() on the same input (in place, no blend), warm-up and fade included */
static void test_fxchain_steady_bit_identical(void)
{
    const char *const ids[] = { "limiter", "compressor", "gain" };
    for (int k = 0; k < 3; k++) {
        int a = app_id(ids[k]);
        const pc_fxapp_t *ap = app_of(a);
        float v[PC_FXP_MAX]; defaults(a, v);
        W w; w_open(&w);
        void *own = calloc(1, ap->state_size(48000, v));
        ap->init(own, 48000, v);
        swap(&w, a, v, 48000);
        for (int b = 0; b < 8; b++) {
            float in[BLK * 2], out[BLK * 2], ref[BLK * 2];
            sine(&w, in, BLK, 48000);
            memcpy(out, in, sizeof in); memcpy(ref, in, sizeof in);
            pc_fxchain_process(w.c, out, BLK, 48000, 0);
            ap->process(own, ref, BLK);
            if (b >= 4) CHECK(memcmp(out, ref, sizeof out) == 0);
        }
        free(own);
        w_close(&w);
    }
}

/* ---- spy app: records what the chain hands it ---- */
static struct { int nset, nreset, nproc, nrate; uint32_t changed; float v5, v40; int lat; } spy;
static pc_fxparam_t spy_param[44];
static size_t spy_size(uint32_t r, const float *v) { (void)r; (void)v; return 16; }
static void spy_init(void *st, uint32_t r, const float *v) { (void)st; (void)r; (void)v; }
static bool spy_fits(const void *st, uint32_t r, const float *v) { (void)st; (void)r; (void)v; return true; }
static void spy_set(void *st, const float *v, uint32_t ch) { (void)st; spy.nset++; spy.changed = ch; spy.v5 = v[5]; spy.v40 = v[40]; }
static void spy_rate(void *st, uint32_t r) { (void)st; (void)r; spy.nrate++; }
static void spy_reset(void *st) { (void)st; spy.nreset++; }
static void spy_process(void *st, float *lr, int frames) { (void)st; spy.nproc++; for (int i = 0; i < 2 * frames; i++) lr[i] += 0.25f; }
static int spy_latency(const void *st) { (void)st; return spy.lat; }
static pc_fxapp_t spy_app = { "spy", "spy", "spy", "", "", PC_FXFAM_GAIN, PC_FXC_LIGHT, 0, 44, spy_param,
                              spy_size, spy_init, spy_fits, spy_set, spy_rate, spy_reset, spy_process, spy_latency };
static const pc_fxapp_t *const spy_tab[] = { &spy_app };

static void spy_open(W *w, int cls)
{
    memset(&spy, 0, sizeof spy);
    memset(spy_param, 0, sizeof spy_param);
    spy_param[5].flags = PC_FXP_REBUILD;
    spy_app.cls = (uint8_t)cls;
    memset(w, 0, sizeof *w); g_live = g_nfree = 0;
    w->c = pc_fxchain_create_with(al, spy_tab, 1);
    CHECK(w->c != NULL);
}
static void spy_block_rate(W *w, float *o, uint8_t mode, float in, uint32_t rate)
{
    for (int i = 0; i < 2 * BLK; i++) o[i] = in;
    pc_fxchain_process(w->c, o, BLK, rate, mode);
}
static void spy_block(W *w, float *o, uint8_t mode, float in)
{
    for (int i = 0; i < 2 * BLK; i++) o[i] = in;
    pc_fxchain_process(w->c, o, BLK, 48000, mode);
}
static void spy_install(W *w)    /* a block of registry app 0 is big enough for the spy (which keeps no state) */
{
    float v[PC_FXP_MAX]; defaults(0, v);
    swap(w, 0, v, 48000);
}

static void test_fxchain_changed_bits(void)
{
    W w; spy_open(&w, PC_FXC_LIGHT);
    float o[BLK * 2];
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);
    CHECK_EQ_INT(spy.nset, 0);
    post_op(&w, PC_FXOP_SET, 0, 3, 1.0f);
    post_op(&w, PC_FXOP_SET, 0, 7, 2.0f);
    post_op(&w, PC_FXOP_SET, 0, 33, 3.0f);
    post_op(&w, PC_FXOP_SET, 0, 40, 4.0f);
    post_op(&w, PC_FXOP_SET, 0, 5, 9.0f);          /* REBUILD parameter: ignored (X13) */
    post_op(&w, PC_FXOP_SET, 0, 99, 9.0f);         /* no such parameter */
    spy_block(&w, o, 0, 0.1f);
    CHECK_EQ_INT(spy.nset, 1);                      /* all SETs of one drain: one set() */
    CHECK_EQ_INT(spy.changed, (1u << 3) | (1u << 7) | (1u << 31));
    CHECK_NEAR(spy.v40, 4.0f, 0);
    CHECK_NEAR(spy.v5, 0.0f, 0);                    /* the REBUILD value is untouched */
    post_op(&w, PC_FXOP_SET, 0, 5, 9.0f);
    spy_block(&w, o, 0, 0.1f);
    CHECK_EQ_INT(spy.nset, 1);                      /* only REBUILD SETs: no set() at all */
    w_close(&w);
}

static void test_fxchain_warmup_counts(void)
{
    float o[BLK * 2];
    W w;
    /* latency 1000 frames = 3 blocks of 480: three silent blocks, then the fade (block 4), full (block 5) */
    spy_open(&w, PC_FXC_LIGHT); spy.lat = 1000;
    spy_install(&w);
    for (int b = 1; b <= 6; b++) {
        spy_block(&w, o, 0, 0.1f);
        if (b <= 3) { for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], 0.1f, 1e-7); }
        if (b == 4) { CHECK_NEAR(o[0], 0.1f, 0.01f); CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5); }
        if (b >= 5) { CHECK_NEAR(o[0], 0.35f, 1e-5); CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5); }
    }
    w_close(&w);
    /* OFF during warm-up: the block goes back unheard */
    spy_open(&w, PC_FXC_LIGHT); spy.lat = 1000;
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);
    post_op(&w, PC_FXOP_OFF, 0, 0, 0);
    spy_block(&w, o, 0, 0.1f);
    for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], 0.1f, 1e-7);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 1);
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);
    w_close(&w);
    /* a newer SWAP during warm-up: the first block goes back unheard, the second warms from scratch */
    spy_open(&w, PC_FXC_LIGHT); spy.lat = 1000;
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 1);
    for (int b = 0; b < 2; b++) { spy_block(&w, o, 0, 0.1f); for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], 0.1f, 1e-7); }   /* blocks 2, 3 of its warm-up */
    spy_block(&w, o, 0, 0.1f);
    CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5);
    w_close(&w);
    /* a mode suspension during warm-up: installed stays, reset(), the warm-up starts over */
    spy_open(&w, PC_FXC_HEAVY); spy.lat = 1000;
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);
    spy_block(&w, o, PC_FXM_APTX, 0.1f);
    CHECK_EQ_INT(spy.nreset, 1);
    for (int b = 0; b < 3; b++) { spy_block(&w, o, 0, 0.1f); for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], 0.1f, 1e-7); }
    spy_block(&w, o, 0, 0.1f);
    CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 0);
    CHECK_EQ_INT(LIVE_BLOCKS(), 1);
    w_close(&w);
    /* a running spy block replaced by a warming one: the old stays at full until the new can be heard, then they crossfade */
    spy_open(&w, PC_FXC_LIGHT);
    spy_install(&w);
    for (int b = 0; b < 3; b++) spy_block(&w, o, 0, 0.1f);
    spy.lat = 1000;                                  /* the new block's latency (the spy has one global) */
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);                       /* warming: old at full, +0.25 stays */
    CHECK_NEAR(o[0], 0.35f, 1e-5); CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5);
    w_close(&w);
}

/* N1: a fitting rate change calls rate() exactly once on the installed block; N3: during warm-up it restarts the warm-up */
static void test_fxchain_rate_calls(void)
{
    float o[BLK * 2];
    W w; spy_open(&w, PC_FXC_LIGHT);
    spy_install(&w);
    for (int b = 0; b < 3; b++) spy_block_rate(&w, o, 0, 0.1f, 48000);
    CHECK_EQ_INT(spy.nrate, 0);
    for (int b = 0; b < 3; b++) spy_block_rate(&w, o, 0, 0.1f, 44100);
    CHECK_EQ_INT(spy.nrate, 1);
    spy_block_rate(&w, o, 0, 0.1f, 48000);
    CHECK_EQ_INT(spy.nrate, 2);
    w_close(&w);
    /* latency 1000 = 3 warm-up blocks; a rate change in block 2 clears the line, so the warm-up starts over: silent until block 5 */
    spy_open(&w, PC_FXC_LIGHT); spy.lat = 1000;
    spy_install(&w);
    for (int b = 1; b <= 6; b++) {
        spy_block_rate(&w, o, 0, 0.1f, b == 1 ? 48000 : 44100);
        if (b <= 4) { for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], 0.1f, 1e-7); }
        if (b == 5) CHECK_NEAR(o[2 * BLK - 1], 0.35f, 1e-5);       /* the fade-in block */
        if (b == 6) CHECK_NEAR(o[0], 0.35f, 1e-5);
    }
    CHECK_EQ_INT(spy.nrate, 1);
    w_close(&w);
    /* the seam refuses a table the blocks cannot hold */
    spy_open(&w, PC_FXC_LIGHT);
    spy_app.nparam = PC_FXP_MAX + 1;
    CHECK(pc_fxchain_create_with(al, spy_tab, 1) == NULL);
    spy_app.nparam = 44;
    CHECK(pc_fxchain_create_with(al, spy_tab, PC_FXAPP_MAX + 1) == NULL);
    CHECK_EQ_INT(g_live, 1);
    w_close(&w);
}

/* OFF while a replacement warms: the unheard block goes back at once and the old one fades out as itself (both FREEs in the OFF block) */
static void test_fxchain_off_during_swap_warmup(void)
{
    float o[BLK * 2];
    W w; spy_open(&w, PC_FXC_LIGHT);
    spy_install(&w);
    for (int b = 0; b < 3; b++) spy_block(&w, o, 0, 0.1f);
    spy.lat = 1000;
    spy_install(&w);
    spy_block(&w, o, 0, 0.1f);                       /* the new block warms; the old one stays at full */
    CHECK_NEAR(o[0], 0.35f, 1e-5);
    post_op(&w, PC_FXOP_OFF, 0, 0, 0);
    spy_block(&w, o, 0, 0.1f);                       /* the old block fades out */
    CHECK_NEAR(o[0], 0.35f, 0.01f); CHECK_NEAR(o[2 * BLK - 1], 0.1f, 1e-5);
    pump(&w);
    CHECK_EQ_INT(w.nfreed, 2);
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);
    spy_block(&w, o, 0, 0.1f);
    CHECK_NEAR(o[0], 0.1f, 1e-7);
    w_close(&w);
}

/* on battery the LIGHT widener keeps running and the HEAVY app is suspended: the chain equals a chain with the widener alone;
 * back on mains the heavy app returns (moved here from test_dsp.c's test_chain_battery with the old chain, T3) */
static void test_fxchain_battery_light_runs(void)
{
    int h = fx_first_app_of(PC_FXC_HEAVY), wd = app_id("widener");
    CHECK(h >= 0);
    CHECK(app_of(wd)->cls == PC_FXC_LIGHT);
    float vw[PC_FXP_MAX]; defaults(wd, vw);
    vw[pidx(wd, "width")] = 1.8f;
    W a, b;
    w_open(&a);
    swap(&a, wd, vw, 48000);
    swap_def(&a, h, 48000);
    pc_fxchain_t *ca = a.c;
    memset(&b, 0, sizeof b);
    b.c = pc_fxchain_create(al);
    CHECK(b.c != NULL);
    swap(&b, wd, vw, 48000);
    float x[2 * BLK], y[2 * BLK];
    for (int blk = 0; blk < 50; blk++) {
        for (int i = 0; i < BLK; i++) {
            x[2 * i] = 0.3f * sinf(0.05f * (float)(blk * BLK + i));
            x[2 * i + 1] = 0.2f * sinf(0.031f * (float)(blk * BLK + i));
        }
        memcpy(y, x, sizeof x);
        pc_fxchain_process(ca, x, BLK, 48000, PC_FXM_BATT);
        pc_fxchain_process(b.c, y, BLK, 48000, 0);
        for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(x[i], y[i], 1e-6);
    }
    CHECK(peak(x, BLK) > 0.1f);                                 /* the widener really ran */
    double diff = 0;
    for (int blk = 50; blk < 56; blk++) {                       /* back on mains: the heavy app returns (a reverb needs a few blocks to show) */
        for (int i = 0; i < BLK; i++) {
            x[2 * i] = 0.3f * sinf(0.05f * (float)(blk * BLK + i));
            x[2 * i + 1] = 0.2f * sinf(0.031f * (float)(blk * BLK + i));
        }
        memcpy(y, x, sizeof x);
        pc_fxchain_process(ca, x, BLK, 48000, 0);
        pc_fxchain_process(b.c, y, BLK, 48000, 0);
        for (int i = 0; i < 2 * BLK; i++) diff += fabs(x[i] - y[i]);
    }
    CHECK(diff > 1e-3);
    pc_fxchain_destroy(b.c, rel);
    w_close(&a);
}

/* pc_fxchain_busy: what a caller with no source needs to know before skipping the chain (T3 fix round 1) */
static int run_silence_while_busy(W *w, int limit)
{
    float o[BLK * 2];
    int n = 0;
    while (pc_fxchain_busy(w->c) && n < limit) {
        memset(o, 0, sizeof o);
        pc_fxchain_process(w->c, o, BLK, 48000, 0);
        pump(w);
        n++;
    }
    return n;
}

static void test_fxchain_busy(void)
{
    W w; w_open(&w);
    CHECK(!pc_fxchain_busy(w.c));                       /* fresh */
    int g = app_id("gain"), l = app_id("limiter");
    float o[BLK * 2];
    swap_def(&w, g, 48000);
    CHECK(pc_fxchain_busy(w.c));                        /* a message waits */
    CHECK(run_silence_while_busy(&w, 10) == 1);         /* no latency: installed, warm and settled in one block */
    CHECK(!pc_fxchain_busy(w.c));
    /* the limiter's 20 ms look-ahead: it warms up for two blocks, then fades in */
    float v[PC_FXP_MAX]; defaults(l, v);
    v[pidx(l, "lookahead_ms")] = 20;
    swap(&w, l, v, 48000);
    CHECK(pc_fxchain_busy(w.c));
    int n = run_silence_while_busy(&w, 20);
    CHECK(n >= 3 && n <= 6);                            /* the swap block, the warm-up, the fade: bounded */
    CHECK(!pc_fxchain_busy(w.c));
    for (int b = 0; b < 5; b++) { memset(o, 0, sizeof o); pc_fxchain_process(w.c, o, BLK, 48000, 0); CHECK(!pc_fxchain_busy(w.c)); }   /* settled stays settled */
    /* a mode that suspends a heavy app is not "warming": the idle chain is not kept busy by it */
    int h = fx_first_app_of(PC_FXC_HEAVY);
    swap_def(&w, h, 48000);
    memset(o, 0, sizeof o);
    pc_fxchain_process(w.c, o, BLK, 48000, PC_FXM_APTX);
    memset(o, 0, sizeof o);
    pc_fxchain_process(w.c, o, BLK, 48000, PC_FXM_APTX);
    CHECK(!pc_fxchain_busy(w.c));
    pump(&w);
    /* off: the OFF is a message, then the block fades out and goes back */
    post_op(&w, PC_FXOP_OFF, g, 0, 0);
    post_op(&w, PC_FXOP_OFF, l, 0, 0);
    post_op(&w, PC_FXOP_OFF, h, 0, 0);
    CHECK(pc_fxchain_busy(w.c));
    n = run_silence_while_busy(&w, 20);
    CHECK(n >= 1 && n <= 6);
    CHECK(!pc_fxchain_busy(w.c));
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);                     /* everything went back with the source silent */
    /* an OFF block that cannot retire (return ring full) keeps it busy until somebody takes the returns */
    for (int k = 0; k < PC_FXRET_N; k++) {
        swap_def(&w, g, 48000);
        blk_sine(&w, o, 48000, 0);
        post_op(&w, PC_FXOP_OFF, g, 0, 0);
        blk_sine(&w, o, 48000, 0);
        blk_sine(&w, o, 48000, 0);
    }
    swap_def(&w, g, 48000);
    blk_sine(&w, o, 48000, 0);
    post_op(&w, PC_FXOP_OFF, g, 0, 0);
    for (int b = 0; b < 3; b++) { memset(o, 0, sizeof o); pc_fxchain_process(w.c, o, BLK, 48000, 0); CHECK(pc_fxchain_busy(w.c)); }
    pump(&w);
    CHECK(run_silence_while_busy(&w, 10) >= 1);
    CHECK(!pc_fxchain_busy(w.c));
    pump(&w);
    CHECK_EQ_INT(LIVE_BLOCKS(), 0);
    w_close(&w);
}

/* the idle hook: silent input into a chain still holding the music's look-ahead line and a reverb tail leaves as exactly zero, while the chain
 * did its work (T3 fix round 2) */
static void test_fxchain_idle_discards(void)
{
    W w; w_open(&w);
    int l = app_id("limiter"), h = fx_first_app_of(PC_FXC_HEAVY), g = app_id("gain");
    float v[PC_FXP_MAX]; defaults(l, v);
    v[pidx(l, "lookahead_ms")] = 20;
    swap(&w, l, v, 48000);
    swap_def(&w, h, 48000);
    float o[BLK * 2];
    for (int b = 0; b < 40; b++) { blk_sine(&w, o, 48000, 0); pump(&w); }      /* music: the limiter line and the reverb are full */
    CHECK(!pc_fxchain_busy(w.c));
    memset(o, 0, sizeof o);
    pc_fxchain_idle(w.c, o, BLK, 48000, 0);                                      /* settled: nothing happens */
    CHECK(peak(o, BLK) == 0.0f);
    /* a change while stopped: the chain is busy, runs the silent block, and the block leaves as zeros */
    swap_def(&w, g, 48000);
    CHECK(pc_fxchain_busy(w.c));
    float probe[BLK * 2];
    memset(probe, 0, sizeof probe);
    pc_fxchain_process(w.c, probe, BLK, 48000, 0);                               /* what process alone would have played: the stale music is audible */
    CHECK(peak(probe, BLK) > 0.01f);
    for (int b = 0; b < 40; b++) { blk_sine(&w, o, 48000, 0); pump(&w); }      /* put the same music state back */
    swap_def(&w, g, 48000);                                                       /* (the gain is already installed: a second swap is a new block) */
    CHECK(pc_fxchain_busy(w.c));
    int n = 0;
    while (pc_fxchain_busy(w.c) && n < 20) {
        memset(o, 0, sizeof o);
        pc_fxchain_idle(w.c, o, BLK, 48000, 0);
        CHECK(peak(o, BLK) == 0.0f);
        pump(&w);
        n++;
    }
    CHECK(n >= 1 && n < 20);                                                      /* the work was done: messages applied, busy ended */
    pc_fxchain_stat_t st;
    pc_fxchain_stat(w.c, &st);
    CHECK_EQ_INT(st.mail_free, PC_FXMAIL_N);
    w_close(&w);
}

/* hires T8: a 960-frame call at 96 kHz is two 480-frame process() calls of the app, the output the same as two 480 calls */
static void test_fxchain_960_two_subblocks(void)
{
    static float a[960 * 2], b[960 * 2];
    W w; spy_open(&w, PC_FXC_LIGHT);
    float v[PC_FXP_MAX]; defaults(0, v);
    swap(&w, 0, v, 96000);
    for (int i = 0; i < 960 * 2; i++) a[i] = b[i] = 0.1f * (float)((i % 7) - 3);
    pc_fxchain_process(w.c, a, 960, 96000, 0);
    CHECK_EQ_INT(spy.nproc, 2);
    w_close(&w);
    W w2; spy_open(&w2, PC_FXC_LIGHT);
    swap(&w2, 0, v, 96000);
    pc_fxchain_process(w2.c, b, 480, 96000, 0);
    pc_fxchain_process(w2.c, b + 960, 480, 96000, 0);
    CHECK_EQ_INT(spy.nproc, 2);
    for (int i = 0; i < 960 * 2; i++) CHECK_NEAR(a[i], b[i], 1e-7);
    w_close(&w2);
}

/* hires T8: mode HIRES pauses the first HEAVY app within one sub-block (faded to dry), the LIGHT one stays */
static void test_fxchain_hires_suspends_heavy(void)
{
    int h = fx_first_app_of(PC_FXC_HEAVY), g = app_id("gain");
    if (h < 0) { printf("SKIP no heavy app\n"); return; }
    float vg[PC_FXP_MAX]; defaults(g, vg);
    vg[pidx(g, "gain_db")] = 6;
    static float y[16 * BLK * 2];
    W w; w_open(&w);
    swap(&w, g, vg, 96000);
    swap_def(&w, h, 96000);
    for (int b = 0; b < 16; b++) {
        uint8_t mode = (b >= 6 && b < 9) ? PC_FXM_HIRES : 0;
        float *o = y + b * BLK * 2;
        if (b < 7) blk_sine(&w, o, 96000, mode);
        else { memset(o, 0, BLK * 2 * sizeof *o); pc_fxchain_process(w.c, o, BLK, 96000, mode); }
    }
    CHECK(peak(y + 7 * BLK * 2, BLK) < 1e-6f);        /* tail gone, gain alone on silence */
    CHECK(peak(y + 9 * BLK * 2, BLK) < 1e-6f);        /* mode back to 0: reset() was called, no old tail */
    float s_old = fx_maxstep(y + 4 * BLK * 2, 2 * BLK);
    CHECK(fx_click_ok(fx_maxstep(y + 4 * BLK * 2, 3 * BLK), s_old, s_old, FX_CLICK_SLACK_CHAIN));
    w_close(&w);
    W w2; w_open(&w2);
    swap(&w2, g, vg, 96000);
    swap_def(&w2, h, 96000);
    float in[BLK * 2], o[BLK * 2];
    for (int b = 0; b < 4; b++) blk_sine(&w2, o, 96000, 0);
    blk_sine(&w2, o, 96000, PC_FXM_HIRES);
    sine(&w2, in, BLK, 96000);
    memcpy(o, in, sizeof o);
    pc_fxchain_process(w2.c, o, BLK, 96000, PC_FXM_HIRES);
    for (int i = 0; i < 2 * BLK; i++) CHECK_NEAR(o[i], in[i] * 1.9953f, 1e-3);
    w_close(&w2);
}

int main(void)
{
    RUN(test_fxchain_stat);
    RUN(test_fxchain_battery_light_runs);
    RUN(test_fxchain_busy);
    RUN(test_fxchain_idle_discards);
    RUN(test_fxchain_on_off_fade);
    RUN(test_fxchain_mode_suspends_heavy);
    RUN(test_fxchain_mode_during_swap);
    RUN(test_fxchain_rate_change_mid_fade);
    RUN(test_fxchain_split);
    RUN(test_fxchain_rebuild_storm);
    RUN(test_fxchain_ret_full);
    RUN(test_fxchain_order_runs);
    RUN(test_fxchain_set_mix);
    RUN(test_fxchain_warmup_limiter);
    RUN(test_fxchain_swap_crossfade);
    RUN(test_vnd_rebuild_swap);
    RUN(test_fxchain_steady_bit_identical);
    RUN(test_fxchain_changed_bits);
    RUN(test_fxchain_warmup_counts);
    RUN(test_fxchain_rate_calls);
    RUN(test_fxchain_off_during_swap_warmup);
    RUN(test_fxchain_960_two_subblocks);
    RUN(test_fxchain_hires_suspends_heavy);
    return 0;
}
