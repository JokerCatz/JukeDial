// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_vizdraw.h"
#include "pc/pc_voice.h"
#include "pc_test.h"

#define RS PC_VD_RING_SZ                  /* the ring and the circular scope: square images of this size */
#define RIN PC_VD_RING_IN
#define ROUT PC_VD_RING_OUT
#define RCT ((RS - 1) * 0.5f)               /* the centre of those images */
#define GUARD 64
static uint16_t *guarded(int w, int h)
{
    size_t n = (size_t)w * h;
    uint16_t *p = malloc((n + 2 * GUARD) * 2);
    CHECK(p);
    for (size_t i = 0; i < n + 2 * GUARD; i++) p[i] = 0xA5A5;
    for (size_t i = 0; i < n; i++) p[GUARD + i] = 0;
    return p + GUARD;
}
static void guards_ok(const uint16_t *px, int w, int h)
{
    for (int i = 0; i < GUARD; i++) { CHECK(px[-1 - i] == 0xA5A5); CHECK(px[(size_t)w * h + i] == 0xA5A5); }
}
static void gfree(uint16_t *px) { free(px - GUARD); }
static void rect_covers(const uint16_t *before, const uint16_t *after, int w, int h, pc_vd_rect_t r)
{
    if (r.w) CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h && r.h > 0);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        bool in = r.w && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        if (!in) CHECK(before[y * w + x] == after[y * w + x]);
    }
}

static pc_vd_t D;
static void setup(uint16_t *px, int w, int h)
{
    pc_vd_init(&D, px, w, h);
    pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
}
static void tone(float *a, float v) { for (int i = 0; i < 64; i++) a[i] = v; }

static void test_kinds(void)
{
    const int want[PC_VD_KINDS] = { PC_VD_ROUND_SCREEN ? 6 : 4, 4, 3, 3, 2, 3, 1, 2, 2 };       /* the round build adds bars Lens and Arch */
    for (int k = 0; k < PC_VD_KINDS; k++) {
        CHECK_EQ_INT(pc_vd_variants((pc_vd_kind_t)k), want[k]);
        CHECK_EQ_INT(pc_vd_kind_parse(pc_vd_kind_name((pc_vd_kind_t)k)), k);
        for (int v = 0; v < want[k]; v++) {
            int w, h;
            pc_vd_size((pc_vd_kind_t)k, v, &w, &h);
            if (k == PC_VD_LYRICS) { CHECK(w == 0 && h == 0); } else CHECK(w * h <= PC_VD_MAX_PX && w <= 340 && w > 0 && h > 0);   /* lyrics have no picture */
            CHECK(pc_vd_var_label((pc_vd_kind_t)k, v, true)[0] && pc_vd_var_label((pc_vd_kind_t)k, v, false)[0]);
        }
        CHECK_STR(pc_vd_var_label((pc_vd_kind_t)k, 99, false), pc_vd_var_label((pc_vd_kind_t)k, 0, false));
        CHECK_STR(pc_vd_var_label((pc_vd_kind_t)k, -1, true), pc_vd_var_label((pc_vd_kind_t)k, 0, true));
    }
    CHECK_EQ_INT(PC_VD_KINDS, 9);                                     /* bars, ring, scope, fall, phase, voices, rows, roll, lyrics: VU is gone */
    CHECK_EQ_INT(PC_VD_VOICES, 5);                                    /* the stored kind names keep their places */
    static const char *const names[9] = { "bars", "ring", "scope", "fall", "phase", "voices", "rows", "roll", "lyrics" };
    for (int k = 0; k < 9; k++) CHECK_STR(pc_vd_kind_name((pc_vd_kind_t)k), names[k]);
    CHECK_EQ_INT(pc_vd_kind_parse("vu"), PC_VD_BARS);                 /* an old stored "vu" is an unknown name */
    CHECK_STR(pc_vd_kind_name((pc_vd_kind_t)9), "bars");              /* past the last kind: out of range */
    {   int w, h;                                                     /* the round pictures use the screen (spec amendment 2026-11-12) */
        pc_vd_size(PC_VD_RING, 0, &w, &h); CHECK(w == 340 && h == 340);
        for (int v = 0; v < 3; v++) { pc_vd_size(PC_VD_SCOPE, v, &w, &h); CHECK(v == 2 ? (w == 340 && h == 340) : (w == 320 && h == 160)); }
        pc_vd_size(PC_VD_PHASE, 0, &w, &h); CHECK(w == 300 && h == 300);
        pc_vd_size(PC_VD_FALL, 0, &w, &h); CHECK(w == 256 && h == 200);
        pc_vd_size(PC_VD_BARS, 0, &w, &h); CHECK(w == 300 && h == 200);
        CHECK_EQ_INT(PC_VD_MAX_PX, 340 * 340);
        CHECK(PC_VD_RING_IN == 60 && PC_VD_RING_OUT == 168);
    }
    CHECK_EQ_INT(pc_vd_kind_parse("xyz"), PC_VD_BARS);
    CHECK_EQ_INT(pc_vd_kind_parse(NULL), PC_VD_BARS);
    CHECK_STR(pc_vd_kind_name(PC_VD_PHASE), "phase");
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 2, true), "Mirror");
    CHECK_STR(pc_vd_var_label(PC_VD_RING, 3, false), "點");
}

static int bar_cx(int i, int bw) { int nb = (302 / (bw + 2)); if (nb > 32) nb = 32; return (300 - (nb * bw + (nb - 1) * 2)) / 2 + i * (bw + 2) + bw / 2; }

static void test_bars_heights(void)
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    float lv[64];
    for (int i = 0; i < 32; i++) lv[2 * i] = lv[2 * i + 1] = i / 31.0f;
    pc_vd_bars(&D, 0, lv, lv, 7);
    for (int i = 0; i < 32; i++) {
        int n = 0, x = bar_cx(i, 7);
        for (int y = 0; y < 200; y++) if (px[y * 300 + x] != D.bg) n++;
        CHECK_EQ_INT(n, (int)lroundf(i / 31.0f * 200));
        if (i > 0 && i < 31) { CHECK(px[(200 - n) * 300 + x] != D.bg); CHECK(px[(199 - n) * 300 + x] == D.bg); }
    }
    CHECK(px[199 * 300 + bar_cx(31, 7)] != D.bg);          /* bass left, bars grow upwards */
    CHECK(px[0 * 300 + bar_cx(31, 7)] == D.fg);            /* full bar: the cap sits at the top rows */
    guards_ok(px, 300, 200); gfree(px);
}

static void test_bars_peak(void)
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    float lv[64], pk[64];
    tone(lv, 0.25f); tone(pk, 0.75f);
    pc_vd_bars(&D, 0, lv, pk, 7);
    for (int i = 0; i < 32; i++) {
        int x = bar_cx(i, 7), y = 200 - (int)lroundf(0.75f * 200);
        CHECK(px[y * 300 + x] == D.fg);
        CHECK(px[(y + 1) * 300 + x] == D.fg);
        CHECK(px[(y - 1) * 300 + x] == D.bg);
        CHECK(px[(y + 2) * 300 + x] == D.bg);
    }
    guards_ok(px, 300, 200); gfree(px);
}

static int count_rows(const uint16_t *px, int x, int y0, int y1, uint16_t bg)
{
    int n = 0;
    for (int y = y0; y < y1; y++) if (px[y * 300 + x] != bg) n++;
    return n;
}

static void test_bars_variants(void)
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    float lv[64], pk[64];
    for (int i = 0; i < 32; i++) lv[2 * i] = lv[2 * i + 1] = i / 31.0f;
    memcpy(pk, lv, sizeof pk);
    pc_vd_bars(&D, 1, lv, pk, 7);
    int x = bar_cx(31, 7);
    CHECK(px[199 * 300 + x] != D.bg && px[195 * 300 + x] != D.bg);   /* rows 0..4 from the bottom: filled */
    CHECK(px[194 * 300 + x] == D.bg && px[193 * 300 + x] == D.bg);   /* rows 5, 6: gap */
    CHECK(px[192 * 300 + x] != D.bg);
    pc_vd_bars(&D, 2, lv, pk, 7);
    for (int i = 1; i < 32; i++) {
        int c = bar_cx(i, 7);
        int up = count_rows(px, c, 0, 100, D.bg), dn = count_rows(px, c, 100, 200, D.bg);
        CHECK_EQ_INT(up, dn);
        CHECK_EQ_INT(up, (int)lroundf(i / 31.0f * 100));
    }
    pc_vd_bars(&D, 3, lv, pk, 7);
    int c = bar_cx(15, 7), top = 200 - (int)lroundf(15 / 31.0f * 200);
    CHECK(px[top * 300 + c] != D.bg);
    CHECK(px[(top + 10) * 300 + c] == D.bg);
    CHECK(px[199 * 300 + c] == D.bg);                                  /* line style: nothing below the line */
    CHECK(px[(top - 1) * 300 + c] == D.bg);
    guards_ok(px, 300, 200); gfree(px);
}

static void test_bars_width(void)
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    float lv[64], pk[64];
    tone(lv, 1); tone(pk, 1);
    const int want[3] = { 222, 286, 295 }, bw[3] = { 5, 7, 9 };
    for (int t = 0; t < 3; t++) {
        D.fresh = true;
        pc_vd_bars(&D, 0, lv, pk, bw[t]);
        int l = 300, r = -1;
        for (int y = 0; y < 200; y++) for (int x = 0; x < 300; x++) if (px[y * 300 + x] != D.bg) { if (x < l) l = x; if (x > r) r = x; }
        CHECK_EQ_INT(r - l + 1, want[t]);
        CHECK(l + r == 299 || l + r == 298);                           /* centred */
    }
    for (int t = 0; t < 3; t++) {                                      /* invalid widths behave as 7 */
        static const int bad[3] = { 0, -3, 400 };
        D.fresh = true;
        pc_vd_bars(&D, 0, lv, pk, bad[t]);
        int l = 300, r = -1;
        for (int y = 0; y < 200; y++) for (int x = 0; x < 300; x++) if (px[y * 300 + x] != D.bg) { if (x < l) l = x; if (x > r) r = x; }
        CHECK_EQ_INT(r - l + 1, 286);
    }
    guards_ok(px, 300, 200); gfree(px);
}

static void test_bars_dirty(void)
{
    uint16_t *px = guarded(300, 200), *before = malloc(300 * 200 * 2);
    for (int var = 0; var < 4; var++) {
        setup(px, 300, 200);
        float lv[64], pk[64];
        for (int i = 0; i < 64; i++) { lv[i] = 0.2f + 0.01f * i; pk[i] = lv[i] + 0.1f; }
        pc_vd_rect_t r = pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(r.w == 300 && r.h == 200);                               /* fresh: the whole image */
        r = pc_vd_bars(&D, var, lv, pk, 7);
        CHECK_EQ_INT(r.w, 0);
        memcpy(before, px, 300 * 200 * 2);
        lv[10] = 0.9f; lv[11] = 0.1f; pk[10] = 0.95f;
        r = pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(r.w > 0 && r.w < 30);
        rect_covers(before, px, 300, 200, r);
        memcpy(before, px, 300 * 200 * 2);
        lv[10] = 0.3f; lv[40] = 0.0f;                                  /* a bar with a lower value: the old pixels must go */
        r = pc_vd_bars(&D, var, lv, pk, 7);
        rect_covers(before, px, 300, 200, r);
        /* the same picture drawn from scratch is identical to the incremental one */
        uint16_t *ref = guarded(300, 200);
        pc_vd_t *save = malloc(sizeof D); memcpy(save, &D, sizeof D);
        pc_vd_init(&D, ref, 300, 200); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
        pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(!memcmp(ref, px, 300 * 200 * 2));
        memcpy(&D, save, sizeof D); free(save); gfree(ref);
        pc_vd_set_colors(&D, PC_RAMP_ICE, NULL);
        r = pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(r.w == 300 && r.h == 200);
    }
    guards_ok(px, 300, 200); gfree(px); free(before);
}

/* ring geometry: angle clockwise from straight down (deg) and distance, pixel centre (x - 119.5) */
static void polar(int x, int y, float *deg, float *dist)
{
    float dx = x - RCT, dy = y - RCT;
    *dist = sqrtf(dx * dx + dy * dy);
    float th = atan2f(-dx, dy) * 57.2957795f;
    *deg = th < 0 ? th + 360 : th;
}
static void ring_stats(const uint16_t *px, uint16_t bg, int *n, float *dmin, float *dmax, float *amin, float *amax, int *wedge_ok_mirror)
{
    *n = 0; *dmin = 1e9f; *dmax = 0; *amin = 1e9f; *amax = -1e9f; *wedge_ok_mirror = 1;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != bg) {
        float a, r; polar(x, y, &a, &r);
        (*n)++;
        if (r < *dmin) *dmin = r;
        if (r > *dmax) *dmax = r;
        if (a < *amin) *amin = a;
        if (a > *amax) *amax = a;
        if (!(a <= 5.625f + 0.01f || a >= 360 - 5.625f - 0.01f)) *wedge_ok_mirror = 0;
    }
}

static void test_ring(void)
{
    uint16_t *px = guarded(RS, RS), *before = malloc(RS * RS * 2);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64], z[64];
    tone(z, 0);
    for (int var = 0; var < 4; var++) {
        setup(px, RS, RS);
        int n, mo; float dmin, dmax, amin, amax;
        memset(lv, 0, sizeof lv); lv[0] = 1;
        pc_vd_rect_t r = pc_vd_ring(&D, var, map, lv, z);
        CHECK(r.w == RS && r.h == RS);
        ring_stats(px, D.bg, &n, &dmin, &dmax, &amin, &amax, &mo);
        CHECK(n > (var == 3 ? 20 : 50));
        if (var == 2) CHECK(mo && amax > 354.0f);   /* one wedge (5.625 deg) on each side of the bottom */
        else { CHECK(amin >= -0.01f && amax <= 5.625f + 0.01f); }
        if (var == 2) CHECK(amin < 2.0f);                              /* both sides start straight down */
        if (var == 0 || var == 1) CHECK(dmin <= RIN + 1.5f && dmax >= ROUT - 1.0f && dmin >= RIN - 0.1f && dmax <= ROUT + 0.1f);   /* full level reaches the outer radius, nothing beyond */
        if (var == 2) CHECK(dmin <= RIN + 1.5f && dmax >= ROUT - 1.0f);
        if (var == 3) CHECK(dmax - dmin <= 6.0f && dmax >= ROUT - 1.5f);
        {   /* every wedge at full level and full peak: still nothing farther than 170 from the centre */
            float one[64];
            tone(one, 1.0f);
            D.fresh = true;
            pc_vd_ring(&D, var, map, one, one);
            ring_stats(px, D.bg, &n, &dmin, &dmax, &amin, &amax, &mo);
            CHECK(dmax <= 170.0f && dmin >= RIN - 0.1f);
        }
        memset(lv, 0, sizeof lv); lv[0] = 1;
        D.fresh = true;
        pc_vd_ring(&D, var, map, lv, z);
        /* a half-length wedge */
        lv[0] = 0.5f;
        D.fresh = true;
        pc_vd_ring(&D, var, map, lv, z);
        ring_stats(px, D.bg, &n, &dmin, &dmax, &amin, &amax, &mo);
        CHECK(n > 20);
        if (var == 0 || var == 2) CHECK(dmax <= RIN + (ROUT - RIN) / 2 + 3.0f);
        if (var == 1) CHECK(dmin >= ROUT - (ROUT - RIN) / 2 - 3.0f && dmax >= ROUT - 1.0f);
        if (var == 3) CHECK(dmax - dmin <= 6.0f);
        /* all zero: nothing */
        D.fresh = true;
        pc_vd_ring(&D, var, map, z, z);
        ring_stats(px, D.bg, &n, &dmin, &dmax, &amin, &amax, &mo);
        CHECK_EQ_INT(n, 0);
        /* dirty rectangle */
        for (int i = 0; i < 64; i++) { lv[i] = 0.3f + 0.6f * fabsf(sinf(i * 0.21f)); pk[i] = lv[i] + 0.08f; }
        pc_vd_ring(&D, var, map, lv, pk);
        r = pc_vd_ring(&D, var, map, lv, pk);
        CHECK_EQ_INT(r.w, 0);
        memcpy(before, px, RS * RS * 2);
        lv[20] = 0.1f; pk[21] = 0.9f;
        r = pc_vd_ring(&D, var, map, lv, pk);
        CHECK(r.w > 0 && r.w < RS);
        rect_covers(before, px, RS, RS, r);
        uint16_t *ref = guarded(RS, RS);
        pc_vd_t *save = malloc(sizeof D); memcpy(save, &D, sizeof D);
        pc_vd_init(&D, ref, RS, RS); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
        pc_vd_ring(&D, var, map, lv, pk);
        CHECK(!memcmp(ref, px, RS * RS * 2));
        memcpy(&D, save, sizeof D); free(save); gfree(ref);
    }
    guards_ok(px, RS, RS);
    free(map); gfree(px); free(before);
}

static void test_ring_bass_bottom(void)    /* wedge 0 below the centre and clockwise = towards the left on the screen */
{
    uint16_t *px = guarded(RS, RS);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    setup(px, RS, RS);
    float lv[64] = { 0 }, z[64] = { 0 };
    lv[0] = 1;
    pc_vd_ring(&D, 0, map, lv, z);
    CHECK(px[290 * RS + RS / 2 - 2] != D.bg);          /* just left of the vertical line, below the centre */
    CHECK(px[290 * RS + RS / 2 + 2] == D.bg);          /* right of it: not this wedge */
    CHECK(px[50 * RS + RS / 2 - 2] == D.bg);           /* the top is untouched */
    lv[0] = 0; lv[16] = 1;                       /* wedge 16 = a quarter turn clockwise = the left side */
    D.fresh = true;
    pc_vd_ring(&D, 0, map, lv, z);
    CHECK(px[170 * RS + 30] != D.bg || px[169 * RS + 30] != D.bg || px[170 * RS + 40] != D.bg);
    CHECK(px[170 * RS + 310] == D.bg);
    free(map); gfree(px);
}

static int count_px(const uint16_t *px, int n, uint16_t c) { int k = 0; for (int i = 0; i < n; i++) k += px[i] == c; return k; }

static void test_ring_mirror_bands(void)
{
    uint16_t *px = guarded(RS, RS), *ref = guarded(RS, RS);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], z[64];
    tone(z, 0);
    for (int b = 0; b < 64; b++) {                 /* every single band reaches the picture; a pair shares its wedges */
        setup(px, RS, RS);
        memset(lv, 0, sizeof lv); lv[b] = 1;
        pc_vd_ring(&D, 2, map, lv, z);
        CHECK(count_px(px, RS * RS, D.bg) < RS * RS - 50);
        setup(ref, RS, RS);
        memset(lv, 0, sizeof lv); lv[b ^ 1] = 1;
        pc_vd_ring(&D, 2, map, lv, z);
        CHECK(!memcmp(px, ref, RS * RS * 2));
    }
    setup(px, RS, RS);                           /* left/right symmetry for a busy input */
    for (int i = 0; i < 64; i++) lv[i] = 0.1f + 0.8f * fabsf(sinf(i * 0.37f));
    pc_vd_ring(&D, 2, map, lv, lv);
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS / 2; x++) CHECK(px[y * RS + x] == px[y * RS + RS - 1 - x]);
    free(map); gfree(px); gfree(ref);
}

static void test_peaks(void)
{
    uint16_t *px = guarded(RS, RS), *bp = guarded(300, 200);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64] = { 0 }, pk[64] = { 0 }, z[64] = { 0 };
    lv[5] = 0.3f; pk[5] = 0.8f;
    for (int var = 0; var < 4; var++) {            /* ring: an fg arc 2 px thick at the peak (L = 109, peak bucket 87) */
        setup(px, RS, RS);
        pc_vd_ring(&D, var, map, lv, z);
        CHECK_EQ_INT(count_px(px, RS * RS, D.fg), 0);
        D.fresh = true;
        pc_vd_ring(&D, var, map, lv, pk);
        CHECK(count_px(px, RS * RS, D.fg) > 8);
        const int L = ROUT - RIN + 1, pb = (int)(0.8f * (float)L + 0.5f);     /* L buckets, the peak arc is the buckets pb-2, pb-1 */
        float lo = var == 1 ? RIN + (L - pb) - 0.5f : RIN + pb - 2 - 0.5f, hi = lo + 3.0f;   /* out: RIN+pb-2..pb, in: RIN+(L-pb)..(L-pb+2) */
        for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] == D.fg) {
            float a, r; polar(x, y, &a, &r);
            CHECK(r >= lo && r <= hi);
        }
    }
    for (int var = 0; var < 4; var++) {            /* bars: cap rows 2 px at the peak; the line style has no cap by design */
        setup(bp, 300, 200);
        float l2[64] = { 0 }, p2[64] = { 0 };
        l2[10] = l2[11] = 0.3f; p2[10] = p2[11] = 0.8f;
        pc_vd_bars(&D, var, l2, z, 7);
        CHECK_EQ_INT(count_px(bp, 300 * 200, D.fg), 0);
        D.fresh = true;
        pc_vd_bars(&D, var, l2, p2, 7);
        int x = bar_cx(5, 7), n = count_px(bp, 300 * 200, D.fg);
        if (var == 3) { CHECK_EQ_INT(n, 0); continue; }
        if (var == 2) {
            CHECK(bp[20 * 300 + x] == D.fg && bp[21 * 300 + x] == D.fg);       /* upper: mid - 80, +1 */
            CHECK(bp[178 * 300 + x] == D.fg && bp[179 * 300 + x] == D.fg);     /* lower: mid + 80 - 2, -1 */
            CHECK_EQ_INT(n, 4 * 7);
        } else {
            CHECK(bp[40 * 300 + x] == D.fg && bp[41 * 300 + x] == D.fg);       /* 200 - 160 */
            CHECK_EQ_INT(n, 2 * 7);
        }
    }
    free(map); gfree(px); gfree(bp);
}

static void test_style_switch(void)     /* another style (scope: 320 columns) may leave anything in prev_a/prev_b */
{
    uint16_t *px = guarded(RS, RS), *ref = guarded(RS, RS);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64];
    for (int i = 0; i < 64; i++) { lv[i] = 0.2f + 0.7f * fabsf(sinf(i * 0.3f)); pk[i] = lv[i] + 0.05f; }
    for (int to = 0; to < 2; to++) for (int var = 0; var < 4; var++) {
        setup(px, RS, RS);
        if (to) pc_vd_bars(&D, var, lv, pk, 7); else pc_vd_ring(&D, var, map, lv, pk);
        for (int i = 0; i < 320; i++) { D.prev_a[i] = (int16_t)(7 + i % 50); D.prev_b[i] = (int16_t)(3 + i % 20); }
        D.prev_b[319] = to ? 17 + var : 1 + var;      /* the values the old in-array tag would have matched */
        D.prev_b[318] = to ? 0 : 7;
        for (int i = 0; i < RS * RS; i++) px[i] = 0x1234;
        if (to) pc_vd_ring(&D, var, map, lv, pk); else pc_vd_bars(&D, var, lv, pk, 7);
        setup(ref, RS, RS);
        if (to) pc_vd_ring(&D, var, map, lv, pk); else pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(!memcmp(px, ref, RS * RS * 2));
    }
    free(map); gfree(px); gfree(ref);
}

static void test_single_bar_line(void)
{
    uint16_t *px = guarded(300, 200), *ref = guarded(300, 200);
    pc_vd_t *inc = malloc(sizeof D);
    float lv[64], z[64];
    tone(z, 0);
    const float seq[5] = { 0.8f, 0.3f, 0.5f, 0.0f, 0.6f };
    for (int bw = 150; bw <= 300; bw += 150) for (int var = 0; var < 4; var++) {
        setup(px, 300, 200);
        for (int t = 0; t < 5; t++) {
            tone(lv, seq[t]);
            pc_vd_bars(&D, var, lv, z, bw);
            memcpy(inc, &D, sizeof D);
            setup(ref, 300, 200);
            pc_vd_bars(&D, var, lv, z, bw);
            CHECK(!memcmp(px, ref, 300 * 200 * 2));
            memcpy(&D, inc, sizeof D);
        }
    }
    free(inc); gfree(px); gfree(ref);
}

static void test_edges(void)
{
    uint16_t *px = guarded(10, 10);
    pc_vd_set_colors(NULL, PC_RAMP_FIRE, NULL);                         /* no-op */
    pc_vd_init(&D, px, 100000, 100000);                                 /* the product overflows 32 bits: refused */
    CHECK_EQ_INT(D.w, 0);
    pc_vd_init(&D, px, 65536, 65536);
    CHECK_EQ_INT(D.w, 0);
    CHECK_EQ_INT(pc_vd_ringmap_bytes(100000, 100000), 0);
    CHECK_EQ_INT(pc_vd_ringmap_bytes(65536, 65536), 0);
    guards_ok(px, 10, 10); gfree(px);
}

static void test_ringmap_cap(void)
{
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    CHECK_EQ_INT((int)cap, 254126);                      /* 2 * (391 + 64 * 173 + 340 * 340) bytes: PSRAM, 2 bytes per image pixel at most */
    uint8_t *buf = malloc(cap + 64);
    memset(buf, 0xA5, cap + 64);
    CHECK(!pc_vd_ringmap_build(buf, cap - 1, RS, RS, RIN, ROUT, 0));
    for (size_t i = 0; i < cap + 64; i++) CHECK(buf[i] == 0xA5);
    CHECK(pc_vd_ringmap_build(buf, cap, RS, RS, RIN, ROUT, 0));
    for (size_t i = cap; i < cap + 64; i++) CHECK(buf[i] == 0xA5);
    CHECK(!pc_vd_ringmap_build(buf, cap, RS, RS, 50, 40, 0));
    CHECK(!pc_vd_ringmap_build(NULL, cap, RS, RS, RIN, ROUT, 0));
    free(buf);
}

static void test_bad_input(void)
{
    uint16_t *px = guarded(300, 200), *rp = guarded(RS, RS), *zp = guarded(1, 1);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float v[64], pk[64];
    for (int var = 0; var < 4; var++) for (int c = 0; c < 6; c++) {
        for (int i = 0; i < 64; i++) {
            v[i] = c == 0 ? 0 : c == 1 ? 1 : c == 2 ? NAN : c == 3 ? (i % 2 ? -5.0f : 5.0f) : c == 4 ? INFINITY : -INFINITY;
            pk[i] = c == 3 ? 5.0f : v[i];
        }
        setup(px, 300, 200);
        pc_vd_bars(&D, var, v, pk, 7);
        pc_vd_bars(&D, var, v, pk, 7);
        guards_ok(px, 300, 200);
        setup(rp, RS, RS);
        pc_vd_ring(&D, var, map, v, pk);
        pc_vd_ring(&D, var, map, v, pk);
        guards_ok(rp, RS, RS);
    }
    /* a 0 x 0 image: nothing is written */
    pc_vd_init(&D, zp, 0, 0);
    for (int i = 0; i < 64; i++) v[i] = 1;
    for (int var = -1; var < 5; var++) {
        CHECK_EQ_INT(pc_vd_bars(&D, var, v, v, 7).w, 0);
        CHECK_EQ_INT(pc_vd_ring(&D, var, map, v, v).w, 0);
    }
    guards_ok(zp, 1, 1);
    CHECK_EQ_INT(zp[0], 0);
    /* a map for another size, NULL inputs */
    setup(px, 300, 200);
    CHECK_EQ_INT(pc_vd_ring(&D, 0, map, v, v).w, 0);
    setup(rp, RS, RS);
    CHECK_EQ_INT(pc_vd_ring(&D, 0, NULL, v, v).w, 0);
    CHECK_EQ_INT(pc_vd_ring(&D, 0, map, NULL, v).w, 0);
    CHECK_EQ_INT(pc_vd_bars(&D, 0, v, NULL, 7).w, 0);
    guards_ok(px, 300, 200); guards_ok(rp, RS, RS);
    free(map); gfree(px); gfree(rp); gfree(zp);
}


/* ---- scope and waterfall ---- */
static float WAVE[600];
static void dc(float v, int n) { for (int i = 0; i < n; i++) WAVE[i] = v; }
static int rows_nonbg(const uint16_t *px, int w, int h, uint16_t bg, int *lo, int *hi, int *xl, int *xr)
{
    int n = 0; *lo = h; *hi = -1; *xl = w; *xr = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (px[y * w + x] != bg) {
        n++; if (y < *lo) *lo = y; if (y > *hi) *hi = y; if (x < *xl) *xl = x; if (x > *xr) *xr = x;
    }
    return n;
}

static void test_scope_line(void)
{
    uint16_t *px = guarded(320, 160);
    const float v[3] = { 0, 1, -1 };
    const int y0[3] = { 79, 0, 158 };
    for (int t = 0; t < 3; t++) {
        setup(px, 320, 160);
        dc(v[t], 600);
        pc_vd_rect_t r = pc_vd_scope(&D, 0, WAVE, 600);
        CHECK(r.x == 0 && r.y == 0 && r.w == 320 && r.h == 160);
        int lo, hi, xl, xr, n = rows_nonbg(px, 320, 160, D.bg, &lo, &hi, &xl, &xr);
        CHECK_EQ_INT(n, 600); CHECK_EQ_INT(lo, y0[t]); CHECK_EQ_INT(hi, y0[t] + 1);
        CHECK_EQ_INT(xl, 10); CHECK_EQ_INT(xr, 309);
    }
    guards_ok(px, 320, 160); gfree(px);
}

static void test_scope_trigger(void)
{
    uint16_t *a = guarded(320, 160), *b = guarded(320, 160);
    static float w2[600];
    for (int k = 0; k < 600; k++) { WAVE[k] = 0.8f * sinf(6.2831853f * (float)(k % 50) / 50.0f); w2[k] = 0.8f * sinf(6.2831853f * (float)((k + 13) % 50) / 50.0f); }
    for (int var = 0; var < 2; var++) {
        setup(a, 320, 160); pc_vd_scope(&D, var, WAVE, 600);
        setup(b, 320, 160); pc_vd_scope(&D, var, w2, 600);
        CHECK(!memcmp(a, b, 320 * 160 * 2));
        int lo, hi, xl, xr;
        CHECK(rows_nonbg(a, 320, 160, D.bg, &lo, &hi, &xl, &xr) > 600);
    }
    /* the circle triggers the same way (256 points) */
    uint16_t *c = guarded(RS, RS), *e = guarded(RS, RS);
    setup(c, RS, RS); pc_vd_scope(&D, 2, WAVE, 600);
    setup(e, RS, RS); pc_vd_scope(&D, 2, w2, 600);
    CHECK(!memcmp(c, e, RS * RS * 2));
    gfree(a); gfree(b); gfree(c); gfree(e);
}

static void test_scope_filled(void)
{
    uint16_t *px = guarded(320, 160);
    setup(px, 320, 160);
    dc(0.5f, 600);
    pc_vd_scope(&D, 1, WAVE, 600);
    for (int x = 10; x < 310; x++) {
        for (int y = 40; y <= 79; y++) CHECK(px[y * 320 + x] != D.bg);
        CHECK(px[39 * 320 + x] == D.bg); CHECK(px[100 * 320 + x] == D.bg);
    }
    CHECK(px[60 * 320 + 9] == D.bg && px[60 * 320 + 310] == D.bg);
    dc(-0.5f, 600);
    pc_vd_scope(&D, 1, WAVE, 600);
    for (int x = 10; x < 310; x++) { for (int y = 80; y <= 119; y++) CHECK(px[y * 320 + x] != D.bg); CHECK(px[60 * 320 + x] == D.bg); }
    /* the colour index grows with the distance from the zero line on both sides, and the two zero-line rows (79, 80) are equal */
    for (int i = 0; i < 256; i++) D.lut[i] = (uint16_t)(0x100 + i);                  /* every step distinct, so one step off shows */
    D.fresh = true;
    for (int side = 0; side < 2; side++) {
        dc(side ? -0.5f : 0.5f, 600);
        pc_vd_scope(&D, 1, WAVE, 600);
        int last = -1;
        for (int step = 0; step < 38; step++) {
            const int y = side ? 80 + step : 79 - step, dist = step, k = 64 + dist * 191 / 79;
            CHECK(k >= last); last = k;
            CHECK(px[y * 320 + 100] == D.lut[k]);
        }
        CHECK(px[79 * 320 + 100] == px[80 * 320 + 100]);
    }
    guards_ok(px, 320, 160); gfree(px);
}

static const float C_R0 = 70.0f * 340.0f / 240.0f, C_AMP = 40.0f * 340.0f / 240.0f;   /* the circle's radius at 0 and its swing at full scale: the 240 px values scaled to 340 */
static void test_scope_circle(void)
{
    uint16_t *px = guarded(RS, RS);
    setup(px, RS, RS);
    dc(0, 600);
    pc_vd_rect_t r = pc_vd_scope(&D, 2, WAVE, 600);
    CHECK(r.w == RS && r.h == RS);
    int n = 0; float dmin = 1e9f, dmax = 0;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != D.bg) {
        float dx = x - RCT, dy = y - RCT, dd = sqrtf(dx * dx + dy * dy);
        n++; if (dd < dmin) dmin = dd; if (dd > dmax) dmax = dd;
    }
    CHECK(n > 500);
    CHECK(dmin >= C_R0 - 2.0f && dmax <= C_R0 + 2.0f);      /* 2 px thick around radius 99.2 (pixel centres within the 2x2 block rounding) */
    CHECK(dmin <= C_R0 - 0.4f && dmax >= C_R0 + 0.4f);
    /* full scale: radius R0 + A and R0 - A, and nothing farther than 170 from the centre */
    setup(px, RS, RS); dc(1, 600); pc_vd_scope(&D, 2, WAVE, 600);
    n = 0; dmin = 1e9f; dmax = 0;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != D.bg) {
        float dx = x - RCT, dy = y - RCT, dd = sqrtf(dx * dx + dy * dy);
        n++; if (dd < dmin) dmin = dd; if (dd > dmax) dmax = dd;
    }
    CHECK(dmin >= C_R0 + C_AMP - 2.0f && dmax <= C_R0 + C_AMP + 2.0f && dmax <= 170.0f);
    setup(px, RS, RS); dc(-1, 600); pc_vd_scope(&D, 2, WAVE, 600);
    int lo, hi, xl, xr;
    CHECK(rows_nonbg(px, RS, RS, D.bg, &lo, &hi, &xl, &xr) > 100);
    CHECK(xl >= (int)(RCT - (C_R0 - C_AMP) - 2.0f) && xr <= (int)(RCT + (C_R0 - C_AMP) + 2.0f));
    guards_ok(px, RS, RS); gfree(px);
}

static void test_scope_seam(void)
{
    uint16_t *px = guarded(RS, RS);
    setup(px, RS, RS);
    for (int i = 0; i < 256; i++) WAVE[i] = -1.0f + 2.0f * (float)i / 255.0f;      /* a spiral from radius 42.5 to 155.8: no jump inside */
    pc_vd_scope(&D, 2, WAVE, 256);
    /* both end points are drawn: radius 42.5 at angle 0 (x ~ 212) and radius 155.8 one step before it (x ~ 325, y ~ 166) */
    int a = 0, b = 0;
    for (int y = 167; y <= 172; y++) for (int x = 210; x <= 215; x++) if (px[y * RS + x] != D.bg) a++;
    for (int y = 161; y <= 168; y++) for (int x = 322; x <= 328; x++) if (px[y * RS + x] != D.bg) b++;
    CHECK(a > 0 && b > 0);
    /* but nothing joins them: the radial line between the two end radii (more than 2 px from either end) is empty */
    for (int y = 167; y <= 173; y++) for (int x = 217; x <= 320; x++) CHECK(px[y * RS + x] == D.bg);
    guards_ok(px, RS, RS); gfree(px);
}

static void test_scope_dirty(void)
{
    static const struct { int w, h; } sz[3] = { { 320, 160 }, { 320, 160 }, { RS, RS } };
    uint16_t *ref = guarded(320, 160);
    for (int var = 0; var < 3; var++) {
        const int w = sz[var].w, h = sz[var].h;
        uint16_t *px = guarded(w, h), *before = malloc((size_t)w * h * 2), *rf = guarded(w, h);
        setup(px, w, h);
        for (int k = 0; k < 600; k++) WAVE[k] = 0.7f * sinf(k * 0.11f);
        pc_vd_rect_t r = pc_vd_scope(&D, var, WAVE, 600);
        CHECK(r.w == w && r.h == h);
        r = pc_vd_scope(&D, var, WAVE, 600);
        CHECK_EQ_INT(r.w, 0);
        for (int t = 0; t < 12; t++) {
            memcpy(before, px, (size_t)w * h * 2);
            for (int k = 0; k < 600; k++) WAVE[k] = (0.3f + 0.05f * t) * sinf(k * (0.08f + 0.013f * t)) + (k % 7 == 0 ? 0.3f : 0);
            if (t == 5) WAVE[599] += 0.01f;
            r = pc_vd_scope(&D, var, WAVE, 600);
            rect_covers(before, px, w, h, r);
            pc_vd_t *save = malloc(sizeof D); memcpy(save, &D, sizeof D);
            pc_vd_init(&D, rf, w, h); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
            pc_vd_scope(&D, var, WAVE, 600);
            CHECK(!memcmp(rf, px, (size_t)w * h * 2));
            memcpy(&D, save, sizeof D); free(save);
        }
        /* a small change stays a small rectangle. A wave with no zero crossing: the last `cols` samples are drawn, so sample
         * 300 + x is column 10 + x of the line variants (cols = 300, start = 600 - 300) */
        dc(0.3f, 600);
        pc_vd_scope(&D, var, WAVE, 600);
        CHECK_EQ_INT(pc_vd_scope(&D, var, WAVE, 600).w, 0);
        memcpy(before, px, (size_t)w * h * 2);
        WAVE[450] = 0.9f;
        r = pc_vd_scope(&D, var, WAVE, 600);
        CHECK(r.w > 0);
        rect_covers(before, px, w, h, r);
        if (var < 2) {
            CHECK(r.w <= 3 && r.x <= 160 && r.x + r.w > 160);       /* the changed column and the one that joins to it */
            bool ch = false;
            for (int y = 0; y < h; y++) if (before[y * w + 160] != px[y * w + 160]) ch = true;
            CHECK(ch);
        }
        pc_vd_set_colors(&D, PC_RAMP_ICE, NULL);
        r = pc_vd_scope(&D, var, WAVE, 600);
        CHECK(r.w == w && r.h == h);
        /* fewer samples than the picture has points: the rest is the zero line */
        setup(px, w, h);
        dc(0.5f, 600);
        pc_vd_scope(&D, var, WAVE, 40);
        guards_ok(px, w, h);
        gfree(px); gfree(rf); free(before);
    }
    gfree(ref);
}

#define FALL_FADE_T 48                                       /* pc_vizdraw.c FALL_FADE: the tests name the rule, they do not reach into the module */
static int fall_idx(int k) { return (k - FALL_FADE_T) * 255 / (255 - FALL_FADE_T); }   /* ramp index of level k >= FALL_FADE */
static void test_fall(void)
{
    uint16_t *px = guarded(256, 200);
    float lv[64] = { 0 }, z[64] = { 0 };
    lv[5] = 1;
    for (int var = 0; var < 3; var++) {
        setup(px, 256, 200);
        const int up = var == 1;
        int xa, xb;                                         /* the block of band 5 (var 2: the mirrored pair of the segment) */
        memset(lv, 0, sizeof lv);
        if (var == 2) { lv[0] = 1; xa = 124; xb = 128; } else { lv[5] = 1; xa = 20; xb = 20; }
        pc_vd_rect_t r = pc_vd_fall(&D, var, lv);
        CHECK(r.x == 0 && r.y == 0 && r.w == 256 && r.h == 200);
        int top = up ? 198 : 0;
        for (int y = 0; y < 200; y++) for (int x = 0; x < 256; x++) {
            bool in = y >= top && y < top + 2 && ((x >= xa && x < xa + 4) || (var == 2 && x >= xb && x < xb + 4));
            CHECK(px[y * 256 + x] == (in ? D.lut[255] : D.bg));
        }
        r = pc_vd_fall(&D, var, z);
        CHECK(r.w == 256 && r.h == 200);
        int t2 = up ? 196 : 2;
        for (int y = 0; y < 200; y++) for (int x = 0; x < 256; x++) {
            bool in = y >= t2 && y < t2 + 2 && ((x >= xa && x < xa + 4) || (var == 2 && x >= xb && x < xb + 4));
            CHECK(px[y * 256 + x] == (in ? D.lut[255] : D.bg));
        }
        /* a block scrolls off the far edge after 100 rows */
        for (int i = 0; i < 100; i++) pc_vd_fall(&D, var, z);
        int lo, hi, xl, xr;
        CHECK_EQ_INT(rows_nonbg(px, 256, 200, D.bg, &lo, &hi, &xl, &xr), 0);
    }
    /* mirror: the segment takes the larger of its two bands, so band 1 alone must light the same two blocks as band 0 */
    setup(px, 256, 200);
    memset(lv, 0, sizeof lv); lv[1] = 1;
    pc_vd_fall(&D, 2, lv);
    for (int y = 0; y < 200; y++) for (int x = 0; x < 256; x++) {
        bool in = y < 2 && ((x >= 124 && x < 128) || (x >= 128 && x < 132));
        CHECK(px[y * 256 + x] == (in ? D.lut[255] : D.bg));
    }
    /* colour by level: 0.5 is level 128, above the blend zone, so it sits on the whole ramp */
    setup(px, 256, 200);
    tone(lv, 0.5f);
    pc_vd_fall(&D, 0, lv);
    CHECK(px[0] == D.lut[fall_idx(128)] && px[255] == D.lut[fall_idx(128)]);
    {   /* a light theme (2026-11-17): the quiet end is the page background (2026-11-17 round 5; it was the panel, a white box on the page), the ramp is the paper one */
        const pc_theme_t lt = { "lt", "lt", 0xf4fafa, 0xffffff, 0xecf7f6, 0x1e282c, 0x46565a, 0x1e968e, 0xd6447f, 0xb07a00, 0xd6447f };
        setup(px, 256, 200);
        pc_vd_set_colors(&D, PC_RAMP_FIRE, &lt);
        memset(z, 0, sizeof z);
        pc_vd_fall(&D, 0, z);
        CHECK(D.quiet == D.bg && D.bg != 0xffff && px[0] == D.bg && px[256 * 199 + 255] == D.bg);
        CHECK(D.lut[0] != 0 && D.lut[255] == (uint16_t)(((0x5a >> 3) << 11)));
    }
    guards_ok(px, 256, 200); gfree(px);
}

/* waterfall colour rule (owner's correction 2026-11-13, supersedes R15 "quiet = the ramp's first colour" and the older fade towards lut[k]):
 * level 0 = the page background, 0..FALL_FADE a smooth blend from the background to the ramp's first colour, FALL_FADE..255 the whole ramp */
static int count_not(const uint16_t *px, int n, uint16_t c) { int k = 0; for (int i = 0; i < n; i++) k += px[i] != c; return k; }
static int ch_r(uint16_t c) { return c >> 11; }
static int ch_g(uint16_t c) { return (c >> 5) & 63; }
static int ch_b(uint16_t c) { return c & 31; }
static int imax_t(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int rnd_mid(int a, int b, int k) { return (a * (FALL_FADE_T - k) + b * k + FALL_FADE_T / 2) / FALL_FADE_T; }
static uint16_t cell_of(uint16_t *px, float *lv, int k)   /* the colour a cell of level k gets (first pixel of a fresh row of band 0) */
{
    tone(lv, (float)k / 255.0f); pc_vd_fall(&D, 0, lv);
    return px[0];
}
static void test_fall_rule(void)
{
    static const pc_ramp_t ramps[2] = { PC_RAMP_FIRE, PC_RAMP_ICE };
    uint16_t *px = guarded(256, 200);
    float lv[64];
    for (int q = 0; q < 2; q++) for (int var = 0; var < 3; var++) {
        pc_vd_init(&D, px, 256, 200);
        pc_vd_set_colors(&D, ramps[q], pc_theme_find("neon"));
        CHECK(D.lut[0] != D.bg);                                   /* the test only means something while the two differ */
        /* 1. silence: the very first frame is the background everywhere, and stays so while rows scroll */
        tone(lv, 0); pc_vd_fall(&D, var, lv);
        CHECK_EQ_INT(count_not(px, 256 * 200, D.bg), 0);
        for (int i = 0; i < 120; i++) pc_vd_fall(&D, var, lv);
        CHECK_EQ_INT(count_not(px, 256 * 200, D.bg), 0);
        /* one loud frame, then silence until it has scrolled off: back to a plain background */
        tone(lv, 1.0f); pc_vd_fall(&D, var, lv);
        CHECK(count_not(px, 256 * 200, D.bg) > 0);
        tone(lv, 0);
        for (int i = 0; i < 100; i++) pc_vd_fall(&D, var, lv);
        CHECK_EQ_INT(count_not(px, 256 * 200, D.bg), 0);
        /* a loud row with no history under it (the page was just opened): the other rows are the background */
        pc_vd_init(&D, px, 256, 200); pc_vd_set_colors(&D, ramps[q], pc_theme_find("neon"));
        tone(lv, 1.0f); pc_vd_fall(&D, var, lv);
        CHECK_EQ_INT(count_px(px, 256 * 200, D.bg), 256 * 198);
        CHECK_EQ_INT(count_px(px, 256 * 200, D.lut[255]), 256 * 2);
        /* a new ramp clears everything to the background on the next frame */
        pc_vd_set_colors(&D, ramps[1 - q], pc_theme_find("neon"));
        tone(lv, 0); pc_vd_fall(&D, var, lv);
        CHECK_EQ_INT(count_not(px, 256 * 200, D.bg), 0);
        /* a changed variant clears too */
        tone(lv, 1.0f); pc_vd_fall(&D, var, lv);
        { float z[64]; tone(z, 0); pc_vd_fall(&D, (var + 1) % 3, z); CHECK_EQ_INT(count_not(px, 256 * 200, D.bg), 0); }
    }
    for (int q = 0; q < 3; q++) {
        static const pc_ramp_t r3[3] = { PC_RAMP_DUO, PC_RAMP_FIRE, PC_RAMP_ICE };
        pc_vd_init(&D, px, 256, 200); pc_vd_set_colors(&D, r3[q], pc_theme_find("neon"));
        CHECK(D.lut[0] != D.bg);
        /* 2. the ends: level FALL_FADE is the ramp's first colour, level 1.0 its last; level 0 the background */
        CHECK(cell_of(px, lv, 0) == D.bg);
        CHECK(cell_of(px, lv, FALL_FADE_T) == D.lut[0]);
        CHECK(cell_of(px, lv, 255) == D.lut[255]);
        /* 3. half way through the blend zone: every channel is the rounded midpoint of bg and the first colour */
        const uint16_t m = cell_of(px, lv, FALL_FADE_T / 2);
        CHECK_EQ_INT(ch_r(m), rnd_mid(ch_r(D.bg), ch_r(D.lut[0]), FALL_FADE_T / 2));
        CHECK_EQ_INT(ch_g(m), rnd_mid(ch_g(D.bg), ch_g(D.lut[0]), FALL_FADE_T / 2));
        CHECK_EQ_INT(ch_b(m), rnd_mid(ch_b(D.bg), ch_b(D.lut[0]), FALL_FADE_T / 2));
        /* 4. no jump anywhere (the "hole" complaint): neighbouring levels differ per channel by at most the blend's step or twice the ramp's
         * largest neighbour step, +1; in particular not at level 1 and not at FALL_FADE */
        int dbl = (iabs(ch_r(D.lut[0]) - ch_r(D.bg)) + FALL_FADE_T - 1) / FALL_FADE_T, dbg = (iabs(ch_g(D.lut[0]) - ch_g(D.bg)) + FALL_FADE_T - 1) / FALL_FADE_T, dbb = (iabs(ch_b(D.lut[0]) - ch_b(D.bg)) + FALL_FADE_T - 1) / FALL_FADE_T;
        int sr = 0, sg = 0, sb = 0;
        for (int i = 0; i < 255; i++) {
            sr = imax_t(sr, iabs(ch_r(D.lut[i + 1]) - ch_r(D.lut[i]))); sg = imax_t(sg, iabs(ch_g(D.lut[i + 1]) - ch_g(D.lut[i]))); sb = imax_t(sb, iabs(ch_b(D.lut[i + 1]) - ch_b(D.lut[i])));
        }
        uint16_t prev = cell_of(px, lv, 0);
        int used_prev = -1, used_lo = 1000, used_hi = -1;
        for (int k = 1; k <= 255; k++) {
            const uint16_t c = cell_of(px, lv, k);
            CHECK(iabs(ch_r(c) - ch_r(prev)) <= imax_t(dbl, 2 * sr) + 1);
            CHECK(iabs(ch_g(c) - ch_g(prev)) <= imax_t(dbg, 2 * sg) + 1);
            CHECK(iabs(ch_b(c) - ch_b(prev)) <= imax_t(dbb, 2 * sb) + 1);
            prev = c;
            /* 5. from FALL_FADE up the colour is lut[fall_idx(k)]: that index never decreases and spans 0..255 */
            if (k >= FALL_FADE_T) {
                CHECK(c == D.lut[fall_idx(k)]);
                CHECK(fall_idx(k) >= used_prev);
                used_prev = fall_idx(k); if (used_prev < used_lo) used_lo = used_prev; if (used_prev > used_hi) used_hi = used_prev;
            }
        }
        CHECK_EQ_INT(used_lo, 0);
        CHECK_EQ_INT(used_hi, 255);
    }
    guards_ok(px, 256, 200); gfree(px);
}

/* the first frame (board bug 2026-11-12): pc_vd_init paints the image before the theme's colours are known, so after init + set_colors the
 * image holds the DEFAULT theme's bg; a page that shows the buffer before its first frame must call pc_vd_blank */
static void test_blank(void)
{
    uint16_t *px = guarded(RS, RS);
    const pc_theme_t *t = pc_theme_find("vapor");
    pc_vd_init(&D, px, RS, RS);
    pc_vd_set_colors(&D, PC_RAMP_FIRE, t);
    CHECK(px[0] != D.bg);                                   /* the cause: the image still holds the default theme's colour */
    for (int k = 0; k < PC_VD_KINDS; k++) {
        for (int i = 0; i < RS * RS; i++) px[i] = 0x1234;
        pc_vd_blank(&D, (pc_vd_kind_t)k);
        const uint16_t want = D.bg;
        CHECK(want != 0x1234);
        CHECK_EQ_INT(count_px(px, RS * RS, want), RS * RS);
        CHECK(D.fresh);                                     /* the first frame still redraws everything */
    }
    pc_vd_blank(NULL, PC_VD_BARS);
    pc_vd_t none; pc_vd_init(&none, NULL, 10, 10); pc_vd_blank(&none, PC_VD_FALL);   /* no image: nothing happens */
    guards_ok(px, RS, RS); gfree(px);
}

static void test_scope_bad_input(void)
{
    static const struct { int w, h; } sz[3] = { { 320, 160 }, { 320, 160 }, { RS, RS } };
    static const int ns[6] = { 0, 1, 2, 299, 600, 2000 };
    static float big[2000];
    for (int var = -1; var < 4; var++) for (int c = 0; c < 6; c++) for (int ni = 0; ni < 6; ni++) {
        int v = var < 0 || var > 2 ? 0 : var;
        uint16_t *px = guarded(sz[v].w, sz[v].h);
        for (int i = 0; i < 2000; i++)
            big[i] = c == 0 ? 0 : c == 1 ? 1 : c == 2 ? NAN : c == 3 ? (i % 2 ? -5.0f : 5.0f) : c == 4 ? INFINITY : -INFINITY;
        setup(px, sz[v].w, sz[v].h);
        pc_vd_scope(&D, var, big, ns[ni]);
        pc_vd_scope(&D, var, big, ns[ni]);
        guards_ok(px, sz[v].w, sz[v].h);
        gfree(px);
    }
    uint16_t *zp = guarded(1, 1), *px = guarded(256, 200), *wrong = guarded(300, 200);
    pc_vd_init(&D, zp, 0, 0);
    for (int var = -1; var < 5; var++) CHECK_EQ_INT(pc_vd_scope(&D, var, big, 600).w, 0);
    guards_ok(zp, 1, 1); CHECK_EQ_INT(zp[0], 0);
    setup(wrong, 300, 200);                                 /* another image size: nothing drawn */
    for (int var = 0; var < 3; var++) CHECK_EQ_INT(pc_vd_scope(&D, var, big, 600).w, 0);
    setup(px, 256, 200);
    CHECK_EQ_INT(pc_vd_scope(&D, 0, big, 600).w, 0);
    for (int i = 0; i < 256 * 200; i++) CHECK(px[i] == D.bg);
    uint16_t *s2 = guarded(320, 160);
    setup(s2, 320, 160);
    CHECK_EQ_INT(pc_vd_scope(&D, 0, NULL, 600).w, 0);
    CHECK_EQ_INT(pc_vd_scope(&D, 0, big, 0).w, 0);
    CHECK_EQ_INT(pc_vd_scope(&D, 0, big, -4).w, 0);
    CHECK_EQ_INT(pc_vd_scope(NULL, 0, big, 600).w, 0);
    gfree(zp); gfree(px); gfree(wrong); gfree(s2);
}

static void test_fall_bad_input(void)
{
    float v[64];
    for (int var = -1; var < 4; var++) for (int c = 0; c < 6; c++) {
        uint16_t *px = guarded(256, 200);
        for (int i = 0; i < 64; i++) v[i] = c == 0 ? 0 : c == 1 ? 1 : c == 2 ? NAN : c == 3 ? (i % 2 ? -5.0f : 5.0f) : c == 4 ? INFINITY : -INFINITY;
        setup(px, 256, 200);
        for (int k = 0; k < 3; k++) pc_vd_fall(&D, var, v);
        guards_ok(px, 256, 200);
        gfree(px);
    }
    uint16_t *zp = guarded(1, 1), *wrong = guarded(300, 200), *px = guarded(256, 200);
    for (int i = 0; i < 64; i++) v[i] = 1;
    pc_vd_init(&D, zp, 0, 0);
    for (int var = -1; var < 5; var++) CHECK_EQ_INT(pc_vd_fall(&D, var, v).w, 0);
    guards_ok(zp, 1, 1); CHECK_EQ_INT(zp[0], 0);
    setup(wrong, 300, 200);
    CHECK_EQ_INT(pc_vd_fall(&D, 0, v).w, 0);
    for (int i = 0; i < 300 * 200; i++) CHECK(wrong[i] == D.bg);
    setup(px, 256, 200);
    CHECK_EQ_INT(pc_vd_fall(&D, 0, NULL).w, 0);
    CHECK_EQ_INT(pc_vd_fall(NULL, 0, v).w, 0);
    guards_ok(wrong, 300, 200); guards_ok(px, 256, 200);
    gfree(zp); gfree(wrong); gfree(px);
}

/* any kind to any other on the same image: the first frame of the new style is the same as a fresh draw */
static pc_vd_rect_t draw_any(int id, const void *map, const float *lv, const float *pk, const float *wave)
{
    if (id < 4) return pc_vd_bars(&D, id, lv, pk, 7);
    if (id < 8) return pc_vd_ring(&D, id - 4, map, lv, pk);
    if (id < 11) return pc_vd_scope(&D, id - 8, wave, 600);
    if (id < 14) return pc_vd_fall(&D, id - 11, lv);
    if (id < 16) return pc_vd_bars(&D, id - 10, lv, pk, 7);           /* 14, 15: bars Lens and Arch (340 picture) */
    return pc_vd_fall(&D, id - 16, lv);                               /* 16..18: the full waterfall (340 picture) */
}
static void test_style_switch_all(void)
{
    static const struct { int w, h; int ids[16]; } grp[3] = {
        { RS, RS, { 0, 1, 2, 3, 4, 5, 6, 7, 10, 14, 15, 16, 17, 18, -1 } },
        { 320, 160, { 0, 1, 2, 3, 8, 9, -1 } },
        { 256, 200, { 0, 1, 2, 3, 11, 12, 13, -1 } } };
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64];
    for (int i = 0; i < 64; i++) { lv[i] = 0.2f + 0.7f * fabsf(sinf(i * 0.3f)); pk[i] = lv[i] + 0.05f; }
    for (int k = 0; k < 600; k++) WAVE[k] = 0.6f * sinf(k * 0.2f);
    for (int g = 0; g < 3; g++) {
        const int w = grp[g].w, h = grp[g].h;
        uint16_t *px = guarded(w, h), *ref = guarded(w, h);
        for (int a = 0; grp[g].ids[a] >= 0; a++) for (int b = 0; grp[g].ids[b] >= 0; b++) {
            if (a == b) continue;
            setup(px, w, h);
            draw_any(grp[g].ids[a], map, lv, pk, WAVE);
            for (int i = 0; i < 320; i++) { D.prev_a[i] = (int16_t)(7 + i % 50); D.prev_b[i] = (int16_t)(3 + i % 20); }
            for (int i = 0; i < w * h; i++) px[i] = 0x1234;
            pc_vd_rect_t r = draw_any(grp[g].ids[b], map, lv, pk, WAVE);
            CHECK(r.x == 0 && r.y == 0 && r.w == w && r.h == h);
            setup(ref, w, h);
            draw_any(grp[g].ids[b], map, lv, pk, WAVE);
            CHECK(!memcmp(px, ref, (size_t)w * h * 2));
        }
        guards_ok(px, w, h); gfree(px); gfree(ref);
    }
    free(map);
}

/* ---- phase scope ---- */
static pc_vd_t F;                                           /* a second drawer: the fresh render the incremental one is compared to */
#define PS PC_VD_PHASE_SZ
#define PCX ((PS - 1) / 2)                                   /* the centre pixel of the phase image: cx = cy = PCX + 0.5 */
static void mono(int16_t *lr, int n, int amp, int sign)       /* L = a, R = sign * a, a a sweep through -amp..amp */
{
    for (int i = 0; i < n; i++) { int a = amp * (i % 41 - 20) / 20; lr[2 * i] = (int16_t)a; lr[2 * i + 1] = (int16_t)(sign * a); }
}
static int ch_fade(int c, int b) { return b + ((c - b) * 3) / 4; }              /* difference times 3/4, truncated towards zero */
static uint16_t fade_model(uint16_t v, uint16_t bg)
{
    int r = ch_fade(v >> 11, bg >> 11), g = ch_fade((v >> 5) & 63, (bg >> 5) & 63), b = ch_fade(v & 31, bg & 31);
    return (uint16_t)(r << 11 | g << 5 | b);
}

static void test_phase(void)
{
    static int16_t lr[2 * 1024];
    uint16_t *px = guarded(PS, PS);
    for (int var = 0; var < 2; var++) {
        /* mono (L = R), full scale: a vertical line at x = cx +- 1 */
        setup(px, PS, PS);
        mono(lr, 41, 32767, 1); lr[0] = 32767; lr[1] = 32767; lr[2] = -32768; lr[3] = -32768;
        pc_vd_rect_t r = pc_vd_phase(&D, var, lr, 41);
        CHECK(r.x == 0 && r.y == 0 && r.w == PS && r.h == PS);
        int n = 0, ymin = PS, ymax = -1;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) {
            n++; CHECK(x >= PCX - 1 && x <= PCX + 2); if (y < ymin) ymin = y; if (y > ymax) ymax = y;
        }
        CHECK(n >= 40 && ymin == 2 && ymax == PS - 3);       /* full scale ends 2 px inside the edge */
        /* L = -R: a horizontal line at y = cy +- 1 */
        setup(px, PS, PS);
        mono(lr, 41, 32767, -1); lr[0] = 32767; lr[1] = -32768; lr[2] = -32768; lr[3] = 32767;
        pc_vd_phase(&D, var, lr, 41);
        int xmin = PS, xmax = -1;
        n = 0;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) {
            n++; CHECK(y >= PCX - 1 && y <= PCX + 2); if (x < xmin) xmin = x; if (x > xmax) xmax = x;
        }
        CHECK(n >= 40 && xmin <= 3 && xmax >= PS - 4);
        /* silence: the centre point only */
        setup(px, PS, PS);
        memset(lr, 0, sizeof lr);
        pc_vd_phase(&D, var, lr, 64);
        n = 0;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) { n++; CHECK(abs(x - PCX) <= 1 && abs(y - PCX) <= 1); }
        CHECK_EQ_INT(n, 1);
        /* left only: L - R = L, L + R = L: a diagonal up and to the right for positive L (up = y smaller) */
        setup(px, PS, PS);
        lr[0] = 20000; lr[1] = 0;
        pc_vd_phase(&D, var, lr, 1);
        n = 0;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) { n++; CHECK(x > PCX && y < PCX); CHECK(abs((x - PCX) - (PCX - y)) <= 2); }
        CHECK_EQ_INT(n, 1);
        /* one full-scale frame, then 16 empty ones: the whole picture is back to bg (no ghost layer of a near-bg colour) */
        setup(px, PS, PS);
        mono(lr, 41, 32767, 1);
        pc_vd_phase(&D, var, lr, 41);
        for (int k = 0; k < 16; k++) { r = pc_vd_phase(&D, var, lr, 0); CHECK(r.w == PS && r.h == PS); }
        for (int i = 0; i < PS * PS; i++) CHECK(px[i] == D.bg);
        /* the fade is the model, channel by channel, from any colour (above and below bg), step by step */
        for (int i = 0; i < PS * PS; i++) px[i] = (uint16_t)(i * 2654435761u >> 7);
        px[0] = 0x0000; px[1] = 0xFFFF; px[2] = D.bg;
        static uint16_t want[PS * PS];
        for (int k = 0; k < 16; k++) {
            for (int i = 0; i < PS * PS; i++) want[i] = fade_model(px[i], D.bg);
            pc_vd_phase(&D, var, NULL, 0);
            CHECK(!memcmp(want, px, sizeof want));
        }
        for (int i = 0; i < PS * PS; i++) CHECK(px[i] == D.bg);
        /* the new points are drawn after the fade: in the full fg colour */
        setup(px, PS, PS);
        lr[0] = 0; lr[1] = 0;
        pc_vd_phase(&D, var, lr, 1);
        CHECK(px[PCX * PS + PCX] == D.fg);
        pc_vd_phase(&D, var, NULL, 0);
        CHECK(px[PCX * PS + PCX] == fade_model(D.fg, D.bg) && px[PCX * PS + PCX] != D.fg);
        guards_ok(px, PS, PS);
    }
    /* auto gain: a quiet mono signal (peak 3000) is lifted 12x (to about half way); a signal of 12000 reaches 3/4; the gain relaxes slowly; a loud one is not shrunk */
    setup(px, PS, PS);
    mono(lr, 41, 1500, 1); lr[0] = 1500; lr[1] = 1500; lr[2] = -1500; lr[3] = -1500;
    pc_vd_phase(&D, 0, lr, 41);
    {   int ymin = PS, ymax = -1;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) { if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
        CHECK(ymin < PCX - 60 && ymin > 20 && ymax > PCX + 60);
    }
    setup(px, PS, PS);
    lr[0] = lr[1] = 20; lr[2] = lr[3] = -20;                  /* near silence: capped at 12x, not blown up to the edge */
    pc_vd_phase(&D, 0, lr, 2);
    {   int ymin = PS;
        for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg && y < ymin) ymin = y;
        CHECK(ymin > PCX - 20);
    }
    /* variant 1 joins the points: two full-scale mono points far apart give a continuous vertical line */
    setup(px, PS, PS);
    lr[0] = lr[1] = 32767; lr[2] = lr[3] = -32768;
    pc_vd_phase(&D, 1, lr, 2);
    for (int y = 2; y <= PS - 3; y++) { int c = 0; for (int x = PCX - 1; x <= PCX + 2; x++) c += px[y * PS + x] != D.bg; CHECK(c >= 1); }
    setup(px, PS, PS);                                      /* variant 0 leaves just the two points */
    pc_vd_phase(&D, 0, lr, 2);
    { int n = 0; for (int i = 0; i < PS * PS; i++) n += px[i] != D.bg; CHECK_EQ_INT(n, 2); }
    /* more than 128 segments' worth of frames: lines stay on the vertical, an interleaved 1024-frame buffer is fine */
    setup(px, PS, PS);
    mono(lr, 1024, 32767, 1);
    pc_vd_phase(&D, 1, lr, 1024);
    for (int y = 0; y < PS; y++) for (int x = 0; x < PS; x++) if (px[y * PS + x] != D.bg) CHECK(x >= PCX - 1 && x <= PCX + 2);
    guards_ok(px, PS, PS); gfree(px);
}

static void test_phase_bad_input(void)
{
    static int16_t lr[2 * 3000];
    for (int i = 0; i < 6000; i++) lr[i] = (int16_t)(i % 3 == 0 ? INT16_MIN : i % 3 == 1 ? INT16_MAX : (i * 977));
    static const int fr[9] = { -5, 0, 1, 2, 127, 129, 1024, 1025, 3000 };
    for (int var = -1; var < 3; var++) for (int f = 0; f < 9; f++) {
        uint16_t *px = guarded(PS, PS);
        setup(px, PS, PS);
        for (int k = 0; k < 3; k++) pc_vd_phase(&D, var, lr, fr[f]);
        guards_ok(px, PS, PS);
        gfree(px);
    }
    uint16_t *px = guarded(PS, PS);
    setup(px, PS, PS);
    CHECK_EQ_INT(pc_vd_phase(&D, 0, NULL, 0).w, PS);        /* NULL with 0 frames: only the fade */
    CHECK_EQ_INT(pc_vd_phase(&D, 0, NULL, 50).w, PS);       /* NULL with frames: treated as none */
    for (int i = 0; i < PS * PS; i++) CHECK(px[i] == D.bg);
    CHECK_EQ_INT(pc_vd_phase(NULL, 0, lr, 10).w, 0);
    /* the extreme corners of the 32-bit sum: INT16_MIN on both channels, and opposite */
    lr[0] = lr[1] = INT16_MIN; lr[2] = INT16_MIN; lr[3] = INT16_MAX; lr[4] = INT16_MAX; lr[5] = INT16_MIN; lr[6] = lr[7] = INT16_MAX;
    pc_vd_phase(&D, 0, lr, 4);
    int n = 0;
    for (int i = 0; i < PS * PS; i++) n += px[i] != D.bg;
    CHECK_EQ_INT(n, 4);
    CHECK(px[2 * PS + PCX] == D.fg && px[(PS - 3) * PS + PCX] == D.fg);
    CHECK(px[(PCX + 1) * PS + 2] == D.fg && px[(PCX + 1) * PS + PS - 4] == D.fg);   /* L + R = -1: one row below the centre */
    guards_ok(px, PS, PS); gfree(px);
    uint16_t *zp = guarded(1, 1);
    pc_vd_init(&D, zp, 0, 0);
    for (int var = -1; var < 3; var++) CHECK_EQ_INT(pc_vd_phase(&D, var, lr, 100).w, 0);
    guards_ok(zp, 1, 1); CHECK_EQ_INT(zp[0], 0); gfree(zp);
    uint16_t *wrong = guarded(RS, RS), *w2 = guarded(PS, PS - 2);
    setup(wrong, RS, RS);
    CHECK_EQ_INT(pc_vd_phase(&D, 0, lr, 100).w, 0);
    for (int i = 0; i < RS * RS; i++) CHECK(wrong[i] == D.bg);
    setup(w2, PS, PS - 2);
    CHECK_EQ_INT(pc_vd_phase(&D, 1, lr, 100).w, 0);
    for (int i = 0; i < PS * (PS - 2); i++) CHECK(w2[i] == D.bg);
    guards_ok(wrong, RS, RS); guards_ok(w2, PS, PS - 2); gfree(wrong); gfree(w2);
}

/* Style switches on one image. Which kinds share an image size: only bars (no size check: any image of at least 8 x 4) shares with
 * the phase scope (300x300). The scopes (320x160, 340x340), the waterfall (256x200) and the ring (340x340) check their size and
 * refuse every one of these images, so those pairs are covered by the size-mismatch tests, not here (test_style_switch_all has
 * the ring/scope/fall groups). */
enum { SW_BARS0, SW_BARS2, SW_PH0, SW_PH1 };
static pc_vd_rect_t draw_id(pc_vd_t *d, int id, int t)
{
    static int16_t lr[2 * 200];
    float b64[64], p64[64];
    for (int i = 0; i < 64; i++) { b64[i] = 0.2f + 0.6f * fabsf(sinf((float)(i + t) * 0.3f)); p64[i] = b64[i] + 0.05f; }
    for (int i = 0; i < 200; i++) {
        lr[2 * i] = (int16_t)(20000.0f * sinf((float)(i + 9 * t) * 0.17f));
        lr[2 * i + 1] = (int16_t)(15000.0f * sinf((float)(i + 9 * t) * 0.23f + 1.0f));
    }
    switch (id) {
    case SW_BARS0: return pc_vd_bars(d, 0, b64, p64, 7);
    case SW_BARS2: return pc_vd_bars(d, 2, b64, p64, 5);
    case SW_PH0: return pc_vd_phase(d, 0, lr, 200);
    default: return pc_vd_phase(d, 1, lr, 200);
    }
}
static void garble_prev(pc_vd_t *d, int salt)
{
    for (int i = 0; i < 320; i++) {
        d->prev_a[i] = (int16_t)((i + salt) % 3 == 0 ? 0x7fff : (i + salt) % 3 == 1 ? -1 : -32768);
        d->prev_b[i] = (int16_t)((i + salt) % 2 ? 0x4000 : -7);
    }
}
static int tag_of(int id) { return id <= SW_BARS0 ? 1 : id == SW_BARS2 ? 3 : id == SW_PH0 ? 81 : 82; }
static void expect_fresh(int id, int t, int w, int h, const uint16_t *px)
{
    uint16_t *rf = guarded(w, h);
    pc_vd_init(&F, rf, w, h); pc_vd_set_colors(&F, PC_RAMP_FIRE, pc_theme_find("neon"));
    draw_id(&F, id, t);
    CHECK(!memcmp(rf, px, (size_t)w * h * 2));
    guards_ok(rf, w, h); gfree(rf);
}
static void test_phase_switch(void)
{
    static const struct { int w, h, n, ids[4]; } grp[1] = {
        { PC_VD_PHASE_SZ, PC_VD_PHASE_SZ, 4, { SW_BARS0, SW_BARS2, SW_PH0, SW_PH1 } } };
    for (int g = 0; g < 1; g++) {
        const int w = grp[g].w, h = grp[g].h;
        uint16_t *px = guarded(w, h), *before = malloc((size_t)w * h * 2);
        /* every ordered pair of kinds: real frames of the old style, garbage in prev (another style's memory), the picture left as it
         * is or scribbled over; the first frame of the new style is a full redraw equal to a fresh render */
        for (int a = 0; a < grp[g].n; a++) for (int b = 0; b < grp[g].n; b++) for (int mode = 0; mode < 3; mode++) {
            if (a == b) continue;
            setup(px, w, h);
            for (int t = 0; t < 4; t++) draw_id(&D, grp[g].ids[a], t);
            if (mode >= 1) garble_prev(&D, a + b);
            if (mode == 2) for (int i = 0; i < w * h; i++) px[i] = 0x1234;
            memcpy(before, px, (size_t)w * h * 2);
            pc_vd_rect_t r = draw_id(&D, grp[g].ids[b], 9);
            CHECK(r.x == 0 && r.y == 0 && r.w == w && r.h == h);
            rect_covers(before, px, w, h, r);
            expect_fresh(grp[g].ids[b], 9, w, h, px);
            guards_ok(px, w, h);
        }
        /* tag/extra from another renderer, or only the extra changed, with garbage prev: next frame is a fresh render */
        for (int a = 0; a < grp[g].n; a++) {
            const int id = grp[g].ids[a];
            static const int16_t other[4][2] = { { 17, 0 }, { 33, 0 }, { 49, 0 }, { 0, 9 } };    /* ring, scope, fall tags; extra only */
            for (int o = 0; o < 4; o++) {
                setup(px, w, h);
                for (int t = 0; t < 4; t++) draw_id(&D, id, t);
                garble_prev(&D, o);
                D.tag = o == 3 ? (int16_t)tag_of(id) : other[o][0]; D.extra = other[o][1];
                for (int i = 0; i < w * h; i++) px[i] = 0x1234;
                memcpy(before, px, (size_t)w * h * 2);
                pc_vd_rect_t r = draw_id(&D, id, 11);
                CHECK(r.w == w && r.h == h);
                rect_covers(before, px, w, h, r);
                expect_fresh(id, 11, w, h, px);
            }
            /* The contract: prev_a/prev_b are the renderer's own memory of its last frame. Garbage there with the renderer's own
             * valid tag is out of contract, nothing about the picture is promised; what must still hold is that no write leaves the
             * image (the guard bands) and that a following fresh frame recovers. */
            setup(px, w, h);
            for (int t = 0; t < 4; t++) draw_id(&D, id, t);
            for (int k = 0; k < 4; k++) { garble_prev(&D, k); draw_id(&D, id, 20 + k); }
            guards_ok(px, w, h);
            D.fresh = true;
            draw_id(&D, id, 30);
            expect_fresh(id, 30, w, h, px);
        }
        /* a long run of one style changing back and forth with another on the same image stays equal to a fresh render of the last */
        guards_ok(px, w, h); gfree(px); free(before);
    }
    /* phase 0 -> 1 -> 0 on the phase image with different real input each time */
    uint16_t *px = guarded(PS, PS), *before = malloc(sizeof(uint16_t) * PS * PS);
    setup(px, PS, PS);
    const int seq[5] = { SW_PH0, SW_PH1, SW_PH0, SW_PH1, SW_PH0 };
    for (int k = 0; k < 5; k++) {
        memcpy(before, px, sizeof(uint16_t) * PS * PS);
        pc_vd_rect_t r = draw_id(&D, seq[k], 3 + 5 * k);
        CHECK(r.w == PS && r.h == PS);
        rect_covers(before, px, PS, PS, r);
        if (k) expect_fresh(seq[k], 3 + 5 * k, PS, PS, px);       /* a style change: the first frame of the new style is a fresh render */
    }
    guards_ok(px, PS, PS); gfree(px); free(before);
}

/* ---- colour mapping (height / tip / band / level) ---- */
static void mk_in(float *lv, float *pk)
{
    for (int i = 0; i < 64; i++) { lv[i] = 0.1f + 0.85f * fabsf(sinf(i * 0.37f)); pk[i] = lv[i] + 0.06f; if (pk[i] > 1) pk[i] = 1; }
}
static bool in_lut_or_fg(const pc_vd_t *d, uint16_t c)
{
    if (c == d->bg || c == d->fg) return true;
    for (int i = 0; i < 256; i++) if (d->lut[i] == c) return true;
    return false;
}
static int lut_idx(const pc_vd_t *d, uint16_t c) { for (int i = 0; i < 256; i++) if (d->lut[i] == c) return i; return -1; }
/* bar draw dispatcher: id 0..3 bars variant, 4..7 ring variant */
static void draw_map(pc_vd_t *d, int id, const void *map, const float *lv, const float *pk)
{
    if (id < 4) pc_vd_bars(d, id, lv, pk, 7); else pc_vd_ring(d, id - 4, map, lv, pk);
}

static void test_map_names(void)
{
    static const char *const n[PC_VD_MAPS] = { "height", "tip", "band", "level" };
    for (int m = 0; m < PC_VD_MAPS; m++) {
        CHECK_STR(pc_vd_map_name((pc_vd_map_t)m), n[m]);
        CHECK_EQ_INT(pc_vd_map_parse(n[m]), m);
        CHECK(pc_vd_map_label((pc_vd_map_t)m, true)[0] && pc_vd_map_label((pc_vd_map_t)m, false)[0]);
    }
    CHECK_STR(pc_vd_map_name((pc_vd_map_t)99), "height");
    CHECK_STR(pc_vd_map_name((pc_vd_map_t)-1), "height");
    CHECK_EQ_INT(pc_vd_map_parse("zzz"), PC_VD_MAP_HEIGHT);
    CHECK_EQ_INT(pc_vd_map_parse(NULL), PC_VD_MAP_HEIGHT);
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_HEIGHT, false), "高度");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_TIP, false), "火苗");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_BAND, false), "頻段");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_LEVEL, false), "音量");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_HEIGHT, true), "Height");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_TIP, true), "Tip");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_BAND, true), "Band");
    CHECK_STR(pc_vd_map_label(PC_VD_MAP_LEVEL, true), "Level");
    CHECK_STR(pc_vd_map_label((pc_vd_map_t)42, true), "Height");
    for (int k = 0; k < PC_VD_KINDS; k++) CHECK_EQ_INT(pc_vd_kind_has_map((pc_vd_kind_t)k), k == PC_VD_BARS || k == PC_VD_RING || k == PC_VD_VOICES);
    CHECK(!pc_vd_kind_has_map((pc_vd_kind_t)99));
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    CHECK_EQ_INT(D.map, PC_VD_MAP_HEIGHT);
    D.fresh = false;
    pc_vd_set_map(&D, PC_VD_MAP_HEIGHT); CHECK(!D.fresh);
    pc_vd_set_map(&D, PC_VD_MAP_TIP); CHECK(D.fresh); CHECK_EQ_INT(D.map, PC_VD_MAP_TIP);
    D.fresh = false;
    pc_vd_set_map(&D, PC_VD_MAP_TIP); CHECK(!D.fresh);
    pc_vd_set_map(&D, (pc_vd_map_t)77); CHECK(D.fresh); CHECK_EQ_INT(D.map, PC_VD_MAP_HEIGHT);
    pc_vd_set_map(NULL, PC_VD_MAP_TIP);
    gfree(px);
}

static void test_map_height_same(void)       /* default and an explicit HEIGHT draw the very same picture, all 8 variants, also incremental */
{
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64], lv2[64], pk2[64];
    mk_in(lv, pk);
    for (int i = 0; i < 64; i++) { lv2[i] = lv[(i * 5) % 64]; pk2[i] = lv2[i] + 0.04f; }
    for (int id = 0; id < 8; id++) {
        const int w = id < 4 ? 300 : RS, h = id < 4 ? 200 : RS;
        uint16_t *a = guarded(w, h), *b = guarded(w, h);
        setup(a, w, h); pc_vd_t Da = D;
        setup(b, w, h); pc_vd_set_map(&D, PC_VD_MAP_HEIGHT); pc_vd_t Db = D;
        draw_map(&Da, id, map, lv, pk); draw_map(&Db, id, map, lv, pk);
        CHECK(!memcmp(a, b, (size_t)w * h * 2));
        draw_map(&Da, id, map, lv2, pk2); draw_map(&Db, id, map, lv2, pk2);
        CHECK(!memcmp(a, b, (size_t)w * h * 2));
        guards_ok(a, w, h); guards_ok(b, w, h); gfree(a); gfree(b);
    }
    free(map);
}

static void test_map_band(void)
{
    uint16_t *px = guarded(300, 200);
    float lv[64], pk[64];
    tone(lv, 0.6f); tone(pk, 0.6f);
    setup(px, 300, 200);
    pc_vd_set_map(&D, PC_VD_MAP_BAND);
    pc_vd_bars(&D, 0, lv, pk, 7);
    int prev = -1;
    for (int i = 0; i < 32; i++) {
        const int cx = bar_cx(i, 7), idx = i * 255 / 31;
        for (int y = 0; y < 200; y++) for (int x = cx - 3; x <= cx + 3; x++) {
            uint16_t c = px[y * 300 + x];
            if (c != D.bg && c != D.fg) CHECK(c == D.lut[idx]);     /* one colour for the whole bar */
        }
        CHECK(idx >= prev); prev = idx;
    }
    CHECK(px[199 * 300 + bar_cx(0, 7)] == D.lut[0]);
    CHECK(px[199 * 300 + bar_cx(31, 7)] == D.lut[255]);
    gfree(px);
}

static void test_map_level(void)
{
    uint16_t *px = guarded(300, 200);
    float lv[64], pk[64];
    tone(lv, 0.5f); tone(pk, 0);
    setup(px, 300, 200);
    pc_vd_set_map(&D, PC_VD_MAP_LEVEL);
    pc_vd_bars(&D, 0, lv, pk, 7);
    const int h = 100;
    for (int i = 0; i < 32; i++) {
        int cx = bar_cx(i, 7);
        for (int k = 0; k < h; k++) CHECK(px[(199 - k) * 300 + cx] == D.lut[h * 255 / 200]);
    }
    lv[10 * 2] = lv[10 * 2 + 1] = 1.0f;       /* bar 10 (64 -> 32 bars: two bands each) */
    pc_vd_bars(&D, 0, lv, pk, 7);
    for (int i = 0; i < 32; i++) {
        int cx = bar_cx(i, 7);
        if (i == 10) { for (int k = 0; k < 200; k++) CHECK(px[(199 - k) * 300 + cx] == D.lut[255]); }
        else for (int k = 0; k < h; k++) CHECK(px[(199 - k) * 300 + cx] == D.lut[h * 255 / 200]);
    }
    gfree(px);
}

static void test_map_tip(void)
{
    uint16_t *px = guarded(300, 200);
    float lv[64], pk[64];
    for (int i = 0; i < 64; i++) { lv[i] = (i % 32) / 32.0f * 0.9f + 0.02f; pk[i] = 0; }
    setup(px, 300, 200);
    pc_vd_set_map(&D, PC_VD_MAP_TIP);
    pc_vd_bars(&D, 0, lv, pk, 7);
    const int tip = 200 * 42 / 100;
    for (int i = 0; i < 32; i++) {
        int cx = bar_cx(i, 7), h = 0;
        while (h < 200 && px[(199 - h) * 300 + cx] != D.bg) h++;
        CHECK(h > 0);
        CHECK(px[(199 - (h - 1)) * 300 + cx] == D.lut[255]);
        if (h > tip) CHECK(px[199 * 300 + cx] == D.lut[0]);
        int last = -1;      /* from the top down the index never rises, so from the foot up it never falls */
        for (int k = 0; k < h; k++) { int idx = lut_idx(&D, px[(199 - k) * 300 + cx]); CHECK(idx >= last); last = idx; }
    }
    gfree(px);
}

static void test_map_incremental(void)
{
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float a[64], b[64], pk[64];
    tone(a, 0.3f); tone(b, 0.8f); tone(pk, 0.0f);
    const int ids[3] = { 0, 4, 5 };
    for (int m = 0; m < PC_VD_MAPS; m++) for (int q = 0; q < 3; q++) {
        const int id = ids[q], w = id < 4 ? 300 : RS, h = id < 4 ? 200 : RS;
        uint16_t *x = guarded(w, h), *y = guarded(w, h);
        setup(x, w, h); pc_vd_set_map(&D, (pc_vd_map_t)m);
        draw_map(&D, id, map, a, pk); draw_map(&D, id, map, b, pk);
        setup(y, w, h); pc_vd_set_map(&D, (pc_vd_map_t)m);
        draw_map(&D, id, map, b, pk);
        CHECK(!memcmp(x, y, (size_t)w * h * 2));
        /* a falling step and one bar only */
        setup(x, w, h); pc_vd_set_map(&D, (pc_vd_map_t)m);
        draw_map(&D, id, map, b, pk);
        float c[64]; memcpy(c, b, sizeof c); c[17] = 0.15f; c[40] = 1.0f;
        draw_map(&D, id, map, c, pk);
        setup(y, w, h); pc_vd_set_map(&D, (pc_vd_map_t)m);
        draw_map(&D, id, map, c, pk);
        CHECK(!memcmp(x, y, (size_t)w * h * 2));
        guards_ok(x, w, h); guards_ok(y, w, h); gfree(x); gfree(y);
    }
    free(map);
}

static void test_map_all_variants(void)       /* every map on every variant of bars and ring: no overrun, only lut / fg / bg colours */
{
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64];
    mk_in(lv, pk);
    for (int id = 0; id < 8; id++) for (int m = 0; m < PC_VD_MAPS; m++) {
        const int w = id < 4 ? 300 : RS, h = id < 4 ? 200 : RS;
        uint16_t *px = guarded(w, h);
        setup(px, w, h); pc_vd_set_map(&D, (pc_vd_map_t)m);
        draw_map(&D, id, map, lv, pk);
        lv[3] = 0; lv[4] = 0; lv[30] = 1; draw_map(&D, id, map, lv, pk); mk_in(lv, pk);
        int nonbg = 0;
        for (int i = 0; i < w * h; i++) { CHECK(in_lut_or_fg(&D, px[i])); nonbg += px[i] != D.bg; }
        CHECK(nonbg > 100);
        if (id == 3 && m == PC_VD_MAP_TIP) {                    /* thin line: every pixel is a bar top */
            for (int i = 0; i < w * h; i++) CHECK(px[i] == D.bg || px[i] == D.lut[255]);
        }
        if (id == 7 && m == PC_VD_MAP_TIP) {
            for (int i = 0; i < w * h; i++) CHECK(px[i] == D.bg || px[i] == D.fg || px[i] == D.lut[255]);
        }
        if (id == 3 && m == PC_VD_MAP_BAND) {
            int lo = 255, hi = 0;
            for (int i = 0; i < w * h; i++) if (px[i] != D.bg) { int k = lut_idx(&D, px[i]); if (k < lo) lo = k; if (k > hi) hi = k; }
            CHECK(lo <= 10 && hi >= 245);
        }
        guards_ok(px, w, h); gfree(px);
    }
    free(map);
}

static void test_map_ignored(void)            /* the other kinds do not look at the map: bit for bit the same with LEVEL */
{
    static const struct { int w, h, kind, var; } c[] = {
        { 320, 160, 2, 0 }, { 320, 160, 2, 1 }, { PC_VD_CIRCLE_SZ, PC_VD_CIRCLE_SZ, 2, 2 }, { 256, 200, 3, 0 }, { 256, 200, 3, 1 }, { 256, 200, 3, 2 },
        { PC_VD_PHASE_SZ, PC_VD_PHASE_SZ, 4, 0 }, { PC_VD_PHASE_SZ, PC_VD_PHASE_SZ, 4, 1 } };
    float lv[80] = { 0 }, pk[80] = { 0 };
    mk_in(lv, pk);
    static int16_t lr[2 * 256];
    for (int i = 0; i < 256; i++) { lr[2 * i] = (int16_t)(15000 * sinf(i * 0.1f)); lr[2 * i + 1] = (int16_t)(12000 * sinf(i * 0.13f)); }
    for (int k = 0; k < 600; k++) WAVE[k] = 0.6f * sinf(k * 0.2f);
    for (unsigned q = 0; q < sizeof c / sizeof c[0]; q++) {
        uint16_t *a = guarded(c[q].w, c[q].h), *b = guarded(c[q].w, c[q].h);
        for (int pass = 0; pass < 2; pass++) {
            uint16_t *px = pass ? b : a;
            setup(px, c[q].w, c[q].h);
            if (pass) pc_vd_set_map(&D, PC_VD_MAP_LEVEL);
            for (int t = 0; t < 6; t++) {
                switch (c[q].kind) {
                case 2: pc_vd_scope(&D, c[q].var, WAVE + t * 7, 400); break;
                case 3: pc_vd_fall(&D, c[q].var, lv + t % 3); break;
                default: pc_vd_phase(&D, c[q].var, lr + 2 * t * 20, 100); break;
                }
            }
        }
        CHECK(!memcmp(a, b, (size_t)c[q].w * c[q].h * 2));
        gfree(a); gfree(b);
    }
}


/* ---- voices (Task 10): one bar per chip voice; bars picture 300x200 (variants 0, 1) or the ring picture + map (variant 2) ---- */
#define VBH 180                           /* the bars' region: the names take the 20 rows under it */
static void vnames(char (*nm)[4], int n) { memset(nm, 0, 4 * (n > 16 ? n : 16)); for (int i = 0; i < n; i++) { int v = i + 1; if (v > 9) { nm[i][0] = (char)('0' + v / 10); nm[i][1] = (char)('0' + v % 10); } else nm[i][0] = (char)('0' + v); } }
static void vfull(float *lv, int n, float v) { for (int i = 0; i < 16; i++) lv[i] = i < n ? v : 0; }
static void *vmap(void)
{
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *m = malloc(cap);
    CHECK(m && pc_vd_ringmap_build(m, cap, RS, RS, RIN, ROUT, 0));
    return m;
}

static void test_voices_kind(void)
{
    CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 0, false), "長條"); CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 1, false), "分段"); CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 2, false), "環形");
    CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 0, true), "Bars"); CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 1, true), "Segments"); CHECK_STR(pc_vd_var_label(PC_VD_VOICES, 2, true), "Ring");
    int w, h;
    pc_vd_size(PC_VD_VOICES, 0, &w, &h); CHECK(w == 300 && h == 200);
    pc_vd_size(PC_VD_VOICES, 1, &w, &h); CHECK(w == 300 && h == 200);
    pc_vd_size(PC_VD_VOICES, 2, &w, &h); CHECK(w == RS && h == RS);
    CHECK(pc_vd_kind_has_map(PC_VD_VOICES));
    CHECK(!pc_vd_kind_has_map(PC_VD_SCOPE));
}

/* the runs of non-background pixels along row y: count, and for each its first column and width */
static int runs_row(const uint16_t *px, int w, int y, uint16_t bg, int *x0, int *wd)
{
    int n = 0;
    for (int x = 0; x < w; x++) if (px[y * w + x] != bg && (x == 0 || px[y * w + x - 1] == bg)) {
        int e = x; while (e < w && px[y * w + e] != bg) e++;
        if (n < 32) { x0[n] = x; wd[n] = e - x; }
        n++;
    }
    return n;
}

static void test_voices_bars(void)
{
    char nm[16][4];
    for (int var = 0; var < 2; var++) for (int n = 1; n <= 16; n++) {
        uint16_t *px = guarded(300, 200);
        setup(px, 300, 200);
        float lv[16], pk[16];
        vfull(lv, n, 1.0f); vfull(pk, n, 1.0f); vnames(nm, n);
        pc_vd_rect_t r = pc_vd_voices(&D, var, NULL, n, lv, pk, nm);
        CHECK(r.x == 0 && r.y == 0 && r.w == 300 && r.h == 200);          /* the first frame is the whole picture */
        int x0[32], wd[32];
        CHECK_EQ_INT(runs_row(px, 300, VBH - 1, D.bg, x0, wd), n);        /* one bar per voice, no more */
        for (int i = 0; i < n; i++) {
            CHECK(wd[i] <= 40 && wd[i] >= 4);
            if (i) CHECK(x0[i] - (x0[i - 1] + wd[i - 1]) >= 6);           /* at least 6 px between bars */
            CHECK(abs(x0[i] + wd[i] / 2 - (2 * i + 1) * 300 / (2 * n)) <= 1);     /* evenly spread */
            CHECK(px[0 * 300 + x0[i] + wd[i] / 2] == D.fg);               /* full level: the cap is at the top rows */
            CHECK(px[2 * 300 + x0[i] + wd[i] / 2] != D.bg);               /* and the bar reaches it */
            CHECK(px[(VBH - 1) * 300 + x0[i]] != D.bg);
        }
        for (int y = VBH; y < VBH + 4; y++) for (int x = 0; x < 300; x++) CHECK(px[y * 300 + x] == D.bg);   /* the bars stop above the names */
        for (int i = 0; i < n; i++) {                                       /* the name's glyphs are under their bar, between the neighbours' */
            int cx = (2 * i + 1) * 300 / (2 * n), slot = 300 / n, cnt = 0;
            for (int y = VBH + 4; y < 200; y++) for (int x = cx - slot / 2; x < cx + slot / 2; x++) if (px[y * 300 + x] == D.fg) cnt++;
            CHECK(cnt >= 8);
        }
        guards_ok(px, 300, 200); gfree(px);
    }
}

static void test_voices_height_peak(void)
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    char nm[16][4]; vnames(nm, 4);
    float lv[16] = { 0, 0.25f, 0.5f, 1 }, pk[16] = { 0, 0.75f, 0.5f, 1 };
    pc_vd_voices(&D, 0, NULL, 4, lv, pk, nm);
    for (int i = 0; i < 4; i++) {
        int cx = (2 * i + 1) * 300 / 8, h = 0;
        for (int y = 0; y < VBH; y++) if (px[y * 300 + cx] != D.bg) h++;
        if (i == 1) { CHECK(px[(VBH - 135) * 300 + cx] == D.fg && px[(VBH - 134) * 300 + cx] == D.fg && px[(VBH - 136) * 300 + cx] == D.bg); CHECK_EQ_INT(h, 45 + 2); }   /* level 45 rows, the peak cap 2 rows at 135 */
        else CHECK_EQ_INT(h, i == 0 ? 0 : i == 2 ? 90 : VBH);
    }
    guards_ok(px, 300, 200); gfree(px);
}

static void test_voices_glyphs(void)       /* one voice, name "T": the 5x7 glyph at 2x, centred under the bar, rows 184..197 */
{
    uint16_t *px = guarded(300, 200);
    setup(px, 300, 200);
    char nm[16][4]; memset(nm, 0, sizeof nm); strcpy(nm[0], "T");
    float lv[16] = { 0.5f }, pk[16] = { 0.5f };
    pc_vd_voices(&D, 0, NULL, 1, lv, pk, nm);
    for (int y = 184; y < 198; y++) {
        int g = (y - 184) / 2;                                              /* glyph row 0..6: T = 11111, then 00100 */
        for (int x = 140; x < 160; x++) {
            bool want = g == 0 ? (x >= 145 && x < 155) : (x >= 149 && x < 151);
            CHECK_EQ_INT(px[y * 300 + x] == D.fg, want);
        }
    }
    for (int y = 198; y < 200; y++) for (int x = 0; x < 300; x++) CHECK(px[y * 300 + x] == D.bg);
    guards_ok(px, 300, 200); gfree(px);
    /* a name that is not letters or digits draws no glyph; NULL names draw none at all; lower case is drawn as upper case */
    px = guarded(300, 200); setup(px, 300, 200);
    strcpy(nm[0], "t");
    pc_vd_voices(&D, 0, NULL, 1, lv, pk, nm);
    CHECK(px[184 * 300 + 150] == D.fg);
    px[0] = 0; setup(px, 300, 200);
    pc_vd_voices(&D, 0, NULL, 1, lv, pk, NULL);
    for (int y = 180; y < 200; y++) for (int x = 0; x < 300; x++) CHECK(px[y * 300 + x] == D.bg);
    guards_ok(px, 300, 200); gfree(px);
}

static void test_voices_dirty(void)        /* later frames: only the rectangle of the changed bars; a changed name or count redraws all */
{
    uint16_t *px = guarded(300, 200), *before = malloc(300 * 200 * 2);
    setup(px, 300, 200);
    char nm[16][4]; vnames(nm, 5);
    float lv[16] = { 0.2f, 0.4f, 0.6f, 0.8f, 0.5f }, pk[16];
    memcpy(pk, lv, sizeof pk);
    for (int var = 0; var < 2; var++) {
        pc_vd_voices(&D, var, NULL, 5, lv, pk, nm);
        memcpy(before, px, 300 * 200 * 2);
        pc_vd_rect_t r = pc_vd_voices(&D, var, NULL, 5, lv, pk, nm);
        CHECK(r.w == 0 && !memcmp(before, px, 300 * 200 * 2));            /* nothing changed: nothing drawn, nothing reported */
        lv[2] = 0.9f; pk[2] = 0.9f;
        r = pc_vd_voices(&D, var, NULL, 5, lv, pk, nm);
        CHECK(r.w > 0 && r.w < 300);
        rect_covers(before, px, 300, 200, r);
        lv[2] = 0.6f; pk[2] = 0.6f;
        pc_vd_voices(&D, var, NULL, 5, lv, pk, nm);
        memcpy(before, px, 300 * 200 * 2);
        nm[1][0] = 'Z';                                                    /* the same count, another name: all again */
        r = pc_vd_voices(&D, var, NULL, 5, lv, pk, nm);
        CHECK(r.x == 0 && r.w == 300 && r.h == 200 && memcmp(before, px, 300 * 200 * 2));
        vnames(nm, 6);
        r = pc_vd_voices(&D, var, NULL, 6, lv, pk, nm);                  /* another count */
        CHECK(r.w == 300 && r.h == 200);
        vnames(nm, 5);
    }
    guards_ok(px, 300, 200); gfree(px); free(before);
}

/* wedge (0..63, 5.625 degrees each from straight down, clockwise on screen) of a lit pixel: by angle, with a little room for the pixel grid */
static int lit_wedges(const uint16_t *px, uint16_t bg, int *per_wedge, float *dmin, float *dmax)
{
    int tot = 0;
    memset(per_wedge, 0, 64 * sizeof(int)); *dmin = 1e9f; *dmax = 0;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != bg) {
        float a, r; polar(x, y, &a, &r);
        int wd = (int)(a / 5.625f); if (wd > 63) wd = 63;
        per_wedge[wd]++; tot++;
        if (r < *dmin) *dmin = r;
        if (r > *dmax) *dmax = r;
    }
    return tot;
}

static void test_voices_ring(void)
{
    void *map = vmap();
    char nm[16][4];
    for (int n = 1; n <= 16; n++) {
        uint16_t *px = guarded(RS, RS);
        setup(px, RS, RS);
        float lv[16], pk[16];
        vfull(lv, n, 1.0f); vfull(pk, n, 1.0f); vnames(nm, n);
        pc_vd_rect_t r = pc_vd_voices(&D, 2, map, n, lv, pk, nm);
        CHECK(r.x == 0 && r.y == 0 && r.w == RS && r.h == RS);
        int pw[64]; float dmin, dmax;
        lit_wedges(px, D.bg, pw, &dmin, &dmax);
        CHECK(dmax >= ROUT - 2.0f && dmax <= ROUT + 1.0f && dmin >= RIN - 1.0f);      /* full level reaches the outer radius; nothing on the inside */
        int tr = 0;                                                                   /* groups around the circle at radius 100: one per voice, one wedge gap between */
        const int S = 1440;
        for (int i = 0; i < S; i++) {
            float a0 = 6.2831853f * i / S, a1 = 6.2831853f * (i + 1) / S;
            int x0 = (int)lroundf(RCT + 100 * cosf(a0)), y0 = (int)lroundf(RCT + 100 * sinf(a0)), x1 = (int)lroundf(RCT + 100 * cosf(a1)), y1 = (int)lroundf(RCT + 100 * sinf(a1));
            if (px[y0 * RS + x0] == D.bg && px[y1 * RS + x1] != D.bg) tr++;
        }
        CHECK_EQ_INT(tr, n);
        int gaps = 0; for (int w = 0; w < 64; w++) if (pw[w] < 200) gaps++;          /* a gap is a wedge nothing is drawn in (a lit wedge has ~1200 pixels; the 45 degree diagonal can give a gap up to 77 of its neighbour: ties in the angle) */
        CHECK_EQ_INT(gaps, n);
        guards_ok(px, RS, RS); gfree(px);
    }
    free(map);
}

static void test_voices_ring_one(void)     /* one voice lit: only its group, and its length follows its level */
{
    void *map = vmap();
    char nm[16][4]; vnames(nm, 5);
    for (int j = 0; j < 5; j++) for (int q = 0; q < 2; q++) {
        uint16_t *px = guarded(RS, RS);
        setup(px, RS, RS);
        float lv[16] = { 0 }, pk[16] = { 0 };
        lv[j] = pk[j] = q ? 0.5f : 1.0f;
        pc_vd_voices(&D, 2, map, 5, lv, pk, nm);
        int pw[64]; float dmin, dmax;
        CHECK(lit_wedges(px, D.bg, pw, &dmin, &dmax) > 500);
        const int g0 = j * 64 / 5, g1 = (j + 1) * 64 / 5 - 1;                           /* the wedges of group j, the last one is the gap */
        for (int w = 0; w < 64; w++) if (w < g0 || w >= g1) CHECK(pw[w] < 100);           /* (the diagonals: a tie in the angle may put up to 77 pixels in the next wedge) */
        for (int w = g0 + 1; w < g1 - 1; w++) CHECK(pw[w] > 0);
        CHECK_NEAR(dmax, RIN + (q ? 0.5f : 1.0f) * (ROUT - RIN), 3.0);
        guards_ok(px, RS, RS); gfree(px);
    }
    free(map);
}

static void test_voices_colours(void)      /* the colour mapping applies as it does to bars: BAND gives every voice its own colour */
{
    void *map = vmap();
    char nm[16][4]; vnames(nm, 4);
    float lv[16], pk[16];
    vfull(lv, 4, 0.8f); vfull(pk, 4, 0.8f);
    for (int var = 0; var < 3; var++) {
        uint16_t *px = guarded(var == 2 ? RS : 300, var == 2 ? RS : 200);
        setup(px, var == 2 ? RS : 300, var == 2 ? RS : 200);
        pc_vd_set_map(&D, PC_VD_MAP_BAND);
        pc_vd_voices(&D, var, map, 4, lv, pk, nm);
        uint16_t c[4];
        for (int i = 0; i < 4; i++) {
            if (var < 2) { int cx = (2 * i + 1) * 300 / 8; c[i] = px[(VBH - 22) * 300 + cx];   /* row 21 above the foot: lit also in the segments (21 % 7 = 0) */ }
            else { int w = i * 16 + 7; float al = (w + 0.5f) * 5.625f * 3.14159265f / 180.0f; c[i] = px[(int)lroundf(RCT + 100 * cosf(al)) * RS + (int)lroundf(RCT - 100 * sinf(al))]; }   /* the middle of the voice's group, on the left of straight down */
            CHECK(c[i] != D.bg);
        }
        for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) CHECK(c[i] != c[j]);
        gfree(px);
    }
    free(map);
}

static void test_voices_bad_input(void)
{
    void *map = vmap();
    char nm[PC_VOICES_MAX][4]; vnames(nm, PC_VOICES_MAX);
    float lv[PC_VOICES_MAX + 1], pk[PC_VOICES_MAX + 1];
    for (int i = 0; i < PC_VOICES_MAX + 1; i++) { lv[i] = i % 3 ? NAN : -2.0f + i; pk[i] = 7.0f; }
    for (int var = -1; var < 4; var++) for (int n = -1; n < PC_VOICES_MAX + 4; n++) {
        for (int sz = 0; sz < 2; sz++) {
            int w = sz ? RS : 300, h = sz ? RS : 200;
            uint16_t *px = guarded(w, h);
            setup(px, w, h);
            pc_vd_rect_t r = pc_vd_voices(&D, var, map, n, lv, pk, nm);
            if (n < 1 || n > PC_VOICES_MAX) CHECK(r.w == 0);                          /* the count out of range draws nothing */
            if (r.w) CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h);
            guards_ok(px, w, h); gfree(px);
        }
    }
    uint16_t *px = guarded(RS, RS); setup(px, RS, RS);
    CHECK(pc_vd_voices(&D, 2, NULL, 5, lv, pk, nm).w == 0);               /* the ring without its map */
    CHECK(pc_vd_voices(&D, 0, map, 5, NULL, pk, nm).w == 0);
    CHECK(pc_vd_voices(&D, 0, map, 5, lv, NULL, nm).w == 0);
    guards_ok(px, RS, RS); gfree(px);
    CHECK(pc_vd_voices(NULL, 0, map, 5, lv, pk, nm).w == 0);
    px = guarded(300, 200); setup(px, 300, 200);
    D.tag = 0; for (int i = 0; i < 320; i++) D.prev_a[i] = D.prev_b[i] = (int16_t)(i % 2 ? 30000 : -30000);   /* garbage memory of another style */
    D.fresh = false;
    pc_vd_voices(&D, 1, map, 5, lv, pk, nm);
    pc_vd_voices(&D, 1, map, 5, lv, pk, nm);
    guards_ok(px, 300, 200); gfree(px);
    free(map);
}


/* ---- voice rows and piano roll (visualiser phase 1) ---- */
static void snap(pc_voices_t *v, int n)
{
    memset(v, 0, sizeof *v);
    v->n = (uint8_t)n;
    for (int i = 0; i < n; i++) { pc_voice_short(NULL, i, v->name[i]); pc_voice_clear_extra(v, i); }
}
static int count_non_bg(const uint16_t *px, int n, uint16_t bg) { int c = 0; for (int i = 0; i < n; i++) c += px[i] != bg; return c; }

static void test_vstate_feed(void)
{
    static pc_vd_vstate_t st;
    pc_voices_t v;
    snap(&v, 3);
    pc_vd_vstate_reset(&st);
    pc_vd_vstate_feed(&st, &v, 1000, 33);                   /* first feed: the state adopts n and the clock */
    CHECK_EQ_INT(st.n, 3);
    CHECK_EQ_INT(st.head, 0);
    v.level[1] = 200; v.cents[1] = 6900; v.flags[1] = PC_VF_PITCH; v.strikes[1] = 1;
    pc_vd_vstate_feed(&st, &v, 1040, 40);                   /* one column due, and the strike lights the lamp */
    CHECK_EQ_INT(st.head, 1);
    CHECK_EQ_INT(st.flash[1], 255);
    CHECK_EQ_INT(st.note[1][1], 46);                        /* A4 = MIDI 69 = 69 - 23 */
    CHECK(st.lv[1][1] & 0x80);                              /* the note start */
    CHECK_EQ_INT(st.note[1][0], 0);                         /* a quiet voice: nothing */
    pc_vd_vstate_feed(&st, &v, 1160, 120);                  /* three more columns at once, the lamp has fallen to 0 (180 ms) or close */
    CHECK_EQ_INT(st.head, 4);
    CHECK(st.flash[1] < 130);
    CHECK(!(st.lv[4][1] & 0x80));                           /* a held note: no new start */
    v.flags[2] = 0; v.level[2] = 100;                       /* sounding but without a pitch: the unpitched lane marker */
    pc_vd_vstate_feed(&st, &v, 1200, 40);
    CHECK_EQ_INT(st.note[5][2], 255);
    pc_vd_vstate_feed(&st, &v, 1200 + 40 * 400, 33);        /* a stall longer than the whole roll: bounded work, the clock restarts */
    CHECK(st.head >= 0 && st.head < PC_VD_ROLL_COLS);
    snap(&v, 5);
    pc_vd_vstate_feed(&st, &v, 99999, 33);                  /* another voice count: everything cleared */
    CHECK_EQ_INT(st.n, 5);
    CHECK_EQ_INT(st.head, 0);
    CHECK_EQ_INT(st.note[1][1], 0);
}

static void test_rows(void)
{
    static pc_vd_vstate_t st;
    pc_voices_t v;
    float lv[PC_VOICES_MAX], pk[PC_VOICES_MAX];
    uint8_t ms[PC_VOICES_MAX];
    for (int n = 1; n <= PC_VOICES_MAX; n += (n < 9 ? 1 : 7)) {
        snap(&v, n);
        for (int i = 0; i < n; i++) { v.level[i] = (uint8_t)(255 * (i + 1) / n); v.cents[i] = (int16_t)(4800 + 100 * i); v.flags[i] = PC_VF_PITCH; v.pan[i] = (int8_t)(i * 20 - 60); lv[i] = v.level[i] / 255.0f; pk[i] = lv[i]; ms[i] = 0; }
        uint16_t *px = guarded(300, 200); setup(px, 300, 200);
        pc_vd_vstate_reset(&st);
        pc_vd_vstate_feed(&st, &v, 0, 33);
        pc_vd_rect_t r = pc_vd_rows(&D, &st, &v, lv, pk, NULL, -1);
        CHECK(r.x == 0 && r.y == 0 && r.w == 300 && r.h == 200);
        const int base = count_non_bg(px, 300 * 200, D.bg);
        CHECK(base > 100 * n / 2);                           /* names, bars, notes are drawn */
        ms[0] = 1;                                           /* a muted row: the "M" mark and the dim look change pixels */
        uint16_t *p2 = guarded(300, 200); setup(p2, 300, 200);
        pc_vd_rows(&D, &st, &v, lv, pk, ms, 0);
        CHECK(memcmp(px, p2, 300 * 200 * 2) != 0);
        guards_ok(px, 300, 200); guards_ok(p2, 300, 200); gfree(px); gfree(p2);
    }
    /* bad input: nothing drawn, nothing written */
    uint16_t *px = guarded(300, 200); setup(px, 300, 200);
    snap(&v, 4);
    CHECK(pc_vd_rows(&D, &st, &v, lv, pk, NULL, -1).w > 0);
    CHECK(pc_vd_rows(&D, NULL, &v, lv, pk, NULL, -1).w == 0);
    CHECK(pc_vd_rows(&D, &st, NULL, lv, pk, NULL, -1).w == 0);
    CHECK(pc_vd_rows(&D, &st, &v, NULL, pk, NULL, -1).w == 0);
    v.n = 0; CHECK(pc_vd_rows(&D, &st, &v, lv, pk, NULL, -1).w == 0);
    v.n = PC_VOICES_MAX + 1; CHECK(pc_vd_rows(&D, &st, &v, lv, pk, NULL, -1).w == 0);
    guards_ok(px, 300, 200); gfree(px);
    uint16_t *q = guarded(340, 340); setup(q, 340, 340);
    snap(&v, 4);
    CHECK(pc_vd_rows(&D, &st, &v, lv, pk, NULL, -1).w == 0);   /* another picture size */
    guards_ok(q, 340, 340); gfree(q);
}

static void test_roll(void)
{
    static pc_vd_vstate_t st;
    pc_voices_t v;
    snap(&v, 2);
    uint8_t ms[2] = { 0, 0 };
    uint16_t *px = guarded(300, 200); setup(px, 300, 200);
    pc_vd_vstate_reset(&st);
    pc_vd_vstate_feed(&st, &v, 0, 33);
    v.level[0] = 220; v.cents[0] = 6900; v.flags[0] = PC_VF_PITCH; v.strikes[0] = 1;
    v.level[1] = 220; v.cents[1] = 2400; v.flags[1] = PC_VF_PITCH;
    pc_vd_vstate_feed(&st, &v, 40, 40);
    pc_vd_rect_t r = pc_vd_roll(&D, 0, &st, &v, ms, -1);
    CHECK(r.w == 300 && r.h == 200);
    /* A4 is on row 196 - 2 * 45 = 106 (columns 298, 299); C1 (the lowest note) on row 196 */
    CHECK(px[106 * 300 + 298] != D.bg);
    CHECK(px[196 * 300 + 298] != D.bg);
    CHECK(px[150 * 300 + 298] == D.bg || px[150 * 300 + 298] != px[106 * 300 + 298]);
    CHECK(px[106 * 300 + 100] == D.bg);                      /* the past is empty */
    ms[0] = 1;                                               /* muted: the voice's notes are not drawn */
    pc_vd_roll(&D, 0, &st, &v, ms, -1);
    CHECK(px[106 * 300 + 298] == D.bg);
    CHECK(px[196 * 300 + 298] != D.bg);
    pc_vd_roll(&D, 1, &st, &v, NULL, 1);                     /* the pitch colouring and the cursor: still drawn */
    CHECK(px[106 * 300 + 298] != D.bg);
    for (int i = 0; i < PC_VD_ROLL_COLS * 2; i++) pc_vd_vstate_feed(&st, &v, 80 + 40 * (uint32_t)i, 40);   /* the ring wraps twice */
    CHECK(pc_vd_roll(&D, 0, &st, &v, NULL, -1).w == 300);
    CHECK(pc_vd_roll(&D, 0, NULL, &v, NULL, -1).w == 0);
    CHECK(pc_vd_roll(&D, 0, &st, NULL, NULL, -1).w == 0);
    CHECK(pc_vd_roll(&D, 9, &st, &v, NULL, 99).w == 300);    /* a bad variant and cursor are tolerated */
    guards_ok(px, 300, 200); gfree(px);
}

/* ---- round-screen looks: bars variants 4 (Lens) and 5 (Arch) ---- */
static void test_round_switch(void)
{
    static const int sq[PC_VD_KINDS] = { 4, 4, 3, 3, 2, 3, 1, 2, 2 }, rd[PC_VD_KINDS] = { 6, 4, 3, 3, 2, 3, 1, 2, 2 };
    for (int k = 0; k < PC_VD_KINDS; k++) {
        CHECK_EQ_INT(pc_vd_variants_on((pc_vd_kind_t)k, false), sq[k]);
        CHECK_EQ_INT(pc_vd_variants_on((pc_vd_kind_t)k, true), rd[k]);
        CHECK_EQ_INT(pc_vd_variants((pc_vd_kind_t)k), PC_VD_ROUND_SCREEN ? rd[k] : sq[k]);
    }
    CHECK_EQ_INT(pc_vd_variants_on((pc_vd_kind_t)99, true), 1);
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 4, false), "圓鏡");
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 4, true), "Lens");
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 5, false), "拱形");
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 5, true), "Arch");
    CHECK_STR(pc_vd_var_label(PC_VD_BARS, 6, true), "Solid");
    for (int v = 4; v < 6; v++) { int w, h; pc_vd_size(PC_VD_BARS, v, &w, &h); CHECK(w == 340 && h == 340); }
}

static pc_vd_rect_t rbars(int var, const float *lv, const float *pk, int bw) { return pc_vd_bars(&D, var, lv, pk, bw); }
static float far_from_centre(const uint16_t *px, uint16_t bg)       /* the largest distance (pixel centres, from 169.5) of a pixel that is not bg */
{
    float m = 0;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != bg) {
        float dx = x - RCT, dy = y - RCT, r = sqrtf(dx * dx + dy * dy);
        if (r > m) m = r;
    }
    return m;
}
static int col_x(int i, int nb, int bw) { return (RS - (nb * bw + (nb - 1) * 2)) / 2 + i * (bw + 2) + bw / 2; }    /* the middle pixel column of bar i */

static void test_bars_round(void)
{
    uint16_t *px = guarded(RS, RS), *before = malloc(RS * RS * 2);
    float lv[64], pk[64], z[64];
    tone(z, 0);
    for (int var = 4; var < 6; var++) {
        for (int bw = 5; bw <= 9; bw += 2) {
            const int nb = (314 + 2) / (bw + 2) > 48 ? 48 : (314 + 2) / (bw + 2), cols = var == 5 ? (nb & ~1) : nb;
            setup(px, RS, RS);
            tone(lv, 1.0f); tone(pk, 1.0f);
            pc_vd_rect_t r = rbars(var, lv, pk, bw);
            CHECK(r.w == RS && r.h == RS);
            /* a full level never leaves the glass (radius 180 round the picture centre, the circle the spike was approved on) and not far from the disc */
            CHECK(far_from_centre(px, D.bg) <= 176.0f);                       /* measured: Lens 170.2, Arch 173.4 at most */
            int l = RS, rr = -1;
            for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) if (px[y * RS + x] != D.bg) { if (x < l) l = x; if (x > rr) rr = x; }
            const int span = cols * bw + (cols - 1) * 2;
            CHECK_EQ_INT(rr - l + 1, span);                                              /* the columns span nb*pitch - 2, centred */
            CHECK(span >= 300 && span <= 322);                                           /* about +-157 */
            /* a column's full length follows the circle: the centre columns are the longest, the outer ones shorter */
            const int mid = cols / 2;
            int in = 0, out = 0, xi = col_x(mid, cols, bw), xo = col_x(0, cols, bw);
            for (int y = 0; y < RS; y++) { in += px[y * RS + xi] != D.bg; out += px[y * RS + xo] != D.bg; }
            CHECK(in > out && out >= 2);
            if (var == 4) { CHECK(in >= 2 * (int)(0.92f * sqrtf(170.0f * 170.0f - (bw + 2) * (bw + 2) * 0.25f)) - 2); }   /* the full half length either side of the centre row */
            guards_ok(px, RS, RS);
            /* an unchanged input: nothing to do */
            r = rbars(var, lv, pk, bw);
            CHECK_EQ_INT(r.w, 0);
        }
    }
    {   /* Lens: top/bottom symmetric; Arch: left/right symmetric, bass (the first bands) at the two centre columns, a flat baseline at row 213 */
        mk_in(lv, pk);
        setup(px, RS, RS);
        rbars(4, lv, pk, 7);
        for (int y = 0; y < RS / 2; y++) for (int x = 0; x < RS; x++) CHECK(px[y * RS + x] == px[(RS - 1 - y) * RS + x]);
        setup(px, RS, RS);
        rbars(5, lv, pk, 7);
        for (int y = 0; y < RS; y++) for (int x = 0; x < RS / 2; x++) CHECK(px[y * RS + x] == px[y * RS + RS - 1 - x]);
        for (int x = 0; x < RS; x++) CHECK(px[214 * RS + x] == D.bg);                     /* nothing below the baseline */
        setup(px, RS, RS);
        memset(lv, 0, sizeof lv); memset(pk, 0, sizeof pk); lv[0] = 1;                    /* the lowest band alone */
        rbars(5, lv, z, 7);
        const int nb = 34, x1 = col_x(nb / 2 - 1, nb, 7), x2 = col_x(nb / 2, nb, 7);
        CHECK(px[212 * RS + x1] != D.bg && px[212 * RS + x2] != D.bg);
        CHECK(px[212 * RS + x1 - 9] == D.bg && px[212 * RS + x2 + 9] == D.bg);            /* only the two centre columns */
        CHECK(px[213 * RS + x1] == D.bg);                                                 /* the baseline: the lowest bar row is 212 */
        setup(px, RS, RS);
        lv[0] = 0; lv[63] = 1;                                                            /* the highest band: the outer pair */
        rbars(5, lv, z, 7);
        CHECK(px[212 * RS + col_x(0, nb, 7)] != D.bg && px[212 * RS + col_x(nb - 1, nb, 7)] != D.bg);
    }
    for (int var = 4; var < 6; var++) {      /* one changed band dirties only its column(s); incremental == from scratch; every map and bar width */
        for (int m = 0; m < PC_VD_MAPS; m++) {
            setup(px, RS, RS);
            pc_vd_set_map(&D, (pc_vd_map_t)m);
            mk_in(lv, pk);
            rbars(var, lv, pk, 7);
            CHECK_EQ_INT(rbars(var, lv, pk, 7).w, 0);
            memcpy(before, px, RS * RS * 2);
            for (int b = 20; b < (var == 5 ? 24 : 21); b++) { lv[b] = 0.1f; pk[b] = 0.97f; }         /* Arch: a band is a group of 3 or 4 inputs, take them all */
            pc_vd_rect_t r = rbars(var, lv, pk, 7);
            CHECK(r.w > 0 && r.w <= (var == 4 ? 7 + 9 : 36 * 9) && r.h > 0);
            rect_covers(before, px, RS, RS, r);
            if (var == 4) CHECK(r.w <= 16);                                               /* a band belongs to one or two columns */
            memcpy(before, px, RS * RS * 2);
            lv[20] = 0.6f; lv[3] = 0; pk[3] = 0;
            r = rbars(var, lv, pk, 7);
            rect_covers(before, px, RS, RS, r);
            uint16_t *ref = guarded(RS, RS);
            pc_vd_t *save = malloc(sizeof D); memcpy(save, &D, sizeof D);
            pc_vd_init(&D, ref, RS, RS); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon")); pc_vd_set_map(&D, (pc_vd_map_t)m);
            rbars(var, lv, pk, 7);
            CHECK(!memcmp(ref, px, RS * RS * 2));
            memcpy(&D, save, sizeof D); free(save); gfree(ref);
            for (int i = 0; i < RS * RS; i++) CHECK(in_lut_or_fg(&D, px[i]));
        }
    }
    {   /* Arch: BAND gives the two columns of a band one colour (and differs between bands); HEIGHT/TIP/LEVEL are relative to the column's own length */
        setup(px, RS, RS);
        pc_vd_set_map(&D, PC_VD_MAP_BAND);
        tone(lv, 1.0f);
        rbars(5, lv, z, 7);
        const int nb = 34;
        for (int b = 0; b < nb / 2; b++) CHECK(px[200 * RS + col_x(nb / 2 - 1 - b, nb, 7)] == px[200 * RS + col_x(nb / 2 + b, nb, 7)]);
        CHECK(px[200 * RS + col_x(16, nb, 7)] != px[200 * RS + col_x(0, nb, 7)]);
        pc_vd_set_map(&D, PC_VD_MAP_HEIGHT);
        rbars(5, lv, z, 7);
        for (int i = 0; i < nb; i++) CHECK(px[212 * RS + col_x(i, nb, 7)] == D.lut[0]);   /* the foot of every column is the ramp start */
        const int xo = col_x(0, nb, 7);
        int top = 0; while (px[top * RS + xo] == D.bg) top++;
        CHECK(px[top * RS + xo] == D.lut[255]);                                           /* a full column ends in the ramp's last colour, long or short */
        const int xi = col_x(nb / 2, nb, 7);
        top = 0; while (px[top * RS + xi] == D.bg) top++;
        CHECK(px[top * RS + xi] == D.lut[255]);
        /* Lens: the same */
        setup(px, RS, RS);
        rbars(4, lv, z, 7);
        top = 0; while (px[top * RS + col_x(0, 35, 7)] == D.bg) top++;
        CHECK(px[top * RS + col_x(0, 35, 7)] == D.lut[255]);
        CHECK(px[169 * RS + col_x(0, 35, 7)] == D.lut[0] && px[170 * RS + col_x(0, 35, 7)] == D.lut[0]);   /* no gap on the centre line */
    }
    {   /* peaks: a held peak above the bar is a 2 px cap, mirrored in Lens */
        setup(px, RS, RS);
        tone(lv, 0.2f); tone(pk, 0.8f);
        rbars(4, lv, pk, 7);
        const int x = col_x(10, 35, 7);
        int n = 0;
        for (int y = 0; y < 170; y++) n += px[y * RS + x] == D.fg;
        CHECK_EQ_INT(n, 2);
        for (int y = 0; y < RS; y++) CHECK((px[y * RS + x] == D.fg) == (px[(RS - 1 - y) * RS + x] == D.fg));
        setup(px, RS, RS);
        rbars(5, lv, pk, 7);
        n = 0; for (int y = 0; y < RS; y++) n += px[y * RS + col_x(10, 34, 7)] == D.fg;
        CHECK_EQ_INT(n, 2);
    }
    guards_ok(px, RS, RS); gfree(px); free(before);
}

static void test_bars_round_edges(void)
{
    float lv[64], pk[64];
    tone(lv, 1); tone(pk, 1);
    uint16_t *px = guarded(300, 200);                                 /* a picture of another size: nothing drawn */
    setup(px, 300, 200);
    pc_vd_rect_t r = pc_vd_bars(&D, 4, lv, pk, 7);
    CHECK_EQ_INT(r.w, 0);
    for (int i = 0; i < 300 * 200; i++) CHECK(px[i] == D.bg);
    guards_ok(px, 300, 200); gfree(px);
    px = guarded(RS, RS);
    for (int var = 4; var < 6; var++) for (int t = 0; t < 4; t++) {   /* invalid widths behave as 7; NaN and wild input stay inside the picture */
        static const int bad[4] = { 0, -3, 400, 157 };
        setup(px, RS, RS);
        pc_vd_rect_t q = pc_vd_bars(&D, var, lv, pk, bad[t]);
        CHECK(q.w == RS);
        float w2[64]; for (int i = 0; i < 64; i++) w2[i] = i % 3 == 0 ? NAN : i % 3 == 1 ? -5.0f : 9.0f;
        garble_prev(&D, t);
        D.tag = (int16_t)(1 + var); D.extra = (int16_t)(bad[t] < 1 || bad[t] > 156 ? 7 : bad[t]);
        pc_vd_bars(&D, var, w2, w2, bad[t]);
    }
    guards_ok(px, RS, RS); gfree(px);
}

/* ---- the full-size waterfall (340x340 disc) ---- */
static int chord_hw(int y) { float dy = (float)y - RCT; return (int)sqrtf(170.0f * 170.0f - dy * dy); }       /* half the chord of picture row y; the row spans [170 - hw, 170 + hw) */
static void test_fall_full(void)
{
    uint16_t *px = guarded(RS, RS);
    float lv[64], z[64], on[64];
    tone(z, 0); tone(on, 1);
    {   int w, h;
        pc_vd_size_fall(false, &w, &h); CHECK(w == 256 && h == 200);
        pc_vd_size_fall(true, &w, &h); CHECK(PC_VD_ROUND_SCREEN ? (w == 340 && h == 340) : (w == 256 && h == 200));
    }
    for (int var = 0; var < 3; var++) {
        const int up = var == 1;
        setup(px, RS, RS);
        pc_vd_rect_t r = pc_vd_fall(&D, var, on);
        CHECK(r.x == 0 && r.y == 0 && r.w == RS && r.h == RS);
        /* the newest row (two picture rows) is lit across its whole chord and not a pixel beyond */
        for (int rr = 0; rr < 2; rr++) {
            const int y = up ? RS - 2 + rr : rr, hw = chord_hw(y);
            for (int x = 0; x < RS; x++) CHECK((px[y * RS + x] != D.bg) == (x >= 170 - hw && x < 170 + hw));
        }
        for (int y = 2; y < RS - 2; y++) for (int x = 0; x < RS; x++) CHECK(px[y * RS + x] == D.bg);
        /* every row of the history is drawn only across its chord: after 200 full frames the corners are untouched */
        for (int i = 0; i < 200; i++) pc_vd_fall(&D, var, on);
        for (int y = 0; y < RS; y++) { const int hw = chord_hw(y); for (int x = 0; x < RS; x++) CHECK((px[y * RS + x] != D.bg) == (x >= 170 - hw && x < 170 + hw)); }
        CHECK(px[0] == D.bg && px[RS - 1] == D.bg && px[(RS - 1) * RS] == D.bg && px[RS * RS - 1] == D.bg);
        /* a marked row moves 2 px a frame, and reaches both ends of the chord it is in then */
        setup(px, RS, RS);
        pc_vd_fall(&D, var, on);
        for (int n = 1; n <= 150; n += 7) {
            for (int k = 0; k < (n == 1 ? 1 : 7); k++) pc_vd_fall(&D, var, z);
            const int age = n == 1 ? 1 : n;
            for (int rr = 0; rr < 2; rr++) {
                const int y = up ? RS - 2 - 2 * age + rr : 2 * age + rr, hw = chord_hw(y);
                CHECK(px[y * RS + 170 - hw] == D.lut[255] && px[y * RS + 169 + hw] == D.lut[255]);
                CHECK(px[y * RS + 169 - hw] == D.bg && px[y * RS + 170 + hw] == D.bg);
            }
        }
        /* a marked row leaves the far edge after 170 frames: nothing left */
        for (int i = 0; i < 40; i++) pc_vd_fall(&D, var, z);
        for (int i = 0; i < RS * RS; i++) CHECK(px[i] == D.bg);
        guards_ok(px, RS, RS);
    }
    {   /* bins are by x over the whole width (x * 64 / 340): band 5 is the columns 27..31 in every row, vertical stripes */
        setup(px, RS, RS);
        memset(lv, 0, sizeof lv); lv[5] = 1;
        pc_vd_fall(&D, 0, lv);
        for (int i = 0; i < 50; i++) pc_vd_fall(&D, 0, z);
        for (int x = 0; x < RS; x++) CHECK((px[100 * RS + x] == D.lut[255]) == (x * 64 / 340 == 5));
        CHECK(px[101 * RS + 27] == D.lut[255] && px[101 * RS + 32] == D.bg);
        setup(px, RS, RS);                                   /* mirrored: bass in the middle, band 0 lights columns 31 and 32 */
        memset(lv, 0, sizeof lv); lv[0] = 1;
        pc_vd_fall(&D, 2, lv);
        for (int x = 160; x < 180; x++) CHECK((px[0 * RS + x] == D.lut[255]) == (x * 64 / 340 == 31 || x * 64 / 340 == 32));
    }
    {   /* the colour rule is the box's: level 0 is bg, a small level blends, 128 is on the ramp */
        setup(px, RS, RS);
        tone(lv, 0.5f);
        pc_vd_fall(&D, 0, lv);
        CHECK(px[0 * RS + 170] == D.lut[fall_idx(128)] && px[100 * RS + 170] == D.bg);
        for (int i = 0; i < 50; i++) pc_vd_fall(&D, 0, lv);
        CHECK(px[100 * RS + 170] == D.lut[fall_idx(128)]);
        /* a style or ramp change clears the history */
        pc_vd_fall(&D, 1, z);
        for (int i = 0; i < RS * RS; i++) CHECK(px[i] == D.bg);
        pc_vd_fall(&D, 1, on);
        pc_vd_set_colors(&D, PC_RAMP_ICE, NULL);
        pc_vd_fall(&D, 1, z);
        for (int i = 0; i < RS * RS; i++) CHECK(px[i] == D.bg);
    }
    guards_ok(px, RS, RS); gfree(px);
}

/* ---- ring gaps and the band order ---- */
static float bdist(int x, int y)         /* distance of a pixel centre to the nearest wedge boundary ray (angle k * 360 / 64 from straight down) */
{
    float dx = x - RCT, dy = y - RCT, r = sqrtf(dx * dx + dy * dy), u = atan2f(fabsf(dx), dy) * (64.0f / 6.28318531f), f = u - floorf(u);
    float e = f < 1 - f ? f : 1 - f;
    return r * sinf(e * 6.28318531f / 64.0f);
}
static int lit_all(void *map, uint16_t *px, int var, float *min_edge_lit, float *max_edge_dark)   /* every wedge at full level: the lit pixels; their smallest edge distance; the largest edge distance of a dark annulus pixel */
{
    float one[64], z[64];
    tone(one, 1.0f); tone(z, 0);
    setup(px, RS, RS);
    pc_vd_ring(&D, var, map, one, z);
    int n = 0;
    *min_edge_lit = 1e9f; *max_edge_dark = 0;
    for (int y = 0; y < RS; y++) for (int x = 0; x < RS; x++) {
        float dx = x - RCT, dy = y - RCT, r = sqrtf(dx * dx + dy * dy);
        if (px[y * RS + x] != D.bg) { n++; if (bdist(x, y) < *min_edge_lit) *min_edge_lit = bdist(x, y); }
        else if (r >= RIN + 1.0f && r <= ROUT - 1.0f && bdist(x, y) > *max_edge_dark) *max_edge_dark = bdist(x, y);
    }
    return n;
}
static void test_ring_gap(void)
{
    uint16_t *px = guarded(RS, RS);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *m0 = malloc(cap), *m2 = malloc(cap), *m4 = malloc(cap);
    CHECK(pc_vd_ringmap_build(m0, cap, RS, RS, RIN, ROUT, 0));
    CHECK(pc_vd_ringmap_build(m2, cap, RS, RS, RIN, ROUT, 2));
    CHECK(pc_vd_ringmap_build(m4, cap, RS, RS, RIN, ROUT, 4));
    CHECK_EQ_INT(pc_vd_ringmap_gap(m0), 0); CHECK_EQ_INT(pc_vd_ringmap_gap(m2), 2); CHECK_EQ_INT(pc_vd_ringmap_gap(m4), 4);
    CHECK_EQ_INT(pc_vd_ringmap_gap(NULL), 0);
    {   void *mb = malloc(cap);                                   /* anything but 0, 2, 4 is a plain map */
        for (int g = -1; g <= 9; g++) if (g != 2 && g != 4) { CHECK(pc_vd_ringmap_build(mb, cap, RS, RS, RIN, ROUT, g)); CHECK_EQ_INT(pc_vd_ringmap_gap(mb), 0); CHECK(!memcmp(mb, m0, cap)); }
        memset(mb, 0, cap); CHECK_EQ_INT(pc_vd_ringmap_gap(mb), 0);   /* not a map */
        free(mb); }
    for (int var = 0; var < 4; var++) {
        float e0, e2, e4, d0, d2, d4;
        int n0 = lit_all(m0, px, var, &e0, &d0), n2 = lit_all(m2, px, var, &e2, &d2), n4 = lit_all(m4, px, var, &e4, &d4);
        if (var == 3) continue;                                   /* dots: only the tip of every wedge, the edge rule is checked on the others */
        CHECK(n0 > n2 && n2 > n4 && n4 > n0 / 2);
        CHECK(e2 >= 1.0f && e4 >= 2.0f);                          /* no lit pixel closer than gap / 2 to a boundary */
        CHECK(d2 < 1.0f + 1e-3f && d4 < 2.0f + 1e-3f);            /* and every annulus pixel farther than that is lit, so the gap is exactly gap px wide */
        CHECK(e0 < 1.0f);                                         /* the plain map has pixels on the boundary lines */
    }
    /* with gap 2 the two wedges either side of straight down are 2 px apart: at full level the columns 169 and 170 of a row below the centre are bg (the plain map has them lit) */
    {   float one[64], z[64]; tone(one, 1.0f); tone(z, 0);
        setup(px, RS, RS); pc_vd_ring(&D, 0, m2, one, z);
        CHECK(px[300 * RS + 169] == D.bg && px[300 * RS + 170] == D.bg && px[300 * RS + 160] != D.bg && px[300 * RS + 179] != D.bg);
        setup(px, RS, RS); pc_vd_ring(&D, 0, m0, one, z);
        CHECK(px[300 * RS + 169] != D.bg && px[300 * RS + 170] != D.bg);
    }
    {   /* the voices' ring draws on a plain map and is not changed by the order */
        float lv[16] = { 0 }; char nm[16][4]; memset(nm, 0, sizeof nm);
        setup(px, RS, RS);
        for (int i = 0; i < 3; i++) lv[i] = 1;
        pc_vd_set_order(&D, PC_VD_ORD_SPREAD);
        pc_vd_voices(&D, 2, m0, 3, lv, lv, (const char (*)[4])nm);
        uint16_t *ref = guarded(RS, RS);
        pc_vd_init(&D, ref, RS, RS); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
        pc_vd_voices(&D, 2, m0, 3, lv, lv, (const char (*)[4])nm);
        CHECK(!memcmp(ref, px, RS * RS * 2));
        gfree(ref);
    }
    free(m0); free(m2); free(m4); guards_ok(px, RS, RS); gfree(px);
}

static void test_ring_order(void)
{
    CHECK_EQ_INT(PC_VD_ORDS, 3);
    static const char *const nm[3] = { "seq", "zigzag", "spread" };
    for (int o = 0; o < 3; o++) {
        CHECK_STR(pc_vd_order_name((pc_vd_order_t)o), nm[o]);
        CHECK_EQ_INT(pc_vd_order_parse(nm[o]), o);
        CHECK(pc_vd_order_label((pc_vd_order_t)o, true)[0] && pc_vd_order_label((pc_vd_order_t)o, false)[0]);
        for (int n = 32; n <= 64; n += 32) {                      /* every order is a permutation of 0..n-1, and band() is its inverse */
            int seen[64] = { 0 };
            for (int b = 0; b < n; b++) {
                int p = pc_vd_order_pos((pc_vd_order_t)o, b, n);
                CHECK(p >= 0 && p < n); seen[p]++;
                CHECK_EQ_INT(pc_vd_order_band((pc_vd_order_t)o, p, n), b);
                if (o == 0) CHECK_EQ_INT(p, b);                   /* SEQ is the identity */
            }
            for (int p = 0; p < n; p++) CHECK_EQ_INT(seen[p], 1);
        }
    }
    CHECK_STR(pc_vd_order_name((pc_vd_order_t)7), "seq"); CHECK_STR(pc_vd_order_name((pc_vd_order_t)-1), "seq");
    CHECK_EQ_INT(pc_vd_order_parse("bogus"), PC_VD_ORD_SEQ); CHECK_EQ_INT(pc_vd_order_parse(NULL), PC_VD_ORD_SEQ);
    CHECK_STR(pc_vd_order_label(PC_VD_ORD_SEQ, false), "順序"); CHECK_STR(pc_vd_order_label(PC_VD_ORD_ZIGZAG, false), "交錯"); CHECK_STR(pc_vd_order_label(PC_VD_ORD_SPREAD, false), "打散");
    CHECK_STR(pc_vd_order_label(PC_VD_ORD_SEQ, true), "Sequential"); CHECK_STR(pc_vd_order_label(PC_VD_ORD_ZIGZAG, true), "Alternate"); CHECK_STR(pc_vd_order_label(PC_VD_ORD_SPREAD, true), "Scatter");
    CHECK_STR(pc_vd_order_label((pc_vd_order_t)9, true), "Sequential");
    CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_ZIGZAG, 0, 64), 0); CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_ZIGZAG, 1, 64), 63);
    CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_ZIGZAG, 2, 64), 1); CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_ZIGZAG, 3, 64), 62);
    CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_SPREAD, 1, 64), 39); CHECK_EQ_INT(pc_vd_order_pos(PC_VD_ORD_SPREAD, 1, 32), 19);
    {   pc_vd_t t; uint16_t px[4];                                 /* the setter: fresh only on a change; out of range = SEQ */
        pc_vd_init(&t, px, 2, 2);
        t.fresh = false; pc_vd_set_order(&t, PC_VD_ORD_SEQ); CHECK(!t.fresh);
        pc_vd_set_order(&t, PC_VD_ORD_ZIGZAG); CHECK(t.fresh && t.order == PC_VD_ORD_ZIGZAG);
        t.fresh = false; pc_vd_set_order(&t, PC_VD_ORD_ZIGZAG); CHECK(!t.fresh);
        pc_vd_set_order(&t, (pc_vd_order_t)42); CHECK(t.fresh && t.order == PC_VD_ORD_SEQ);
        pc_vd_set_order(NULL, PC_VD_ORD_SPREAD); }
    /* drawn: band 1 alone lights wedge 63 with ZIGZAG (bottom, one wedge left of straight down... mirrored: the last wedge, just right of straight down) and wedge 1 with SEQ */
    uint16_t *px = guarded(RS, RS);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64] = { 0 }, z[64] = { 0 };
    lv[1] = 1;
    setup(px, RS, RS);
    pc_vd_set_order(&D, PC_VD_ORD_ZIGZAG);
    pc_vd_ring(&D, 0, map, lv, z);
    CHECK(px[290 * RS + RS / 2 + 2] != D.bg && px[290 * RS + RS / 2 - 2] == D.bg);   /* right of the vertical line below the centre: wedge 63 */
    setup(px, RS, RS);
    pc_vd_ring(&D, 0, map, lv, z);                                                    /* SEQ: wedge 1, left */
    CHECK(px[290 * RS + RS / 2 - 18] != D.bg && px[290 * RS + RS / 2 + 2] == D.bg);
    for (int o = 1; o < 3; o++) for (int b = 0; b < 64; b++) {   /* every band reaches the picture, and a change of order redraws all */
        setup(px, RS, RS);
        pc_vd_set_order(&D, (pc_vd_order_t)o);
        memset(lv, 0, sizeof lv); lv[b] = 1;
        pc_vd_ring(&D, 0, map, lv, z);
        int n = 0; for (int i = 0; i < RS * RS; i++) n += px[i] != D.bg;
        CHECK(n > 50);
        float one[64]; tone(one, 1.0f); (void)one;
    }
    {   /* BAND maps by the band: the same colour for a band at any position; and the order change redraws everything */
        setup(px, RS, RS);
        pc_vd_set_map(&D, PC_VD_MAP_BAND);
        memset(lv, 0, sizeof lv); lv[10] = 1;
        pc_vd_ring(&D, 0, map, lv, z);
        int c1 = -1; for (int i = 0; i < RS * RS; i++) if (px[i] != D.bg) { c1 = lut_idx(&D, px[i]); break; }
        pc_vd_set_order(&D, PC_VD_ORD_SPREAD);
        CHECK(pc_vd_ring(&D, 0, map, lv, z).w == RS);
        int c2 = -1; for (int i = 0; i < RS * RS; i++) if (px[i] != D.bg) { c2 = lut_idx(&D, px[i]); break; }
        CHECK(c1 >= 0 && c1 == c2);
        CHECK_EQ_INT(pc_vd_ring(&D, 0, map, lv, z).w, 0);
    }
    {   /* mirrored variant with ZIGZAG: left/right symmetric, bass pair at the bottom */
        setup(px, RS, RS);
        pc_vd_set_order(&D, PC_VD_ORD_ZIGZAG);
        mk_in(lv, z);
        pc_vd_ring(&D, 2, map, lv, lv);
        for (int y = 0; y < RS; y++) for (int x = 0; x < RS / 2; x++) CHECK(px[y * RS + x] == px[y * RS + RS - 1 - x]);
    }
    free(map); guards_ok(px, RS, RS); gfree(px);
}

/* ---- peak marks on and off ---- */
static int count_fg(const uint16_t *px, int n, uint16_t fg) { int k = 0; for (int i = 0; i < n; i++) k += px[i] == fg; return k; }
static void test_peaks_off(void)
{
    uint16_t *px = guarded(RS, RS), *p3 = guarded(300, 200);
    size_t cap = pc_vd_ringmap_bytes(RS, RS);
    void *map = malloc(cap);
    CHECK(pc_vd_ringmap_build(map, cap, RS, RS, RIN, ROUT, 0));
    float lv[64], pk[64], z[64];
    tone(lv, 0.25f); tone(pk, 0.75f); tone(z, 0);
    {   pc_vd_t t; uint16_t q[4];                                  /* the default is on; the setter marks fresh only on a change */
        pc_vd_init(&t, q, 2, 2);
        CHECK(t.peaks_off == 0);
        t.fresh = false; pc_vd_set_peaks(&t, true); CHECK(!t.fresh);
        pc_vd_set_peaks(&t, false); CHECK(t.fresh && t.peaks_off);
        t.fresh = false; pc_vd_set_peaks(&t, false); CHECK(!t.fresh);
        pc_vd_set_peaks(&t, true); CHECK(t.fresh && !t.peaks_off);
        pc_vd_set_peaks(NULL, true); }
    for (int var = 0; var < 3; var++) {                            /* bars: on has the 2 px cap above the bar, off has no fg at all; both unchanged otherwise */
        setup(p3, 300, 200);
        pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(count_fg(p3, 300 * 200, D.fg) > 0);
        const int with = count_not(p3, 300 * 200, D.bg);
        pc_vd_set_peaks(&D, false);
        CHECK(pc_vd_bars(&D, var, lv, pk, 7).w == 300);            /* the switch redraws everything */
        CHECK_EQ_INT(count_fg(p3, 300 * 200, D.fg), 0);
        CHECK(count_not(p3, 300 * 200, D.bg) < with);
        CHECK_EQ_INT(pc_vd_bars(&D, var, lv, pk, 7).w, 0);
        pc_vd_set_peaks(&D, true);                                 /* and back: the very same picture as one that never turned them off */
        pc_vd_bars(&D, var, lv, pk, 7);
        uint16_t *ref = guarded(300, 200);
        pc_vd_t *save = malloc(sizeof D); memcpy(save, &D, sizeof D);
        pc_vd_init(&D, ref, 300, 200); pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
        pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(!memcmp(ref, p3, 300 * 200 * 2));
        memcpy(&D, save, sizeof D); free(save); gfree(ref);
    }
    for (int var = 4; var < 6; var++) {
        setup(px, RS, RS);
        pc_vd_bars(&D, var, lv, pk, 7);
        CHECK(count_fg(px, RS * RS, D.fg) > 0);
        pc_vd_set_peaks(&D, false);
        pc_vd_bars(&D, var, lv, pk, 7);
        CHECK_EQ_INT(count_fg(px, RS * RS, D.fg), 0);
    }
    for (int var = 0; var < 4; var++) {
        setup(px, RS, RS);
        pc_vd_ring(&D, var, map, lv, pk);
        CHECK(count_fg(px, RS * RS, D.fg) > 0);
        pc_vd_set_peaks(&D, false);
        pc_vd_ring(&D, var, map, lv, pk);
        CHECK_EQ_INT(count_fg(px, RS * RS, D.fg), 0);
        CHECK_EQ_INT(pc_vd_ring(&D, var, map, lv, pk).w, 0);
    }
    {   /* voices: bars and ring */
        float vl[16], vp[16]; char nm[16][4]; memset(nm, 0, sizeof nm);
        for (int i = 0; i < 16; i++) { vl[i] = 0.25f; vp[i] = 0.75f; }
        setup(p3, 300, 200);
        pc_vd_voices(&D, 0, NULL, 4, vl, vp, (const char (*)[4])nm);
        CHECK(count_fg(p3, 300 * 200, D.fg) > 0);
        pc_vd_set_peaks(&D, false);
        pc_vd_voices(&D, 0, NULL, 4, vl, vp, (const char (*)[4])nm);
        CHECK_EQ_INT(count_fg(p3, 300 * 200, D.fg), 0);
        setup(px, RS, RS);
        pc_vd_voices(&D, 2, map, 4, vl, vp, (const char (*)[4])nm);
        CHECK(count_fg(px, RS * RS, D.fg) > 0);
        pc_vd_set_peaks(&D, false);
        pc_vd_voices(&D, 2, map, 4, vl, vp, (const char (*)[4])nm);
        CHECK_EQ_INT(count_fg(px, RS * RS, D.fg), 0);
    }
    free(map); guards_ok(px, RS, RS); gfree(px); gfree(p3);
}

int main(void)
{
    RUN(test_kinds);
    RUN(test_bars_heights);
    RUN(test_bars_peak);
    RUN(test_bars_variants);
    RUN(test_bars_width);
    RUN(test_bars_dirty);
    RUN(test_ring);
    RUN(test_ring_bass_bottom);
    RUN(test_ring_mirror_bands);
    RUN(test_peaks);
    RUN(test_style_switch);
    RUN(test_single_bar_line);
    RUN(test_edges);
    RUN(test_ringmap_cap);
    RUN(test_bad_input);
    RUN(test_scope_line);
    RUN(test_scope_trigger);
    RUN(test_scope_filled);
    RUN(test_scope_circle);
    RUN(test_scope_seam);
    RUN(test_scope_dirty);
    RUN(test_fall);
    RUN(test_fall_rule);
    RUN(test_blank);
    RUN(test_scope_bad_input);
    RUN(test_fall_bad_input);
    RUN(test_style_switch_all);
    RUN(test_phase);
    RUN(test_phase_bad_input);
    RUN(test_phase_switch);
    RUN(test_map_names);
    RUN(test_map_height_same);
    RUN(test_map_band);
    RUN(test_map_level);
    RUN(test_map_tip);
    RUN(test_map_incremental);
    RUN(test_map_all_variants);
    RUN(test_map_ignored);
    RUN(test_voices_kind);
    RUN(test_voices_bars);
    RUN(test_voices_height_peak);
    RUN(test_voices_glyphs);
    RUN(test_voices_dirty);
    RUN(test_voices_ring);
    RUN(test_voices_ring_one);
    RUN(test_voices_colours);
    RUN(test_vstate_feed);
    RUN(test_rows);
    RUN(test_roll);
    RUN(test_voices_bad_input);
    RUN(test_round_switch);
    RUN(test_bars_round);
    RUN(test_bars_round_edges);
    RUN(test_fall_full);
    RUN(test_ring_gap);
    RUN(test_ring_order);
    RUN(test_peaks_off);
    return 0;
}
