// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_loop.h"
#include "pc_test.h"

typedef struct { int len, pos; } fake_t;
static int opens, fail_open, src_len = 10000;

static int f_open(void *ctx, void **dec, uint32_t *rate)
{
    (void)ctx;
    if (fail_open) return -1;
    fake_t *f = calloc(1, sizeof *f);
    f->len = src_len;
    *dec = f;
    *rate = 44100;
    opens++;
    return 0;
}
static int f_read(void *dec, float *st, int frames)
{
    fake_t *f = dec;
    int n = f->len - f->pos < frames ? f->len - f->pos : frames;
    for (int i = 0; i < 2 * n; i++) st[i] = 0.5f;
    f->pos += n;
    return n;
}
static void f_close(void *dec) { free(dec); }
static const pc_layer_src_t SRC = { .open = f_open, .read = f_read, .close = f_close };

static void render_all(pc_loop_t *l, int total, float *minv, float *maxv)
{
    float buf[2 * 441];
    *minv = 10; *maxv = -10;
    for (int done = 0; done < total; done += 441) {
        CHECK_EQ_INT(pc_loop_render(l, buf, 441), 441);
        for (int i = 0; i < 2 * 441; i++) { if (buf[i] < *minv) *minv = buf[i]; if (buf[i] > *maxv) *maxv = buf[i]; }
    }
}

static void test_loop_no_gap(void)
{
    opens = 0; src_len = 10000;
    pc_loop_t *l = pc_loop_create(&SRC, 50);
    CHECK(l != NULL);
    CHECK_EQ_INT(pc_loop_rate(l), 44100);
    float mn, mx;
    render_all(l, 50000, &mn, &mx);
    CHECK(mn >= 0.3f);
    CHECK(pc_loop_seams(l) >= 5);
    pc_loop_destroy(l);
}

static void test_loop_seam_level(void)
{
    opens = 0; src_len = 10000;
    pc_loop_t *l = pc_loop_create(&SRC, 50);
    float mn, mx;
    render_all(l, 50000, &mn, &mx);
    CHECK(mx <= 0.5f * 1.41422f + 1e-3f);
    CHECK(mx > 0.6f);                          /* the equal-power crossfade actually happened */
    pc_loop_destroy(l);
}

static void test_loop_reopens(void)
{
    opens = 0; src_len = 10000;
    pc_loop_t *l = pc_loop_create(&SRC, 50);
    float mn, mx;
    render_all(l, 50000, &mn, &mx);
    CHECK_EQ_INT(opens, pc_loop_seams(l) + 2);
    pc_loop_destroy(l);
}

static void test_loop_short_source(void)
{
    opens = 0; src_len = 1000;                 /* shorter than the 2205-frame crossfade */
    pc_loop_t *l = pc_loop_create(&SRC, 50);
    float mn, mx;
    render_all(l, 20000, &mn, &mx);
    CHECK(mn >= 0.3f);
    pc_loop_destroy(l);
}

static void test_loop_open_fail(void)
{
    fail_open = 1;
    CHECK(pc_loop_create(&SRC, 50) == NULL);
    fail_open = 0;
}

static int z_read(void *dec, float *st, int frames) { (void)dec; (void)st; (void)frames; return 0; }
static void test_loop_empty_source(void)
{
    pc_layer_src_t s = SRC;
    s.read = z_read;
    pc_loop_t *l = pc_loop_create(&s, 50);
    CHECK(l != NULL);
    float buf[2 * 64];
    CHECK(pc_loop_render(l, buf, 64) < 0);     /* nothing ever decodes: error, not a hang */
    pc_loop_destroy(l);
}

static int rewinds, fail_rewind;
static int f_rewind(void *dec)
{
    if (fail_rewind) return -1;
    ((fake_t *)dec)->pos = 0;
    rewinds++;
    return 0;
}
static const pc_layer_src_t SRC_RW = { .open = f_open, .read = f_read, .close = f_close, .rewind = f_rewind };

/* a source that can rewind is reused at each seam: no decoder open on the audio thread */
static void test_loop_rewind_no_reopen(void)
{
    opens = 0; rewinds = 0; fail_rewind = 0; src_len = 10000;
    pc_loop_t *l = pc_loop_create(&SRC_RW, 50);
    CHECK(l != NULL);
    float mn, mx;
    render_all(l, 50000, &mn, &mx);
    CHECK(pc_loop_seams(l) >= 4);
    CHECK_EQ_INT(opens, 1);
    CHECK_EQ_INT(rewinds, pc_loop_seams(l));
    CHECK(mn >= 0.3f);                          /* no gap */
    CHECK(mx > 0.6f);                           /* still crossfaded */
    CHECK(mx <= 0.5f * 1.41422f + 1e-3f);
    pc_loop_destroy(l);
}

static void test_loop_rewind_short_source(void)
{
    opens = 0; rewinds = 0; fail_rewind = 0; src_len = 1000;
    pc_loop_t *l = pc_loop_create(&SRC_RW, 50);
    float mn, mx;
    render_all(l, 20000, &mn, &mx);
    CHECK(mn >= 0.3f);
    pc_loop_destroy(l);
}

/* a failed rewind falls back to opening a fresh decoder */
static void test_loop_rewind_fail_reopens(void)
{
    opens = 0; rewinds = 0; fail_rewind = 1; src_len = 10000;
    pc_loop_t *l = pc_loop_create(&SRC_RW, 50);
    float mn, mx;
    render_all(l, 50000, &mn, &mx);
    CHECK(mn >= 0.3f);
    CHECK(pc_loop_seams(l) >= 4);
    CHECK_EQ_INT(opens, pc_loop_seams(l) + 1);
    fail_rewind = 0;
    pc_loop_destroy(l);
}

int main(void)
{
    RUN(test_loop_rewind_no_reopen);
    RUN(test_loop_rewind_short_source);
    RUN(test_loop_rewind_fail_reopens);
    RUN(test_loop_no_gap);
    RUN(test_loop_seam_level);
    RUN(test_loop_reopens);
    RUN(test_loop_short_source);
    RUN(test_loop_open_fail);
    RUN(test_loop_empty_source);
    return 0;
}
