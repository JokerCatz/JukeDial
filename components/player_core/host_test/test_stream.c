// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include "pc/pc_stream.h"
#include "pc_test.h"

static uint8_t *load(const char *name, uint32_t *len)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*len);
    CHECK(fread(d, 1, *len, f) == *len);
    fclose(f);
    return d;
}

static float peak(const float *b, int frames)
{
    float m = 0;
    for (int i = 0; i < 2 * frames; i++) m = fabsf(b[i]) > m ? fabsf(b[i]) : m;
    return m;
}

static uint32_t s_len, s_fed;
static uint8_t *s_mp3;
static uint8_t s_mem[256 * 1024];
static pc_ring_t s_ring;
static void feed(uint32_t n) { if (n > s_len - s_fed) n = s_len - s_fed; s_fed += pc_ring_write(&s_ring, s_mp3 + s_fed, n); }

static void *open_stream(pc_stream_t *st)
{
    s_mp3 = load("song_head.mp3", &s_len);
    s_fed = 0;
    pc_ring_init(&s_ring, s_mem, sizeof s_mem);
    *st = (pc_stream_t){ .ring = &s_ring, .inner = &pc_backend_mp3, .prebuffer = 32768 };
    feed(40000);                                      /* the network task starts playback after the prebuffer */
    pc_reader_t r = { .u = st };
    void *h = NULL;
    CHECK_EQ_INT(pc_backend_stream.open(&h, &r, 0), 0);
    CHECK(pc_backend_stream.rate(h) > 0);
    return h;
}

static void test_plays_then_pauses_on_a_stall(void)
{
    pc_stream_t st;
    void *h = open_stream(&st);
    st.prebuffer = 16384;
    float b[2 * 480];
    for (int i = 0; i < 20; i++) CHECK_EQ_INT(pc_backend_stream.render(h, b, 480), 480);
    CHECK(pc_backend_stream.position_ms(h) >= 190);   /* 40 KB in: it plays */
    int i;
    for (i = 0; i < 2000 && !st.buffering; i++)       /* no more data: runs dry */
        CHECK_EQ_INT(pc_backend_stream.render(h, b, 480), 480);
    CHECK(st.buffering);
    CHECK_EQ_INT(st.underruns, 1);
    int32_t at = pc_backend_stream.position_ms(h);
    CHECK_EQ_INT(pc_backend_stream.render(h, b, 480), 480);   /* a stall is silence, not the end */
    CHECK(peak(b, 480) == 0.0f);
    CHECK_EQ_INT(pc_backend_stream.position_ms(h), at);
    feed(st.prebuffer - pc_ring_used(&s_ring) - 1000);  /* still under the prebuffer: waits */
    CHECK_EQ_INT(pc_backend_stream.render(h, b, 480), 480);
    CHECK(peak(b, 480) == 0.0f);
    CHECK_EQ_INT(pc_backend_stream.position_ms(h), at);
    feed(2000);                                       /* refilled: plays again */
    for (int k = 0; k < 10; k++) CHECK_EQ_INT(pc_backend_stream.render(h, b, 480), 480);
    CHECK(!st.buffering);
    CHECK(pc_backend_stream.position_ms(h) >= at + 90);
    CHECK(peak(b, 480) > 0.0f);
    CHECK_EQ_INT(st.underruns, 1);
    pc_backend_stream.close(h);
    free(s_mp3);
}

static void test_ends_only_after_eof_and_drained(void)
{
    pc_stream_t st;
    void *h = open_stream(&st);
    feed(s_len);                                      /* the rest of the file */
    st.eof = true;
    float b[2 * 480];
    int n, guard = 0;
    while ((n = pc_backend_stream.render(h, b, 480)) == 480 && guard++ < 100000) {}
    CHECK(n >= 0 && n < 480);                         /* end of stream */
    CHECK_EQ_INT(st.underruns, 0);
    CHECK_EQ_INT(pc_ring_used(&s_ring), 0);
    pc_backend_stream.close(h);
    free(s_mp3);
}

int main(void)
{
    RUN(test_plays_then_pauses_on_a_stall);
    RUN(test_ends_only_after_eof_and_drained);
    return 0;
}
