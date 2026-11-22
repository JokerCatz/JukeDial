// SPDX-License-Identifier: LGPL-2.1-or-later
/* Atari ST YM backend: files are built in memory (YM3 and YM5/YM6 register dumps), so no tune is committed. Pitch, noise, envelope, the YM6 timer
 * tricks, the loop-frame end, mute, refusals and hostile input (truncations, flips) under ASan; the YM files of the main checkout's .local/chiptest
 * are played too when present (the LHA-packed one must come out identical to its plain twin). */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_id3.h"
#include "pc/pc_lh5.h"
#include "pc/pc_vmute.h"
#include "pc_test.h"

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* regs[frame][16] -> a YM6 file (interleaved), with `ndrums` drums of `dlen` bytes (all 0x80..0xf0 ramps) */
static uint8_t *make_ym6(const uint8_t (*regs)[16], int frames, int ndrums, int dlen, uint32_t *len)
{
    uint32_t n = 34 + (uint32_t)ndrums * (4 + (uint32_t)dlen) + 2 * 5 + 1 + 3 + (uint32_t)frames * 16 + 4;
    uint8_t *f = calloc(1, n);
    memcpy(f, "YM6!LeOnArD!", 12);
    put32(f + 12, (uint32_t)frames); put32(f + 16, 1 | 4); put16(f + 20, (uint32_t)ndrums); put32(f + 22, 2000000); put16(f + 26, 50); put32(f + 28, 0); put16(f + 32, 0);
    uint32_t o = 34;
    for (int d = 0; d < ndrums; d++) {
        put32(f + o, (uint32_t)dlen); o += 4;
        for (int i = 0; i < dlen; i++) f[o++] = (uint8_t)(i % 16);   /* 4-bit samples: the volume register values */
    }
    memcpy(f + o, "Song\0Me\0\0", 9); o += 9;
    for (int r = 0; r < 16; r++) for (int fr = 0; fr < frames; fr++) f[o + (uint32_t)r * (uint32_t)frames + (uint32_t)fr] = regs[fr][r];
    o += (uint32_t)frames * 16;
    memcpy(f + o, "End!", 4); o += 4;
    *len = o;
    return f;
}

static double goertzel(const float *x, int n, double rate, double freq)
{
    double w = 2 * M_PI * freq / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}

static float *play(const uint8_t *d, uint32_t len, int *frames_out)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_ym.open(&h, &r, 0), 0);
    float *all = NULL, out[2 * 441];
    int tot = 0, cap = 0;
    for (;;) {
        int g = pc_backend_ym.render(h, out, 441);
        CHECK(g >= 0);
        if (g == 0) break;
        if (tot + g > cap) { cap = (tot + g) * 2; all = realloc(all, sizeof(float) * (size_t)cap); }
        for (int i = 0; i < g; i++) { CHECK(out[2 * i] == out[2 * i + 1]); all[tot + i] = out[2 * i]; }
        tot += g;
    }
    pc_backend_ym.close(h);
    *frames_out = tot;
    return all;
}

static void test_pitch_and_end(void)
{
    enum { FR = 100 };                                           /* 2 s at 50 Hz */
    static uint8_t regs[FR][16];
    for (int i = 0; i < FR; i++) { regs[i][0] = 284 & 255; regs[i][1] = 284 >> 8; regs[i][7] = 0x3e; regs[i][8] = 15; }
    uint32_t len;
    uint8_t *d = make_ym6(regs, FR, 0, 0, &len);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_ym.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_ym.length_ms(h), 2000);
    CHECK_EQ_INT(pc_backend_ym.rate(h), 44100);
    float out[2 * 441];
    CHECK(pc_backend_ym.render(h, out, 441) > 0);
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 3);
    CHECK(v.flags[0] & PC_VF_PITCH);
    CHECK_NEAR(v.cents[0], 6900, 8);                              /* period 284 at 2 MHz = 440 Hz = A4 */
    CHECK(v.level[0] > 200);
    CHECK_EQ_INT(v.level[1], 0);
    pc_backend_ym.close(h);
    int n;
    float *x = play(d, len, &n);
    CHECK_EQ_INT(n, FR * 882);                                    /* ends where the song ends */
    double e440 = goertzel(x, 44100, 44100, 440.0), e300 = goertzel(x, 44100, 44100, 300.0), e660 = goertzel(x, 44100, 44100, 660.0);
    CHECK(e440 > 50 * e300 && e440 > 5 * e660);                   /* the 440 Hz line is far above its neighbourhood */
    CHECK(e440 > 1e-3);
    free(x);
    free(d);
}

static void test_mixer_noise_envelope_mute(void)
{
    enum { FR = 50 };
    static uint8_t regs[FR][16];
    memset(regs, 0, sizeof regs);
    for (int i = 0; i < FR; i++) { regs[i][7] = 0x37; regs[i][6] = 8; regs[i][8] = 15; }        /* noise only on A (tone off, noise on) */
    uint32_t len;
    uint8_t *d = make_ym6(regs, FR, 0, 0, &len);
    int n;
    float *x = play(d, len, &n);
    double e = 0;
    for (int i = 0; i < n; i++) e += (double)x[i] * x[i];
    CHECK(e / n > 1e-4);
    free(x);
    free(d);
    for (int i = 0; i < FR; i++) { regs[i][7] = 0x3e; regs[i][0] = 100; regs[i][8] = 0x10; regs[i][11] = 40; regs[i][12] = 0; regs[i][13] = i == 0 ? 14 : 0xff; }   /* a /\/\ envelope on A */
    d = make_ym6(regs, FR, 0, 0, &len);
    x = play(d, len, &n);
    float lo = 1, hi = -1;
    for (int i = 0; i < n; i++) { if (x[i] < lo) lo = x[i]; if (x[i] > hi) hi = x[i]; }
    CHECK(hi - lo > 0.05f);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_ym.open(&h, &r, 0), 0);
    float out[2 * 441];
    CHECK(pc_backend_ym.render(h, out, 441) > 0);
    pc_vmute_sync();                                   /* what the UI does every frame; a set before it after the source reset would be dropped */
    pc_vmute_set(0, PC_VM_MUTED, 3);
    CHECK(pc_backend_ym.render(h, out, 441) > 0);
    for (int b = 0; b < 30; b++) pc_backend_ym.render(h, out, 441);        /* the high-pass (DC removal) tail dies away */
    double em = 0;
    for (int b = 0; b < 10; b++) { pc_backend_ym.render(h, out, 441); for (int i = 0; i < 441; i++) em += (double)out[2 * i] * out[2 * i]; }
    CHECK(em < 1e-3);
    pc_backend_ym.close(h);
    pc_vmute_reset();
    free(x);
    free(d);
}

static void test_sid_and_digidrum(void)
{
    enum { FR = 50 };
    static uint8_t regs[FR][16];
    memset(regs, 0, sizeof regs);
    /* SID on voice A: r1 high nibble 0x10 (voice 1, type SID), r6 bits 5-7 = prediv index 1 (4), r14 = count 100 -> the timer runs at 2457600/400 = 6144 Hz,
       the volume toggles between r8 and 0 each time: a square of 3072 Hz; tone and noise of the voice off, level 15 */
    for (int i = 0; i < FR; i++) { regs[i][1] = 0x10; regs[i][6] = 1 << 5; regs[i][14] = 100; regs[i][7] = 0x3f; regs[i][8] = 15; regs[i][0] = 1; }
    uint32_t len;
    uint8_t *d = make_ym6(regs, FR, 0, 0, &len);
    int n;
    float *x = play(d, len, &n);
    double e3072 = goertzel(x, 22050, 44100, 3072.0), e2000 = goertzel(x, 22050, 44100, 2000.0), e5000 = goertzel(x, 22050, 44100, 5000.0);
    CHECK(e3072 > 20 * e2000 && e3072 > 5 * e5000 && e3072 > 1e-3);              /* the timer's 6144 Hz toggling = a 3072 Hz square */
    free(x);
    free(d);
    /* digidrum 0 on voice B, started in frame 5: r3 high nibble 0x20|0x40 (voice 2, digidrum), r8 low bits (voice B's volume register r9) = drum id 0, timer 2457600/(4*100) */
    for (int i = 0; i < FR; i++) { regs[i][7] = 0x3f; memset(regs[i], 0, 8 * 0 + 1); }
    memset(regs, 0, sizeof regs);
    for (int i = 5; i < 8; i++) { regs[i][3] = 0x60; regs[i][8] = 1 << 5; regs[i][15] = 100; regs[i][9] = 0; regs[i][7] = 0x3f; }
    d = make_ym6(regs, FR, 1, 400, &len);
    x = play(d, len, &n);
    double e = 0;
    for (int i = 5 * 882; i < 7 * 882; i++) e += (double)x[i] * x[i];
    CHECK(e > 0.01);                                              /* the drum is audible although tone and noise are off */
    free(x);
    free(d);
}

static void test_refusals_and_tags(void)
{
    uint8_t mix[64] = "MIX1LeOnArD!", trk[64] = "YMT1LeOnArD!", junk[64] = "nothing";
    pc_reader_t r;
    void *h;
    pc_reader_mem(&r, mix, sizeof mix);
    CHECK(pc_backend_ym.open(&h, &r, 0) != 0);
    CHECK_EQ_INT(pc_ym_check(&r), -2);
    pc_reader_mem(&r, trk, sizeof trk);
    CHECK_EQ_INT(pc_ym_check(&r), -2);
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK_EQ_INT(pc_ym_check(&r), -1);
    static uint8_t regs[2][16];
    uint32_t len;
    uint8_t *d = make_ym6(regs, 2, 0, 0, &len);
    pc_reader_mem(&r, d, len);
    pc_id3_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Song");
    CHECK_STR(t.artist, "Me");
    pc_id3_free(&t);
    CHECK_EQ_INT(pc_ym_check(&r), 0);
    free(d);
}

static void try_play(const uint8_t *d, uint32_t len)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    if (pc_backend_ym.open(&h, &r, (uint32_t)rand())) return;
    float out[2 * 441];
    for (int i = 0; i < 12; i++) if (pc_backend_ym.render(h, out, 441) <= 0) break;
    pc_backend_ym.close(h);
}

static void test_hostile(void)
{
    enum { FR = 20 };
    static uint8_t regs[FR][16];
    for (int i = 0; i < FR; i++) { regs[i][1] = 0x30; regs[i][6] = 2 << 5; regs[i][14] = 7; regs[i][0] = (uint8_t)i; regs[i][8] = 12; }
    uint32_t len;
    uint8_t *d = make_ym6(regs, FR, 2, 30, &len);
    srand(3);
    for (uint32_t cut = 0; cut < len; cut += (cut < 120 ? 1 : 9)) {
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, d, cut);
        try_play(c, cut);
        free(c);
    }
    for (int k = 0; k < 1500; k++) {
        uint8_t *c = malloc(len);
        memcpy(c, d, len);
        int flips = 1 + rand() % 4;
        for (int i = 0; i < flips; i++) c[rand() % (int)(k % 2 ? len : 60)] = (uint8_t)rand();
        try_play(c, len);
        free(c);
    }
    free(d);
    pc_vmute_reset();
}

#ifdef CHIPTEST_DIR
static uint8_t *slurp(const char *rel, uint32_t *n)
{
    char path[512];
    snprintf(path, sizeof path, CHIPTEST_DIR "/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)sz);
    CHECK(fread(d, 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    *n = (uint32_t)sz;
    return d;
}

static void test_files(void)
{
    uint32_t n1, n2;
    uint8_t *plain = slurp("atari/voices.ym", &n1), *lha = slurp("atari/voices-lha.ym", &n2);
    if (!plain || !lha) { printf("skip atari files\n"); free(plain); free(lha); return; }
    uint8_t *u;
    uint32_t ul;
    CHECK(pc_lh5_is_packed(lha, n2) && !pc_lh5_is_packed(plain, n1));
    CHECK_EQ_INT(pc_lh5_unpack(lha, n2, 1 << 20, &u, &ul), 0);
    CHECK(ul == n1 && !memcmp(u, plain, n1));                     /* the depacker gives back the plain file, byte for byte */
    free(u);
    int a, b;
    float *x = play(plain, n1, &a), *y = play(lha, n2, &b);
    CHECK(a == b && a > 44100 && !memcmp(x, y, sizeof(float) * (size_t)a));
    double e = 0;
    for (int i = 0; i < a; i++) e += (double)x[i] * x[i];
    printf("voices.ym: %d frames, energy %.3f\n", a, e);
    CHECK(e > 1.0);
    free(x); free(y);
    for (uint32_t cut = 0; cut < n2; cut += 11) {                 /* a truncated LHA file never reads past its end */
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, lha, cut);
        try_play(c, cut);
        free(c);
    }
    srand(9);
    for (int k = 0; k < 800; k++) {
        uint8_t *c = malloc(n2);
        memcpy(c, lha, n2);
        for (int i = 0; i < 3; i++) c[rand() % (int)n2] = (uint8_t)rand();
        try_play(c, n2);
        free(c);
    }
    uint32_t n3;
    uint8_t *old = slurp("atari/old.ym", &n3);
    if (old) { x = play(old, n3, &a); printf("old.ym (YM3): %d frames\n", a); CHECK(a > 0); free(x); free(old); }
    uint8_t *mix = slurp("atari/digi.ym", &n3), *trk = slurp("atari/tracker.ym", &n3);
    pc_reader_t r;
    if (mix) { pc_reader_mem(&r, mix, n3); CHECK_EQ_INT(pc_ym_check(&r), -2); free(mix); }
    if (trk) { pc_reader_mem(&r, trk, n3); CHECK_EQ_INT(pc_ym_check(&r), -2); free(trk); }
    free(plain); free(lha);
}
#else
static void test_files(void) {}
#endif

int main(void)
{
    RUN(test_pitch_and_end);
    RUN(test_mixer_noise_envelope_mute);
    RUN(test_sid_and_digidrum);
    RUN(test_refusals_and_tags);
    RUN(test_hostile);
    RUN(test_files);
    return 0;
}
