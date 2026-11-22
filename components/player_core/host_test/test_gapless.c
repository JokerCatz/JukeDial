// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_backend.h"
#include "pc/pc_gapless.h"
#include "pc_test.h"

/* mock source: frames k = 0..len-1 carry the value base + k; render hands out at most `chunk` per call */
typedef struct { int pos, len, chunk, base, fail_at; } mock_t;
static int m_render(void *h, float *out, int frames)
{
    mock_t *m = h;
    if (m->fail_at >= 0 && m->pos >= m->fail_at) return -1;
    int n = m->len - m->pos;
    if (n > frames) n = frames;
    if (n > m->chunk) n = m->chunk;
    for (int i = 0; i < n; i++) out[2 * i] = out[2 * i + 1] = (float)(m->base + m->pos + i);
    m->pos += n;
    return n;
}
static const pc_backend_t MOCK = { .render = m_render };

static void test_fill_short_reads(void)
{
    /* a decoder that returns short counts mid-stream is asked again: no zeros in the block */
    mock_t a = { 0, 1000, 7, 0, -1 };
    pc_play_src_t cur = { &MOCK, &a, 1.0f };
    float buf[2 * 480];
    pc_fill_t r = pc_gapless_fill(&cur, NULL, buf, 480);
    CHECK_EQ_INT(r.frames, 480);
    CHECK_EQ_INT(r.cur_rc, 1);
    for (int i = 0; i < 480; i++) CHECK_EQ_INT((int)buf[2 * i], i);
}

static void test_fill_joins_next(void)
{
    /* A (1000 frames) then B in 480-frame blocks: every frame of A then every frame of B, nothing between */
    mock_t a = { 0, 1000, 333, 0, -1 }, b = { 0, 700, 100, 1000, -1 };
    pc_play_src_t cur = { &MOCK, &a, 1.0f }, nxt = { &MOCK, &b, 1.0f };
    float buf[2 * 480];
    int expect = 0, blocks = 0, switched = 0;
    pc_play_src_t *c = &cur, *n = &nxt;
    for (;;) {
        pc_fill_t r = pc_gapless_fill(c, n, buf, 480);
        for (int i = 0; i < r.frames; i++) CHECK_EQ_INT((int)buf[2 * i], expect++);
        blocks++;
        if (r.next_frames >= 0) {                       /* the caller swaps */
            CHECK_EQ_INT(r.cur_rc, 0);
            CHECK_EQ_INT(r.frames, 480);                /* nothing inserted at the join */
            CHECK_EQ_INT(r.next_frames, 480 - 40);      /* 1000 = 2 * 480 + 40 */
            c = n; n = NULL; switched++;
            continue;
        }
        if (r.cur_rc <= 0) break;                       /* B ended mid-block: the caller pads */
        CHECK(r.frames == 480 || r.cur_rc == 1);
    }
    CHECK_EQ_INT(switched, 1);
    CHECK_EQ_INT(expect, 1700);
}

static void test_fill_gain_per_source(void)
{
    /* ReplayGain: each source's own gain, applied up to and from the join */
    mock_t a = { 0, 10, 100, 1, -1 }, b = { 0, 100, 100, 1, -1 };
    pc_play_src_t cur = { &MOCK, &a, 0.5f }, nxt = { &MOCK, &b, 2.0f };
    float buf[2 * 20];
    pc_fill_t r = pc_gapless_fill(&cur, &nxt, buf, 20);
    CHECK_EQ_INT(r.frames, 20);
    CHECK_NEAR(buf[0], 0.5, 1e-6);                     /* a: 1 * 0.5 */
    CHECK_NEAR(buf[2 * 9 + 1], 5.0, 1e-6);             /* a: 10 * 0.5 */
    CHECK_NEAR(buf[2 * 10], 2.0, 1e-6);                /* b: 1 * 2 */
    CHECK_NEAR(buf[2 * 19], 20.0, 1e-6);
}

static void test_fill_end_without_next(void)
{
    mock_t a = { 0, 100, 64, 0, -1 };
    pc_play_src_t cur = { &MOCK, &a, 1.0f };
    float buf[2 * 480];
    pc_fill_t r = pc_gapless_fill(&cur, NULL, buf, 480);
    CHECK_EQ_INT(r.frames, 100);                       /* the caller pads the block */
    CHECK_EQ_INT(r.cur_rc, 0);
    CHECK_EQ_INT(r.next_frames, -1);
}

static void test_fill_error(void)
{
    mock_t a = { 0, 1000, 50, 0, 120 }, b = { 0, 100, 100, 0, -1 };
    pc_play_src_t cur = { &MOCK, &a, 1.0f }, nxt = { &MOCK, &b, 1.0f };
    float buf[2 * 480];
    pc_fill_t r = pc_gapless_fill(&cur, &nxt, buf, 480);
    CHECK_EQ_INT(r.cur_rc, -1);                        /* a decode error is not a clean end: no join */
    CHECK_EQ_INT(r.frames, 150);
    CHECK_EQ_INT(r.next_frames, -1);
    a.pos = a.fail_at = 0;
    r = pc_gapless_fill(&cur, &nxt, buf, 480);
    CHECK_EQ_INT(r.frames, 0);
    CHECK_EQ_INT(r.cur_rc, -1);
}

static void test_remaining(void)
{
    CHECK_EQ_INT(pc_gapless_remaining_ms(60000, 52000, 0, 0, 0), 8000);   /* the backend knows its length */
    CHECK_EQ_INT(pc_gapless_remaining_ms(60000, 61000, 0, 0, 0), 0);
    /* unknown length: the bytes left at the rate read so far (bytes read / time played) */
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 10000, 1000000, 200000, 200000), 40000);
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 10000, 1000000, 1000000, 1000000), 0);
    /* a resumed file (or one with a big tag skipped): only the bytes actually read count */
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 10000, 8000000, 7600000, 200000), 20000);
    /* too early to tell: the decoder's read-ahead would make the rate look huge */
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 500, 8000000, 16384, 16384), -1);
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 0, 1000000, 16384, 16384), -1);
    CHECK_EQ_INT(pc_gapless_remaining_ms(-1, 5000, 0, 0, 0), -1);         /* no size: unknown */
}

int main(void)
{
    RUN(test_fill_short_reads);
    RUN(test_fill_joins_next);
    RUN(test_fill_gain_per_source);
    RUN(test_fill_end_without_next);
    RUN(test_fill_error);
    RUN(test_remaining);
    return 0;
}
