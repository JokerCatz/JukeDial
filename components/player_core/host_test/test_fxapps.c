// SPDX-License-Identifier: LGPL-2.1-or-later
/* fx apps (spec §2, §10.2): finite output, silence after the tail, click-free set(), block independence, set() keeps history,
 * rate()/reset()/fits()/latency(); the biquad measurements (eq peak, low band, shelves), the limiter ceiling and the reverb tail
 * (moved here from test_dsp.c with the old chain, T3). The old nodes the apps were once compared against are gone (T1's match_old). */
#include "pc/pc_fxapp.h"
#include "pc/pc_fxchain.h"
#include "pc/pc_dsp.h"
#include "pc_fxapp_parts.h"     /* fx_bqs_t (slew bounds) and the test hooks */
#include "fx_test.h"
#include "pc_test.h"

#define RATE 48000
#define BLK PC_FX_BLOCK
#define PI 3.14159265358979

typedef struct { const char *k; float v; } kv_t;
typedef struct { const char *app; kv_t p[16]; } pset_t;

/* two parameter sets per kind (from test_dsp.c where it has them: tone, loudness, bass, compressor, gain, widener, limiter, eq) */
static const pset_t SETS[] = {
    { "gain", { { "gain_db", 12 } } },
    { "gain", { { "gain_db", -6 } } },
    { "balance", { { "balance", 1 } } },
    { "balance", { { "balance", -0.5f } } },
    { "widener", { { "width", 2 } } },
    { "widener", { { "width", 0 } } },
    { "crossfeed", { { "feed_db", 0 }, { "cutoff_hz", 2000 } } },
    { "crossfeed", { { "feed_db", 15 }, { "cutoff_hz", 300 } } },
    { "velvet_widener", { { "amount", 1 }, { "length_ms", 50 }, { "mode", 1 }, { "seed", 99 } } },
    { "velvet_widener", { { "amount", 0.2f }, { "length_ms", 5 }, { "mode", 0 }, { "seed", 3 } } },
    { "equalizer", { { "count", 1 }, { "b0_type", 0 }, { "b0_fc", 500 }, { "b0_gain_db", 3 }, { "b0_q", 1 } } },
    { "equalizer", { { "preamp_db", -3 }, { "count", 3 }, { "b0_type", 0 }, { "b0_fc", 500 }, { "b0_gain_db", 6 }, { "b0_q", 1 },
                     { "b1_type", 1 }, { "b1_fc", 100 }, { "b1_gain_db", 4 }, { "b1_q", 0.707f },
                     { "b2_type", 2 }, { "b2_fc", 8000 }, { "b2_gain_db", -3 }, { "b2_q", 0.707f } } },
    { "tone", { { "warmth", 1.5f }, { "depth", 1.5f }, { "air", -1.5f } } },
    { "tone", { { "punch", 0.5f }, { "voice", -0.7f }, { "sibilance", 1.0f }, { "bite", -1.5f } } },
    { "bass", { { "amount", 1 }, { "reach_hz", 60 }, { "cut", 1 } } },
    { "bass", { { "amount", 0.25f }, { "reach_hz", 160 } } },
    { "loudness", { { "quieter_db", 40 } } },
    { "loudness", { { "quieter_db", 5 } } },
    { "tube", { { "drive_db", 24 }, { "even", 1 }, { "level_db", 6 } } },
    { "tube", { { "drive_db", -12 }, { "even", 0 }, { "level_db", -24 }, { "mix", 0.5f } } },
    { "compressor", { { "threshold_db", -40 }, { "ratio", 20 } } },
    { "compressor", { { "threshold_db", -12 }, { "ratio", 4 }, { "knee_db", 0 }, { "attack_ms", 1 }, { "release_ms", 50 }, { "makeup_db", 3 },
                      { "auto_makeup", 0 } } },
    { "freeverb", { { "room", 1 }, { "damp", 0 }, { "width", 0 }, { "level", 0.5f } } },
    { "freeverb", { { "room", 0.2f }, { "damp", 0.9f }, { "width", 0.5f }, { "level", 0.1f } } },
    { "limiter", { { "ceiling_db", -6 }, { "release_ms", 50 }, { "lookahead_ms", 20 } } },
    { "limiter", { { "ceiling_db", -0.1f }, { "release_ms", 1000 }, { "lookahead_ms", 0 } } },
};
#define NSETS ((int)(sizeof SETS / sizeof *SETS))

static int napps(const pc_fxapp_t *const **a) { return pc_fxapps(a); }

static int pidx(const pc_fxapp_t *ap, const char *id)
{
    for (int p = 0; p < ap->nparam; p++)
        if (!strcmp(ap->param[p].id, id)) return p;
    fprintf(stderr, "no param %s.%s\n", ap->id, id);
    exit(1);
}

static void defaults(const pc_fxapp_t *ap, float *v)
{
    for (int p = 0; p < ap->nparam; p++) v[p] = ap->param[p].def;
}

static void apply_set(const pc_fxapp_t *ap, const pset_t *s, float *v)
{
    defaults(ap, v);
    for (int k = 0; k < 16 && s->p[k].k; k++) v[pidx(ap, s->p[k].k)] = s->p[k].v;
}

static void *make(const pc_fxapp_t *ap, uint32_t rate, const float *v)
{
    size_t n = ap->state_size(rate, v);
    CHECK(n > 0);
    void *st = malloc(n);
    CHECK(st != NULL);
    memset(st, 0xA5, n);   /* init must lay out everything itself */
    ap->init(st, rate, v);
    return st;
}

/* the test signal: two tones with a slow swell, a high tone and noise; peaks above 1 now and then (limiter, compressor) */
static uint32_t s_noise = 1;
static float noise(void) { s_noise = s_noise * 1664525u + 1013904223u; return (float)(s_noise >> 8) / 8388608.0f - 1.0f; }
static void signal(float *x, long start, int frames, uint32_t rate)
{
    for (int i = 0; i < frames; i++) {
        double t = (double)(start + i) / rate, sw = 0.5 + 0.5 * sin(2 * PI * 1.3 * t);
        x[2 * i] = (float)(0.7 * sw * sin(2 * PI * 220 * t) + 0.3 * sin(2 * PI * 3100 * t)) + 0.2f * noise();
        x[2 * i + 1] = (float)(0.6 * sw * sin(2 * PI * 330 * t + 1) + 0.25 * sin(2 * PI * 5200 * t)) + 0.2f * noise();
    }
}
/* 480 frames are exactly 10 periods at 48 kHz: the cosine puts every block start (where set() lands) on a peak,
 * so a gain that jumps instead of ramping shows as a step (a sine would hide it in its zero crossing) */
static void cosine(float *x, long start, int frames, uint32_t rate, double f, double amp)
{
    for (int i = 0; i < frames; i++) x[2 * i] = x[2 * i + 1] = (float)(amp * cos(2 * PI * f * (double)(start + i) / rate));
}

/* (b) finite output for random parameters including both ends */
static uint32_t s_rnd = 12345;
static float rnd01(void) { s_rnd = s_rnd * 1103515245u + 12345u; return (float)((s_rnd >> 8) & 0xFFFFFF) / 16777216.0f; }
static void random_values(const pc_fxapp_t *ap, float *v)
{
    for (int p = 0; p < ap->nparam; p++) {
        const pc_fxparam_t *q = &ap->param[p];
        float r = rnd01();
        int end = (int)(rnd01() * 4);
        if (end == 0) v[p] = q->lo;
        else if (end == 1) v[p] = q->hi;
        else if (q->unit == PC_FXU_BOOL || q->unit == PC_FXU_CHOICE) v[p] = q->lo + floorf(r * (q->hi - q->lo + 1) * 0.999f);
        else if (q->flags & PC_FXP_LOG) v[p] = q->lo * powf(q->hi / q->lo, r);
        else v[p] = q->lo + r * (q->hi - q->lo);
    }
}

static void test_random_finite(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    static float x[2 * BLK];
    for (int i = 0; i < n; i++) {
        for (int set = 0; set < 200; set++) {
            float v[PC_FXP_MAX], w[PC_FXP_MAX];
            random_values(a[i], v);
            random_values(a[i], w);
            for (int p = 0; p < a[i]->nparam; p++) if (a[i]->param[p].flags & PC_FXP_REBUILD) w[p] = v[p];
            uint32_t rate = set & 1 ? 44100 : RATE;
            void *st = make(a[i], rate, v);
            for (int blk = 0; blk < 12; blk++) {
                for (int k = 0; k < 2 * BLK; k++) x[k] = 0.9f * noise();
                if (blk == 6) a[i]->set(st, w, 0xFFFFFFFFu);
                a[i]->process(st, x, BLK);
                for (int k = 0; k < 2 * BLK; k++)
                    if (!isfinite(x[k])) { fprintf(stderr, "%s: set %d block %d not finite\n", a[i]->id, set, blk); exit(1); }
            }
            free(st);
        }
    }
}

/* (c) silence in -> silence out after the tail */
static void silence_after(const pc_fxapp_t *ap, const float *v, double tail_s)
{
    void *st = make(ap, RATE, v);
    static float x[2 * BLK];
    s_noise = 3;
    for (int blk = 0; blk < 50; blk++) { signal(x, (long)blk * BLK, BLK, RATE); ap->process(st, x, BLK); }
    int tail = (int)(tail_s * RATE / BLK);
    float worst = 0;
    for (int blk = 0; blk < tail + 10; blk++) {
        memset(x, 0, sizeof x);
        ap->process(st, x, BLK);
        if (blk >= tail) for (int k = 0; k < 2 * BLK; k++) worst = fmaxf(worst, fabsf(x[k]));
    }
    free(st);
    if (worst > 1e-7f) { fprintf(stderr, "%s: %g after %.1f s of silence\n", ap->id, worst, tail_s); exit(1); }
}

/* the tail: 0.1 s, freeverb 3 s (spec/brief), tube 0.6 s. Where the old node itself decays slower (measured on the old DSP, which
 * the apps match exactly), the tail is that decay: bass holds its envelope for 50 ms (time constant) and makes a
 * DC-like -0.6 x envelope from it, below 1e-7 after 0.3 s; freeverb at room 1 has a loop gain of 0.98, below 1e-7 after ~25 s. */
static double tail_of(const pc_fxapp_t *ap, const float *v)
{
    if (!strcmp(ap->id, "bass")) return 0.3;
    if (!strcmp(ap->id, "tube")) return 0.6;   /* the 10 Hz DC blocker (time constant 16 ms) lets the curve's own offset go: 1e-7 after 0.6 s */
    if (!strcmp(ap->id, "freeverb")) return v[pidx(ap, "room")] > 0.95f ? 30.0 : 3.0;
    if (!strcmp(ap->id, "velvet_room")) return 5.0 * v[pidx(ap, "decay_s")] + 1.0;   /* 60 dB per decay_s: 5 of them take 0.3 down to ~1e-7 */
    return 0.1;
}

static void test_silence(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    for (int i = 0; i < n; i++) {
        float v[PC_FXP_MAX];
        defaults(a[i], v);
        silence_after(a[i], v, tail_of(a[i], v));
        for (int s = 0; s < NSETS; s++)
            if (!strcmp(SETS[s].app, a[i]->id)) { apply_set(a[i], &SETS[s], v); silence_after(a[i], v, tail_of(a[i], v)); }
    }
}

/* (d) click: set() mid-stream on a 1 kHz 0.5 sine against the steady runs at both values */
#define WARM 50
#define WIN 20
static float click_run(const pc_fxapp_t *ap, const float *va, const float *vb, int p, double f)
{
    void *st = make(ap, RATE, va);
    static float x[2 * BLK], y[2 * (1 + WIN * BLK)];
    for (int blk = 0; blk < WARM; blk++) { cosine(x, (long)blk * BLK, BLK, RATE, f, 0.5); ap->process(st, x, BLK); }
    y[0] = x[2 * BLK - 2]; y[1] = x[2 * BLK - 1];
    if (vb) ap->set(st, vb, 1u << (p < 31 ? p : 31));
    for (int blk = 0; blk < WIN; blk++) {
        cosine(x, (long)(WARM + blk) * BLK, BLK, RATE, f, 0.5);
        ap->process(st, x, BLK);
        memcpy(y + 2 + 2 * blk * BLK, x, sizeof x);
    }
    free(st);
    return fx_maxstep(y, 1 + WIN * BLK);
}

static void click_case_at(const pc_fxapp_t *ap, const float *base, int p, float va, float vb, const char *what, double f)
{
    float a[PC_FXP_MAX], b[PC_FXP_MAX];
    memcpy(a, base, sizeof a);
    memcpy(b, base, sizeof b);
    a[p] = va;
    b[p] = vb;
    float sw = click_run(ap, a, b, p, f), so = click_run(ap, a, NULL, p, f), sn = click_run(ap, b, NULL, p, f);
    if (!fx_click_ok(sw, so, sn, FX_CLICK_SLACK_APP)) {
        fprintf(stderr, "%s.%s %s %g -> %g at %g Hz: step %g, steady %g / %g\n", ap->id, ap->param[p].id, what, va, vb, f, sw, so, sn);
        exit(1);
    }
}
static void click_case(const pc_fxapp_t *ap, const float *base, int p, float va, float vb, const char *what)
{
    click_case_at(ap, base, p, va, vb, what, 1000);
}

static void test_click(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    for (int i = 0; i < n; i++) {
        const pc_fxapp_t *ap = a[i];
        float base[PC_FXP_MAX];
        defaults(ap, base);
        if (!strcmp(ap->id, "equalizer")) {   /* all ten bands on, so band rows change the sound */
            base[pidx(ap, "count")] = 10;
            for (int b = 0; b < 10; b++) {
                char k[32];
                snprintf(k, sizeof k, "b%d_gain_db", b); base[pidx(ap, k)] = b & 1 ? -6 : 6;
                snprintf(k, sizeof k, "b%d_fc", b); base[pidx(ap, k)] = fminf(40.0f * powf(2, (float)b), ap->param[pidx(ap, k)].hi);
            }
        }
        int cases = 0;
        for (int p = 0; p < ap->nparam; p++) {
            const pc_fxparam_t *q = &ap->param[p];
            if (q->flags & PC_FXP_REBUILD) continue;
            float v0 = base[p], v1 = pc_fxparam_step(q, v0, 1, false);
            if (v1 == v0) v1 = pc_fxparam_step(q, v0, -1, false);
            if (q->unit == PC_FXU_BOOL) v1 = v0 > 0.5f ? 0 : 1;
            click_case(ap, base, p, v0, v1, "one detent");
            click_case(ap, base, p, q->lo, q->hi, "full range");
            click_case(ap, base, p, q->hi, q->lo, "full range down");
            cases += 3;
        }
        printf("  click %-10s %d cases\n", ap->id, cases);
    }
}

/* (e) blocks of 1, 7 and 480 frames give the same output at constant params */
static void run_blocks(const pc_fxapp_t *ap, const float *v, int bs, float *out, int frames)
{
    void *st = make(ap, RATE, v);
    s_noise = 11;
    signal(out, 0, frames, RATE);
    for (int off = 0; off < frames; off += bs) ap->process(st, out + 2 * off, frames - off < bs ? frames - off : bs);
    free(st);
}

static void test_block_independence(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    const int frames = 48 * BLK;
    float *r1 = malloc(2 * frames * sizeof(float)), *r7 = malloc(2 * frames * sizeof(float)), *r480 = malloc(2 * frames * sizeof(float));
    for (int i = 0; i < n; i++)
        for (int s = -1; s < NSETS; s++) {
            if (s >= 0 && strcmp(SETS[s].app, a[i]->id)) continue;
            float v[PC_FXP_MAX];
            if (s < 0) defaults(a[i], v); else apply_set(a[i], &SETS[s], v);
            run_blocks(a[i], v, 1, r1, frames);
            run_blocks(a[i], v, 7, r7, frames);
            run_blocks(a[i], v, BLK, r480, frames);
            for (int k = 0; k < 2 * frames; k++)
                if (fabsf(r1[k] - r480[k]) > 1e-6f || fabsf(r7[k] - r480[k]) > 1e-6f) {
                    fprintf(stderr, "%s set %d: sample %d %g / %g / %g\n", a[i]->id, s, k, r1[k], r7[k], r480[k]);
                    exit(1);
                }
        }
    free(r1); free(r7); free(r480);
}

/* (f) set() never clears history: a set with the same values and changed == 0 leaves the output identical */
static void test_set_keeps_history(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    static float x[2 * BLK], y[2 * BLK];
    for (int i = 0; i < n; i++)
        for (int s = -1; s < NSETS; s++) {
            if (s >= 0 && strcmp(SETS[s].app, a[i]->id)) continue;
            float v[PC_FXP_MAX];
            if (s < 0) defaults(a[i], v); else apply_set(a[i], &SETS[s], v);
            void *p = make(a[i], RATE, v), *q = make(a[i], RATE, v);
            s_noise = 5;
            for (int blk = 0; blk < 30; blk++) {
                signal(x, (long)blk * BLK, BLK, RATE);
                memcpy(y, x, sizeof x);
                if (blk == 10) a[i]->set(q, v, 0);
                if (blk == 20) a[i]->set(q, v, 0xFFFFFFFFu);
                a[i]->process(p, x, BLK);
                a[i]->process(q, y, BLK);
                for (int k = 0; k < 2 * BLK; k++)
                    if (x[k] != y[k]) { fprintf(stderr, "%s set %d: block %d differs after set()\n", a[i]->id, s, blk); exit(1); }
            }
            free(p);
            free(q);
        }
}

/* rate() keeps the block, recomputes and clears: equal to a fresh init at the new rate; reset() equal to a fresh init */
static void test_rate_reset(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    static float x[2 * BLK], y[2 * BLK];
    for (int i = 0; i < n; i++)
        for (int mode = 0; mode < 2; mode++) {
            float v[PC_FXP_MAX];
            defaults(a[i], v);
            uint32_t to = mode ? 44100 : RATE;
            void *p = make(a[i], RATE, v);
            s_noise = 9;
            for (int blk = 0; blk < 20; blk++) { signal(x, (long)blk * BLK, BLK, RATE); a[i]->process(p, x, BLK); }
            CHECK(a[i]->fits(p, to, v));
            if (mode) a[i]->rate(p, to); else a[i]->reset(p);
            void *q = make(a[i], to, v);
            for (int blk = 0; blk < 20; blk++) {
                signal(x, (long)blk * BLK, BLK, to);
                memcpy(y, x, sizeof x);
                a[i]->process(p, x, BLK);
                a[i]->process(q, y, BLK);
                for (int k = 0; k < 2 * BLK; k++)
                    if (fabsf(x[k] - y[k]) > 1e-6f) { fprintf(stderr, "%s %s: block %d differs from a fresh init\n", a[i]->id, mode ? "rate" : "reset", blk); exit(1); }
            }
            free(p);
            free(q);
        }
}

static void test_fits_latency(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    for (int i = 0; i < n; i++) {
        float v[PC_FXP_MAX];
        defaults(a[i], v);
        void *st = make(a[i], RATE, v);
        bool lim = !strcmp(a[i]->id, "limiter"), verb = !strcmp(a[i]->id, "freeverb"), vnd = !strcmp(a[i]->id, "velvet_widener") || !strcmp(a[i]->id, "velvet_room");   /* the lines are sized by the rate */
        CHECK(a[i]->fits(st, RATE, v));
        CHECK(a[i]->fits(st, 44100, v));
        CHECK(a[i]->fits(st, 96000, v) == !(lim || verb || vnd));
        int lat = a[i]->latency ? a[i]->latency(st) : 0;
        CHECK_EQ_INT(lat, lim ? (int)(5 * 0.001f * RATE) + 1 : 0);
        if (lim) {
            float w[PC_FXP_MAX];
            memcpy(w, v, sizeof w);
            w[pidx(a[i], "lookahead_ms")] = 20;
            CHECK(!a[i]->fits(st, RATE, w));
            CHECK(a[i]->state_size(RATE, w) > a[i]->state_size(RATE, v));
        }
        free(st);
    }
}

/* (g) level hold at the band's own frequency (review I2): a steady sine where the changed filter acts; per-block RMS
 * around a change stays between the steady levels before and after (one detent), or inside the steady levels along the
 * way and monotone where the response is (full range), and arrives. Frequencies are multiples of 50 Hz, so a 480-frame
 * block holds whole half-periods and the block RMS of a steady sine is constant. Every case also runs the click test
 * at that frequency. */
#define LWARM 150     /* 1.5 s: a 50 Hz Q10 band at +24 dB rings with a 0.25 s time constant */
#define LSTEADY 10
#define LRUN 200      /* the change's own blocks, then the filter settles (that time belongs to the filter, not to the change) */
#define LTOL 0.2
#define LARRIVE 20    /* the last blocks of the run: within LTOL of the new steady level (parameter arrival: test_slew_bounds) */
/* dip_d / dip_f: extra dB allowed BELOW the range, one detent / full range. Non-zero only where a resonant band's
 * frequency (or its type) moves off the steady tone at that band: the band rings at its new frequency and beats with
 * the tone for a few blocks. Slower frequency steps make it smaller (1.03 per block: about 2 dB on Q10) but cannot
 * reach 20 Hz..20 kHz in 50 blocks; an instant switch is as deep or deeper. Values = measured worst + about 0.35 dB,
 * a regression guard. Accepted, ruling X12: the dip stays, the click rule still applies there, every other case
 * keeps 0.2 dB (task-1-report.md, fix rounds 1-2). */
typedef struct { const char *app, *param; double f; bool lonly, mono; kv_t base[8]; double dip_d, dip_f; } lcase_t;
#define EQ_Q4 { { "count", 2 }, { "b0_fc", 50 }, { "b0_gain_db", 12 }, { "b0_q", 4 }, { "b1_fc", 1000 }, { "b1_gain_db", -6 } }
#define EQ_Q10 { { "count", 1 }, { "b0_fc", 50 }, { "b0_gain_db", 12 }, { "b0_q", 10 } }
static const lcase_t LCASES[] = {
    { "equalizer", "b0_gain_db", 50, false, true, EQ_Q4, 0, 0 },
    { "equalizer", "b0_q", 50, false, true, EQ_Q4, 0, 0 },
    { "equalizer", "b0_fc", 50, false, false, EQ_Q4, 6.5, 8.7 },
    { "equalizer", "b0_type", 50, false, false, EQ_Q4, 0, 4.0 },
    { "equalizer", "b1_gain_db", 1000, false, true, EQ_Q4, 0, 0 },
    { "equalizer", "b1_q", 1000, false, true, EQ_Q4, 0, 0 },
    { "equalizer", "b1_fc", 1000, false, false, EQ_Q4, 0, 0 },
    { "equalizer", "count", 1000, false, true, EQ_Q4, 0, 0 },
    { "equalizer", "b0_gain_db", 50, false, true, EQ_Q10, 0, 0 },
    { "equalizer", "b0_q", 50, false, true, EQ_Q10, 0, 0 },
    { "equalizer", "b0_fc", 50, false, false, EQ_Q10, 10.9, 5.5 },
    { "tone", "warmth", 250, false, true, { { 0 } }, 0, 0 },
    { "tone", "warmth", 4000, false, true, { { 0 } }, 0, 0 },
    { "tone", "depth", 50, false, true, { { 0 } }, 0, 0 },
    { "tone", "punch", 100, false, true, { { 0 } }, 0, 0 },
    { "tone", "body", 200, false, true, { { 0 } }, 0, 0 },
    { "tone", "voice", 800, false, true, { { 0 } }, 0, 0 },
    { "tone", "presence", 3000, false, true, { { 0 } }, 0, 0 },
    { "tone", "bite", 5500, false, true, { { 0 } }, 0, 0 },
    { "tone", "sibilance", 7500, false, true, { { 0 } }, 0, 0 },
    { "tone", "air", 11000, false, true, { { 0 } }, 0, 0 },
    { "tone", "depth", 50, false, true, { { "depth", 1 } }, 0, 0 },
    { "tone", "punch", 100, false, true, { { "punch", -1 } }, 0, 0 },
    { "loudness", "quieter_db", 100, false, true, { { 0 } }, 0, 0 },
    { "loudness", "quieter_db", 10000, false, true, { { 0 } }, 0, 0 },
    { "bass", "reach_hz", 100, false, false, { { 0 } }, 0, 0 },
    { "bass", "cut", 100, false, false, { { 0 } }, 0, 0 },
    { "crossfeed", "cutoff_hz", 700, true, true, { { 0 } }, 0, 0 },
};

static double block_db(const float *x, int frames)
{
    double e = 0;
    for (int i = 0; i < 2 * frames; i++) e += (double)x[i] * x[i];
    return 10 * log10(e / (2 * frames) + 1e-30);
}

/* per-block RMS (dB) of `run` blocks after LWARM; vb: set() right after the warm-up */
static void level_run(const pc_fxapp_t *ap, const float *va, const float *vb, double f, bool lonly, double *out, int run)
{
    void *st = make(ap, RATE, va);
    static float x[2 * BLK];
    for (int blk = 0; blk < LWARM + run; blk++) {
        if (blk == LWARM && vb) ap->set(st, vb, 0xFFFFFFFFu);
        cosine(x, (long)blk * BLK, BLK, RATE, f, 0.1);
        if (lonly) for (int i = 0; i < BLK; i++) x[2 * i + 1] = 0;
        ap->process(st, x, BLK);
        if (blk >= LWARM) out[blk - LWARM] = block_db(x, BLK);
    }
    free(st);
}

static double level_steady(const pc_fxapp_t *ap, const float *v, double f, bool lonly)
{
    double out[LSTEADY], m = 0;
    level_run(ap, v, NULL, f, lonly, out, LSTEADY);
    for (int k = 0; k < LSTEADY; k++) m += out[k];
    return m / LSTEADY;
}

static int s_level_cases;
static void level_case(const pc_fxapp_t *ap, const lcase_t *c, const float *base, int p, float va, float vb, bool full)
{
    const pc_fxparam_t *q = &ap->param[p];
    float a[PC_FXP_MAX], b[PC_FXP_MAX];
    memcpy(a, base, sizeof a);
    memcpy(b, base, sizeof b);
    a[p] = va;
    b[p] = vb;
    double lb = level_steady(ap, a, c->f, c->lonly), la = level_steady(ap, b, c->f, c->lonly);
    double lo = fmin(lb, la), hi = fmax(lb, la);
    if (full && q->unit != PC_FXU_BOOL) {                  /* the steady levels along the way */
        for (int k = 1; k < 24; k++) {
            float w[PC_FXP_MAX];
            memcpy(w, base, sizeof w);
            double t = k / 24.0;
            w[p] = q->unit == PC_FXU_CHOICE ? q->lo + (float)(k % (int)(q->hi - q->lo + 1))
                 : (q->flags & PC_FXP_LOG) ? (float)(va * pow((double)vb / va, t)) : (float)(va + (vb - va) * t);
            double l = level_steady(ap, w, c->f, c->lonly);
            lo = fmin(lo, l); hi = fmax(hi, l);
        }
    }
    if (q->unit == PC_FXU_CHOICE) {                        /* a band changes its type through 0 dB */
        char k[32];
        snprintf(k, sizeof k, "%.*s_gain_db", (int)(strchr(q->id, '_') - q->id), q->id);
        float w[PC_FXP_MAX];
        memcpy(w, a, sizeof w);
        w[pidx(ap, k)] = 0;
        double l = level_steady(ap, w, c->f, c->lonly);
        lo = fmin(lo, l); hi = fmax(hi, l);
    }
    double out[LRUN], ext = out[0] = 0;
    level_run(ap, a, b, c->f, c->lonly, out, LRUN);
    ext = out[0];
    for (int k = 0; k < LRUN; k++) {
        bool bad = out[k] < lo - LTOL - (full ? c->dip_f : c->dip_d) || out[k] > hi + LTOL;
        if (full && c->mono) {                             /* monotone towards the new level */
            if (la >= lb) { bad |= out[k] < ext - LTOL; ext = fmax(ext, out[k]); }
            else { bad |= out[k] > ext + LTOL; ext = fmin(ext, out[k]); }
        }
        if (k >= LRUN - LARRIVE) bad |= fabs(out[k] - la) > LTOL;
        if (bad) {
            fprintf(stderr, "%s.%s %s %g -> %g at %g Hz: block %d %.2f dB, before %.2f after %.2f, range %.2f..%.2f\n", ap->id, q->id,
                    full ? "full range" : "one detent", va, vb, c->f, k, out[k], lb, la, lo, hi);
            exit(1);
        }
    }
    click_case_at(ap, base, p, va, vb, full ? "full range (band)" : "one detent (band)", c->f);
    s_level_cases++;
}

/* the slew steps (pc_fxapp_parts.h): every full-range change of a biquad value arrives within 50 blocks */
static void test_slew_bounds(void)
{
    static const struct { char t0, t1; float fc0, fc1, g0, g1, q0, q1; } J[] = {
        { 'P', 'P', 20, 20000, -24, 24, 0.1f, 10 }, { 'P', 'P', 20000, 20, 24, -24, 10, 0.1f },
        { 'L', 'H', 1000, 1000, 24, 24, 0.71f, 0.71f }, { 'H', 'P', 50, 50, -24, 24, 4, 4 },
    };
    for (size_t j = 0; j < sizeof J / sizeof *J; j++) {
        fx_bqs_t s;
        fx_bqs_design(&s, J[j].t0, J[j].fc0, J[j].g0, J[j].q0, RATE);
        fx_bqs_target(&s, J[j].t1, J[j].fc1, J[j].g1, J[j].q1);
        int blocks = 0;
        while (fx_bqs_moving(&s) && blocks < 100) { fx_bqs_step(&s, RATE); blocks++; }
        CHECK(!fx_bqs_moving(&s));
        if (blocks > 50) { fprintf(stderr, "slew case %zu: %d blocks\n", j, blocks); exit(1); }
        pc_biquad_t want;
        pc_biquad_design(&want, J[j].t1, J[j].fc1, J[j].g1, J[j].q1, RATE);
        CHECK(want.b0 == s.bq.b0 && want.b1 == s.bq.b1 && want.b2 == s.bq.b2 && want.a1 == s.bq.a1 && want.a2 == s.bq.a2);
    }
    for (int b = 0; b < 2; b++) {                     /* one detent below the step bound arrives in one block */
        fx_bqs_t s;
        fx_bqs_design(&s, 'P', 1000, 6, 0.71f, RATE);
        fx_bqs_target(&s, 'P', b ? 1000 * 1.15f : 1000, b ? 6 : 6.5f, 0.71f);
        fx_bqs_step(&s, RATE);
        CHECK(!fx_bqs_moving(&s));
    }
}

static void test_level_hold(void)
{
    for (size_t i = 0; i < sizeof LCASES / sizeof *LCASES; i++) {
        const lcase_t *c = &LCASES[i];
        const pc_fxapp_t *const *a;
        pc_fxapps(&a);
        const pc_fxapp_t *ap = a[pc_fxapp_find(c->app)];
        float base[PC_FXP_MAX];
        defaults(ap, base);
        for (int k = 0; k < 8 && c->base[k].k; k++) base[pidx(ap, c->base[k].k)] = c->base[k].v;
        int p = pidx(ap, c->param);
        const pc_fxparam_t *q = &ap->param[p];
        float v0 = base[p];
        for (int d = -1; d <= 1; d += 2) {
            float v1 = q->unit == PC_FXU_BOOL ? (v0 > 0.5f ? 0 : 1) : pc_fxparam_step(q, v0, d, false);
            if (v1 != v0) level_case(ap, c, base, p, v0, v1, false);
        }
        if (q->unit != PC_FXU_BOOL) {
            level_case(ap, c, base, p, q->lo, q->hi, true);
            level_case(ap, c, base, p, q->hi, q->lo, true);
        }
    }
    /* every row ran: 4 runs a row (2 detents + 2 full range); a BOOL row has 2 (its toggle twice); a row whose value
     * sits at an end has one detent: b0_q = 10 (Q10 row, at hi) and b0_type = 0 (at lo) -> 28 * 4 - 2 (2 BOOL) - 2 = 108 */
    const int want = 28 * 4 - 2 - 2;
    printf("  level hold: %zu rows, %d runs\n", sizeof LCASES / sizeof *LCASES, s_level_cases);
    CHECK_EQ_INT(sizeof LCASES / sizeof *LCASES, 28);
    CHECK_EQ_INT(s_level_cases, want);
}

/* non-finite values (review I3): set() treats them as the parameter's default */
static void test_nonfinite_set(void)
{
    const float BAD[3] = { NAN, INFINITY, -INFINITY };
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    static float x[2 * BLK], y[2 * BLK];
    for (int i = 0; i < n; i++)
        for (int p = 0; p < a[i]->nparam; p++)
            for (int b = 0; b < 3; b++) {
                float v[PC_FXP_MAX], w[PC_FXP_MAX];
                defaults(a[i], v);
                memcpy(w, v, sizeof w);
                w[p] = BAD[b];
                void *s1 = make(a[i], RATE, v), *s2 = make(a[i], RATE, v), *s3 = make(a[i], RATE, w);
                for (int blk = 0; blk < 6; blk++) {
                    s_noise = (uint32_t)blk + 1;
                    signal(x, (long)blk * BLK, BLK, RATE);
                    memcpy(y, x, sizeof x);
                    if (blk == 2) { a[i]->set(s1, v, 0xFFFFFFFFu); a[i]->set(s2, w, 0xFFFFFFFFu); }
                    a[i]->process(s1, x, BLK);
                    a[i]->process(s2, y, BLK);
                    for (int k = 0; k < 2 * BLK; k++)
                        if (x[k] != y[k] || !isfinite(y[k])) { fprintf(stderr, "%s.%s = %g in set(): differs from the default\n", a[i]->id, a[i]->param[p].id, BAD[b]); exit(1); }
                    signal(y, (long)blk * BLK, BLK, RATE);
                    a[i]->process(s3, y, BLK);
                    for (int k = 0; k < 2 * BLK; k++)
                        if (!isfinite(y[k])) { fprintf(stderr, "%s.%s = %g in init(): not finite\n", a[i]->id, a[i]->param[p].id, BAD[b]); exit(1); }
                }
                free(s1); free(s2); free(s3);
            }
}

/* a tone word set to 0 and equalizer bands above a lowered count slew to 0 dB and then stop costing work (review M4) */
static void test_drain_stops(void)
{
    static float x[2 * BLK];
    const pc_fxapp_t *const *a;
    pc_fxapps(&a);
    for (int which = 0; which < 2; which++) {
        const pc_fxapp_t *ap = a[pc_fxapp_find(which ? "equalizer" : "tone")];
        int (*running)(const void *) = which ? fx_eq_running : fx_tone_running;
        float v[PC_FXP_MAX];
        defaults(ap, v);
        if (which) { v[pidx(ap, "count")] = 3; v[pidx(ap, "b1_gain_db")] = 12; v[pidx(ap, "b2_gain_db")] = -24; }
        else { v[pidx(ap, "warmth")] = 1.5f; v[pidx(ap, "air")] = -1; }
        void *st = make(ap, RATE, v);
        CHECK_EQ_INT(running(st), 3);
        s_noise = 4;
        for (int blk = 0; blk < 10; blk++) { signal(x, (long)blk * BLK, BLK, RATE); ap->process(st, x, BLK); }
        if (which) v[pidx(ap, "count")] = 1; else { v[pidx(ap, "warmth")] = 0; v[pidx(ap, "air")] = 0; }
        ap->set(st, v, 0xFFFFFFFFu);
        int blk = 0;
        for (; blk < 60 && running(st) > (which ? 1 : 0); blk++) { signal(x, (long)(10 + blk) * BLK, BLK, RATE); ap->process(st, x, BLK); }
        if (running(st) != (which ? 1 : 0)) { fprintf(stderr, "%s: %d filters still running after %d blocks\n", ap->id, running(st), blk); exit(1); }
        printf("  %s: drained in %d blocks\n", ap->id, blk);
        free(st);
    }
}

/* a leaving band under a loud steady tone at its own frequency: float rounding keeps its 0 dB history above "quiet"
 * for good; it still stops (FX_DRAIN_BLOCKS at 0 dB, then one fade block), the step at the stop is below -90 dB of
 * full scale, and the click rule holds across the whole leave (review B-m1) */
static void test_drain_loud_tone(void)
{
    static const struct { const char *app; float fc, q, g; double f; } C[] = {
        { "equalizer", 50, 10, 24, 50 }, { "equalizer", 50, 10, -24, 50 }, { "equalizer", 20, 10, 24, 20 },
        { "equalizer", 20, 10, -24, 20 }, { "equalizer", 200, 4, -24, 200 }, { "tone", 0, 0, -1.5f, 50 },
    };
    const uint32_t rates[2] = { RATE, 44100 };
    const int bound = 50 + FX_DRAIN_BLOCKS + 2, n = 2 + bound;
    float *y = malloc((size_t)2 * BLK * n * sizeof(float)), *xin = malloc((size_t)2 * BLK * n * sizeof(float));
    const pc_fxapp_t *const *a;
    pc_fxapps(&a);
    double worst = 0;
    for (size_t c = 0; c < sizeof C / sizeof *C; c++)
        for (int r = 0; r < 2; r++) {
            const pc_fxapp_t *ap = a[pc_fxapp_find(C[c].app)];
            bool eq = !strcmp(ap->id, "equalizer");
            int (*running)(const void *) = eq ? fx_eq_running : fx_tone_running;
            float v[PC_FXP_MAX];
            defaults(ap, v);
            if (eq) { v[pidx(ap, "count")] = 1; v[pidx(ap, "b0_fc")] = C[c].fc; v[pidx(ap, "b0_q")] = C[c].q; v[pidx(ap, "b0_gain_db")] = C[c].g; }
            else v[pidx(ap, "depth")] = C[c].g;
            void *st = make(ap, rates[r], v);
            static float x[2 * BLK];
            for (int blk = 0; blk < 150; blk++) { cosine(x, (long)blk * BLK, BLK, rates[r], C[c].f, 0.9); ap->process(st, x, BLK); }
            float so = fx_maxstep(x, BLK);
            if (eq) v[pidx(ap, "count")] = 0; else v[pidx(ap, "depth")] = 0;
            ap->set(st, v, 0xFFFFFFFFu);
            int blk = 0;
            for (; blk < n && running(st) > 0; blk++) {
                float *yb = y + (size_t)2 * BLK * blk, *xb = xin + (size_t)2 * BLK * blk;
                cosine(xb, (long)(150 + blk) * BLK, BLK, rates[r], C[c].f, 0.9);
                memcpy(yb, xb, (size_t)2 * BLK * sizeof(float));
                ap->process(st, yb, BLK);
            }
            if (running(st) != 0 || blk > bound) { fprintf(stderr, "%s %g Hz Q%g %+g dB at %u: still running after %d blocks\n", ap->id, C[c].fc, C[c].q, C[c].g, rates[r], blk); exit(1); }
            /* the step at the stop: from here on the output is the input */
            size_t last = (size_t)2 * BLK * blk - 2;
            double step = fmax(fabs(y[last] - xin[last]), fabs(y[last + 1] - xin[last + 1]));
            worst = fmax(worst, step);
            if (step > 3.2e-5) { fprintf(stderr, "%s %g Hz Q%g %+g dB at %u: step %g at the stop\n", ap->id, C[c].fc, C[c].q, C[c].g, rates[r], step); exit(1); }
            cosine(y + (size_t)2 * BLK * blk, (long)(150 + blk) * BLK, BLK, rates[r], C[c].f, 0.9);   /* the block after: dry */
            float sw = fx_maxstep(y, BLK * (blk + 1)), sn = fx_maxstep(xin, BLK);
            if (!fx_click_ok(sw, so, sn, FX_CLICK_SLACK_APP)) { fprintf(stderr, "%s %g Hz: click %g against %g / %g\n", ap->id, C[c].fc, sw, so, sn); exit(1); }
            free(st);
        }
    free(y);
    free(xin);
    printf("  loud-tone drain: worst step at the stop %.2e (%.1f dBFS), bound %d blocks\n", worst, 20 * log10(worst + 1e-30), bound);
}

/* finite but out-of-range values (review B-m2): set() clamps them to [lo, hi], output finite and bit-identical to
 * set() with the clamped value; init() likewise */
static void test_out_of_range_set(void)
{
    const pc_fxapp_t *const *a;
    int n = napps(&a);
    static float x[2 * BLK], y[2 * BLK];
    for (int i = 0; i < n; i++)
        for (int p = 0; p < a[i]->nparam; p++) {
            const pc_fxparam_t *q = &a[i]->param[p];
            const float span = q->hi - q->lo, BAD[5] = { q->lo - 1000 * span, q->hi + 1000 * span, 0, 1e30f, -1e30f };
            for (int b = 0; b < 5; b++) {
                if (q->flags & PC_FXP_REBUILD) continue;
                float v[PC_FXP_MAX], w[PC_FXP_MAX], c[PC_FXP_MAX];
                defaults(a[i], v);
                memcpy(w, v, sizeof w);
                memcpy(c, v, sizeof c);
                w[p] = BAD[b];
                c[p] = BAD[b] < q->lo ? q->lo : BAD[b] > q->hi ? q->hi : BAD[b];
                void *s1 = make(a[i], RATE, v), *s2 = make(a[i], RATE, v), *s3 = make(a[i], RATE, w), *s4 = make(a[i], RATE, c);
                for (int blk = 0; blk < 8; blk++) {
                    s_noise = (uint32_t)blk + 1;
                    signal(x, (long)blk * BLK, BLK, RATE);
                    memcpy(y, x, sizeof x);
                    if (blk == 2) { a[i]->set(s1, c, 0xFFFFFFFFu); a[i]->set(s2, w, 0xFFFFFFFFu); }
                    a[i]->process(s1, x, BLK);
                    a[i]->process(s2, y, BLK);
                    for (int k = 0; k < 2 * BLK; k++)
                        if (x[k] != y[k] || !isfinite(y[k])) { fprintf(stderr, "%s.%s = %g in set(): differs from %g\n", a[i]->id, q->id, BAD[b], c[p]); exit(1); }
                    signal(x, (long)blk * BLK, BLK, RATE);
                    memcpy(y, x, sizeof x);
                    a[i]->process(s3, x, BLK);
                    a[i]->process(s4, y, BLK);
                    for (int k = 0; k < 2 * BLK; k++)
                        if (x[k] != y[k] || !isfinite(x[k])) { fprintf(stderr, "%s.%s = %g in init(): differs from %g\n", a[i]->id, q->id, BAD[b], c[p]); exit(1); }
                }
                free(s1); free(s2); free(s3); free(s4);
            }
        }
}

/* the glide ends exactly on the new coefficients: the last frame of the glide block equals the plain new filter
 * from the same history, so the next steady block is bit-exact with the plain run (review B-m3) */
static void test_glide_lands(void)
{
    static float x[2 * BLK], y[2];
    const int frames[3] = { BLK, 7, 1 };
    for (int f = 0; f < 3; f++) {
        fx_bqs_t s;
        fx_bqs_design(&s, 'P', 50, 12, 10, RATE);
        s_noise = 2;
        signal(x, 0, BLK, RATE);
        fx_bqs_run(&s, x, BLK, 0, BLK);
        fx_bqs_target(&s, 'P', 55, 11, 9.5f);   /* within one step: there after this block */
        fx_bqs_step(&s, RATE);
        CHECK(s.glide);
        int fr = frames[f];
        signal(x, BLK, fr, RATE);
        if (fr > 1) fx_bqs_run(&s, x, fr - 1, 0, fr);
        pc_biquad_t plain = s.bq;                 /* the new coefficients and the history before the last frame */
        y[0] = x[2 * (fr - 1)]; y[1] = x[2 * (fr - 1) + 1];
        fx_bqs_run(&s, x + 2 * (fr - 1), 1, fr - 1, fr);
        pc_biquad_run(&plain, y, 1);
        CHECK(x[2 * (fr - 1)] == y[0] && x[2 * (fr - 1) + 1] == y[1]);
        fx_bqs_end(&s);
        CHECK(!s.glide && !fx_bqs_moving(&s));
        CHECK(memcmp(&plain, &s.bq, sizeof plain) == 0);
        signal(x, BLK + fr, BLK, RATE);           /* the next block: plain and bit-exact */
        float z[2 * BLK];
        memcpy(z, x, sizeof z);
        fx_bqs_run(&s, x, BLK, 0, BLK);
        pc_biquad_run(&plain, z, BLK);
        CHECK(memcmp(x, z, sizeof z) == 0);
    }
}

/* a band re-joined around the forced stop (review N1): wherever it re-joins - a few blocks before the stop, with the
 * fade block next, exactly at it, after the clear - it ends at the level of a fresh instance set to the same values,
 * and the click rule holds across it */
static double rejoin_level(const pc_fxapp_t *ap, const float *v, double f, int nblk, int from, float *ybuf)
{
    void *st = make(ap, RATE, v);
    static float x[2 * BLK];
    double l = 0;
    for (int blk = 0; blk < nblk; blk++) {
        cosine(x, (long)(from + blk) * BLK, BLK, RATE, f, 0.9);
        ap->process(st, x, BLK);
        if (ybuf) memcpy(ybuf + (size_t)2 * BLK * blk, x, sizeof x);
        if (blk == nblk - 1) l = block_db(x, BLK);
    }
    free(st);
    return l;
}

static void test_rejoin_forced_stop(void)
{
    const pc_fxapp_t *const *a;
    pc_fxapps(&a);
    static float x[2 * BLK];
    const int after = 300;                        /* blocks after the re-join: the slew and the settling */
    float *y = malloc((size_t)2 * BLK * (after + 1) * sizeof(float)), *ref = malloc((size_t)2 * BLK * after * sizeof(float));
    for (int which = 0; which < 2; which++) {
        const pc_fxapp_t *ap = a[pc_fxapp_find(which ? "tone" : "equalizer")];
        int (*running)(const void *) = which ? fx_tone_running : fx_eq_running;
        float on[PC_FXP_MAX], off[PC_FXP_MAX], back[PC_FXP_MAX];
        defaults(ap, on);
        if (which) on[pidx(ap, "depth")] = -1.5f;
        else { on[pidx(ap, "count")] = 1; on[pidx(ap, "b0_fc")] = 50; on[pidx(ap, "b0_q")] = 10; on[pidx(ap, "b0_gain_db")] = 12; }
        memcpy(off, on, sizeof off);
        memcpy(back, on, sizeof back);
        if (which) { off[pidx(ap, "depth")] = 0; back[pidx(ap, "depth")] = 0.25f; }
        else { off[pidx(ap, "count")] = 0; back[pidx(ap, "b0_gain_db")] = 3; }
        /* the block where it stops when left alone */
        void *st = make(ap, RATE, on);
        int blk = 0;
        for (; blk < 100; blk++) { cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9); ap->process(st, x, BLK); }
        ap->set(st, off, 0xFFFFFFFFu);
        int stop = -1;
        for (int k = 0; k < 400 && stop < 0; k++, blk++) {
            cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9);
            ap->process(st, x, BLK);
            if (running(st) == 0) stop = k + 1;      /* blocks processed after the leave */
        }
        free(st);
        CHECK(stop > FX_DRAIN_BLOCKS);
        for (int d = -3; d <= 1; d++) {
            int join = stop + d;                     /* re-join after this many blocks (stop - 1: the fade block is next) */
            st = make(ap, RATE, on);
            blk = 0;
            for (; blk < 100; blk++) { cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9); ap->process(st, x, BLK); }
            ap->set(st, off, 0xFFFFFFFFu);
            for (int k = 0; k < join; k++, blk++) { cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9); ap->process(st, x, BLK); }
            memcpy(y, x, sizeof x);                   /* the last block before the re-join, for the click window */
            ap->set(st, back, 0xFFFFFFFFu);
            double l = 0;
            for (int k = 0; k < after; k++, blk++) {
                cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9);
                ap->process(st, x, BLK);
                memcpy(y + (size_t)2 * BLK * (k + 1), x, sizeof x);
                l = block_db(x, BLK);
            }
            free(st);
            double lref = rejoin_level(ap, back, 50, after, blk - after, ref);
            if (fabs(l - lref) > LTOL) { fprintf(stderr, "%s re-joined at stop%+d: %.2f dB, a fresh instance %.2f dB\n", ap->id, d, l, lref); exit(1); }
            float sw = fx_maxstep(y, BLK * (after + 1)), so = fx_maxstep(y, BLK), sn = fx_maxstep(ref + (size_t)2 * BLK * (after - 1), BLK);
            if (!fx_click_ok(sw, so, sn, FX_CLICK_SLACK_APP)) { fprintf(stderr, "%s re-joined at stop%+d: click %g against %g / %g\n", ap->id, d, sw, so, sn); exit(1); }
        }
        printf("  %s: re-join at stop-3..stop+1 (stop = %d blocks) ok\n", ap->id, stop);
    }
    free(y);
    free(ref);
}

/* a type change passes 0 dB on a band that stays enabled: it keeps running and its history (no clear, no fade) */
static void test_type_change_keeps_band(void)
{
    const pc_fxapp_t *const *a;
    pc_fxapps(&a);
    const pc_fxapp_t *ap = a[pc_fxapp_find("equalizer")];
    float v[PC_FXP_MAX], w[PC_FXP_MAX];
    defaults(ap, v);
    v[pidx(ap, "count")] = 1; v[pidx(ap, "b0_fc")] = 50; v[pidx(ap, "b0_q")] = 10; v[pidx(ap, "b0_gain_db")] = 1;
    memcpy(w, v, sizeof w);
    w[pidx(ap, "b0_type")] = 1;
    static float x[2 * BLK];
    void *st = make(ap, RATE, v);
    int blk = 0;
    for (; blk < 300; blk++) { cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9); ap->process(st, x, BLK); }   /* > FX_DRAIN_BLOCKS */
    ap->set(st, w, 0xFFFFFFFFu);
    double l = 0;
    for (int k = 0; k < 300; k++, blk++) {
        cosine(x, (long)blk * BLK, BLK, RATE, 50, 0.9);
        ap->process(st, x, BLK);
        CHECK_EQ_INT(fx_eq_running(st), 1);
        l = block_db(x, BLK);
    }
    free(st);
    double lref = rejoin_level(ap, w, 50, 300, blk - 300, NULL);
    if (fabs(l - lref) > LTOL) { fprintf(stderr, "type change: %.2f dB, a fresh instance %.2f dB\n", l, lref); exit(1); }
}

/* ---- moved from test_dsp.c (T3): the biquad's gain, the limiter's ceiling, the reverb's tail ---- */
/* gain in dB of a sine at f through bq, measured on the last second of 2 s */
static double measure(pc_biquad_t *bq, double f, uint32_t rate)
{
    int n = (int)rate * 2;
    float *b = malloc((size_t)n * 2 * sizeof(float));
    for (int i = 0; i < n; i++) b[2 * i] = b[2 * i + 1] = (float)(0.25 * sin(2 * PI * f * i / rate));
    pc_biquad_run(bq, b, n);
    double e_in = 0, e_out = 0;
    for (int i = n / 2; i < n; i++) {
        double x = 0.25 * sin(2 * PI * f * i / rate);
        e_in += x * x;
        e_out += (double)b[2 * i] * b[2 * i];
    }
    free(b);
    return 10 * log10(e_out / e_in);
}

static void test_eq_peak(void)
{
    pc_biquad_t bq;
    pc_biquad_design(&bq, 'P', 1000, 6, 1, 48000);
    CHECK_NEAR(measure(&bq, 1000, 48000), 6, 0.5);
    CHECK_NEAR(pc_biquad_gain_db(&bq, 1000, 48000), 6, 0.05);
    pc_biquad_design(&bq, 'P', 1000, 6, 1, 48000);
    CHECK_NEAR(measure(&bq, 10000, 48000), 0, 0.5);
}

static void test_eq_low_band(void)
{
    uint32_t rates[] = { 48000, 44100 };
    for (int r = 0; r < 2; r++) {
        pc_biquad_t bq;
        pc_biquad_design(&bq, 'P', 30, 6, 1, rates[r]);
        CHECK_NEAR(measure(&bq, 30, rates[r]), 6, 0.5);
        pc_biquad_design(&bq, 'P', 30, 6, 1, rates[r]);
        CHECK_NEAR(measure(&bq, 1000, rates[r]), 0, 0.5);
    }
}

static void test_eq_shelves(void)
{
    pc_biquad_t bq;
    pc_biquad_design(&bq, 'L', 100, 4, 0.707f, 48000);
    CHECK_NEAR(measure(&bq, 30, 48000), 4, 0.5);
    pc_biquad_design(&bq, 'L', 100, 4, 0.707f, 48000);
    CHECK_NEAR(measure(&bq, 5000, 48000), 0, 0.5);
    pc_biquad_design(&bq, 'H', 8000, -3, 0.707f, 48000);
    CHECK_NEAR(measure(&bq, 16000, 48000), -3, 0.5);
}

static void test_limiter_ceiling(void)
{
    const pc_fxapp_t *ap = &pc_fxapp_limiter;
    float v[PC_FXP_MAX];
    defaults(ap, v);
    v[pidx(ap, "ceiling_db")] = -1.0f;
    void *st = make(ap, RATE, v);
    float b[2 * 480];
    float ceil = powf(10, -1.0f / 20), peak = 0;
    for (int blk = 0; blk < 200; blk++) {
        for (int i = 0; i < 480; i++) b[2 * i] = b[2 * i + 1] = ((blk * 480 + i) / 50) % 2 ? 3.98f : -3.98f;   /* +12 dB square */
        ap->process(st, b, 480);
        for (int i = 0; i < 960; i++) if (fabsf(b[i]) > peak) peak = fabsf(b[i]);
    }
    CHECK(peak <= ceil + 1e-4f);
    CHECK(peak > ceil * 0.8f);
    free(st);
}

static void test_freeverb_tail(void)
{
    const pc_fxapp_t *ap = &pc_fxapp_freeverb;
    float v[PC_FXP_MAX];
    defaults(ap, v);
    v[pidx(ap, "level")] = 0.5f;
    void *st = make(ap, 44100, v);
    float b[2 * 441];
    double tail = 0;
    for (int blk = 0; blk < 1000; blk++) {                    /* 10 s */
        memset(b, 0, sizeof b);
        if (blk == 0) b[0] = b[1] = 1.0f;
        ap->process(st, b, 441);
        for (int i = 0; i < 882; i++) CHECK(isfinite(b[i]));
        if (blk >= 50 && blk < 60) for (int i = 0; i < 882; i++) tail += fabs(b[i]);   /* 0.5 .. 0.6 s */
    }
    CHECK(tail > 1e-3);
    free(st);
}

int main(void)
{
    RUN(test_eq_peak);
    RUN(test_eq_low_band);
    RUN(test_eq_shelves);
    RUN(test_limiter_ceiling);
    RUN(test_freeverb_tail);
    RUN(test_random_finite);
    RUN(test_silence);
    RUN(test_click);
    RUN(test_block_independence);
    RUN(test_set_keeps_history);
    RUN(test_rate_reset);
    RUN(test_fits_latency);
    RUN(test_level_hold);
    RUN(test_slew_bounds);
    RUN(test_nonfinite_set);
    RUN(test_drain_stops);
    RUN(test_drain_loud_tone);
    RUN(test_out_of_range_set);
    RUN(test_glide_lands);
    RUN(test_rejoin_forced_stop);
    RUN(test_type_change_keeps_band);
    return 0;
}
