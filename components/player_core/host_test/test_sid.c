// SPDX-License-Identifier: LGPL-2.1-or-later
/* SID backend (cRSID): a minimal PSID built in memory (init = RTS, play writes voice 1: triangle, gate on, C4), so no tune is committed.
 * Pitch, sub-tunes, mute, voice snapshot, the 180 s end with its fade, the BASIC-RSID refusal and hostile input (truncations, byte
 * flips) under ASan; the SID files of the main checkout's .local/chiptest are played too when present. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_vmute.h"
#include "pc_test.h"

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* freq register 0x1167 = 261.6 Hz at the PAL clock (985248 Hz): C4 */
static uint8_t *make_psid(uint32_t *len, bool rsid, int songs, int flags)
{
    static const uint8_t code[] = {
        0x60,                                  /* $1000 init: RTS */
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,          /* $1001 play: LDA #$0F  STA $D418 */
        0xA9, 0x67, 0x8D, 0x00, 0xD4,          /* freq lo */
        0xA9, 0x11, 0x8D, 0x01, 0xD4,          /* freq hi */
        0xA9, 0x00, 0x8D, 0x05, 0xD4,          /* attack / decay 0 */
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,          /* sustain F */
        0xA9, 0x11, 0x8D, 0x04, 0xD4,          /* triangle + gate */
        0x60 };
    uint32_t n = 0x7C + sizeof code;
    uint8_t *b = calloc(1, n);
    memcpy(b, rsid ? "RSID" : "PSID", 4);
    put16(b + 4, 2); put16(b + 6, 0x7C); put16(b + 8, 0x1000); put16(b + 10, rsid ? 0x1000 : 0x1000); put16(b + 12, rsid ? 0 : 0x1001);
    put16(b + 14, (uint32_t)songs); put16(b + 16, 1);
    memcpy(b + 0x16, "Test", 4); memcpy(b + 0x36, "Me", 2);
    put16(b + 0x76, (uint32_t)flags);
    memcpy(b + 0x7C, code, sizeof code);
    *len = n;
    return b;
}

static double goertzel(const float *x, int n, double rate, double freq)
{
    double w = 2 * M_PI * freq / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}

static int render_all(void *h, float *buf, int max_frames)
{
    int tot = 0;
    while (tot < max_frames) {
        int g = pc_backend_sid.render(h, buf + 2 * tot, max_frames - tot > 441 ? 441 : max_frames - tot);
        CHECK(g >= 0);
        if (g == 0) break;
        tot += g;
    }
    return tot;
}

static void test_play(void)
{
    uint32_t len;
    uint8_t *d = make_psid(&len, false, 2, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_sid_check(&r), 0);
    void *h;
    CHECK_EQ_INT(pc_backend_sid.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_sid.track_count(h), 2);
    CHECK_EQ_INT(pc_backend_sid.rate(h), 44100);
    CHECK_EQ_INT(pc_backend_sid.length_ms(h), 180000);
    float *buf = malloc(sizeof(float) * 2 * 44100);
    CHECK_EQ_INT(render_all(h, buf, 44100), 44100);
    float *mono = malloc(sizeof(float) * 44100);
    for (int i = 0; i < 44100; i++) mono[i] = buf[2 * i];
    double at = goertzel(mono + 4410, 22050, 44100, 261.6), off = goertzel(mono + 4410, 22050, 44100, 440.0);
    CHECK(at > 1e-5);
    CHECK(at > 50 * off);
    CHECK_EQ_INT(pc_backend_sid.position_ms(h), 1000);
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 3);
    CHECK(v.level[0] > 100);
    CHECK_EQ_INT(v.level[1], 0);
    CHECK(v.cents[0] > 5950 && v.cents[0] < 6050);
    CHECK(v.strikes[0] >= 1);
    CHECK(v.flags[0] & PC_VF_PITCH);
    /* mute voice 0 (pc_vmute_set posts the mask; the audio side applies it before the next block) */
    pc_vmute_set(0, PC_VM_MUTED, 3);
    CHECK_EQ_INT(render_all(h, buf, 4410), 4410);
    CHECK_EQ_INT(render_all(h, buf, 4410), 4410);
    double e = 0;
    for (int i = 0; i < 4410; i++) e += (double)buf[2 * i] * buf[2 * i];
    CHECK(sqrt(e / 4410) < 3e-4);                     /* what is left is cRSID's dither noise (a few LSB) */
    pc_vmute_clear(3);
    /* restart on sub-tune 1: plays again from 0 */
    CHECK_EQ_INT(pc_backend_sid.start_track(h, 1), 0);
    CHECK_EQ_INT(pc_backend_sid.position_ms(h), 0);
    CHECK_EQ_INT(pc_backend_sid.start_track(h, 2), -1);
    CHECK_EQ_INT(render_all(h, buf, 22050), 22050);
    e = 0;
    for (int i = 0; i < 22050; i++) e += (double)buf[2 * i] * buf[2 * i];
    CHECK(e > 1.0);
    pc_backend_sid.close(h);
    CHECK_EQ_INT(pc_extvoice_read(&v), 0);
    free(buf); free(mono); free(d);
}

static void test_end_and_fade(void)
{
    uint32_t len;
    uint8_t *d = make_psid(&len, false, 1, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_sid.open(&h, &r, 0), 0);
    float out[2 * 441];
    long frames = 0;
    double tail_peak = 0, last_peak = 0;
    for (;;) {
        int g = pc_backend_sid.render(h, out, 441);
        CHECK(g >= 0);
        if (g == 0) break;
        for (int i = 0; i < g; i++) {
            double a = fabs(out[2 * i]);
            long t = frames + i;
            if (t >= 176L * 44100 && t < 177L * 44100 && a > tail_peak) tail_peak = a;   /* 4 s fade: 176 s is at ~1 s into it */
            if (t >= 179L * 44100 + 40000 && a > last_peak) last_peak = a;
        }
        frames += g;
    }
    CHECK_EQ_INT(frames, 180L * 44100);
    CHECK(tail_peak > 0.02);
    CHECK(last_peak < 0.01);
    pc_backend_sid.close(h);
    free(d);
}

static void test_refusals(void)
{
    uint32_t len;
    pc_reader_t r;
    uint8_t *d = make_psid(&len, true, 1, 0x02);       /* RSID with the BASIC flag */
    pc_reader_mem(&r, d, len);
    CHECK_EQ_INT(pc_sid_check(&r), -2);
    void *h;
    CHECK(pc_backend_sid.open(&h, &r, 0) != 0);
    free(d);
    uint8_t junk[300];
    memset(junk, 0x55, sizeof junk);
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK_EQ_INT(pc_sid_check(&r), -1);
    CHECK(pc_backend_sid.open(&h, &r, 0) != 0);
    d = make_psid(&len, false, 0, 0);                  /* no tunes */
    pc_reader_mem(&r, d, len);
    CHECK(pc_backend_sid.open(&h, &r, 0) != 0);
    free(d);
}

static void test_hostile(void)
{
    uint32_t len;
    uint8_t *d = make_psid(&len, false, 3, 0);
    float out[2 * 441];
    for (uint32_t n = 0; n < len; n += 3) {            /* every truncation: refused or plays, never crashes */
        pc_reader_t r;
        pc_reader_mem(&r, d, n);
        void *h;
        if (!pc_backend_sid.open(&h, &r, 0)) { pc_backend_sid.render(h, out, 441); pc_backend_sid.close(h); }
    }
    unsigned s = 12345;
    for (int it = 0; it < 300; it++) {                 /* random byte flips */
        uint8_t *c = malloc(len);
        memcpy(c, d, len);
        for (int k = 0; k < 4; k++) { s = s * 1103515245u + 12345u; c[(s >> 8) % len] = (uint8_t)(s >> 16); }
        pc_reader_t r;
        pc_reader_mem(&r, c, len);
        void *h;
        if (!pc_backend_sid.open(&h, &r, 0)) {
            for (int b = 0; b < 3; b++) CHECK(pc_backend_sid.render(h, out, 441) >= 0);
            if (pc_backend_sid.track_count(h) > 1) pc_backend_sid.start_track(h, pc_backend_sid.track_count(h) - 1);
            pc_backend_sid.close(h);
        }
        free(c);
    }
    free(d);
}

#ifdef CHIPTEST_DIR
#include <dirent.h>
static void test_local_files(void)
{
    const char *dirs[] = { CHIPTEST_DIR "/sid", CHIPTEST_DIR "/sid/hvsc" };
    for (int k = 0; k < 2; k++) {
        DIR *dir = opendir(dirs[k]);
        if (!dir) continue;
        struct dirent *e;
        while ((e = readdir(dir))) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcmp(e->d_name + l - 4, ".sid")) continue;
            char path[512];
            snprintf(path, sizeof path, "%s/%s", dirs[k], e->d_name);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            pc_reader_t r;
            CHECK_EQ_INT(pc_reader_file(&r, f), 0);
            void *h;
            int rc = pc_backend_sid.open(&h, &r, 0);
            if (!rc) {
                float out[2 * 441];
                double e2 = 0;
                for (int b = 0; b < 300; b++) { CHECK_EQ_INT(pc_backend_sid.render(h, out, 441), 441); for (int i = 0; i < 441; i++) e2 += fabs(out[2 * i]); }
                CHECK(e2 > 0);
                for (int t = 0; t < pc_backend_sid.track_count(h) && t < 3; t++) { CHECK_EQ_INT(pc_backend_sid.start_track(h, t), 0); CHECK_EQ_INT(pc_backend_sid.render(h, out, 441), 441); }
                pc_backend_sid.close(h);
            }
            fclose(f);
        }
        closedir(dir);
    }
}
#endif

int main(void)
{
    RUN(test_play);
    RUN(test_end_and_fade);
    RUN(test_refusals);
    RUN(test_hostile);
#ifdef CHIPTEST_DIR
    RUN(test_local_files);
#endif
    return 0;
}
