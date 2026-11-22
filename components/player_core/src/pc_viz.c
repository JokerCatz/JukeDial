// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_viz.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static const pc_theme_t s_themes[] = {
#include "pc_theme_table.inc"
};
#define NTHEMES ((int)(sizeof s_themes / sizeof s_themes[0]))

int pc_theme_count(void) { return NTHEMES; }
const pc_theme_t *pc_theme_at(int i) { return i >= 0 && i < NTHEMES ? &s_themes[i] : NULL; }
const pc_theme_t *pc_theme_find(const char *name)
{
    for (int i = 0; name && i < NTHEMES; i++)
        if (!strcmp(s_themes[i].name, name)) return &s_themes[i];
    return &s_themes[0];
}

/* ramps and HSV as rgxx src/player/viz_color.c, so the colours match the other players */
static const char *const s_ramp_names[PC_RAMP_COUNT] = { "theme", "fire", "ice", "winamp", "rainbow", "duo", "mono" };
const char *pc_ramp_name(pc_ramp_t r) { return r >= 0 && r < PC_RAMP_COUNT ? s_ramp_names[r] : "theme"; }
pc_ramp_t pc_ramp_parse(const char *name)
{
    for (int i = 0; name && i < PC_RAMP_COUNT; i++)
        if (!strcmp(s_ramp_names[i], name)) return (pc_ramp_t)i;
    return PC_RAMP_THEME;
}

static uint32_t pack(int r, int g, int b) { return (uint32_t)(r << 16 | g << 8 | b); }
static uint32_t mix(uint32_t a, uint32_t b, int t)
{
    int ar = (int)(a >> 16) & 255, ag = (int)(a >> 8) & 255, ab = (int)a & 255;
    int br = (int)(b >> 16) & 255, bg = (int)(b >> 8) & 255, bb = (int)b & 255;
    return pack((ar * (255 - t) + br * t) / 255, (ag * (255 - t) + bg * t) / 255, (ab * (255 - t) + bb * t) / 255);
}
static uint32_t hsv(int h, float s, float v)
{
    h = ((h % 360) + 360) % 360;
    float c = v * s, x = c * (1 - fabsf(fmodf((float)h / 60.0f, 2.0f) - 1)), m = v - c, r = 0, g = 0, b = 0;
    switch (h / 60) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    return pack((int)lroundf((r + m) * 255), (int)lroundf((g + m) * 255), (int)lroundf((b + m) * 255));
}
struct stop { float at; unsigned char r, g, b; };
static const struct stop fire[] = { { 0, 64, 0, 0 }, { 0.4f, 196, 16, 0 }, { 0.75f, 255, 116, 0 }, { 1, 255, 226, 96 } };
static const struct stop ice[] = { { 0, 0, 16, 72 }, { 0.45f, 0, 110, 220 }, { 0.8f, 90, 230, 255 }, { 1, 235, 255, 255 } };
static const struct stop winamp[] = { { 0, 24, 176, 40 }, { 0.55f, 230, 220, 30 }, { 0.8f, 255, 140, 0 }, { 1, 255, 40, 30 } };
static uint32_t through(const struct stop *s, int n, float f)
{
    for (int i = 0; i + 1 < n; i++)
        if (f <= s[i + 1].at) {
            float t = (f - s[i].at) / (s[i + 1].at - s[i].at);
            return pack((int)(s[i].r + (s[i + 1].r - s[i].r) * t), (int)(s[i].g + (s[i + 1].g - s[i].g) * t),
                        (int)(s[i].b + (s[i + 1].b - s[i].b) * t));
        }
    return pack(s[n - 1].r, s[n - 1].g, s[n - 1].b);
}

/* Light themes (the firmware's rule: background luminance >= 128, as ui_theme_is_light). Ported from chipchill-viz colour.rs (2026-10-09, "the
 * waterfall washed out on paper"): on paper the ink is dark, so the loud end is the dark end and the quiet end nearly the paper. */
static int lum(uint32_t c) { return ((int)((c >> 16) & 255) * 299 + (int)((c >> 8) & 255) * 587 + (int)(c & 255) * 114) / 1000; }
bool pc_theme_is_light(const pc_theme_t *t) { return t && lum(t->bg) >= 128; }

static const struct stop fire_light[] = { { 0, 255, 236, 200 }, { 0.4f, 255, 170, 40 }, { 0.75f, 210, 40, 0 }, { 1, 90, 0, 0 } };
static const struct stop ice_light[] = { { 0, 230, 245, 255 }, { 0.45f, 90, 180, 255 }, { 0.8f, 0, 90, 200 }, { 1, 0, 20, 80 } };
static const struct stop winamp_light[] = { { 0, 170, 230, 130 }, { 0.55f, 190, 170, 0 }, { 0.8f, 220, 100, 0 }, { 1, 150, 0, 10 } };

/* c with its brightness forced to `target`: brighter than that -> scaled down (hue kept), darker -> mixed toward white */
static uint32_t fit_lum(uint32_t c, int target)
{
    int l = lum(c);
    if (target < 0) target = 0;
    if (target > 255) target = 255;
    if (l == target) return c;
    if (l > target) {
        int r = (int)((c >> 16) & 255) * target / l, g = (int)((c >> 8) & 255) * target / l, b = (int)(c & 255) * target / l;
        return pack(r, g, b);
    }
    return mix(c, 0xffffff, (target - l) * 255 / (255 - l));
}
static int paper_lum(float f) { return (int)(225.0f - 190.0f * f); }   /* pale 225 when quiet -> ink 35 when loud */

static uint32_t ramp_dark(pc_ramp_t r, const pc_theme_t *t, float f)
{
    switch (r) {
    case PC_RAMP_FIRE: return through(fire, 4, f);
    case PC_RAMP_ICE: return through(ice, 4, f);
    case PC_RAMP_WINAMP: return through(winamp, 4, f);
    case PC_RAMP_RAINBOW: return hsv((int)(280 - 280 * f), 0.85f, 1.0f);
    case PC_RAMP_DUO: return mix(hsv(320, 0.85f, 1.0f), hsv(50, 0.85f, 1.0f), (int)(f * 255));   /* rgxx default hues */
    case PC_RAMP_MONO: return t->a1;
    default: return mix(t->panel2, t->a1, (int)(f * 255));
    }
}

uint32_t pc_ramp_color_ex(pc_ramp_t r, const pc_theme_t *t, float f, bool light)
{
    if (!(f > 0)) f = 0;
    if (f > 1) f = 1;
    if (!light) return ramp_dark(r, t, f);
    uint32_t c;
    switch (r) {
    case PC_RAMP_FIRE: c = through(fire_light, 4, f); break;
    case PC_RAMP_ICE: c = through(ice_light, 4, f); break;
    case PC_RAMP_WINAMP: c = through(winamp_light, 4, f); break;
    case PC_RAMP_RAINBOW:
    case PC_RAMP_DUO: c = fit_lum(ramp_dark(r, t, f), paper_lum(f)); break;
    default: return ramp_dark(r, t, f);              /* the theme's own tokens (and mono) already follow the theme */
    }
    const int pl = lum(t->panel);                    /* a tinted (not white) panel: the whole ramp is scaled so no step is brighter than it */
    if (pl < 245) c = fit_lum(c, lum(c) * pl / 245);   /* 245 > the palest step of any ramp (ice, 241) */
    return c;
}

uint32_t pc_ramp_color(pc_ramp_t r, const pc_theme_t *t, float f) { return pc_ramp_color_ex(r, t, f, pc_theme_is_light(t)); }

bool pc_viz_should_draw(bool thumb_busy, int64_t now_ms, int64_t changed_ms, bool battery, uint32_t tick)
{
    if (thumb_busy) return false;                               /* the JPEG decode needs core 0 */
    if (now_ms - changed_ms < PC_VIZ_SWITCH_PAUSE_MS) return false;
    return !battery || tick % 3 != 2;
}

void pc_viz_follow(const float *in, int n, float dt, float *level, float *peak, float *hold)
{
    if (!(dt > 0.0f)) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;
    for (int i = 0; i < n; i++) {
        float v = in[i] > 0.0f ? (in[i] < 1.0f ? in[i] : 1.0f) : 0.0f;
        float l = level[i] - PC_VIZ_FALL_PER_S * dt;
        if (v > l) l = v;
        if (l < 0.0f) l = 0.0f;
        level[i] = l;
        if (l >= peak[i]) {
            peak[i] = l;
            hold[i] = PC_VIZ_PEAK_HOLD_S;
        } else {
            hold[i] -= dt;
            if (hold[i] < 0.0f) {
                peak[i] -= PC_VIZ_PEAK_FALL_PER_S * dt;
                if (peak[i] < 0.0f) peak[i] = 0.0f;
            }
        }
    }
}
