// SPDX-License-Identifier: LGPL-2.1-or-later
/* fx apps: the registry rules (spec §2), parameter stepping and text, the mode rule (§5). */
#include "pc/pc_fxapp.h"
#include "pc/pc_fxchain.h"
#include "pc/pc_ledfx.h"   /* compiles together with the LED effects' pc_fx_ names */
#include "fx_test.h"
#include "pc_test.h"

static const pc_fxapp_t *app(const char *id)
{
    const pc_fxapp_t *const *a;
    pc_fxapps(&a);
    int i = pc_fxapp_find(id);
    CHECK(i >= 0);
    return a[i];
}

static const pc_fxparam_t *param(const pc_fxapp_t *ap, const char *id)
{
    for (int p = 0; p < ap->nparam; p++)
        if (!strcmp(ap->param[p].id, id)) return &ap->param[p];
    fprintf(stderr, "no param %s.%s\n", ap->id, id);
    exit(1);
}

static float def_of(const char *a, const char *p) { return param(app(a), p)->def; }

static void test_registry_rules(void)
{
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a);
    CHECK(n > 0 && n <= PC_FXAPP_MAX);
    for (int i = 0; i < n; i++) {
        const pc_fxapp_t *x = a[i];
        CHECK(x->id && strlen(x->id) > 0 && strlen(x->id) <= 15);
        for (int j = 0; j < i; j++) CHECK(strcmp(a[j]->id, x->id) != 0);
        CHECK(x->zh && *x->zh && x->en && *x->en);
        CHECK(x->desc_zh && *x->desc_zh && x->desc_en && *x->desc_en);
        CHECK(x->nparam <= PC_FXP_MAX);
        CHECK(x->state_size && x->init && x->fits && x->set && x->rate && x->reset && x->process);
        float v[PC_FXP_MAX];
        for (int p = 0; p < x->nparam; p++) v[p] = x->param[p].def;
        CHECK(x->state_size(48000, v) > 0);
        bool own_mix = false;
        for (int p = 0; p < x->nparam; p++) {
            const pc_fxparam_t *q = &x->param[p];
            CHECK(q->id && *q->id && q->zh && *q->zh && q->en && *q->en);
            for (int r = 0; r < p; r++) CHECK(strcmp(x->param[r].id, q->id) != 0);
            CHECK(q->lo < q->hi);
            CHECK(q->lo <= q->def && q->def <= q->hi);
            CHECK(q->step > 0);
            if (q->flags & PC_FXP_LOG) {
                CHECK(q->lo > 0 && q->step > 1);
                double taps = ceil(log((double)q->hi / q->lo) / log((double)q->step));
                if (taps > 40) { fprintf(stderr, "%s.%s: %g taps\n", x->id, q->id, taps); exit(1); }
            }
            if (q->unit == PC_FXU_CHOICE) {
                CHECK(q->ch_zh && q->ch_en);
                for (int c = 0; c <= (int)(q->hi - q->lo); c++) CHECK(q->ch_zh[c] && *q->ch_zh[c] && q->ch_en[c] && *q->ch_en[c]);
            }
            CHECK(strlen(x->id) + 1 + strlen(q->id) <= 31);
            if (!strcmp(q->id, "mix")) own_mix = true;
        }
        CHECK(!(own_mix && (x->flags & PC_FXA_MIX)));
        CHECK(((x->flags & PC_FXA_ALWAYS_LAST) != 0) == (i == n - 1));
    }
    CHECK(a[n - 1]->flags & PC_FXA_ALWAYS_LAST);
}

static void test_mix_flags(void)
{
    static const char *const MIX[] = { "equalizer", "tone", "bass", "loudness", "crossfeed", "compressor" };
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a);
    for (int i = 0; i < n; i++) {
        bool want = false;
        for (size_t k = 0; k < sizeof MIX / sizeof *MIX; k++) want |= !strcmp(a[i]->id, MIX[k]);
        if (((a[i]->flags & PC_FXA_MIX) != 0) != want) { fprintf(stderr, "%s: PC_FXA_MIX %d\n", a[i]->id, !want); exit(1); }
    }
    CHECK(app("equalizer")->flags & PC_FXA_BANDS);
    const pc_fxparam_t *m = &pc_fxapp_mix_param;
    CHECK_STR(m->id, "mix");
    CHECK(m->zh && *m->zh && m->en && *m->en);
    CHECK_EQ_INT(m->unit, PC_FXU_PCT);
    CHECK_NEAR(m->lo, 0, 0); CHECK_NEAR(m->hi, 1, 0); CHECK_NEAR(m->step, 0.05, 1e-7); CHECK_NEAR(m->def, 1, 0);
}

/* the cost classes, stated as a table: a port or the bench (T11/T12) edits the registry and this list together */
static void test_classes(void)
{
    static const struct { const char *id; pc_fxclass_t cls; } CLS[] = {
        { "gain", PC_FXC_LIGHT }, { "balance", PC_FXC_LIGHT }, { "widener", PC_FXC_LIGHT }, { "crossfeed", PC_FXC_LIGHT }, { "velvet_widener", PC_FXC_HEAVY },
        { "equalizer", PC_FXC_HEAVY }, { "tone", PC_FXC_HEAVY }, { "bass", PC_FXC_HEAVY }, { "loudness", PC_FXC_LIGHT }, { "tube", PC_FXC_HEAVY },
        { "compressor", PC_FXC_HEAVY }, { "freeverb", PC_FXC_HEAVY },
#ifdef CONFIG_FX_VELVET_ROOM
        { "velvet_room", PC_FXC_HEAVY },
#endif
        { "limiter", PC_FXC_LIGHT },
    };
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a);
    CHECK_EQ_INT(n, (int)(sizeof CLS / sizeof *CLS));
    for (size_t k = 0; k < sizeof CLS / sizeof *CLS; k++)
        if (app(CLS[k].id)->cls != CLS[k].cls) { fprintf(stderr, "%s: class %d\n", CLS[k].id, app(CLS[k].id)->cls); exit(1); }
    CHECK(fx_first_app_of(PC_FXC_HEAVY) >= 0);
    CHECK_EQ_INT(fx_first_app_of(PC_FXC_LIGHT), 0);
}

static void test_defaults(void)
{
    CHECK_NEAR(def_of("bass", "reach_hz"), 90, 0); CHECK_NEAR(def_of("bass", "amount"), 0.5, 1e-7); CHECK_NEAR(def_of("bass", "cut"), 0, 0);
    CHECK_NEAR(def_of("loudness", "quieter_db"), 20, 0);
    CHECK_NEAR(def_of("crossfeed", "feed_db"), 4.5, 1e-7); CHECK_NEAR(def_of("crossfeed", "cutoff_hz"), 700, 0);
    CHECK_NEAR(def_of("compressor", "threshold_db"), -24, 0); CHECK_NEAR(def_of("compressor", "ratio"), 2.5, 1e-7);
    CHECK_NEAR(def_of("compressor", "knee_db"), 12, 0); CHECK_NEAR(def_of("compressor", "attack_ms"), 10, 1e-6);
    CHECK_NEAR(def_of("compressor", "release_ms"), 200, 0); CHECK_NEAR(def_of("compressor", "makeup_db"), 0, 0);
    CHECK_NEAR(def_of("compressor", "auto_makeup"), 1, 0);
    CHECK_NEAR(def_of("limiter", "ceiling_db"), -1, 0); CHECK_NEAR(def_of("limiter", "release_ms"), 120, 0);
    CHECK_NEAR(def_of("limiter", "lookahead_ms"), 5, 0);
    CHECK(param(app("limiter"), "lookahead_ms")->flags & PC_FXP_REBUILD);
    CHECK_NEAR(def_of("gain", "gain_db"), 0, 0);
    CHECK_NEAR(def_of("balance", "balance"), 0, 0);
    CHECK_NEAR(def_of("widener", "width"), 1, 0);
    CHECK_NEAR(def_of("freeverb", "room"), 0.5, 1e-7); CHECK_NEAR(def_of("freeverb", "damp"), 0.5, 1e-7);
    CHECK_NEAR(def_of("freeverb", "width"), 1, 0); CHECK_NEAR(def_of("freeverb", "level"), 0.3, 1e-7);
    CHECK_NEAR(def_of("equalizer", "count"), 0, 0); CHECK_NEAR(def_of("equalizer", "preamp_db"), 0, 0);
    CHECK_NEAR(def_of("equalizer", "b9_fc"), 1000, 0); CHECK_NEAR(def_of("equalizer", "b0_q"), 0.71, 1e-6);
    CHECK(param(app("equalizer"), "b3_gain_db")->flags & PC_FXP_BAND);
    CHECK_EQ_INT(app("equalizer")->nparam, 42);
    CHECK_EQ_INT(app("tone")->nparam, 9);
}

static void test_fxparam_step(void)
{
    const pc_fxapp_t *c = app("compressor");
    const pc_fxparam_t *thr = param(c, "threshold_db"), *ratio = param(c, "ratio"), *am = param(c, "auto_makeup");
    CHECK_NEAR(pc_fxparam_step(thr, 0, 1, true), -60, 1e-6);
    CHECK_NEAR(pc_fxparam_step(thr, -60, -1, false), -60, 1e-6);
    CHECK_NEAR(pc_fxparam_step(thr, -24, 3, false), -21, 1e-6);
    CHECK_NEAR(pc_fxparam_step(ratio, 2.5f, 1, false), 2.8, 1e-4);
    CHECK_NEAR(pc_fxparam_step(ratio, 2.5f, 100, false), 20, 1e-6);
    CHECK_NEAR(pc_fxparam_step(ratio, 20, 1, true), 1, 1e-6);
    const pc_fxparam_t *room = param(app("freeverb"), "room");
    CHECK_EQ_INT(room->unit, PC_FXU_PCT);
    float v = 0;
    for (int i = 0; i < 20; i++) v = pc_fxparam_step(room, v, 1, true);
    CHECK_NEAR(v, 1.0, 1e-6);
    CHECK_NEAR(pc_fxparam_step(room, v, 1, true), 0, 0);
    CHECK_NEAR(pc_fxparam_step(am, 0, 1, true), 1, 0);
    CHECK_NEAR(pc_fxparam_step(am, 1, 1, true), 0, 0);
    const pc_fxparam_t *ty = param(app("equalizer"), "b0_type");
    CHECK_EQ_INT(ty->unit, PC_FXU_CHOICE);
    CHECK_NEAR(pc_fxparam_step(ty, 2, 1, true), 0, 0);
    CHECK_NEAR(pc_fxparam_step(ty, 0, 5, false), 2, 0);
}

static void text_is(const pc_fxparam_t *p, float v, bool zh, const char *want)
{
    char b[32];
    int n = pc_fxparam_text(p, v, zh, b, sizeof b);
    CHECK_STR(b, want);
    CHECK_EQ_INT(n, (int)strlen(want));
}

static void test_fxparam_text(void)
{
    const pc_fxapp_t *c = app("compressor");
    text_is(param(app("gain"), "gain_db"), -3.5f, true, "-3.5 dB");
    text_is(param(c, "threshold_db"), -24, true, "-24 dB");
    text_is(param(app("crossfeed"), "cutoff_hz"), 1200, true, "1.2 kHz");
    text_is(param(app("crossfeed"), "cutoff_hz"), 700, true, "700 Hz");
    text_is(param(app("equalizer"), "b0_fc"), 999.7f, true, "1 kHz");
    text_is(param(app("equalizer"), "b0_fc"), 999.4f, true, "999 Hz");
    text_is(param(app("freeverb"), "room"), 0.4f, true, "40 %");
    text_is(param(c, "ratio"), 2.5f, true, "2.5:1");
    text_is(param(c, "attack_ms"), 10, true, "10 ms");
    text_is(param(c, "auto_makeup"), 1, true, "開");
    text_is(param(c, "auto_makeup"), 1, false, "on");
    text_is(param(app("equalizer"), "b0_type"), 1, true, "低架");
    text_is(param(app("equalizer"), "b0_type"), 2, false, "High shelf");
    char small[4];
    pc_fxparam_text(param(c, "threshold_db"), -24, true, small, sizeof small);   /* truncated, terminated */
    CHECK(strlen(small) == 3);
}

static void test_fx_allowed(void)
{
    const uint8_t light_runs[] = { 0, PC_FXM_BATT, PC_FXM_APTX, PC_FXM_LDAC, PC_FXM_CAST, PC_FXM_BATT | PC_FXM_CAST };
    for (size_t i = 0; i < sizeof light_runs; i++) CHECK(pc_fx_allowed(PC_FXC_LIGHT, light_runs[i]));
    CHECK(pc_fx_allowed(PC_FXC_HEAVY, 0));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_LDAC));   /* T12: the M3 run (all HEAVY on, LDAC 990) failed */
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_BATT));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_APTX));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_CAST));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_LDAC | PC_FXM_APTX));
    CHECK(pc_fx_mode_why(0, true) == NULL);
    CHECK_STR(pc_fx_mode_why(PC_FXM_LDAC, true), "LDAC 編碼需要 CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_LDAC, false), "LDAC encoding needs the CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_APTX, true), "aptX 編碼需要 CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_APTX, false), "aptX encoding needs the CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_BATT | PC_FXM_APTX | PC_FXM_CAST, true), "用電池時省電");
    CHECK_STR(pc_fx_mode_why(PC_FXM_BATT, false), "On battery");
    CHECK_STR(pc_fx_mode_why(PC_FXM_CAST | PC_FXM_APTX, false), "aptX encoding needs the CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_CAST | PC_FXM_LDAC, true), "廣播時需要 CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_CAST, false), "Broadcasting needs the CPU");
    /* hires T8: HIRES suspends HEAVY, HIRES2 every class; the rate bits */
    CHECK(pc_fx_allowed(PC_FXC_LIGHT, PC_FXM_HIRES));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_HIRES));
    CHECK(!pc_fx_allowed(PC_FXC_LIGHT, PC_FXM_HIRES2));
    CHECK(!pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_HIRES2));
    CHECK(!pc_fx_allowed(PC_FXC_LIGHT, PC_FXM_HIRES | PC_FXM_HIRES2));
    /* hires T8b (H27, Batch B fxbench: 3.85 % of a core at 96k, 7.69 % at 192k): the limiter is re-allowed under HIRES2, every other app is
     * not; outside HIRES2 an app follows its class */
    const pc_fxapp_t *const *all;
    int na = pc_fxapps(&all);
    for (int i = 0; i < na; i++) {
        const pc_fxapp_t *ap = all[i];
        bool lim = !strcmp(ap->id, "limiter");
        CHECK(pc_fx_app_allowed(ap, PC_FXM_HIRES | PC_FXM_HIRES2) == lim);
        CHECK(pc_fx_app_allowed(ap, PC_FXM_HIRES) == (ap->cls == PC_FXC_LIGHT));
        CHECK(pc_fx_app_allowed(ap, 0));
        CHECK(pc_fx_app_allowed(ap, PC_FXM_BATT | PC_FXM_HIRES | PC_FXM_HIRES2) == lim);
        CHECK(pc_fx_app_allowed(ap, PC_FXM_BATT) == pc_fx_allowed((pc_fxclass_t)ap->cls, PC_FXM_BATT));
    }
    CHECK(pc_fx_app_allowed(app("limiter"), PC_FXM_HIRES | PC_FXM_HIRES2));
    CHECK(!pc_fx_app_allowed(app("gain"), PC_FXM_HIRES | PC_FXM_HIRES2));
    CHECK(!pc_fx_app_allowed(NULL, 0));
    CHECK_EQ_INT(pc_fx_bench_block(44100), 441);
    CHECK_EQ_INT(pc_fx_bench_block(48000), 480);
    CHECK_EQ_INT(pc_fx_bench_block(96000), 480);
    CHECK_EQ_INT(pc_fx_bench_block(192000), 480);
    CHECK_EQ_INT(pc_fx_bench_block(384000), 480);
    CHECK_EQ_INT(pc_fx_rate_mode(44100), 0);
    CHECK_EQ_INT(pc_fx_rate_mode(48000), 0);
    CHECK_EQ_INT(pc_fx_rate_mode(88200), PC_FXM_HIRES);
    CHECK_EQ_INT(pc_fx_rate_mode(96000), PC_FXM_HIRES);
    CHECK_EQ_INT(pc_fx_rate_mode(176400), PC_FXM_HIRES | PC_FXM_HIRES2);
    CHECK_EQ_INT(pc_fx_rate_mode(384000), PC_FXM_HIRES | PC_FXM_HIRES2);
    CHECK_STR(pc_fx_mode_why(PC_FXM_HIRES, true), "高取樣率需要 CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_HIRES, false), "hi-res needs the CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_BATT | PC_FXM_HIRES2, true), "超高取樣率需要 CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_HIRES2, false), "very high rates need the CPU");
    CHECK_STR(pc_fx_mode_why(PC_FXM_BATT | PC_FXM_HIRES, true), "用電池時省電");
    CHECK_STR(pc_fx_mode_why(PC_FXM_CAST | PC_FXM_HIRES, true), "廣播時需要 CPU");

    /* fx apps T17: a Bluetooth sink of any codec is the BT bit; no class suspends for it alone (a plain SBC / AAC headset keeps the
     * LIGHT and HEAVY apps as before) */
    CHECK(pc_fx_allowed(PC_FXC_LIGHT, PC_FXM_BT));
    CHECK(pc_fx_allowed(PC_FXC_HEAVY, PC_FXM_BT));
    CHECK(pc_fx_mode_why(PC_FXM_BT, true) == NULL);
    for (int i = 0; i < na; i++) CHECK(pc_fx_app_allowed(all[i], PC_FXM_BT) == !(all[i]->flags & PC_FXA_WIRED_ONLY));
#ifdef CONFIG_FX_VELVET_ROOM
    /* velvet_room runs only with no mode bit at all (wired, mains, <= 48 kHz); every bit, alone or combined, suspends it, with the one reason */
    const pc_fxapp_t *vr = app("velvet_room");
    CHECK(vr->flags & PC_FXA_WIRED_ONLY);
    CHECK_EQ_INT(vr->cls, PC_FXC_HEAVY);                 /* the class stays HEAVY: the per-app rule is the stricter one */
    CHECK(pc_fx_app_allowed(vr, 0));
    const uint8_t bits[] = { PC_FXM_BATT, PC_FXM_APTX, PC_FXM_LDAC, PC_FXM_CAST, PC_FXM_HIRES, PC_FXM_HIRES2, PC_FXM_BT,
                             PC_FXM_HIRES | PC_FXM_HIRES2, PC_FXM_BT | PC_FXM_LDAC, PC_FXM_BT | PC_FXM_BATT, 0x7f };
    for (size_t i = 0; i < sizeof bits; i++) {
        CHECK(!pc_fx_app_allowed(vr, bits[i]));
        CHECK_STR(pc_fx_app_why(vr, bits[i], true), "只在有線播放時可用");
        CHECK_STR(pc_fx_app_why(vr, bits[i], false), "wired playback only");
    }
    CHECK(pc_fx_app_why(vr, 0, true) == NULL);
#endif
    /* every other app: the app reason is the class reason */
    for (int i = 0; i < na; i++) {
        if (all[i]->flags & PC_FXA_WIRED_ONLY) continue;
        CHECK(pc_fx_app_why(all[i], PC_FXM_LDAC, true) == pc_fx_mode_why(PC_FXM_LDAC, true));
        CHECK(pc_fx_app_why(all[i], PC_FXM_BT, false) == NULL);
    }
    CHECK(pc_fx_app_why(NULL, PC_FXM_LDAC, true) == NULL);
}

static void test_ramp(void)
{
    pc_fx_ramp_t r = { 1, 1 };
    pc_fx_ramp_set(&r, 3);
    CHECK_NEAR(pc_fx_ramp_at(&r, 0, 4), 1.5, 1e-6);
    CHECK_NEAR(pc_fx_ramp_at(&r, 3, 4), 3, 1e-6);
    pc_fx_ramp_end(&r);
    CHECK_NEAR(r.cur, 3, 0);
    CHECK_NEAR(pc_fx_ramp_at(&r, 0, 4), 3, 0);
}

/* non-finite values (review I3): step and text treat them as the parameter's default, for every unit */
static void test_nonfinite(void)
{
    const float BAD[3] = { NAN, INFINITY, -INFINITY };
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a), units = 0;
    for (int i = 0; i <= n; i++) {
        int np = i < n ? a[i]->nparam : 1;
        for (int p = 0; p < np; p++) {
            const pc_fxparam_t *q = i < n ? &a[i]->param[p] : &pc_fxapp_mix_param;
            units |= 1 << q->unit;
            for (int b = 0; b < 3; b++) {
                for (int d = -2; d <= 2; d++)
                    for (int tap = 0; tap < 2; tap++) {
                        float r = pc_fxparam_step(q, BAD[b], d, tap), w = pc_fxparam_step(q, q->def, d, tap);
                        if (!(r == w)) { fprintf(stderr, "%s: step(%g, %d, %d) = %g, default gives %g\n", q->id, BAD[b], d, tap, r, w); exit(1); }
                    }
                char t1[32], t2[32];
                for (int zh = 0; zh < 2; zh++) {
                    pc_fxparam_text(q, BAD[b], zh, t1, sizeof t1);
                    pc_fxparam_text(q, q->def, zh, t2, sizeof t2);
                    CHECK_STR(t1, t2);
                }
            }
            char t[32];   /* huge finite values stay printable */
            pc_fxparam_text(q, 3e38f, false, t, sizeof t);
            pc_fxparam_text(q, -3e38f, false, t, sizeof t);
        }
    }
#ifdef CONFIG_FX_VELVET_ROOM
    const int seconds = 1 << PC_FXU_S;   /* only the room's decay time is in seconds */
#else
    const int seconds = 0;
#endif
    CHECK_EQ_INT(units, seconds | (1 << PC_FXU_NONE) | (1 << PC_FXU_DB) | (1 << PC_FXU_HZ) | (1 << PC_FXU_MS) | (1 << PC_FXU_PCT) | (1 << PC_FXU_RATIO) | (1 << PC_FXU_BOOL) | (1 << PC_FXU_CHOICE));
}

int main(void)
{
    RUN(test_registry_rules);
    RUN(test_mix_flags);
    RUN(test_classes);
    RUN(test_defaults);
    RUN(test_fxparam_step);
    RUN(test_nonfinite);
    RUN(test_fxparam_text);
    RUN(test_fx_allowed);
    RUN(test_ramp);
    return 0;
}
