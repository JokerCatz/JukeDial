// SPDX-License-Identifier: LGPL-2.1-or-later
/* fx state (spec §4, §7): the value table, its kv form, import of a chipchill preset (the whole sound), export, and the
 * rules that moved here from the old chain (static order, equalizer merge, duplicates). */
#include <stdlib.h>
#include <string.h>
#include "pc/pc_fxstate.h"
#include "pc_test.h"

static char *slurp(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static const pc_fxapp_t *app_of(int i) { const pc_fxapp_t *const *a; pc_fxapps(&a); return a[i]; }
static int napps(void) { return pc_fxapps(NULL); }
static int A(const char *id) { int i = pc_fxapp_find(id); CHECK(i >= 0); return i; }
static int P(int app, const char *id)
{
    const pc_fxapp_t *ap = app_of(app);
    for (int p = 0; p < ap->nparam; p++)
        if (!strcmp(ap->param[p].id, id)) return p;
    fprintf(stderr, "no param %s.%s\n", ap->id, id);
    exit(1);
}
static float V(const pc_fxstate_t *s, const char *app, const char *param) { int a = A(app); return s->v[a][P(a, param)]; }

static uint32_t s_rnd = 99;
static float rnd01(void) { s_rnd = s_rnd * 1103515245u + 12345u; return (float)((s_rnd >> 8) & 0xFFFFFF) / 16777216.0f; }

static void random_state(pc_fxstate_t *s)
{
    pc_fxstate_defaults(s);
    for (int i = 0; i < napps(); i++) {
        const pc_fxapp_t *ap = app_of(i);
        s->on[i] = rnd01() > 0.5f;
        for (int p = 0; p < ap->nparam; p++) {
            const pc_fxparam_t *q = &ap->param[p];
            float r = rnd01(), v;
            if (q->unit == PC_FXU_BOOL || q->unit == PC_FXU_CHOICE) v = q->lo + floorf(r * (q->hi - q->lo + 1) * 0.999f);
            else v = q->lo + r * (q->hi - q->lo);
            s->v[i][p] = v;
        }
        if (ap->flags & PC_FXA_MIX) s->mix[i] = rnd01();
    }
    snprintf(s->state_from, sizeof s->state_from, "warm.toml");
}

static void same_state(const pc_fxstate_t *a, const pc_fxstate_t *b, bool bits)
{
    CHECK_STR(a->state_from, b->state_from);
    for (int i = 0; i < napps(); i++) {
        const pc_fxapp_t *ap = app_of(i);
        CHECK_EQ_INT(a->on[i], b->on[i]);
        for (int p = 0; p < ap->nparam; p++) {
            if (bits) CHECK(memcmp(&a->v[i][p], &b->v[i][p], sizeof(float)) == 0);
            else CHECK_NEAR(a->v[i][p], b->v[i][p], 1e-5 * fmax(1.0, fabs(a->v[i][p])));
        }
        if (bits) CHECK(memcmp(&a->mix[i], &b->mix[i], sizeof(float)) == 0);
        else CHECK_NEAR(a->mix[i], b->mix[i], 1e-5);
    }
}

static void test_fxstate_kv_roundtrip(void)
{
    pc_kv_t *kv = malloc(256 * sizeof *kv);
    CHECK(kv != NULL);
    for (int round = 0; round < 50; round++) {
        pc_fxstate_t a, b;
        random_state(&a);
        int n = pc_fxstate_to_kv(&a, kv, 256);
        CHECK(n > 0 && n <= 128);
        for (int i = 0; i < n; i++) CHECK(strlen(kv[i].key) <= 31);
        pc_fxstate_from_kv(&b, kv, n);
        same_state(&a, &b, true);
        CHECK(pc_fxstate_to_kv(&a, kv, n - 1) == -1);          /* max too small */
    }
    pc_fxstate_t d;
    pc_fxstate_defaults(&d);
    for (int i = 0; i < napps(); i++) { CHECK(!d.on[i]); CHECK_NEAR(d.mix[i], 1, 0); }
    CHECK(pc_fxstate_to_kv(&d, kv, 256) <= 128);
    /* mix is written for MIX apps only, once; an app with neither writes no .mix key */
    pc_fxstate_t s;
    random_state(&s);
    int n = pc_fxstate_to_kv(&s, kv, 256);
    CHECK(pc_kv_get(kv, n, "tone.mix") != NULL);
    CHECK(pc_kv_get(kv, n, "gain.mix") == NULL);
    CHECK(pc_kv_get(kv, n, "equalizer.b0") != NULL);
    CHECK(pc_kv_get(kv, n, "equalizer.b0_fc") == NULL);
    CHECK_STR(pc_kv_get(kv, n, "format"), "1");
    free(kv);
}

static void kv_from_text(pc_fxstate_t *s, const char *text)
{
    static pc_kv_t kv[300];
    int n = pc_kv_parse(text, kv, 300);
    pc_fxstate_from_kv(s, kv, n);
}

static void test_fxstate_kv_bad(void)
{
    pc_fxstate_t s, d;
    pc_fxstate_defaults(&d);
    kv_from_text(&s, "");
    same_state(&s, &d, true);                                  /* missing keys = defaults */
    kv_from_text(&s, "format=1\ncompressor.ratio=-3\ngain.gain_db=nan\n");
    CHECK_NEAR(V(&s, "compressor", "ratio"), 1, 0);
    CHECK_NEAR(V(&s, "gain", "gain_db"), 0, 0);
    kv_from_text(&s, "format=1\ngain.gain_db=\ngain.on=1\n");        /* a truncated line: empty value */
    CHECK_NEAR(V(&s, "gain", "gain_db"), 0, 0);
    CHECK(s.on[A("gain")]);
    kv_from_text(&s, "format=1\nequalizer.count=50\nequalizer.b0=X 1e9\n");
    CHECK_NEAR(V(&s, "equalizer", "count"), 10, 0);
    CHECK_NEAR(V(&s, "equalizer", "b0_type"), 0, 0);            /* not P/L/H: the default type */
    CHECK_NEAR(V(&s, "equalizer", "b0_fc"), 20000, 0);
    CHECK_NEAR(V(&s, "equalizer", "b0_q"), 0.71, 1e-6);         /* missing fields: the defaults */
    kv_from_text(&s, "equalizer.b1=H 8000 -3.5 0.9\n");
    CHECK_NEAR(V(&s, "equalizer", "b1_type"), 2, 0);
    CHECK_NEAR(V(&s, "equalizer", "b1_fc"), 8000, 0);
    CHECK_NEAR(V(&s, "equalizer", "b1_gain_db"), -3.5, 1e-6);
    kv_from_text(&s, "equalizer.b2=105 -2.1 0.7\n");                  /* a hand-edited line without the type letter keeps its first digit (final review M9) */
    CHECK_NEAR(V(&s, "equalizer", "b2_type"), 0, 0);
    CHECK_NEAR(V(&s, "equalizer", "b2_fc"), 105, 0);
    CHECK_NEAR(V(&s, "equalizer", "b2_gain_db"), -2.1, 1e-6);
    CHECK_NEAR(V(&s, "equalizer", "b2_q"), 0.7, 1e-6);
    kv_from_text(&s, "format=1\n");
    same_state(&s, &d, true);                                  /* only a header: defaults, every app off */
    kv_from_text(&s, "format=2\ngain.on=1\ngain.gain_db=5\nstate_from=x.toml\n");
    same_state(&s, &d, true);                                  /* an unknown format: defaults */
    kv_from_text(&s, "format=1\ngain.mix=0.5\ntone.mix=2\nstate_from=a.toml\n");
    CHECK_NEAR(s.mix[A("gain")], 1, 0);                        /* gain has no mix */
    CHECK_NEAR(s.mix[A("tone")], 1, 0);                        /* clamped by pc_fxapp_mix_param */
    CHECK_STR(s.state_from, "a.toml");
    kv_from_text(&s, "format=1\ntone.mix=0.25\nbogus=1\ngain.nothing=3\n");
    CHECK_NEAR(s.mix[A("tone")], 0.25, 1e-7);
    kv_from_text(&s, "format=1\ngain.on=2\nx\ncompressor.ratio=\ncompressor.auto_makeup=0.7\n");
    CHECK_NEAR(V(&s, "compressor", "auto_makeup"), 1, 0);      /* BOOL rounded */
    CHECK_NEAR(V(&s, "compressor", "ratio"), 2.5, 0);
    /* the clamp rule on its own */
    int g = A("gain"), c = A("compressor");
    CHECK_NEAR(pc_fxstate_clamp(g, P(g, "gain_db"), NAN), 0, 0);
    CHECK_NEAR(pc_fxstate_clamp(g, P(g, "gain_db"), 100), 12, 0);
    CHECK_NEAR(pc_fxstate_clamp(g, P(g, "gain_db"), -INFINITY), 0, 0);
    CHECK_NEAR(pc_fxstate_clamp(c, P(c, "auto_makeup"), 0.4f), 0, 0);
    CHECK_NEAR(pc_fxstate_clamp(c, PC_FXSTATE_MIX, NAN), 1, 0);
    CHECK_NEAR(pc_fxstate_clamp(c, PC_FXSTATE_MIX, -1), 0, 0);
}

static pc_preset_t preset(const char *toml)
{
    static pc_preset_t p;
    CHECK_EQ_INT(pc_preset_parse(toml, &p), 0);
    return p;
}

static void test_fxstate_import_basic(void)
{
    pc_preset_t p = preset(slurp("preset_basic.toml"));
    pc_fxstate_t s;
    pc_fxstate_defaults(&s);
    snprintf(s.state_from, sizeof s.state_from, "keep.toml");
    char un[8][40];
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 8), 0);
    CHECK_STR(s.state_from, "keep.toml");                      /* the caller's label */
    for (int i = 0; i < napps(); i++) {
        const char *id = app_of(i)->id;
        bool want = !strcmp(id, "equalizer") || !strcmp(id, "tone") || !strcmp(id, "limiter") || !strcmp(id, "freeverb");
        CHECK_EQ_INT(s.on[i], want);
    }
    CHECK_NEAR(V(&s, "equalizer", "count"), 2, 0);
    CHECK_NEAR(V(&s, "equalizer", "preamp_db"), -6.2, 1e-5);
    CHECK_NEAR(V(&s, "equalizer", "b0_type"), 0, 0);
    CHECK_NEAR(V(&s, "equalizer", "b0_fc"), 105, 1e-4);
    CHECK_NEAR(V(&s, "equalizer", "b0_gain_db"), -2.3, 1e-5);
    CHECK_NEAR(V(&s, "equalizer", "b0_q"), 0.70, 1e-5);
    CHECK_NEAR(V(&s, "equalizer", "b1_type"), 1, 0);
    CHECK_NEAR(V(&s, "equalizer", "b1_fc"), 80, 1e-4);
    CHECK_NEAR(V(&s, "equalizer", "b1_gain_db"), 4.0, 1e-5);
    CHECK_NEAR(V(&s, "equalizer", "b1_q"), 0.71, 1e-5);
    CHECK_NEAR(s.mix[A("tone")], 0.5, 1e-6);
    CHECK_NEAR(s.mix[A("equalizer")], 1, 0);
    CHECK_NEAR(V(&s, "tone", "warmth"), 0.4, 1e-6);
    CHECK_NEAR(V(&s, "tone", "depth"), -0.2, 1e-6);
    CHECK_NEAR(V(&s, "limiter", "ceiling_db"), -1.5, 1e-6);
    CHECK_NEAR(V(&s, "freeverb", "level"), 0.3, 1e-6);
    CHECK_NEAR(V(&s, "freeverb", "room"), 0.7, 1e-6);
}

static void test_fxstate_import_whole(void)
{
    pc_fxstate_t s;
    pc_fxstate_defaults(&s);
    char un[8][40];
    pc_preset_t p = preset(slurp("preset_basic.toml"));
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 8), 0);
    p = preset(slurp("preset_unsupported.toml"));          /* owner question 4 (provisional): an import is the whole sound */
    int n = pc_fxstate_import(&s, &p, un, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_STR(un[0], "tape");
    CHECK_STR(un[1], "bass (only_for)");
    CHECK(s.on[A("gain")]);
    CHECK_NEAR(V(&s, "gain", "gain_db"), -3, 1e-6);
    for (int i = 0; i < napps(); i++)
        if (strcmp(app_of(i)->id, "gain")) CHECK(!s.on[i]);   /* equalizer/tone/limiter/freeverb off; widener is enabled = false; bass skipped */
    CHECK_NEAR(V(&s, "tone", "warmth"), 0, 0);              /* values of the earlier sound are gone too: defaults */
    CHECK_NEAR(s.mix[A("tone")], 1, 0);
    CHECK_NEAR(V(&s, "equalizer", "count"), 0, 0);
    /* a max of 1 keeps the first report only, the return value says how many were kept */
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 1), 1);
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, NULL, 0), 0);
}

static void test_fxstate_import_merge_dup(void)
{
    const char *t =
        "format = 1\n"
        "[[music]]\nkind = \"equalizer\"\nequalizer = \"\"\"\nPreamp: -3 dB\nFilter 1: ON PK Fc 500 Hz Gain 6 dB Q 1\nFilter 2: ON PK Fc 600 Hz Gain 1 dB Q 1\n\"\"\"\n"
        "[[music]]\nkind = \"equalizer\"\nequalizer = \"\"\"\nPreamp: -2 dB\nFilter 1: ON LSC Fc 100 Hz Gain 2 dB Q 0.7\n\"\"\"\n"
        "[[music]]\nkind = \"compressor\"\nparams = { threshold_db = -30 }\n"
        "[[music]]\nkind = \"compressor\"\nparams = { threshold_db = -10 }\n"
        "[[music]]\nkind = \"gain\"\nmix = 0.5\nparams = { gain_db = -3 }\n"
        "[[music]]\nkind = \"freeverb\"\nparams = { room = 0.8, level = 0.4 }\n";
    pc_preset_t p = preset(t);
    pc_fxstate_t s;
    pc_fxstate_defaults(&s);
    char un[8][40];
    int n = pc_fxstate_import(&s, &p, un, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_STR(un[0], "compressor (重複)");
    CHECK_STR(un[1], "gain mix");
    CHECK_NEAR(V(&s, "equalizer", "count"), 3, 0);          /* two nodes, one app */
    CHECK_NEAR(V(&s, "equalizer", "preamp_db"), -5, 1e-6);  /* preamps summed */
    CHECK_NEAR(V(&s, "equalizer", "b2_type"), 1, 0);
    CHECK_NEAR(V(&s, "equalizer", "b2_fc"), 100, 1e-4);
    CHECK_NEAR(V(&s, "compressor", "threshold_db"), -30, 1e-6);   /* the first one wins */
    CHECK(s.on[A("compressor")]);
    CHECK_NEAR(V(&s, "gain", "gain_db"), -3, 1e-6);
    CHECK(s.on[A("gain")]);
    CHECK(s.on[A("freeverb")]);                             /* a music-list freeverb is accepted like a send */
    CHECK_NEAR(V(&s, "freeverb", "room"), 0.8, 1e-6);
    CHECK_NEAR(V(&s, "freeverb", "level"), 0.4, 1e-6);
    /* bands stop at 10: 12 filters over three nodes */
    char big[4096] = "format = 1\n";
    for (int k = 0; k < 3; k++) {
        strcat(big, "[[music]]\nkind = \"equalizer\"\nequalizer = \"\"\"\n");
        for (int b = 0; b < 4; b++) { char l[96]; snprintf(l, sizeof l, "Filter %d: ON PK Fc %d Hz Gain 1 dB Q 1\n", b + 1, 100 * (b + 1 + 4 * k)); strcat(big, l); }
        strcat(big, "\"\"\"\n");
    }
    p = preset(big);
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 8), 0);
    CHECK_NEAR(V(&s, "equalizer", "count"), 10, 0);
    CHECK_NEAR(V(&s, "equalizer", "b9_fc"), 1000, 1e-3);
    /* an unknown parameter id is reported once, with its kind */
    p = preset("format = 1\n[[music]]\nkind = \"gain\"\nparams = { gain_db = -1, foo = 3 }\n");
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 8), 1);
    CHECK_STR(un[0], "gain.foo");
    CHECK_NEAR(V(&s, "gain", "gain_db"), -1, 1e-6);
    /* a send that is not a reverb is reported, not run */
    p = preset("format = 1\n[[sends]]\nfrom = 0\nlevel = 0.5\nnodes = [{ kind = \"gain\" }]\n");
    CHECK_EQ_INT(pc_fxstate_import(&s, &p, un, 8), 1);
    CHECK(!s.on[A("gain")]);
}

/* the order in the file does not matter: the state is per app, the chain order is fixed */
static void test_fxstate_import_order_free(void)
{
    pc_preset_t a = preset("format = 1\n[[music]]\nkind = \"limiter\"\n[[music]]\nkind = \"gain\"\nparams = { gain_db = 12 }\n");
    pc_fxstate_t sa;
    pc_fxstate_defaults(&sa);
    char un[8][40];
    CHECK_EQ_INT(pc_fxstate_import(&sa, &a, un, 8), 0);
    pc_preset_t b = preset("format = 1\n[[music]]\nkind = \"gain\"\nparams = { gain_db = 12 }\n[[music]]\nkind = \"limiter\"\n");
    pc_fxstate_t sb;
    pc_fxstate_defaults(&sb);
    CHECK_EQ_INT(pc_fxstate_import(&sb, &b, un, 8), 0);
    same_state(&sa, &sb, true);
    CHECK(sa.on[A("gain")] && sa.on[A("limiter")]);
}

static void test_fxstate_import_garbage(void)
{
    char t[4096] = "format = 1\n"
        "[[music]]\nkind = \"compressor\"\nparams = { ratio = 1e9, threshold_db = \"nan\", attack_ms = -5, knee_db = 1e30 }\n"
        "[[music]]\nkind = \"tone\"\nmix = 7\nparams = { warmth = 1e30, depth = -1e30 }\n"
        "[[music]]\nkind = \"equalizer\"\nequalizer = \"\"\"\nPreamp: -1e9 dB\n";
    for (int b = 0; b < 14; b++) { char l[96]; snprintf(l, sizeof l, "Filter %d: ON PK Fc %d Hz Gain 99 dB Q 99\n", b + 1, 1000 * (b + 1)); strcat(t, l); }
    strcat(t, "\"\"\"\n");
    pc_preset_t p = preset(t);
    pc_fxstate_t s;
    pc_fxstate_defaults(&s);
    char un[8][40];
    pc_fxstate_import(&s, &p, un, 8);
    for (int i = 0; i < napps(); i++) {
        const pc_fxapp_t *ap = app_of(i);
        for (int q = 0; q < ap->nparam; q++) {
            CHECK(isfinite(s.v[i][q]));
            CHECK(s.v[i][q] >= ap->param[q].lo && s.v[i][q] <= ap->param[q].hi);
        }
        CHECK(s.mix[i] >= 0 && s.mix[i] <= 1);
    }
    CHECK_NEAR(V(&s, "compressor", "ratio"), 20, 0);
    CHECK_NEAR(V(&s, "tone", "warmth"), 1.5, 0);
    CHECK_NEAR(V(&s, "equalizer", "count"), 10, 0);
    CHECK_NEAR(V(&s, "equalizer", "preamp_db"), -24, 0);
    CHECK_NEAR(s.mix[A("tone")], 1, 0);
}

static void test_fxstate_export_identity(void)
{
    pc_fxstate_t s;
    pc_fxstate_defaults(&s);
    int eq = A("equalizer"), tn = A("tone"), cp = A("compressor"), fv = A("freeverb"), lm = A("limiter"), gn = A("gain");
    s.on[eq] = true; s.v[eq][P(eq, "preamp_db")] = -3.5f; s.v[eq][P(eq, "count")] = 3;
    const char *bn[3][4] = { { "b0_type", "b0_fc", "b0_gain_db", "b0_q" }, { "b1_type", "b1_fc", "b1_gain_db", "b1_q" }, { "b2_type", "b2_fc", "b2_gain_db", "b2_q" } };
    float bv[3][4] = { { 0, 105.5f, -2.25f, 0.7f }, { 1, 80, 4, 0.71f }, { 2, 9000, -3.1f, 0.9f } };
    for (int b = 0; b < 3; b++) for (int k = 0; k < 4; k++) s.v[eq][P(eq, bn[b][k])] = bv[b][k];
    s.on[tn] = true; s.mix[tn] = 0.4f; s.v[tn][P(tn, "warmth")] = 0.35f;
    s.on[cp] = true; s.v[cp][P(cp, "threshold_db")] = -18; s.v[cp][P(cp, "auto_makeup")] = 0;
    s.on[fv] = true; s.v[fv][P(fv, "level")] = 0.45f; s.v[fv][P(fv, "room")] = 0.6f;
    s.on[lm] = true; s.v[lm][P(lm, "ceiling_db")] = -2.5f;
    s.v[gn][0] = 3;                                          /* off, but the value stays */
    static char out[16384];
    int n = pc_fxstate_export(&s, out, sizeof out);
    CHECK(n > 0 && (size_t)n == strlen(out));
    CHECK(pc_fxstate_export(&s, out, (size_t)n) == -1);      /* n is too small for the NUL */
    pc_fxstate_export(&s, out, sizeof out);
    CHECK(strstr(out, "format = 1") != NULL);
    /* every app in registry order; off ones enabled = false; freeverb a send with its level; limiter a master node */
    const char *music_end = strstr(out, "[[master]]"), *sends = strstr(out, "[[sends]]");
    CHECK(music_end != NULL && sends != NULL);
    const char *at = out;
    for (int i = 0; i < napps(); i++) {
        char key[64];
        snprintf(key, sizeof key, "kind = \"%s\"", app_of(i)->id);
        const char *f = strstr(out, key);
        CHECK(f != NULL);
        if (i == lm) CHECK(f > music_end && f < sends);
        else if (i == fv) CHECK(f > sends);
        else { CHECK(f >= at && f < music_end); at = f; }
    }
    CHECK(strstr(out, "kind = \"gain\"\nenabled = false") != NULL);
    CHECK(strstr(out, "Preamp: -3.5 dB") != NULL);
    CHECK(strstr(sends, "level = 0.45") != NULL);
    pc_preset_t p = preset(out);
    CHECK_EQ_INT(p.nunsup, 0);
    pc_fxstate_t r;
    pc_fxstate_defaults(&r);
    char un[8][40];
    CHECK_EQ_INT(pc_fxstate_import(&r, &p, un, 8), 0);
    for (int i = 0; i < napps(); i++) {
        const pc_fxapp_t *ap = app_of(i);
        CHECK_EQ_INT(r.on[i], s.on[i]);
        for (int q = 0; q < ap->nparam; q++) {
            if ((ap->param[q].flags & PC_FXP_BAND) && q >= P(i, "b3_type")) continue;   /* bands past count are not exported */
            CHECK_NEAR(r.v[i][q], s.v[i][q], 1e-5 * fmax(1.0, fabs(s.v[i][q])));
        }
        CHECK_NEAR(r.mix[i], s.mix[i], 1e-6);
    }
    static char again[16384];
    CHECK(pc_fxstate_export(&r, again, sizeof again) == n);
    CHECK(strcmp(again, out) == 0);                          /* a second round gives the same text */
    char tiny[64];
    CHECK(pc_fxstate_export(&s, tiny, sizeof tiny) == -1);
}

#ifndef CONFIG_FX_VELVET_ROOM
/* a build without velvet_room: its keys are ignored when reading and not written back (test_fxstate_novroom) */
static void test_fxstate_drops_absent_app(void)
{
    pc_fxstate_t s, d;
    pc_fxstate_defaults(&d);
    kv_from_text(&s, "format=1\nvelvet_room.size=1\nvelvet_room.on=1\n");
    same_state(&s, &d, true);                                  /* nothing of velvet_room is held */
    kv_from_text(&s, "format=1\nvelvet_room.on=1\ngain.on=1\n");
    CHECK(s.on[A("gain")]);                                    /* the keys around it still count */
    static pc_kv_t kv[300];
    int n = pc_fxstate_to_kv(&s, kv, 300);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) CHECK(strncmp(kv[i].key, "velvet_room.", 12) != 0);
    CHECK(pc_kv_get(kv, n, "gain.on") != NULL);
}
#else
/* a build with it: its keys are read and written like any other app's */
static void test_fxstate_keeps_present_app(void)
{
    pc_fxstate_t s;
    kv_from_text(&s, "format=1\nvelvet_room.on=1\nvelvet_room.size=1.5\nvelvet_room.mix=0.6\n");
    CHECK(s.on[A("velvet_room")]);
    CHECK_NEAR(V(&s, "velvet_room", "size"), 1.5, 1e-6);
    CHECK_NEAR(V(&s, "velvet_room", "mix"), 0.6, 1e-6);
    static pc_kv_t kv[300];
    int n = pc_fxstate_to_kv(&s, kv, 300);
    CHECK(pc_kv_get(kv, n, "velvet_room.on") != NULL);
    CHECK(pc_kv_get(kv, n, "velvet_room.mix") != NULL);
}
#endif

int main(void)
{
#ifndef CONFIG_FX_VELVET_ROOM
    RUN(test_fxstate_drops_absent_app);
#else
    RUN(test_fxstate_keeps_present_app);
#endif
    RUN(test_fxstate_kv_roundtrip);
    RUN(test_fxstate_kv_bad);
    RUN(test_fxstate_import_basic);
    RUN(test_fxstate_import_whole);
    RUN(test_fxstate_import_merge_dup);
    RUN(test_fxstate_import_order_free);
    RUN(test_fxstate_import_garbage);
    RUN(test_fxstate_export_identity);
    return 0;
}
