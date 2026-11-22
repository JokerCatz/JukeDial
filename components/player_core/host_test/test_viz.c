// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include <string.h>
#include "pc/pc_viz.h"
#include "pc_test.h"

static void test_themes(void)
{
    CHECK_EQ_INT(pc_theme_count(), 21);
    const pc_theme_t *t = pc_theme_at(0);
    CHECK_STR(t->name, "neon");
    CHECK_EQ_INT((int)t->bg, 0x070512);
    CHECK_EQ_INT((int)t->a1, 0x00e6ff);
    t = pc_theme_find("nord");
    CHECK_STR(t->label, "北歐霜");
    CHECK_EQ_INT((int)t->bg, 0x2e3440);
    CHECK_STR(pc_theme_find("nope")->name, "neon");
    CHECK_STR(pc_theme_find(NULL)->name, "neon");
    CHECK(pc_theme_at(21) == NULL);
    CHECK(pc_theme_at(-1) == NULL);
    for (int i = 0; i < pc_theme_count(); i++) {   /* dark only: background darker than text */
        const pc_theme_t *x = pc_theme_at(i);
        int lb = ((x->bg >> 16) & 255) + ((x->bg >> 8) & 255) + (x->bg & 255);
        int lf = ((x->fg >> 16) & 255) + ((x->fg >> 8) & 255) + (x->fg & 255);
        CHECK(lb < 200 && lf > lb + 300);
    }
}

/* fx apps T5 (spec 6.6): the second accent colour `on` of every theme */
static void rgb_hsv(uint32_t c, float *h, float *sat)
{
    float r = ((c >> 16) & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = (c & 255) / 255.f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    *sat = mx > 0 ? d / mx : 0;
    float hh = 0;
    if (d > 0) hh = mx == r ? fmodf((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    hh *= 60;
    *h = hh < 0 ? hh + 360 : hh;
}
static float lin(int v) { float c = v / 255.f; return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float lum(uint32_t c) { return 0.2126f * lin((c >> 16) & 255) + 0.7152f * lin((c >> 8) & 255) + 0.0722f * lin(c & 255); }
static float contrast(uint32_t a, uint32_t b) { float x = lum(a), y = lum(b); if (x < y) { float t = x; x = y; y = t; } return (x + 0.05f) / (y + 0.05f); }

static void test_theme_on(void)
{
    for (int i = 0; i < pc_theme_count(); i++) {
        const pc_theme_t *t = pc_theme_at(i);
        float h1, s1, h2, s2;
        rgb_hsv(t->a1, &h1, &s1);
        rgb_hsv(t->on, &h2, &s2);
        float dh = fabsf(h1 - h2);
        if (dh > 180) dh = 360 - dh;
        CHECK(t->on != 0);
        CHECK(dh >= 60.f);
        CHECK(s2 >= 0.25f);
        CHECK(contrast(t->on, t->panel) >= 3.0f);
    }
    CHECK_EQ_INT((int)pc_theme_find("neon")->on, 0xff28a0);
    CHECK_EQ_INT((int)pc_theme_find("amber")->on, 0x3ddc84);
    CHECK_EQ_INT((int)pc_theme_find("phosphor")->on, 0xffb000);
    CHECK_EQ_INT((int)pc_theme_find("eva")->on, 0xff52a0);
    CHECK_EQ_INT((int)pc_theme_find("hiby")->on, 0x3ddc84);
}

static void test_ramps(void)
{
    const pc_theme_t *t = pc_theme_find("neon");
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, t, 0), 0x400000);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, t, 0.4f), 0xc41000);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, t, 1), 0xffe260);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, t, 7), 0xffe260);          /* clamped */
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, t, NAN), 0x400000);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_ICE, t, 0.45f), 0x006edc);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_WINAMP, t, 0), 0x18b028);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_WINAMP, t, 1), 0xff281e);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_RAINBOW, t, 1), 0xff2626);      /* hue 0, s .85, v 1 */
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_MONO, t, 0.3f), (int)t->a1);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_THEME, t, 0), (int)t->panel2);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_THEME, t, 1), (int)t->a1);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_DUO, t, 0), 0xff26b7);          /* hue 320, rgxx viz_hsv rounding */
    CHECK_EQ_INT(pc_ramp_parse("ice"), PC_RAMP_ICE);
    CHECK_EQ_INT(pc_ramp_parse("bogus"), PC_RAMP_THEME);
    CHECK_EQ_INT(pc_ramp_parse(NULL), PC_RAMP_THEME);
    CHECK_STR(pc_ramp_name(PC_RAMP_WINAMP), "winamp");
}

/* light themes (chipchill colour.rs light ramps): pale quiet end, ink loud end, never brighter than the panel; the dark themes and the LED ring keep the dark ramps */
static void test_light_ramps(void)
{
    const pc_theme_t gb = { "gb", "gb", 0xc4cfa1, 0xb4c08c, 0xa8b480, 0x0f380f, 0x306230, 0x306230, 0x8bac0f, 0x0f380f, 0x8bac0f };
    const pc_theme_t *neon = pc_theme_find("neon");
    CHECK(!pc_theme_is_light(neon));
    CHECK(pc_theme_is_light(&gb));
    const pc_theme_t px = { "px", "px", 0xfafcf8, 0xfffffe, 0xecf7f6, 0x1e282c, 0x46565a, 0x1e968e, 0xd6447f, 0xb07a00, 0xd6447f };
    CHECK(pc_theme_is_light(&px));
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, &px, 0), 0xffecc8);        /* FIRE_LIGHT stops */
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, &px, 1), 0x5a0000);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_ICE, &px, 0), 0xe6f5ff);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_WINAMP, &px, 1), 0x96000a);
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_FIRE, neon, 1), 0xffe260);       /* dark: unchanged */
    CHECK_EQ_INT((int)pc_ramp_color_ex(PC_RAMP_FIRE, &px, 1, false), 0xffe260);   /* the LED ring asks for the dark ramp */
    CHECK_EQ_INT((int)pc_ramp_color(PC_RAMP_THEME, &px, 0), (int)px.panel2);   /* the theme's own tokens: untouched */
    for (int r = PC_RAMP_FIRE; r <= PC_RAMP_DUO; r++) {                       /* rainbow / duo: luminance 225 quiet -> 35 loud */
        uint32_t q = pc_ramp_color((pc_ramp_t)r, &px, 0), l = pc_ramp_color((pc_ramp_t)r, &px, 1);
        int lq = (int)(((q >> 16) * 299 + ((q >> 8) & 255) * 587 + (q & 255) * 114) / 1000), ll = (int)(((l >> 16) * 299 + ((l >> 8) & 255) * 587 + (l & 255) * 114) / 1000);
        CHECK(lq > ll + 100);
    }
    for (int r = PC_RAMP_FIRE; r <= PC_RAMP_DUO; r++)                         /* a tinted panel: no step brighter than it */
        for (int i = 0; i <= 20; i++) {
            uint32_t c = pc_ramp_color((pc_ramp_t)r, &gb, i / 20.0f);
            int lc = (int)(((c >> 16) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000);
            CHECK(lc <= (int)((0xb4 * 299 + 0xc0 * 587 + 0x8c * 114) / 1000));
        }
}

static void test_should_draw(void)
{
    CHECK(pc_viz_should_draw(false, 10000, 0, false, 0));
    CHECK(!pc_viz_should_draw(true, 10000, 0, false, 0));                 /* thumbnail decoding */
    CHECK(!pc_viz_should_draw(false, 10000, 9000, false, 0));             /* just switched */
    CHECK(pc_viz_should_draw(false, 10000 + 1, 10000 - PC_VIZ_SWITCH_PAUSE_MS, false, 0));
    int n = 0;
    for (uint32_t t = 0; t < 30; t++) n += pc_viz_should_draw(false, 10000, 0, true, t);
    CHECK_EQ_INT(n, 20);                                                    /* battery: 10 of 15 */
}

static void test_follow(void)
{
    float in[2] = { 1.0f, 0.0f }, lv[2] = { 0 }, pk[2] = { 0 }, hd[2] = { 0 };
    pc_viz_follow(in, 2, 0.033f, lv, pk, hd);
    CHECK_NEAR(lv[0], 1.0f, 1e-6); CHECK_NEAR(pk[0], 1.0f, 1e-6); CHECK_NEAR(lv[1], 0.0f, 1e-6);   /* rise is instant */
    in[0] = 0;
    pc_viz_follow(in, 2, 0.1f, lv, pk, hd);
    CHECK_NEAR(lv[0], 0.85f, 1e-4); CHECK_NEAR(pk[0], 1.0f, 1e-6);                                   /* 1.5 per second; cap holds */
    pc_viz_follow(in, 2, 0.2f, lv, pk, hd);
    CHECK_NEAR(pk[0], 1.0f, 1e-6);                                                                     /* hold has 0.05 left */
    pc_viz_follow(in, 2, 0.1f, lv, pk, hd);
    CHECK_NEAR(pk[0], 0.94f, 1e-3);                                                                    /* hold used up: 0.6 per second */
    for (int i = 0; i < 40; i++) pc_viz_follow(in, 2, 0.1f, lv, pk, hd);
    CHECK_NEAR(lv[0], 0.0f, 1e-6); CHECK_NEAR(pk[0], 0.0f, 1e-6);                                    /* never negative */
    float bad[2] = { NAN, 7.0f };
    pc_viz_follow(bad, 2, 99.0f, lv, pk, hd);
    CHECK_NEAR(lv[0], 0.0f, 1e-6); CHECK_NEAR(lv[1], 1.0f, 1e-6);                                    /* NaN -> 0, clamp to 1, dt clamped */
    float l2[2] = { 0.3f, 0.4f }, p2[2] = { 0.5f, 0.6f }, h2[2] = { 0.1f, 0.2f };
    pc_viz_follow(in, 0, 0.1f, l2, p2, h2);                                                            /* n = 0 touches nothing */
    CHECK(l2[0] == 0.3f && l2[1] == 0.4f && p2[0] == 0.5f && p2[1] == 0.6f && h2[0] == 0.1f && h2[1] == 0.2f);
}

int main(void)
{
    RUN(test_follow);
    RUN(test_should_draw);
    RUN(test_themes);
    RUN(test_theme_on);
    RUN(test_ramps);
    RUN(test_light_ramps);
    return 0;
}
