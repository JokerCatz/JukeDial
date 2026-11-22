// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_backend.h"
#include "pc_test.h"

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

static void test_mp3_decode(void)
{
    uint32_t len;
    uint8_t *data = load("song_head.mp3", &len);
    pc_reader_t r;
    pc_reader_mem(&r, data, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, 0), 0);
    CHECK(pc_backend_mp3.rate(h) == 48000 || pc_backend_mp3.rate(h) == 44100);
    float buf[2 * 512];
    long total = 0;
    float peak = 0;
    for (;;) {
        int n = pc_backend_mp3.render(h, buf, 512);
        CHECK(n >= 0);
        if (n == 0) break;
        for (int i = 0; i < 2 * n; i++) {
            CHECK(buf[i] >= -1.0f && buf[i] <= 1.0f);
            if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]);
        }
        total += n;
    }
    CHECK(total > 48000);            /* 60 KB at >= 128 kbps is over a second */
    CHECK(peak > 0.01f);
    CHECK(pc_backend_mp3.position_ms(h) >= 1000);
    pc_backend_mp3.close(h);
    free(data);
}

/* A file reader on an MP3 without an ID3v2 tag decodes the same first frames as the memory reader (open probes, then decodes at 0). */
static void test_mp3_file_reader(void)
{
    uint32_t len;
    uint8_t *data = load("song_head.mp3", &len);
    CHECK(memcmp(data, "ID3", 3) != 0);
    char p[256];
    snprintf(p, sizeof p, "%s/song_head.mp3", FIXTURES);
    FILE *fp = fopen(p, "rb");
    CHECK(fp != NULL);
    pc_reader_t rm, rf;
    pc_reader_mem(&rm, data, len);
    CHECK_EQ_INT(pc_reader_file(&rf, fp), 0);
    void *hm = NULL, *hf = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&hm, &rm, 0), 0);
    CHECK_EQ_INT(pc_backend_mp3.open(&hf, &rf, 0), 0);
    float bm[2 * 1152], bf[2 * 1152];
    float peak = 0;
    for (int k = 0; k < 20; k++) {
        CHECK_EQ_INT(pc_backend_mp3.render(hm, bm, 1152), 1152);
        CHECK_EQ_INT(pc_backend_mp3.render(hf, bf, 1152), 1152);
        CHECK(memcmp(bm, bf, sizeof bm) == 0);
        for (int i = 0; i < 2 * 1152; i++) if (fabsf(bm[i]) > peak) peak = fabsf(bm[i]);
    }
    CHECK(peak > 0.001f);
    pc_backend_mp3.close(hm);
    pc_backend_mp3.close(hf);
    fclose(fp);
    free(data);
}

static void test_mp3_truncated(void)
{
    uint32_t len;
    uint8_t *data = load("song_head.mp3", &len);
    pc_reader_t r;
    pc_reader_mem(&r, data, 3000);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, 0), 0);
    float buf[2 * 512];
    int n, guard = 0;
    while ((n = pc_backend_mp3.render(h, buf, 512)) > 0 && guard++ < 100) {}
    CHECK(n <= 0);
    pc_backend_mp3.close(h);
    free(data);
}

static void test_mp3_not_audio(void)
{
    uint8_t junk[20000];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 31 + 7);
    pc_reader_t r;
    pc_reader_mem(&r, junk, sizeof junk);
    void *h = NULL;
    CHECK(pc_backend_mp3.open(&h, &r, 0) < 0);
}

static void test_mp3_skips_big_id3(void)
{
    uint32_t len;
    uint8_t *data = load("song_head.mp3", &len);
    uint32_t tag = 100000;                              /* larger than the no-sync limit */
    uint8_t *buf = calloc(1, tag + 10 + len);
    memcpy(buf, "ID3\3\0\0", 6);
    buf[6] = (tag >> 21) & 0x7f; buf[7] = (tag >> 14) & 0x7f; buf[8] = (tag >> 7) & 0x7f; buf[9] = tag & 0x7f;
    memcpy(buf + 10 + tag, data, len);
    pc_reader_t r;
    pc_reader_mem(&r, buf, tag + 10 + len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, 0), 0);
    float out[2 * 64];
    CHECK_EQ_INT(pc_backend_mp3.render(h, out, 64), 64);
    pc_backend_mp3.close(h);
    free(buf);
    free(data);
}

/* byte_pos: where the decoder is in the file; opening there again continues the song */
static void test_mp3_byte_pos(void)
{
    uint32_t len;
    uint8_t *data = load("song_head.mp3", &len);
    pc_reader_t r;
    pc_reader_mem(&r, data, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, 0), 0);
    float buf[2 * 512];
    for (int i = 0; i < 40; i++) CHECK_EQ_INT(pc_backend_mp3.render(h, buf, 512), 512);
    uint32_t at = pc_backend_mp3.byte_pos(h);
    CHECK(at > 10000 && at < len);                               /* ~0.4 s in */
    pc_backend_mp3.close(h);
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, at), 0);            /* lands mid-stream: finds the next frame */
    long total = 0;
    int n;
    while ((n = pc_backend_mp3.render(h, buf, 512)) > 0) total += n;
    CHECK(total > 20000 && total < 80000);                       /* the rest of the clip, not all of it */
    pc_backend_mp3.close(h);
    free(data);
}

/* Gapless (ideas #1): the LAME tag's encoder delay / padding trim the decoder's leading and trailing silence,
 * so two files cut from one sine decode back to exactly the samples that went in, and join without a seam. */
static float *decode_all(const char *name, long *frames, uint32_t *rate, int32_t *len_ms)
{
    uint32_t len;
    uint8_t *data = load(name, &len);
    pc_reader_t r;
    pc_reader_mem(&r, data, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, 0), 0);
    *rate = pc_backend_mp3.rate(h);
    *len_ms = pc_backend_mp3.length_ms(h);
    float *out = malloc(200000 * 2 * sizeof(float));
    long total = 0;
    int n;
    while ((n = pc_backend_mp3.render(h, out + 2 * total, 333)) > 0) total += n;   /* odd block size on purpose */
    CHECK(n == 0);
    pc_backend_mp3.close(h);
    free(data);
    *frames = total;
    return out;
}

static double err_at(const float *x, long from, long to, int shift)
{
    double e = 0;
    for (long i = from; i < to; i++) {
        double d = fabs(x[i] - 0.5 * sin(2 * M_PI * 440.0 * (double)(i + shift) / 44100.0));
        if (d > e) e = d;
    }
    return e;
}
static int best_shift(const float *x, long from, long to)
{
    int best = 0;
    for (int s = -40; s <= 40; s++) if (err_at(x, from, to, s) < err_at(x, from, to, best)) best = s;
    return best;
}

static void test_mp3_gapless_trim(void)
{
    long na, nb;
    uint32_t ra, rb;
    int32_t la, lb;
    float *a = decode_all("gapless_a.mp3", &na, &ra, &la);      /* CBR, "Info" */
    float *b = decode_all("gapless_b.mp3", &nb, &rb, &lb);      /* VBR, "Xing" */
    CHECK_EQ_INT(ra, 44100);
    CHECK_EQ_INT(na, 50000);
    CHECK_EQ_INT(nb, 40000);
    CHECK_NEAR(la, 50000 * 1000 / 44100, 1);                       /* length from the tag's frame count */
    CHECK_NEAR(lb, 40000 * 1000 / 44100, 1);
    /* A then B against the ideal sine: the best alignment is no shift, at the start and across the join
     * (one sample off costs about 0.015 here; the codec's own error is about 0.025 at 96k) */
    float *ab = malloc((size_t)(na + nb) * sizeof *ab);
    for (long i = 0; i < na + nb; i++) ab[i] = i < na ? a[2 * i] : b[2 * (i - na)];
    CHECK_EQ_INT(best_shift(ab, 1000, 3000), 0);
    CHECK_EQ_INT(best_shift(ab, na - 1500, na + 1500), 0);
    CHECK(err_at(ab, 1000, 3000, 0) < 0.04);
    CHECK(err_at(ab, na - 1500, na + 1500, 0) < 0.1);          /* B is VBR q4: its first frames are rougher; a gap would be 0.5 */
    free(ab);
    free(a);
    free(b);
}

/* a resumed file starts mid-stream: no tag there, nothing trimmed, still plays */
static void test_mp3_gapless_resume_untrimmed(void)
{
    uint32_t len;
    uint8_t *data = load("gapless_a.mp3", &len);
    pc_reader_t r;
    pc_reader_mem(&r, data, len);
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_mp3.open(&h, &r, len / 2), 0);
    float buf[2 * 512];
    long total = 0;
    int n;
    while ((n = pc_backend_mp3.render(h, buf, 512)) > 0) total += n;
    CHECK(total > 20000 && total < 30000);
    pc_backend_mp3.close(h);
    free(data);
}

int main(void)
{
    RUN(test_mp3_gapless_trim);
    RUN(test_mp3_gapless_resume_untrimmed);
    RUN(test_mp3_byte_pos);
    RUN(test_mp3_skips_big_id3);
    RUN(test_mp3_decode);
    RUN(test_mp3_file_reader);
    RUN(test_mp3_truncated);
    RUN(test_mp3_not_audio);
    return 0;
}
