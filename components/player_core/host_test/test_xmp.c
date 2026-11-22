// SPDX-License-Identifier: LGPL-2.1-or-later
/* Tracker modules (libxmp): playback to the end, resume token, module titles via pc_id3_parse. */
#include <math.h>
#include "pc/pc_backend.h"
#include "pc/pc_id3.h"
#include "pc_test.h"
#include "pc/pc_vmute.h"
#include "pc/pc_chipseq.h"

static uint8_t *load(const char *name, uint32_t *len)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}

typedef struct { long frames; double rms; float peak; int zc; } stats_t;

/* renders until the backend ends (bounded); records the resume token halfway through `at_frame` */
static stats_t drain(void *h, long at_frame, uint32_t *token)
{
    float buf[2 * 300], prev = 0;
    stats_t s = { 0 };
    double sq = 0;
    for (;;) {
        int n = pc_backend_xmp.render(h, buf, 300);
        CHECK(n >= 0);
        if (n == 0) break;
        for (int i = 0; i < 2 * n; i++) {
            float v = buf[i];
            CHECK(v >= -1.0f && v <= 1.0f);
            sq += (double)v * v;
            if (fabsf(v) > s.peak) s.peak = fabsf(v);
        }
        for (int i = 0; i < n; i++) {
            float l = buf[2 * i] + buf[2 * i + 1];
            if ((prev < 0) != (l < 0)) s.zc++;
            prev = l;
        }
        if (token && s.frames < at_frame && s.frames + n >= at_frame) *token = pc_backend_xmp.byte_pos(h);
        s.frames += n;
        CHECK(s.frames < 60L * 44100);                  /* it must end: played once, not looped */
    }
    s.rms = s.frames ? sqrt(sq / (2.0 * s.frames)) : 0;
    return s;
}

static void test_mod_plays_once_to_the_end(void)
{
    uint32_t len, token = 0;
    uint8_t *d = load("tiny.mod", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    uint32_t rate = pc_backend_xmp.rate(h);
    CHECK(rate == 44100 || rate == 48000);
    CHECK_NEAR(pc_backend_xmp.length_ms(h), 3840, 50);   /* 2 orders x 16 rows x 6 ticks x 20 ms */
    CHECK_EQ_INT(pc_backend_xmp.byte_pos(h), 0);
    stats_t s = drain(h, (long)rate * 25 / 10, &token);  /* 2.5 s: inside the second order */
    CHECK_NEAR((double)s.frames / rate, 3.84, 0.05);
    CHECK(s.rms > 0.02);                                  /* not silent */
    CHECK(s.peak < 0.99f);                                /* not clipped */
    CHECK(s.zc > 300);                                    /* a tone (~260-520 Hz), not DC */
    CHECK_NEAR(pc_backend_xmp.position_ms(h), 3840, 50);
    CHECK_EQ_INT(pc_backend_xmp.render(h, (float[600]){ 0 }, 300), 0);   /* stays ended */
    pc_backend_xmp.close(h);
    CHECK_EQ_INT(token, 1);                               /* resume token = order index */
    CHECK(token < len);                                   /* mp3_ctl drops tokens >= the file size */

    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, token), 0); /* resume: the second order only */
    CHECK_NEAR(pc_backend_xmp.position_ms(h), 1920, 50);
    s = drain(h, 0, NULL);
    CHECK_NEAR((double)s.frames / rate, 1.92, 0.05);
    CHECK(s.rms > 0.02);
    pc_backend_xmp.close(h);

    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 99), 0);    /* a stale token past the song: from the start */
    CHECK_NEAR(pc_backend_xmp.position_ms(h), 0, 1);
    pc_backend_xmp.close(h);
    free(d);
}

static void test_xm_plays(void)
{
    uint32_t len;
    uint8_t *d = load("tiny.xm", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    CHECK_NEAR(pc_backend_xmp.length_ms(h), 1920, 50);
    stats_t s = drain(h, 0, NULL);
    CHECK_NEAR((double)s.frames / pc_backend_xmp.rate(h), 1.92, 0.05);
    CHECK(s.rms > 0.02 && s.peak < 0.99f && s.zc > 150);
    pc_backend_xmp.close(h);
    free(d);
}

/* voice levels: n = module channels (at most 16), names "1".."n", a note makes its channel jump, they all rest at 0 after close;
 * a module opened for gapless while another plays must not take the snapshot before it renders */
static void test_voice_levels(void)
{
    uint32_t len;
    uint8_t *d = load("tiny.mod", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h, *h2;
    pc_voices_t v;
    CHECK_EQ_INT(pc_backend_xmp_voices(&v), 0);          /* nothing open */
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_xmp_voices(&v), 0);          /* opened but not rendered: not the playing one yet */
    float buf[2 * 441];
    int seen[PC_VOICES_MAX] = { 0 }, n = 0, rises[PC_VOICES_MAX] = { 0 }, falls[PC_VOICES_MAX] = { 0 };
    uint8_t prev[PC_VOICES_MAX] = { 0 };
    for (int blocks = 0; blocks < 800; blocks++) {
        if (pc_backend_xmp.render(h, buf, 441) <= 0) break;
        n = pc_backend_xmp_voices(&v);
        CHECK(n >= 1 && n <= PC_VOICES_MAX);
        for (int i = 0; i < n; i++) if (v.level[i] > seen[i]) seen[i] = v.level[i];
        for (int i = 0; i < n; i++) {
            if (v.level[i] > prev[i]) rises[i]++;
            if (v.level[i] < prev[i]) falls[i]++;
            prev[i] = v.level[i];
        }
        if (blocks == 3) {                               /* a second module opened (gapless pre-open): the snapshot stays ours */
            pc_reader_t r2;
            pc_reader_mem(&r2, d, len);
            CHECK_EQ_INT(pc_backend_xmp.open(&h2, &r2, 0), 0);
            CHECK_EQ_INT(pc_backend_xmp_voices(&v), n);
            pc_backend_xmp.close(h2);                    /* closing a handle that does not own it leaves it */
            CHECK_EQ_INT(pc_backend_xmp_voices(&v), n);
        }
    }
    CHECK_EQ_INT(n, 4);                                  /* a 4 channel MOD */
    CHECK_STR(v.name[0], "1"); CHECK_STR(v.name[3], "4");
    int moved = 0;
    for (int i = 0; i < n; i++) if (seen[i] > 0) moved++;
    CHECK_EQ_INT(moved, 1);                              /* the notes jumped: tiny.mod plays channel 1 only */
    CHECK(falls[0] > 0);                                 /* and fell */
    for (int i = 1; i < n; i++) CHECK_EQ_INT(rises[i] + falls[i], 0);   /* the channels without a note rest at 0 the whole song */
    /* 8 note rows (4 per order, one per 24 frames); a note makes ONE jump, it does not re-trigger on every frame of its row
     * (6 frames, 12 blocks at 10 ms would show about 40 rises) */
    CHECK(rises[0] >= 4 && rises[0] <= 12);
    CHECK(v.strikes[0] >= 4 && v.strikes[0] <= 12);       /* one key-on per note row of channel 1, none on the rest */
    CHECK_EQ_INT(v.strikes[1], 0);
    CHECK(v.flags[0] & PC_VF_PITCH);
    CHECK(v.cents[0] >= 1200 && v.cents[0] < 12800);      /* a real note */
    CHECK(v.pan[0] >= -127 && v.pan[0] <= 127 && v.pan[0] != PC_PAN_UNKNOWN);
    pc_backend_xmp.close(h);
    CHECK_EQ_INT(pc_backend_xmp_voices(&v), 0);
    CHECK_EQ_INT(v.n, 0);
    free(d);
}

/* the mute mask posted by the UI reaches libxmp: a muted channel renders silence and its meter reads 0 */
static void test_mute(void)
{
    uint32_t len;
    uint8_t *d = load("tiny.mod", &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    float buf[2 * 441];
    pc_voices_t v;
    float pk = 0, pk_muted = 0;
    for (int b = 0; b < 40; b++) { CHECK(pc_backend_xmp.render(h, buf, 441) > 0); for (int i = 0; i < 882; i++) if (fabsf(buf[i]) > pk) pk = fabsf(buf[i]); }
    CHECK(pk > 0.01f);
    pc_vmute_sync();
    pc_vmute_set(0, PC_VM_MUTED, 4);
    for (int b = 0; b < 100; b++) {
        if (pc_backend_xmp.render(h, buf, 441) <= 0) break;
        if (b >= 5) for (int i = 0; i < 882; i++) if (fabsf(buf[i]) > pk_muted) pk_muted = fabsf(buf[i]);
        pc_backend_xmp_voices(&v);
        if (b >= 5) CHECK_EQ_INT(v.level[0], 0);
    }
    CHECK_NEAR(pk_muted, 0.0, 1e-4);                     /* tiny.mod plays channel 1 only: muted, nothing is left */
    pc_backend_xmp.close(h);
    free(d);
}

/* wave-6 review #21 (FIX7): a module with more channels than the 32 voice rows. The voices clamp to 32; a solo must also silence the channels
 * past the 32 mask bits (the note here is on channel 35). Built in memory: an XM with `chn` channels, one looped square-wave sample. */
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v); put16(p + 2, v >> 16); }
static uint8_t *make_xm(int chn, int note_ch, uint32_t *len)
{
    const int rows = 16, plen = rows * chn * 5, ilen = 263, smp = 32;
    const size_t n = 336 + 9 + 2 + 2 + (size_t)plen + ilen + 40 + smp;
    uint8_t *d = calloc(1, n);
    memcpy(d, "Extended Module:", 16); d[16] = 0x20;
    memcpy(d + 17, "jc wide xm", 10);
    d[37] = 0x1a;
    put16(d + 58, 0x0104);
    put32(d + 60, 276);
    put16(d + 64, 1);                                    /* song length */
    put16(d + 68, (unsigned)chn);
    put16(d + 70, 1);                                    /* patterns */
    put16(d + 72, 1);                                    /* instruments */
    put16(d + 74, 1);                                    /* linear frequencies */
    put16(d + 76, 6);
    put16(d + 78, 125);
    uint8_t *p = d + 336;
    put32(p, 9); put16(p + 5, (unsigned)rows); put16(p + 7, (unsigned)plen);
    p += 9;
    memset(p, 0, (size_t)plen);                          /* unpacked cells: note, instrument, volume, effect, parameter */
    p[note_ch * 5] = 49; p[note_ch * 5 + 1] = 1;
    p += plen;
    put32(p, (unsigned)ilen); put16(p + 27, 1); put32(p + 29, 40);
    p[225] = p[226] = 0;
    p += ilen;
    put32(p, smp); put32(p + 4, 0); put32(p + 8, smp);   /* length, loop start, loop length */
    p[12] = 64; p[14] = 1; p[15] = 128;                  /* volume, forward loop, pan */
    p += 40;
    int prev = 0;
    for (int i = 0; i < smp; i++) { int v = (i & 1) ? -100 : 100; p[i] = (uint8_t)(v - prev); prev = v; }
    *len = (uint32_t)n;
    return d;
}
static float wide_peak(void *h, int blocks)
{
    float buf[2 * 441], pk = 0;
    for (int b = 0; b < blocks; b++) {
        CHECK(pc_backend_xmp.render(h, buf, 441) > 0);
        if (b >= 4) for (int i = 0; i < 882; i++) if (fabsf(buf[i]) > pk) pk = fabsf(buf[i]);
    }
    return pk;
}
static void test_wide_module(void)
{
    static const int chans[] = { 33, 40, 64 };
    for (size_t k = 0; k < sizeof chans / sizeof *chans; k++) {
        uint32_t len;
        uint8_t *d = make_xm(chans[k], chans[k] - 2, &len);        /* the note is past voice 32 */
        pc_reader_t r;
        pc_reader_mem(&r, d, len);
        void *h;
        CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
        pc_voices_t v;
        CHECK(wide_peak(h, 20) > 0.01f);                          /* the note sounds */
        CHECK_EQ_INT(pc_backend_xmp_voices(&v), PC_VOICES_MAX);   /* clamped to 32 rows */
        pc_vmute_sync();
        pc_vmute_set(0, PC_VM_SOLO, PC_VOICES_MAX);               /* solo: voice 0 only, so channels 33+ go quiet too */
        CHECK(pc_vmute_extra_muted());
        CHECK_NEAR(wide_peak(h, 12), 0.0, 1e-4);
        pc_vmute_clear(PC_VOICES_MAX);
        CHECK(!pc_vmute_extra_muted());
        CHECK(wide_peak(h, 12) > 0.01f);                          /* and back */
        pc_vmute_set(5, PC_VM_MUTED, PC_VOICES_MAX);              /* a plain mute of another voice leaves the wide channels alone */
        CHECK(!pc_vmute_extra_muted());
        CHECK(wide_peak(h, 12) > 0.01f);
        pc_backend_xmp.close(h);
        free(d);
    }
}

/* The two small fixtures render the same as libxmp-lite 4.7.2 did (hashes taken from the lite build, tools/bench/xmp_fuzz/compare_lite.sh renders any file with both).
 * LIBXMP_LITE_COMPAT is OFF since 2026-11-18 (owner: libxmp's own accurate playback is wanted, Paula stays off): of the 17 real tracker files tried, exactly two
 * now differ from lite (r-modules "Beyond The Network.it" and "amigadisco1993r.mod": the per-tracker IT flow mode / the Amiga loop details); these two
 * fixtures use none of the changed behaviour, so they stay the guard that the swap did not break the plain MOD / XM path. FNV-1a over the 16-bit
 * little-endian PCM of the whole song. */
static void test_plain_formats_render_as_before(void)
{
    static const struct { const char *name; long frames; uint32_t fnv; } want[] = {
        { "tiny.mod", 169344, 0x87d0edd1u }, { "tiny.xm", 84672, 0x76ea1a35u } };
    for (size_t k = 0; k < sizeof want / sizeof *want; k++) {
        uint32_t len;
        uint8_t *d = load(want[k].name, &len);
        pc_reader_t r;
        pc_reader_mem(&r, d, len);
        void *h;
        CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
        float buf[2 * 300];
        uint32_t fnv = 0x811c9dc5u;
        long frames = 0;
        for (int n; (n = pc_backend_xmp.render(h, buf, 300)) > 0; frames += n)
            for (int i = 0; i < 2 * n; i++) {
                int v = (int)lrintf(buf[i] * 32768.0f);
                fnv = (fnv ^ (uint32_t)(v & 0xff)) * 16777619u;
                fnv = (fnv ^ (uint32_t)((v >> 8) & 0xff)) * 16777619u;
            }
        CHECK_EQ_INT(frames, want[k].frames);
        CHECK(fnv == want[k].fnv);
        pc_backend_xmp.close(h);
        free(d);
    }
}

/* The Amiga tone: FNV-1a of the whole song's 16-bit PCM under one mode (fresh handle; the setting changes between opens) */
static uint32_t amiga_hash(const char *name, int mode, long *frames_out, int switch_at)
{
    uint32_t len;
    uint8_t *d = load(name, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    pc_backend_xmp_set_amiga(mode);
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    float buf[2 * 300];
    uint32_t fnv = 0x811c9dc5u;
    long frames = 0;
    const bool was_switched = switch_at != 0;
    for (int n; (n = pc_backend_xmp.render(h, buf, 300)) > 0; frames += n) {
        if (switch_at && frames >= switch_at) { pc_backend_xmp_set_amiga(0); switch_at = 0; }   /* live: from the next block */
        for (int i = 0; i < 2 * n; i++) {
            int v = (int)lrintf(buf[i] * 32768.0f);
            CHECK(v >= -32768 && v <= 32767);
            fnv = (fnv ^ (uint32_t)(v & 0xff)) * 16777619u;
            fnv = (fnv ^ (uint32_t)((v >> 8) & 0xff)) * 16777619u;
        }
    }
    if (frames_out) *frames_out = frames;
    CHECK_EQ_INT(pc_backend_xmp_amiga_active(), mode > 0 && mode < PC_AMIGA_N && !strstr(name, ".xm") && !was_switched);
    pc_backend_xmp.close(h);
    free(d);
    pc_backend_xmp_set_amiga(0);
    return fnv;
}

static void test_amiga_tone(void)
{
    long f0, f1, f2, f3, fx;
    const uint32_t off = amiga_hash("tiny.mod", 0, &f0, 0);
    CHECK(off == 0x87d0edd1u);                                  /* off = today's sound, bit for bit */
    const uint32_t a500 = amiga_hash("tiny.mod", PC_AMIGA_A500, &f1, 0);
    const uint32_t led = amiga_hash("tiny.mod", PC_AMIGA_A500LED, &f2, 0);
    const uint32_t a1200 = amiga_hash("tiny.mod", PC_AMIGA_A1200, &f3, 0);
    CHECK(a500 != off && led != off && a1200 != off && a500 != led && a500 != a1200 && led != a1200);   /* each option sounds different */
    CHECK_EQ_INT(f1, f0); CHECK_EQ_INT(f2, f0); CHECK_EQ_INT(f3, f0);                                    /* same length */
    CHECK(amiga_hash("tiny.mod", PC_AMIGA_A500, NULL, 0) == a500);                                         /* deterministic */
    const uint32_t live = amiga_hash("tiny.mod", PC_AMIGA_A500, NULL, 40000);                              /* switched off after ~0.9 s: differs from both */
    CHECK(live != a500 && live != off);
    CHECK(amiga_hash("tiny.xm", PC_AMIGA_A1200, &fx, 0) == 0x76ea1a35u);                                   /* an XM is untouched by every mode */
    pc_backend_xmp_set_amiga(99);                                                                          /* out of range = off */
    CHECK(amiga_hash("tiny.mod", 99, NULL, 0) == off);
}

static void test_extension_table(void)
{
    static const char *const yes[] = { ".mod", ".MOD", ".xm", ".s3m", ".it", ".stm", ".669", ".far", ".mtm", ".ptm", ".okt", ".ult", ".mdl", ".med", ".dbm", ".umx", ".psm", ".stk", ".imf", ".dtm", ".ims" };
    static const char *const no[] = { ".mp3", ".mo3", ".mid", ".mfp", ".smp", ".", "" };
    for (size_t i = 0; i < sizeof yes / sizeof *yes; i++) CHECK(pc_xmp_ext(yes[i]));
    for (size_t i = 0; i < sizeof no / sizeof *no; i++) CHECK(!pc_xmp_ext(no[i]));
    CHECK(!pc_xmp_ext(NULL));
    CHECK(pc_opl_ext(".imf") && pc_opl_ext(".dtm") && pc_opl_ext(".ims"));   /* the three suffixes both families use: both list them (pc_backend_chain tries AdPlug, then libxmp) */
}

/* A PowerPacker (PP20) file wrapping a tiny M.K. module, made by tools/bench/xmp_fuzz/make_seeds.py: loads through the in-memory depacker; with 0xff in the
 * bit stream the old bit buffer shifted a signed int into the sign bit (UBSan, patch 0007) */
static const uint8_t PP20_TINY[] = {
    0x50, 0x50, 0x32, 0x30, 0x09, 0x0a, 0x0c, 0x0d, 0x0c, 0xea, 0x6a, 0x62, 0x60, 0x4b, 0x27, 0x4d,
    0x27, 0x47, 0xcf, 0xc3, 0xce, 0xa6, 0xa6, 0x26, 0x30, 0xff, 0xf3, 0x04, 0x00, 0x02, 0x58, 0x05,
    0x00, 0x17, 0x00, 0xfb, 0xf8, 0x70, 0x03, 0x7f, 0xff, 0xf1, 0xe0, 0x02, 0xe0, 0x1f, 0x7f, 0x0e,
    0x00, 0x6f, 0xff, 0xfe, 0x3c, 0x00, 0x5c, 0x03, 0xef, 0xe1, 0xc0, 0x0d, 0xff, 0xff, 0xc7, 0x80,
    0x0b, 0x80, 0x7d, 0xfc, 0x38, 0x01, 0xbf, 0xff, 0xf8, 0xf0, 0x03, 0x7f, 0xff, 0xf1, 0xe0, 0x06,
    0xff, 0xff, 0xe3, 0xc0, 0x0d, 0xff, 0xff, 0xc7, 0x80, 0x1b, 0xff, 0xff, 0x8f, 0x00, 0x37, 0xff,
    0xff, 0x1e, 0x00, 0x6f, 0xff, 0xfe, 0x3c, 0x00, 0xdf, 0xff, 0xfc, 0x78, 0x01, 0xbf, 0xff, 0xf8,
    0xf0, 0x03, 0x7f, 0xff, 0xf1, 0xe0, 0x06, 0xff, 0xff, 0xe3, 0xc0, 0x0d, 0xff, 0xff, 0xc7, 0x80,
    0x1b, 0xff, 0xff, 0x8f, 0x00, 0x37, 0xff, 0xff, 0x1e, 0x00, 0x6f, 0xff, 0xfe, 0x3c, 0x00, 0xdf,
    0xff, 0xf0, 0x78, 0x01, 0xbf, 0xff, 0xe0, 0xf0, 0x03, 0x7f, 0xff, 0xc1, 0xe0, 0x06, 0xff, 0xff,
    0xe3, 0xc0, 0x0d, 0xff, 0xff, 0xc7, 0x80, 0x1b, 0xff, 0xfe, 0x0f, 0x00, 0x37, 0xff, 0xf3, 0x80,
    0x1b, 0xff, 0xf9, 0xc0, 0x40, 0x7f, 0x2c, 0x01, 0xdf, 0xff, 0xff, 0xc0, 0x3b, 0xff, 0xff, 0xff,
    0xfe, 0x3d, 0x93, 0xa6, 0x93, 0xa4, 0x01, 0xab, 0xbf, 0x0a, 0xf8, 0x5f, 0xff, 0xff, 0xfe, 0x77,
    0xc3, 0xf0, 0xb2, 0x85, 0xff, 0xff, 0xff, 0xe7, 0x94, 0x3f, 0x08, 0x08, 0x1f, 0xff, 0xff, 0xfe,
    0x70, 0x00, 0x53, 0x00, 0x40, 0x5c, 0x1c, 0x06, 0x1b, 0x7f, 0xff, 0x00, 0xef, 0xff, 0xff, 0xe0,
    0x1d, 0xff, 0xff, 0xfc, 0x03, 0xbf, 0xff, 0xff, 0x80, 0x77, 0xff, 0xff, 0xf0, 0x0e, 0xff, 0xff,
    0xfe, 0x01, 0xdf, 0xff, 0xff, 0xc0, 0x3b, 0xff, 0xff, 0xf8, 0x07, 0x7f, 0xff, 0xff, 0x00, 0xef,
    0xff, 0xff, 0xe0, 0x1d, 0xff, 0xff, 0xfc, 0x03, 0xbf, 0xff, 0xff, 0x80, 0x77, 0xff, 0xff, 0xf0,
    0x0e, 0xff, 0xff, 0xfe, 0x01, 0x80, 0x09, 0xff, 0xff, 0xf8, 0x60, 0x62, 0x61, 0x63, 0x60, 0xe2,
    0xe1, 0xe3, 0xe0, 0x02, 0x01, 0x03, 0x00, 0x82, 0x81, 0x83, 0x87, 0xfe, 0x00, 0x08, 0x7c, 0x00
};
static void test_pp20_depacker(void)
{
    pc_reader_t r;
    void *h = NULL;
    pc_reader_mem(&r, PP20_TINY, sizeof PP20_TINY);
    CHECK_EQ_INT(pc_backend_xmp.open(&h, &r, 0), 0);
    float buf[2 * 300];
    CHECK(pc_backend_xmp.render(h, buf, 300) > 0);
    pc_backend_xmp.close(h);
    uint8_t hostile[sizeof PP20_TINY];
    memcpy(hostile, PP20_TINY, sizeof hostile);
    for (size_t i = 16; i < sizeof hostile - 12; i++) hostile[i] = 0xff;
    pc_reader_mem(&r, hostile, sizeof hostile);
    h = NULL;
    if (pc_backend_xmp.open(&h, &r, 0) == 0) pc_backend_xmp.close(h);     /* refused or played: no sanitizer finding */
}

static void test_rejects_garbage(void)
{
    static uint8_t junk[4096];
    for (unsigned i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
    pc_reader_t r;
    pc_reader_mem(&r, junk, sizeof junk);
    void *h = NULL;
    CHECK(pc_backend_xmp.open(&h, &r, 0) != 0);
    uint32_t len;
    uint8_t *d = load("tiny.mod", &len);
    pc_reader_mem(&r, d, 1084 + 100);                    /* truncated in the pattern data */
    CHECK(pc_backend_xmp.open(&h, &r, 0) != 0);
    free(d);
}

static void title_of(const uint8_t *data, uint32_t len, const char *want)
{
    pc_reader_t r;
    pc_id3_t t;
    pc_reader_mem(&r, data, len);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, want);
    CHECK_STR(t.artist, "");
    CHECK_EQ_INT(t.apic_off, 0);
    pc_id3_free(&t);
}

static void test_module_titles(void)
{
    uint32_t len;
    uint8_t *d = load("tiny.mod", &len);
    title_of(d, len, "jc tiny mod");
    free(d);
    d = load("tiny.xm", &len);
    title_of(d, len, "jc tiny xm");
    free(d);

    uint8_t s3m[96] = "Space Debris  ";                 /* S3M: 28-byte title, 0x1A, type 16, "SCRM" at 44 */
    s3m[28] = 0x1a; s3m[29] = 16; memcpy(s3m + 44, "SCRM", 4);
    title_of(s3m, sizeof s3m, "Space Debris");
    uint8_t it[192] = "IMPMBeyond Music\xe9";           /* IT: "IMPM", 26-byte title; CP437/Latin-1 byte -> UTF-8 */
    title_of(it, sizeof it, "Beyond Music\xc3\xa9");
    uint8_t mod[1084] = { 0 };                           /* MOD without a title: empty, the library uses the file name */
    memcpy(mod + 1080, "8CHN", 4);
    title_of(mod, sizeof mod, "");
    uint8_t xm[80];                                      /* blank XM title (spaces), lower-case magic as some trackers write */
    memset(xm, ' ', sizeof xm);
    memcpy(xm, "Extended module: ", 17);
    xm[37] = 0x1a;
    title_of(xm, sizeof xm, "");
}

int main(void)
{
    RUN(test_mod_plays_once_to_the_end);
    RUN(test_xm_plays);
    RUN(test_voice_levels);
    RUN(test_mute);
    RUN(test_wide_module);
    RUN(test_plain_formats_render_as_before);
    RUN(test_amiga_tone);
    RUN(test_extension_table);
    RUN(test_pp20_depacker);
    RUN(test_rejects_garbage);
    RUN(test_module_titles);
    return 0;
}
