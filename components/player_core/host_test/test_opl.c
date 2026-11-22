// SPDX-License-Identifier: LGPL-2.1-or-later
/* OPL / AdPlug backend: files are built in memory (AdPlug's RAW capture format, ext ".rac": one OPL2 voice, then a second chip), so no tune is
 * committed. Pitch, length, end, dual-chip stereo, voices, mute, four-operator and aPlib refusals, the extension list and hostile input
 * (truncations, byte flips) under ASan; the DOS files of the main checkout's .local/chiptest/opl are played too when present. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_vmute.h"
#include "pc_test.h"

typedef struct { uint8_t *d; uint32_t n; } buf_t;
static void rec(buf_t *b, uint8_t param, uint8_t cmd) { b->d[b->n++] = param; b->d[b->n++] = cmd; }
static void voice(buf_t *b, int ch, int fnum, int block)         /* a plain 2-operator voice with the carrier at full volume */
{
    static const uint8_t m[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
    int mo = m[ch], co = m[ch] + 3;
    rec(b, 0x21, (uint8_t)(0x20 + mo)); rec(b, 0x21, (uint8_t)(0x20 + co));   /* 0x21: multiply 1, sustained */
    rec(b, 0x10, (uint8_t)(0x40 + mo)); rec(b, 0x00, (uint8_t)(0x40 + co));
    rec(b, 0xF0, (uint8_t)(0x60 + mo)); rec(b, 0xF0, (uint8_t)(0x60 + co));
    rec(b, 0x07, (uint8_t)(0x80 + mo)); rec(b, 0x07, (uint8_t)(0x80 + co));
    rec(b, (uint8_t)(fnum & 0xFF), (uint8_t)(0xA0 + ch));
    rec(b, (uint8_t)(0x20 | block << 2 | fnum >> 8), (uint8_t)(0xB0 + ch));
}
/* RAW header, 1000 Hz clock; kind 0: one voice A4 (440 Hz); 1: + chip 1 with E5 (659 Hz); 2: + OPL3 four-operator enable on chip 1 */
static buf_t make_raw(int kind)
{
    buf_t b = { calloc(1, 400), 0 };
    memcpy(b.d, "RAWADATA", 8); b.d[8] = 0xA9; b.d[9] = 0x04; b.n = 10;     /* clock 1193 -> 1193180 / 1193 = ~1000 Hz */
    voice(&b, 0, 0x244, 4);
    if (kind >= 1) { rec(&b, 2, 2); voice(&b, 0, 0x364, 4); }
    if (kind == 2) rec(&b, 1, 4);
    for (int i = 0; i < 8; i++) rec(&b, 255, 0);                              /* 8 x 255 ticks = ~2.04 s */
    rec(&b, 0xFF, 0xFF);
    return b;
}

static double goertzel(const float *x, int n, int stride, double rate, double freq)
{
    double w = 2 * M_PI * freq / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = x[i * stride] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}

static void *open_raw(buf_t b, pc_reader_t *r)
{
    pc_reader_mem(r, b.d, b.n);
    pc_reader_set_ext(r, "SONG.RAC");
    void *h = NULL;
    return pc_backend_opl.open(&h, r, 0) ? NULL : h;
}

static int render_all(void *h, float *buf, int max)
{
    int tot = 0;
    while (tot < max) {
        int g = pc_backend_opl.render(h, buf + 2 * tot, max - tot > 441 ? 441 : max - tot);
        CHECK(g >= 0);
        if (g == 0) break;
        tot += g;
    }
    return tot;
}

static void test_ext(void)
{
    CHECK(pc_opl_ext(".rad")); CHECK(pc_opl_ext(".RAD")); CHECK(pc_opl_ext(".imf")); CHECK(pc_opl_ext(".dro"));
    CHECK(!pc_opl_ext(".mid")); CHECK(!pc_opl_ext(".raw")); CHECK(!pc_opl_ext(".vgm")); CHECK(!pc_opl_ext(".mp3")); CHECK(!pc_opl_ext(NULL));
    CHECK(pc_chip_supported_ext("x/tune.rad")); CHECK(!pc_chip_supported_ext("x/tune.mp3"));
    pc_reader_t r;
    pc_reader_mem(&r, "x", 1);
    pc_reader_set_ext(&r, "/a.b/Song.RAD"); CHECK_STR(r.ext, ".rad");
    pc_reader_set_ext(&r, "/a.b/Song"); CHECK_STR(r.ext, "");
    pc_reader_set_ext(&r, "x.toolongext"); CHECK_STR(r.ext, "");
}

static void test_play(void)
{
    buf_t b = make_raw(0);
    pc_reader_t r;
    void *h = open_raw(b, &r);
    CHECK(h != NULL);
    CHECK_EQ_INT(pc_backend_opl.track_count(h), 1);
    CHECK_EQ_INT(pc_backend_opl.rate(h), 44100);
    int32_t len = pc_backend_opl.length_ms(h);
    CHECK(len > 1900 && len < 2200);
    float *buf = malloc(sizeof(float) * 2 * 44100 * 4);
    int tot = render_all(h, buf, 44100 * 4);
    CHECK(tot > 44100 * 19 / 10 && tot < 44100 * 22 / 10);         /* it ends with the song */
    double at = goertzel(buf + 2 * 4410, 22050, 2, 44100, 440.0), off = goertzel(buf + 2 * 4410, 22050, 2, 44100, 587.0);
    CHECK(at > 1e-5);
    CHECK(at > 50 * off);
    for (int i = 0; i < 22050; i++) CHECK(buf[2 * (4410 + i)] == buf[2 * (4410 + i) + 1]);   /* one chip: both sides alike */
    CHECK_EQ_INT(pc_backend_opl.render(h, buf, 441), 0);
    pc_backend_opl.close(h);
    free(buf); free(b.d);
}

static void test_dual_and_voices(void)
{
    buf_t b = make_raw(1);
    pc_reader_t r;
    void *h = open_raw(b, &r);
    CHECK(h != NULL);
    float *buf = malloc(sizeof(float) * 2 * 44100);
    CHECK_EQ_INT(render_all(h, buf, 44100), 44100);
    double l440 = goertzel(buf + 2 * 4410, 22050, 2, 44100, 440.0), l659 = goertzel(buf + 2 * 4410, 22050, 2, 44100, 659.3);
    double r440 = goertzel(buf + 2 * 4410 + 1, 22050, 2, 44100, 440.0), r659 = goertzel(buf + 2 * 4410 + 1, 22050, 2, 44100, 659.3);
    CHECK(l440 > 20 * l659);                   /* chip 0 left */
    CHECK(r659 > 20 * r440);                   /* chip 1 right */
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 18);
    CHECK(v.level[0] > 150 && v.level[9] > 150);
    CHECK_EQ_INT(v.level[1], 0);
    CHECK(v.cents[0] > 6850 && v.cents[0] < 6950);            /* A4 = 6900 */
    CHECK(v.cents[9] > 7550 && v.cents[9] < 7650);            /* E5 = 7600 */
    CHECK(v.strikes[0] >= 1 && v.strikes[9] >= 1);
    /* mute voice 0: its tone goes, voice 9 stays; unmute: it comes back with the next block's re-send of the key */
    pc_vmute_set(0, PC_VM_MUTED, 18);
    pc_backend_opl.start_track(h, 0);
    CHECK_EQ_INT(render_all(h, buf, 44100), 44100);
    l440 = goertzel(buf + 2 * 4410, 22050, 2, 44100, 440.0);
    r659 = goertzel(buf + 2 * 4410 + 1, 22050, 2, 44100, 659.3);
    CHECK(l440 < 1e-8);
    CHECK(r659 > 1e-5);
    pc_extvoice_read(&v);
    CHECK_EQ_INT(v.level[0], 0);
    pc_vmute_clear(18);
    pc_backend_opl.start_track(h, 0);
    CHECK_EQ_INT(render_all(h, buf, 44100), 44100);
    CHECK(goertzel(buf + 2 * 4410, 22050, 2, 44100, 440.0) > 1e-5);
    pc_backend_opl.close(h);
    CHECK_EQ_INT(pc_extvoice_read(&v), 0);
    free(buf); free(b.d);
}

static void test_refusals(void)
{
    buf_t b = make_raw(2);
    pc_reader_t r;
    pc_reader_mem(&r, b.d, b.n);
    pc_reader_set_ext(&r, ".rac");
    CHECK_EQ_INT(pc_opl_check(&r), -2);
    void *h;
    CHECK(pc_backend_opl.open(&h, &r, 0) != 0);
    free(b.d);
    uint8_t a2m[64] = "_A2module_";
    a2m[14] = 10;
    pc_reader_mem(&r, a2m, sizeof a2m);
    pc_reader_set_ext(&r, ".a2m");
    CHECK_EQ_INT(pc_opl_check(&r), -3);
    uint8_t junk[300];
    memset(junk, 0x55, sizeof junk);
    pc_reader_mem(&r, junk, sizeof junk);
    pc_reader_set_ext(&r, ".rad");
    CHECK_EQ_INT(pc_opl_check(&r), -1);
    CHECK(pc_backend_opl.open(&h, &r, 0) != 0);
}

static void test_hostile(void)
{
    for (int kind = 0; kind < 2; kind++) {
        buf_t b = make_raw(kind);
        float out[2 * 441];
        for (uint32_t n = 0; n < b.n; n += 2) {
            pc_reader_t r;
            pc_reader_mem(&r, b.d, n);
            pc_reader_set_ext(&r, ".rac");
            void *h;
            if (!pc_backend_opl.open(&h, &r, 0)) { pc_backend_opl.render(h, out, 441); pc_backend_opl.close(h); }
        }
        unsigned s = 777u + (unsigned)kind;
        for (int it = 0; it < 200; it++) {
            uint8_t *c = malloc(b.n);
            memcpy(c, b.d, b.n);
            for (int k = 0; k < 4; k++) { s = s * 1103515245u + 12345u; c[(s >> 8) % b.n] = (uint8_t)(s >> 16); }
            pc_reader_t r;
            pc_reader_mem(&r, c, b.n);
            pc_reader_set_ext(&r, ".rac");
            void *h;
            if (!pc_backend_opl.open(&h, &r, 0)) { for (int k = 0; k < 3; k++) CHECK(pc_backend_opl.render(h, out, 441) >= 0); pc_backend_opl.close(h); }
            free(c);
        }
        free(b.d);
    }
}

#ifdef CHIPTEST_DIR
#include <dirent.h>
static void test_local_files(void)
{
    DIR *dir = opendir(CHIPTEST_DIR "/opl/adplug");
    if (!dir) return;
    struct dirent *e;
    int played = 0;
    while ((e = readdir(dir))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || !pc_opl_ext(dot)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/opl/adplug/%s", CHIPTEST_DIR, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        pc_reader_t r;
        CHECK_EQ_INT(pc_reader_file(&r, f), 0);
        pc_reader_set_ext(&r, e->d_name);
        void *h;
        if (!pc_backend_opl.open(&h, &r, 0)) {
            float out[2 * 441];
            for (int b = 0; b < 200; b++) { int g = pc_backend_opl.render(h, out, 441); CHECK(g >= 0); if (g < 441) break; }
            for (int t = 1; t < pc_backend_opl.track_count(h) && t < 3; t++) CHECK_EQ_INT(pc_backend_opl.start_track(h, t), 0);
            pc_backend_opl.close(h);
            played++;
        }
        fclose(f);
    }
    closedir(dir);
    CHECK(played > 40);
}

/* byte flips and truncations of the real files (AdPlug's players trust their input: every crash found here is patched in components/libadplug/patches) */
static void test_local_fuzz(void)
{
    DIR *dir = opendir(CHIPTEST_DIR "/opl/adplug");
    if (!dir) return;
    struct dirent *e;
    unsigned s = 99;
    int runs = getenv("OPL_FUZZ") ? atoi(getenv("OPL_FUZZ")) : 12;
    while ((e = readdir(dir))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || !pc_opl_ext(dot)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/opl/adplug/%s", CHIPTEST_DIR, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *d = malloc((size_t)n);
        if (fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); continue; }
        fclose(f);
        for (int it = 0; it < runs; it++) {
            uint8_t *c = malloc((size_t)n);
            memcpy(c, d, (size_t)n);
            uint32_t len = (uint32_t)n;
            s = s * 1103515245u + 12345u;
            if (it % 3 == 2) len = (uint32_t)((s >> 8) % (uint32_t)n);                 /* truncation */
            else for (int k = 0; k < 1 + it % 4; k++) { s = s * 1103515245u + 12345u; c[(s >> 8) % (uint32_t)n] = (uint8_t)(s >> 16); }
            pc_reader_t r;
            pc_reader_mem(&r, c, len);
            pc_reader_set_ext(&r, e->d_name);
            void *h;
            if (getenv("OPL_FUZZ_VERBOSE")) fprintf(stderr, "fuzz %s it=%d len=%u\n", e->d_name, it, len);
            if (!pc_backend_opl.open(&h, &r, 0)) {
                float out[2 * 441];
                for (int b = 0; b < 30; b++) { int g = pc_backend_opl.render(h, out, 441); if (g < 441) break; }
                pc_backend_opl.close(h);
            }
            free(c);
        }
        free(d);
    }
    closedir(dir);
}
#endif

int main(void)
{
    RUN(test_ext);
    RUN(test_play);
    RUN(test_dual_and_voices);
    RUN(test_refusals);
    RUN(test_hostile);
#ifdef CHIPTEST_DIR
    RUN(test_local_files);
    RUN(test_local_fuzz);
#endif
    return 0;
}
