// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_sfx: the toy sound effect synth, its sound bank (the firmware's own text, app/main/sfx_bank.def) and its single-producer queue. */
#include "pc/pc_rate.h"
#include "pc/pc_sfx.h"
#include "pc_test.h"
#include <pthread.h>
#include <time.h>

static const char BANK[] =
#include "sfx_bank.def"
    ;

/* every name a page or game calls (Task 1's presets keep their names) and the brief's list */
static const char *const NEED[] = { "pop", "pop_big", "tick", "stop", "win", "clack", "dice", "ok", "fail",
                                    "tap", "back", "error", "jackpot", "jiao_sheng", "jiao_xiao", "jiao_yin",
                                    "move", "lose", "draw", "combo", "level_up", "extra_life", "game_over", "coin", "power_up" };
#define NNEED ((int)(sizeof NEED / sizeof NEED[0]))
static const uint32_t RATES[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
#define NRATES ((int)(sizeof RATES / sizeof RATES[0]))
#define MAXF 1920                        /* 192 kHz / 100 */
static float buf[2 * MAXF], buf2[2 * MAXF];
static pc_sfx_bank_t B, T;               /* the shipped bank; a test bank */

static char s_log[8192];
static int s_nlog;
static void logcb(const char *m, void *u)
{
    (void)u;
    s_nlog++;
    size_t o = strlen(s_log);
    snprintf(s_log + o, sizeof s_log - o, "%s\n", m);
}
static int parse(pc_sfx_bank_t *b, const char *text) { s_log[0] = 0; s_nlog = 0; return pc_sfx_bank_parse(b, text, logcb, NULL); }

static void test_bank_parses(void)
{
    int n = parse(&B, BANK);
    if (s_nlog) fprintf(stderr, "%s", s_log);
    CHECK_EQ_INT(s_nlog, 0);                     /* the shipped bank has nothing refused */
    CHECK_EQ_INT(B.dropped, 0);
    CHECK(n >= 30 && n <= PC_SFX_ENTRIES);
    CHECK_EQ_INT(pc_sfx_count(&B), n);
    for (int i = 0; i < NNEED; i++) { if (pc_sfx_id(&B, NEED[i]) < 0) fprintf(stderr, "missing %s\n", NEED[i]); CHECK(pc_sfx_id(&B, NEED[i]) >= 0); }
    int groups[PC_SFX_GROUPS] = { 0 };
    for (int id = 0; id < n; id++) {
        const pc_sfx_entry_t *e = pc_sfx_entry(&B, id);
        CHECK(e && e->zh[0] && e->en[0] && e->ntr >= 1 && e->ntr <= PC_SFX_TRACKS);
        CHECK(e->credit < B.ncredit && B.credit[e->credit][0]);   /* every entry is credited */
        CHECK(e->group < PC_SFX_GROUPS);
        groups[e->group]++;
        float sum = 0;
        for (int k = 0; k < e->ntr; k++) sum += e->tr[k].level;
        CHECK(sum <= 1.0001f);                   /* the parser's scaling (the peak rule) */
        CHECK(pc_sfx_len_ms(&B, id) > 0 && pc_sfx_len_ms(&B, id) <= PC_SFX_MAX_MS);
    }
    for (int g = 0; g < PC_SFX_GROUPS; g++) CHECK(groups[g] > 0);
    CHECK(pc_sfx_len_ms(&B, pc_sfx_id(&B, "extra_life")) >= 400);   /* a jingle, not a beep */
    CHECK(pc_sfx_entry(&B, pc_sfx_id(&B, "level_up"))->ntr >= 2);
    printf("sfx bank: %d entries, %d notes, %d wave tables, %d credits\n", n, B.nev, B.nwave, B.ncredit);
}

static void test_names(void)
{
    CHECK_EQ_INT(pc_sfx_id(&B, "nope"), -1);
    CHECK_EQ_INT(pc_sfx_id(&B, ""), -1);
    CHECK_EQ_INT(pc_sfx_id(&B, NULL), -1);
    CHECK_EQ_INT(pc_sfx_id(NULL, "pop"), -1);
    CHECK_EQ_INT(pc_sfx_count(NULL), 0);
    CHECK(pc_sfx_name(&B, -1) == NULL);
    CHECK(pc_sfx_name(&B, pc_sfx_count(&B)) == NULL);
    CHECK_STR(pc_sfx_name(&B, pc_sfx_id(&B, "coin")), "coin");
    CHECK_EQ_INT(pc_sfx_len_ms(&B, -1), 0);
    CHECK(pc_sfx_entry(&B, 999) == NULL);
    CHECK_STR(pc_sfx_group_key(PC_SFX_G_GAME), "game");
    CHECK(pc_sfx_group_key(PC_SFX_GROUPS) == NULL);
    pc_sfx_t s;
    pc_sfx_init(&s, &B);
    CHECK(!pc_sfx_start(&s, -1));
    CHECK(!pc_sfx_start(&s, pc_sfx_count(&B)));
    CHECK(!pc_sfx_active(&s));
    pc_sfx_init(&s, NULL);                       /* no bank yet (before the boot parse): nothing starts, nothing is added */
    CHECK(!pc_sfx_start(&s, 0));
    memset(buf, 0, sizeof buf);
    CHECK(!pc_sfx_mix(&s, buf, 480, 48000));
}

/* each entry alone at each rate, in the audio task's blocks (rate / 100 frames): finite, both channels equal, peak within the level (and clearly
 * audible), and after its length: nothing is active and every block is untouched silence */
static void test_entries_render(void)
{
    for (int k = 0; k < NRATES; k++) {
        uint32_t rate = RATES[k];
        int frames = (int)(rate / 100);
        for (int id = 0; id < pc_sfx_count(&B); id++) {
            pc_sfx_t s;
            pc_sfx_init(&s, &B);
            CHECK(pc_sfx_start(&s, id));
            CHECK(pc_sfx_active(&s));
            int blocks = (int)((pc_sfx_len_ms(&B, id) + 9) / 10);
            float peak = 0;
            for (int b = 0; b < blocks + 1; b++) {
                memset(buf, 0, sizeof buf);
                pc_sfx_mix(&s, buf, frames, rate);
                for (int i = 0; i < frames; i++) {
                    CHECK(isfinite(buf[2 * i]));
                    CHECK(buf[2 * i] == buf[2 * i + 1]);
                    float a = fabsf(buf[2 * i]);
                    if (a > peak) peak = a;
                }
            }
            if (peak > PC_SFX_PEAK * 1.0001f || peak < PC_SFX_PEAK * 0.2f) { fprintf(stderr, "%s @%u peak %g\n", pc_sfx_name(&B, id), rate, peak); exit(1); }
            if (pc_sfx_active(&s)) { fprintf(stderr, "%s @%u still active after %d blocks\n", pc_sfx_name(&B, id), rate, blocks + 1); exit(1); }
            for (int b = 0; b < 5; b++) {
                memset(buf, 0, sizeof buf);
                CHECK(!pc_sfx_mix(&s, buf, frames, rate));
                for (int i = 0; i < 2 * frames; i++) CHECK(buf[i] == 0.0f);
            }
        }
    }
}

static void render_all(int id, uint32_t rate, float *out, int blocks)
{
    pc_sfx_t s;
    pc_sfx_init(&s, &B);
    pc_sfx_start(&s, id);
    int frames = (int)(rate / 100);
    for (int b = 0; b < blocks; b++) {
        memset(out, 0, (size_t)frames * 2 * sizeof(float));
        pc_sfx_mix(&s, out, frames, rate);       /* the last block is compared (every block feeds the state) */
    }
}

static void test_deterministic(void)
{
    for (int id = 0; id < pc_sfx_count(&B); id++)
        for (int b = 1; b < 12; b += 3) {
            render_all(id, 48000, buf, b);
            render_all(id, 48000, buf2, b);
            CHECK(!memcmp(buf, buf2, 480 * 2 * sizeof(float)));
        }
}

static void test_adds_to_music(void)
{
    pc_sfx_t s, s2;
    pc_sfx_init(&s, &B);
    for (int i = 0; i < 2 * 480; i++) buf[i] = 0.5f;
    CHECK(!pc_sfx_mix(&s, buf, 480, 48000));     /* nothing started: the block stays as it was */
    for (int i = 0; i < 2 * 480; i++) CHECK(buf[i] == 0.5f);
    pc_sfx_start(&s, pc_sfx_id(&B, "pop"));
    for (int i = 0; i < 2 * 480; i++) buf[i] = 0.5f;
    memset(buf2, 0, sizeof buf2);
    pc_sfx_init(&s2, &B);
    pc_sfx_start(&s2, pc_sfx_id(&B, "pop"));
    CHECK(pc_sfx_mix(&s, buf, 480, 48000));
    pc_sfx_mix(&s2, buf2, 480, 48000);
    for (int i = 0; i < 2 * 480; i++) CHECK_NEAR(buf[i], 0.5f + buf2[i], 1e-6);
}

static void test_overlap_clamp_and_steal(void)
{
    pc_sfx_t s;
    pc_sfx_init(&s, &B);
    for (int k = 0; k < 40; k++) CHECK(pc_sfx_start(&s, k % pc_sfx_count(&B)));   /* far more tracks than voices: the oldest are taken */
    CHECK_EQ_INT(s.started, 40);
    CHECK(s.stolen > 0);
    int busy = 0;
    for (int v = 0; v < PC_SFX_VOICES; v++) busy += s.v[v].tr != NULL;
    CHECK_EQ_INT(busy, PC_SFX_VOICES);
    float peak = 0;
    for (int b = 0; b < 2 + PC_SFX_MAX_MS / 10; b++) {
        memset(buf, 0, sizeof buf);
        pc_sfx_mix(&s, buf, 480, 48000);
        for (int i = 0; i < 2 * 480; i++) { CHECK(isfinite(buf[i])); if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]); }
    }
    CHECK(peak <= PC_SFX_CLAMP * 1.0001f);
    CHECK(!pc_sfx_active(&s));
    pc_sfx_init(&s, &B);                         /* the newest ids for the dump */
    for (int k = 0; k < 11; k++) pc_sfx_start(&s, k % 3);
    CHECK_EQ_INT(s.started, 11);
    CHECK_EQ_INT(s.last[(s.started - 1) % PC_SFX_LAST], 10 % 3);
    CHECK_EQ_INT(s.last[(s.started - 2) % PC_SFX_LAST], 9 % 3);
}

/* a jingle at the maximum (four tracks at level 1, volume 15, loud waves): the parser scales it to the peak; with every bank entry sounding on top,
 * at every rate, before and after the gain, nothing goes beyond the clamp (times the gain) */
static const char MAXJ[] =
    "@credit original test\n"
    "@entry maxj game 測 Max original tempo=240\n"
    "pulse duty=50 lvl=1 | v15 o5 l8 c e g >c e g >c\n"
    "pulse duty=25 lvl=1 | v15 o4 l8 g >c e g >c e g\n"
    "ay lvl=1 env=14 envhz=40 | v15 o3 l4 c g c g\n"
    "aymix lvl=1 nhz=8000 | v15 o2 l2 c c\n";
static void test_max_level_jingle(void)
{
    CHECK_EQ_INT(parse(&T, MAXJ), 1);
    const pc_sfx_entry_t *e = pc_sfx_entry(&T, 0);
    float sum = 0;
    for (int k = 0; k < e->ntr; k++) sum += e->tr[k].level;
    CHECK_NEAR(sum, 1.0, 1e-4);                  /* scaled from 4 to 1 */
    const float gains[] = { 1.0f, 0.5f, 0.05f };
    for (int r = 0; r < NRATES; r++)
        for (unsigned gi = 0; gi < sizeof gains / sizeof gains[0]; gi++) {
            pc_sfx_t a, b;
            pc_sfx_init(&a, &T);
            pc_sfx_start(&a, 0);
            pc_sfx_init(&b, &B);
            for (int id = 0; id < pc_sfx_count(&B); id++) pc_sfx_start(&b, id);
            int frames = (int)(RATES[r] / 100);
            float pa = 0, pb = 0;
            for (int blk = 0; blk < 120; blk++) {
                memset(buf, 0, sizeof buf);
                memset(buf2, 0, sizeof buf2);
                pc_sfx_mix_gain(&a, buf, frames, RATES[r], gains[gi]);
                pc_sfx_mix_gain(&b, buf2, frames, RATES[r], gains[gi]);
                for (int i = 0; i < 2 * frames; i++) { if (fabsf(buf[i]) > pa) pa = fabsf(buf[i]); if (fabsf(buf2[i]) > pb) pb = fabsf(buf2[i]); }
                if (blk % 7 == 0) pc_sfx_start(&b, blk % pc_sfx_count(&B));   /* more on top */
            }
            CHECK(pa <= gains[gi] * PC_SFX_PEAK * 1.0001f);
            CHECK(pa >= gains[gi] * PC_SFX_PEAK * 0.3f);
            CHECK(pb <= gains[gi] * PC_SFX_CLAMP * 1.0001f);
        }
}

/* m3: mixed into music the toy sound never pushes a sample beyond full scale, nor a louder-than-full-scale music sample further out */
static void test_ceiling(void)
{
    pc_sfx_t s;
    const float music[] = { 0.95f, -0.95f, 1.0f, -1.0f, 1.2f, -1.2f, 0.0f };
    for (unsigned m = 0; m < sizeof music / sizeof music[0]; m++) {
        pc_sfx_init(&s, &B);
        pc_sfx_start(&s, pc_sfx_id(&B, "win"));
        bool hit = false;
        for (int b = 0; b < 50; b++) {
            for (int i = 0; i < 2 * 480; i++) buf[i] = music[m];
            pc_sfx_mix(&s, buf, 480, 48000);
            float lim = fabsf(music[m]) > PC_SFX_CEIL ? fabsf(music[m]) : PC_SFX_CEIL;
            for (int i = 0; i < 2 * 480; i++) {
                CHECK(fabsf(buf[i]) <= lim);
                if (buf[i] != music[m]) hit = true;
            }
        }
        CHECK(hit);                              /* the sound is still there where there is room (towards zero at least) */
    }
}

/* where a block's toy sound goes: not paused = before the gain (pre, gain 1: the block's own gain and sleep fade apply); paused (the block's gain
 * target is 0) = after the gain, the wired device-volume gain times the sleep fade (m1), never above the device gain, never unity on its own */
static void test_route(void)
{
    const float dev[] = { 0.0f, 0.001f, 0.25f, 0.5f, 0.999f, 1.0f };
    for (unsigned i = 0; i < sizeof dev / sizeof dev[0]; i++) {
        pc_sfx_route_t a = pc_sfx_route(false, dev[i], 1.0f);
        CHECK(!a.post);
        CHECK(a.gain == 1.0f);
        CHECK(pc_sfx_route(false, dev[i], 0.3f).gain == 1.0f);   /* not paused: the gain loop applies the fade itself */
        pc_sfx_route_t b = pc_sfx_route(true, dev[i], 1.0f);
        CHECK(b.post);
        CHECK(b.gain == dev[i]);                     /* exactly the device gain */
        pc_sfx_route_t c = pc_sfx_route(true, dev[i], 0.25f);
        CHECK(c.post);
        CHECK_NEAR(c.gain, dev[i] * 0.25f, 1e-7);    /* the sleep fade while paused */
        CHECK(c.gain <= dev[i]);
        CHECK(pc_sfx_route(true, dev[i], 0.0f).gain == 0.0f);
        CHECK(pc_sfx_route(true, dev[i], NAN).gain == 0.0f);
        CHECK(pc_sfx_route(true, dev[i], 1.7f).gain == dev[i]);   /* a fade above 1 never raises it */
    }
    CHECK(pc_sfx_route(true, 1.5f, 1.0f).gain == 1.0f);   /* out of range: never above unity */
    CHECK(pc_sfx_route(true, -0.5f, 1.0f).gain == 0.0f);
    CHECK(pc_sfx_route(true, NAN, 1.0f).gain == 0.0f);
}

/* the post-gain mix is the same sound scaled by g; with nothing sounding a block stays exactly as it was (a paused block: all zero) */
static void test_mix_gain(void)
{
    pc_sfx_t s, s2;
    pc_sfx_init(&s, &B);
    memset(buf, 0, sizeof buf);
    CHECK(!pc_sfx_mix_gain(&s, buf, 480, 48000, 0.3f));   /* paused, no toy sound */
    for (int i = 0; i < 2 * 480; i++) CHECK(buf[i] == 0.0f);
    pc_sfx_start(&s, pc_sfx_id(&B, "level_up"));
    pc_sfx_init(&s2, &B);
    pc_sfx_start(&s2, pc_sfx_id(&B, "level_up"));
    for (int b = 0; b < 80; b++) {
        memset(buf, 0, sizeof buf);
        memset(buf2, 0, sizeof buf2);
        pc_sfx_mix_gain(&s, buf, 480, 48000, 0.3f);
        pc_sfx_mix(&s2, buf2, 480, 48000);
        for (int i = 0; i < 2 * 480; i++) { CHECK_NEAR(buf[i], 0.3f * buf2[i], 1e-7); CHECK(fabsf(buf[i]) <= 0.3f * PC_SFX_CLAMP * 1.0001f); }
    }
    memset(buf, 0, sizeof buf);
    pc_sfx_init(&s, &B);
    pc_sfx_start(&s, pc_sfx_id(&B, "pop"));
    pc_sfx_mix_gain(&s, buf, 480, 48000, 0.0f);      /* volume 0: silence, the voice still advances */
    for (int i = 0; i < 2 * 480; i++) CHECK(buf[i] == 0.0f);
}

/* the text format: MML timing and pitch, the native one-shot keys, the ZzFX mapping */
static void test_mml(void)
{
    CHECK_EQ_INT(parse(&T, "@credit original x\n@entry m more 測 M original tempo=120\ntri | o4 a4 b8. r8 >c+16 v7 q4 <<a-2\n"), 1);
    const pc_sfx_entry_t *e = pc_sfx_entry(&T, 0);
    CHECK_EQ_INT(e->ntr, 1);
    CHECK_EQ_INT(e->tr[0].nev, 4);
    const pc_sfx_ev_t *v = &T.ev[e->tr[0].ev0];
    CHECK_NEAR(v[0].hz, 440.0, 0.01);  CHECK_NEAR(v[0].t_on, 0, 1e-3);    CHECK_NEAR(v[0].t_off, 500, 1e-3); CHECK_EQ_INT(v[0].vol, 15);
    CHECK_NEAR(v[1].hz, 493.883, 0.01); CHECK_NEAR(v[1].t_on, 500, 1e-3); CHECK_NEAR(v[1].t_off, 875, 1e-3);
    CHECK_NEAR(v[2].hz, 554.365, 0.01); CHECK_NEAR(v[2].t_on, 1125, 1e-3); CHECK_NEAR(v[2].t_off, 1250, 1e-3);
    CHECK_NEAR(v[3].hz, 207.652, 0.01); CHECK_NEAR(v[3].t_on, 1250, 1e-3); CHECK_NEAR(v[3].t_off, 1250 + 500, 1e-3);   /* q4: half of the half note */
    CHECK_EQ_INT(v[3].vol, 7);
    CHECK_NEAR(e->len_ms, 1750, 1e-3);
    /* native one-shots: f / to / len / del; a slide `to` is octaves per second over the note */
    CHECK_EQ_INT(parse(&T, "@credit original x\n@entry n toy 測 N original\npulse duty=12 f=1000 to=500 len=200 del=50 att=1 dec=199 sus=0 lvl=.5\nnoise mode=short f=4000 len=20\n"), 1);
    e = pc_sfx_entry(&T, 0);
    CHECK_EQ_INT(e->ntr, 2);
    CHECK_EQ_INT(e->tr[0].duty, 2);
    CHECK_NEAR(e->tr[0].oct, -5.0, 1e-4);           /* one octave down in 0.2 s */
    CHECK_NEAR(T.ev[e->tr[0].ev0].t_on, 50, 1e-4);
    CHECK_NEAR(T.ev[e->tr[0].ev0].t_off, 250, 1e-4);
    CHECK_EQ_INT(e->tr[1].wave, PC_SFX_NOISE);
    CHECK_EQ_INT(e->tr[1].sub, PC_SFX_NZ_SHORT);
    CHECK_NEAR(e->len_ms, 250, 1e-4);
    CHECK_NEAR(e->tr[0].level + e->tr[1].level, 1.0, 1e-4);   /* .5 + 1 scaled to 1 */
}

/* ZzFX "Game Over" (README sample, MIT): every parameter on our voices */
static void test_zzfx_mapping(void)
{
    CHECK_EQ_INT(parse(&T, "@credit zzfx x\n@entry go game 測 G zzfx\nzzfx ,,925,.04,.3,.6,1,.3,,6.27,-184,.09,.17\n"), 1);
    const pc_sfx_track_t *t = &pc_sfx_entry(&T, 0)->tr[0];
    CHECK_EQ_INT(t->wave, PC_SFX_PULSE);            /* triangle with shape curve .3: nearly square, remade as the 50 % pulse */
    CHECK_EQ_INT(t->duty, 8);
    CHECK_NEAR(T.ev[t->ev0].hz, 925, 1e-3);
    CHECK_NEAR(t->att, 40, 1e-3);
    CHECK_NEAR(t->rel, 600, 1e-3);
    CHECK_NEAR(t->sus, 1, 1e-6);
    CHECK_NEAR(T.ev[t->ev0].t_off, 940, 1e-3);      /* attack + decay + sustain + release */
    CHECK_NEAR(t->lin, 0, 1e-6);
    CHECK_NEAR(t->acc, 500 * 6.27, 1e-2);           /* ZzFX delta slide: 500 Hz/s^2 a unit */
    CHECK_NEAR(t->jump_hz, -184, 1e-3);
    CHECK_NEAR(t->jump_ms, 90, 1e-3);
    CHECK_NEAR(t->rep_ms, 170, 1e-3);
    CHECK_NEAR(t->level, 1, 1e-6);
    CHECK_EQ_INT(parse(&T, "@credit zzfx x\n@entry h game 測 H zzfx\nzzfx ,,537,.02,.02,.22,1,1.59,-6.98,4.97\n"), 1);   /* "Heart" */
    t = &pc_sfx_entry(&T, 0)->tr[0];
    CHECK_EQ_INT(t->wave, PC_SFX_TRI);
    CHECK_NEAR(t->lin, 500 * -6.98, 1e-2);
    CHECK_NEAR(t->acc, 500 * 4.97, 1e-2);
    CHECK_EQ_INT(parse(&T, "@credit zzfx x\n@wave sine 89ACDEEFFFEEDCA98653211000112356\n@entry s game 測 S zzfx\nzzfx ,0,440,,.1,.1,5,.5\n"), 1);   /* shape 5: square, curve = 2 x duty */
    t = &pc_sfx_entry(&T, 0)->tr[0];
    CHECK_EQ_INT(t->wave, PC_SFX_PULSE);
    CHECK_EQ_INT(t->duty, 4);
}

/* what the voices cannot do is refused at parse time (the entry is dropped, the reason logged); the rest of the bank still loads */
static void test_parser_rejects(void)
{
    static const struct { const char *body, *why; } BAD[] = {
        { "zzfx 1.5,.8,270,,.1,,1,1.5,,,,,,,,.1,.01", "bitcrush" },   /* ZzFX README "Piano": bit crush + delay */
        { "zzfx ,,129,.01,,.15,,,,,,,,5", "noise" },                  /* ZzFX README "Drum": phase noise */
        { "zzfx ,,400,,.1,.1,3", "tan" },
        { "zzfx ,,400,,.1,.1,,,,,,,,,9", "modulation" },
        { "zzfx ,,400,,.1,.1,,,,,,,,,,,,,,.5", "tremolo" },
        { "zzfx ,,400,,.1,.1,,,,,,,,,,,,,,,800", "filter" },
        { "saw f=440 len=100", "wave" },
        { "pulse duty=33 f=440 len=100", "duty" },
        { "pulse f=440 len=100 bogus=1", "key" },
        { "pulse f=440", "len" },
        { "pulse f=440 len=9000", "long" },
        { "pulse f=99999 len=100", "range" },
        { "wave tab=nope f=440 len=100", "table" },
        { "pulse | o4 c4 x4", "mml" },
        { "pulse | o4 c4\npulse | o4 c4\npulse | o4 c4\npulse | o4 c4\npulse | o4 c4", "tracks" },
        { "pulse | o4 l1 c c c c c c c c c c", "long" },
        { "zzfx ,,400,,.1,.1,,,,,,,,,,,,,,,,1", "21" },
        { "pulse arp=0,4,7,12,19 f=440 len=100", "arp" },
        { "zzfx ,,400,,.1,.1,,,,1e37", "delta slide" },                 /* m-b: would make the pitch Inf / NaN */
        { "zzfx ,,400,,.1,.1,,,,,,,1e30", "repeat" },
        { "zzfx ,,400,,.1,.1,,,1e39", "number" },                        /* strtof overflow: Inf, refused as not a number */
        { "zzfx ,,400,,.1,.1,,,nan", "number" },
        { "zzfx 9,,400,,.1,.1", "volume" },
        { "zzfx ,,400,,.1,.1,,,,,1e9", "jump" },
    };
    for (unsigned i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char text[512];
        snprintf(text, sizeof text, "@credit original x\n@credit zzfx y\n@entry good ui 好 Good original\npulse f=440 len=50\n@entry bad ui 壞 Bad zzfx\n%s\n@entry good2 ui 好 Good original\ntri f=220 len=50\n", BAD[i].body);
        int n = parse(&T, text);
        if (n != 2 || !strstr(s_log, BAD[i].why) || !strstr(s_log, "bad")) { fprintf(stderr, "case %u (%s): n=%d log=%s\n", i, BAD[i].why, n, s_log); exit(1); }
        CHECK_EQ_INT(T.dropped, 1);
        CHECK(pc_sfx_id(&T, "bad") < 0);
        CHECK(pc_sfx_id(&T, "good") >= 0 && pc_sfx_id(&T, "good2") >= 0);
    }
    CHECK_EQ_INT(parse(&T, "@credit original x\n@entry a ui 一 A original\npulse f=440 len=50\n@entry a ui 一 A original\npulse f=440 len=50\n"), 1);   /* a name twice */
    CHECK(strstr(s_log, "twice"));
    CHECK_EQ_INT(parse(&T, "@entry a ui 一 A nobody\npulse f=440 len=50\n"), 0);   /* a credit that was never defined */
    CHECK(strstr(s_log, "credit"));
    CHECK_EQ_INT(parse(&T, "@entry a nogroup 一 A original\npulse f=440 len=50\n"), 0);
    CHECK_EQ_INT(parse(&T, ""), 0);
    CHECK_EQ_INT(T.n, 0);
}

/* the pitch each voice kind really plays: zero crossings over 0.5 s of a steady note (noise: its clock and the short mode's 93-step period) */
static int crossings(const float *x, int n) { int c = 0; for (int i = 1; i < n; i++) c += (x[2 * (i - 1)] < 0) != (x[2 * i] < 0); return c; }
static float s_long[2 * 96000];
static void render_text(const char *text, uint32_t rate, int frames, int band)
{
    CHECK_EQ_INT(parse(&T, text), 1);
    pc_sfx_t s;
    pc_sfx_init(&s, &T);
    s.band = (uint8_t)band;
    pc_sfx_start(&s, 0);
    memset(s_long, 0, sizeof s_long);
    for (int o = 0; o < frames; o += (int)(rate / 100)) pc_sfx_mix(&s, s_long + 2 * o, (int)(rate / 100), rate);
}
static void test_voices_pitch(void)
{
    static const char *const W[] = { "pulse duty=50", "pulse duty=12", "tri", "wave tab=saw", "ay", "wave tab=sine" };
    for (unsigned w = 0; w < sizeof W / sizeof W[0]; w++) {
        char t[256];
        snprintf(t, sizeof t, "@credit original x\n@wave sine 89ACDEEFFFEEDCA98653211000112356\n@wave saw 00112233445566778899AABBCCDDEEFF\n"
                              "@entry p more 測 P original\n%s f=1000 len=1000\n", W[w]);
        render_text(t, 48000, 48000, PC_SFX_BAND_DEFAULT);
        int c = crossings(s_long + 2 * 4800, 24000);   /* 0.1 .. 0.6 s */
        if (c < 990 || c > 1010) { fprintf(stderr, "%s: %d crossings in 0.5 s\n", W[w], c); exit(1); }
    }
    /* vibrato and arpeggio change the pitch (and come back), the exponential slide doubles it in a second */
    render_text("@credit original x\n@entry p more 測 P original\npulse duty=50 f=500 oct=1 len=1000\n", 48000, 48000, PC_SFX_BAND_DEFAULT);
    int c0 = crossings(s_long, 4800), c1 = crossings(s_long + 2 * 43200, 4800);   /* first and last 0.1 s */
    CHECK(c0 >= 98 && c0 <= 112);
    CHECK(c1 >= 185 && c1 <= 205);
    render_text("@credit original x\n@entry p more 測 P original\npulse duty=50 f=500 arp=0,12 arpms=50 len=1000\n", 48000, 48000, PC_SFX_BAND_DEFAULT);
    int ca = crossings(s_long + 2 * 4800, 24000);   /* half the time an octave up: 1.5 x */
    CHECK(ca >= 730 && ca <= 770);
    /* the NES short-mode noise repeats every 93 clocks */
    render_text("@credit original x\n@entry p more 測 P original\nnoise mode=short f=4800 len=1000\n", 48000, 48000, PC_SFX_BAND_DEFAULT);
    int per = 93 * 10, same = 0;                     /* 10 samples a clock */
    for (int i = 4800; i < 4800 + 2000; i++) same += (s_long[2 * i] > 0) == (s_long[2 * (i + per)] > 0);
    CHECK(same >= 1950);
    /* the AY envelope (shape 8, a falling saw at 50 Hz) on a steady tone: the level comes back every 20 ms */
    render_text("@credit original x\n@entry p more 測 P original\nay env=8 envhz=50 f=2000 len=1000\n", 48000, 48000, PC_SFX_BAND_DEFAULT);
    float a0 = 0, a1 = 0, b0 = 0;
    for (int i = 9600; i < 9600 + 48; i++) a0 = fmaxf(a0, fabsf(s_long[2 * i]));            /* the start of a cycle: loud */
    for (int i = 9600 + 860; i < 9600 + 908; i++) b0 = fmaxf(b0, fabsf(s_long[2 * i]));     /* its end: quiet */
    for (int i = 9600 + 960; i < 9600 + 1008; i++) a1 = fmaxf(a1, fabsf(s_long[2 * i]));    /* the next start */
    CHECK(a0 > 4 * b0 && a1 > 4 * b0);
    CHECK_NEAR(a0, a1, a0 * 0.2);
}

/* review m-a: above 48 kHz the synth runs at ceil(rate / 48000) times less (88.2k and 176.4k too) and the pitch stays right */
static void test_high_rates_pitch(void)
{
    const uint32_t R[] = { 88200, 96000, 176400, 192000 };
    for (unsigned k = 0; k < sizeof R / sizeof R[0]; k++) {
        render_text("@credit original x\n@entry p more 測 P original\npulse duty=50 f=1000 len=1000\n", R[k], (int)(R[k] / 2), PC_SFX_BAND_DEFAULT);
        int c = crossings(s_long + 2 * (R[k] / 10), (int)(R[k] / 4));   /* 0.1 .. 0.35 s: 250 periods */
        if (c < 495 || c > 505) { fprintf(stderr, "%u: %d crossings in 0.25 s\n", R[k], c); exit(1); }
    }
}

/* review m-c: a GB wave table with big values next to its wrap, high notes: the band-limited voice stays within +-1, the entry within its peak */
static void test_wave_bounded(void)
{
    static const char *const TAB[] = { "8BDEEDB98778889A9876556778777532", "00112233445566778899AABBCCDDEEFF", "FFFFFFFFFFFFFFFF0000000000000000", "0F0F0F0F0F0F0F0F0F0F0F0F0F0F0F0F" };
    for (unsigned k = 0; k < sizeof TAB / sizeof TAB[0]; k++)
        for (int f = 1000; f <= 12000; f += 1375) {
            char t[256];
            snprintf(t, sizeof t, "@credit original x\n@wave w %s\n@entry p more 測 P original\nwave tab=w f=%d len=300\n", TAB[k], f);
            render_text(t, 44100, 44100 / 4, PC_SFX_BAND_DEFAULT);
            for (int i = 0; i < 44100 / 4; i++) if (fabsf(s_long[2 * i]) > PC_SFX_PEAK * 1.0001f) { fprintf(stderr, "tab %u f %d: %g\n", k, f, s_long[2 * i]); exit(1); }
        }
}

/* band-limiting: the energy that is not at a harmonic of the note (aliasing), against the harmonics; a 2410 Hz note at 44.1 kHz, 0.1 s steady
 * (241 whole periods: the harmonics fall on exact 10 Hz bins). Printed for the report; the polyBLEP must cut the pulse's aliasing clearly. */
static double alias_db(const float *x, int n, double f, double fs)
{
    static double cs[4410], sn[4410];
    for (int i = 0; i < n; i++) { cs[i] = cos(2 * M_PI * i / n); sn[i] = sin(2 * M_PI * i / n); }
    double harm = 0, other = 0;
    int hb = (int)lround(f * n / fs);
    for (int k = 1; k < n / 2; k++) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) { int j = (int)(((long)k * i) % n); re += x[2 * i] * cs[j]; im -= x[2 * i] * sn[j]; }
        double p = re * re + im * im;
        if (k % hb == 0) harm += p; else other += p;
    }
    return 10 * log10(other / harm);
}
static void test_bandlimit(void)
{
    static const char *const W[] = { "pulse duty=50", "pulse duty=25", "pulse duty=12", "tri", "wave tab=saw" };
    static const char *const BN[] = { "naive", "blep", "blep+lp" };
    double db[5][3];
    printf("sfx alias (energy off the harmonics, dB under them), 2410 Hz at 44.1 kHz:\n");
    for (unsigned w = 0; w < sizeof W / sizeof W[0]; w++) {
        printf("  %-14s", W[w]);
        for (int b = 0; b < 3; b++) {
            char t[256];
            snprintf(t, sizeof t, "@credit original x\n@wave saw 00112233445566778899AABBCCDDEEFF\n@entry p more 測 P original\n%s f=2410 len=1000\n", W[w]);
            render_text(t, 44100, 44100 / 2, b);
            db[w][b] = alias_db(s_long + 2 * 4410, 4410, 2410, 44100);
            printf(" %s %6.1f", BN[b], db[w][b]);
        }
        printf("\n");
    }
    for (int w = 0; w < 5; w++) if (w != 3) CHECK(db[w][PC_SFX_BAND_BLEP] < db[w][PC_SFX_BAND_NAIVE] - 6.0);   /* the pulses and the saw table's wrap: at least 6 dB less aliasing */
}

/* the worst case a block can cost: PC_SFX_VOICES tracks of the heaviest kind (pulse with the polyBLEP, the AY envelope, vibrato and arpeggio) at each
 * rate, blocks of rate / 100 frames; printed as the report's CPU table (host) */
static const char HEAVY[] =
    "@credit original x\n@wave saw 00112233445566778899AABBCCDDEEFF\n@entry h more 測 H original\n"
    "pulse duty=25 vib=.02 vibhz=6 arp=0,4,7 arpms=30 env=14 envhz=30 f=1500 len=3000\n"
    "ay vib=.02 vibhz=5 arp=0,3,7 arpms=40 env=10 envhz=20 f=900 len=3000\n"
    "pulse duty=12 vib=.03 vibhz=7 arp=0,5 arpms=20 env=12 envhz=25 f=2200 len=3000\n"
    "aymix nhz=6000 vib=.02 vibhz=4 env=8 envhz=15 f=700 len=3000\n";
static void test_cost(void)
{
    CHECK_EQ_INT(parse(&T, HEAVY), 1);
    printf("sfx cost (host, %d voices busy, worst kind), us per audio-task block (pc_block_frames: frames in brackets):", PC_SFX_VOICES);
    for (int r = 0; r < NRATES; r++) {
        int frames = pc_block_frames(RATES[r]);   /* the real block of the audio task (10 ms, at most PC_AUDIO_MAX_BLOCK) */
        double ns = 0;
        int n = 0;
        for (int rep = 0; rep < 60; rep++) {
            pc_sfx_t s;
            pc_sfx_init(&s, &T);
            pc_sfx_start(&s, 0);
            for (int w = 0; w < 3; w++) { memset(buf, 0, sizeof buf); pc_sfx_mix(&s, buf, frames, RATES[r]); }   /* warm: every voice sounding */
            struct timespec a, b;
            memset(buf, 0, sizeof buf);
            clock_gettime(CLOCK_MONOTONIC, &a);
            pc_sfx_mix(&s, buf, frames, RATES[r]);
            clock_gettime(CLOCK_MONOTONIC, &b);
            ns += (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
            n++;
            int busy = 0;
            for (int v = 0; v < PC_SFX_VOICES; v++) busy += s.v[v].tr != NULL;
            CHECK_EQ_INT(busy, PC_SFX_VOICES);
        }
        printf(" %u [%d]: %.1f", RATES[r], frames, ns / n / 1000.0);
    }
    printf("\nsfx state: pc_sfx_t %zu bytes, pc_sfx_q_t %zu bytes, pc_sfx_bank_t %zu bytes\n", sizeof(pc_sfx_t), sizeof(pc_sfx_q_t), sizeof(pc_sfx_bank_t));
    CHECK(sizeof(pc_sfx_t) + sizeof(pc_sfx_q_t) <= 1024);
    CHECK(sizeof(pc_sfx_bank_t) <= 48 * 1024);
}

static void test_queue(void)
{
    pc_sfx_q_t q;
    uint32_t stale = 0;
    pc_sfx_q_init(&q);
    CHECK_EQ_INT(pc_sfx_q_take(&q, 0, &stale), -1);
    for (int i = 0; i < PC_SFX_QLEN; i++) CHECK(pc_sfx_q_post(&q, (uint8_t)i, 1000));
    CHECK(!pc_sfx_q_post(&q, 99, 1000));          /* full: dropped, the queued ones stay */
    for (int i = 0; i < PC_SFX_QLEN; i++) CHECK_EQ_INT(pc_sfx_q_take(&q, 1010, &stale), i);
    CHECK_EQ_INT(pc_sfx_q_take(&q, 1010, &stale), -1);
    CHECK_EQ_INT(stale, 0);
    for (int i = 0; i < 1000; i++) { CHECK(pc_sfx_q_post(&q, (uint8_t)(i & 7), (uint32_t)i)); CHECK_EQ_INT(pc_sfx_q_take(&q, (uint32_t)i + 5, &stale), i & 7); }   /* wraps */
}

/* m5: a held-up consumer drops what is too old instead of starting it late, all in one block */
static void test_queue_stale(void)
{
    pc_sfx_q_t q;
    uint32_t stale = 0;
    pc_sfx_q_init(&q);
    for (int i = 0; i < 6; i++) CHECK(pc_sfx_q_post(&q, (uint8_t)i, 1000 + (uint32_t)i * 10));   /* posted at 1000 .. 1050 */
    CHECK_EQ_INT(pc_sfx_q_take(&q, 1000 + PC_SFX_STALE_MS + 25, &stale), 3);   /* 0, 1, 2 are older than the limit */
    CHECK_EQ_INT(stale, 3);
    CHECK_EQ_INT(pc_sfx_q_take(&q, 1000 + PC_SFX_STALE_MS + 25, &stale), 4);
    CHECK_EQ_INT(pc_sfx_q_take(&q, 5000, &stale), -1);                         /* 5 is stale too: dropped, the queue is empty */
    CHECK_EQ_INT(stale, 4);
    CHECK(pc_sfx_q_post(&q, 1, 0xFFFFFFF0u));                                  /* the millisecond clock wraps */
    CHECK_EQ_INT(pc_sfx_q_take(&q, 0x10u, &stale), 1);
    CHECK(pc_sfx_q_post(&q, 2, 2001));                                         /* review I1: posted 1 ms after the consumer read its clock (other core) */
    CHECK_EQ_INT(pc_sfx_q_take(&q, 2000, &stale), 2);                          /* fresh: taken, not counted stale */
    CHECK(pc_sfx_q_post(&q, 3, 0u));                                           /* the same across the wrap */
    CHECK_EQ_INT(pc_sfx_q_take(&q, 0xFFFFFFFFu, &stale), 3);
    CHECK_EQ_INT(stale, 4);
}

#define SPSC_N 200000
static pc_sfx_q_t s_q;
static void *producer(void *a)
{
    (void)a;
    for (uint32_t i = 0; i < SPSC_N;) if (pc_sfx_q_post(&s_q, (uint8_t)(i * 7), 7)) i++;
    return NULL;
}
static void test_queue_two_threads(void)        /* one producer thread, one consumer: every id arrives once, in order */
{
    uint32_t stale = 0;
    pc_sfx_q_init(&s_q);
    pthread_t t;
    CHECK(pthread_create(&t, NULL, producer, NULL) == 0);
    for (uint32_t i = 0; i < SPSC_N;) { int v = pc_sfx_q_take(&s_q, 7, &stale); if (v < 0) continue; CHECK_EQ_INT(v, (uint8_t)(i * 7)); i++; }
    pthread_join(t, NULL);
    CHECK_EQ_INT(pc_sfx_q_take(&s_q, 7, &stale), -1);
    CHECK_EQ_INT(stale, 0);
}

int main(void)
{
    RUN(test_bank_parses);
    RUN(test_names);
    RUN(test_entries_render);
    RUN(test_deterministic);
    RUN(test_adds_to_music);
    RUN(test_overlap_clamp_and_steal);
    RUN(test_max_level_jingle);
    RUN(test_ceiling);
    RUN(test_route);
    RUN(test_mix_gain);
    RUN(test_mml);
    RUN(test_zzfx_mapping);
    RUN(test_parser_rejects);
    RUN(test_voices_pitch);
    RUN(test_high_rates_pitch);
    RUN(test_wave_bounded);
    RUN(test_bandlimit);
    RUN(test_cost);
    RUN(test_queue);
    RUN(test_queue_stale);
    RUN(test_queue_two_threads);
    return 0;
}
