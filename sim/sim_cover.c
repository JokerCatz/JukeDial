// SPDX-License-Identifier: LGPL-2.1-or-later
/* Test-pattern covers for the fake thumb_svc and album_svc: a diagonal gradient between two colours taken from a hash of the
 * key (album name), with a disc and a ring on it. Integer arithmetic only, so every host draws the same pixels. */
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "sim.h"

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* hue 0..359, saturation 0..255, value 0..255 -> r,g,b */
static void hsv(int h, int s, int v, int *r, int *g, int *b)
{
    int region = h / 60, rem = (h % 60) * 255 / 60;
    int p = v * (255 - s) / 255, q = v * (255 - s * rem / 255) / 255, t = v * (255 - s * (255 - rem) / 255) / 255;
    switch (region) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

static uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static int tri(int x, int period) { int m = ((x % period) + period) % period; return m < period / 2 ? m : period - m; }   /* 0..period/2 */
static int mixc(int a, int b, int t, int den) { return a + (b - a) * t / den; }

/* SIM_GUIDE=1 (the guide's screenshots, tools/simtools/guide_shots.py): album art that looks like album art, still integer-only and a function of the key alone. */
static void guide_draw(uint32_t h, int size, uint16_t *px)
{
    int h1 = (int)(h % 360), h2 = (h1 + 50 + (int)((h >> 9) % 90)) % 360, style = (int)((h >> 5) % 4);
    int r1, g1, b1, r2, g2, b2, r3, g3, b3;
    hsv(h1, 190, 245, &r1, &g1, &b1); hsv(h2, 210, 110, &r2, &g2, &b2); hsv((h1 + 180) % 360, 120, 255, &r3, &g3, &b3);
    int cx = size * (35 + (int)((h >> 12) % 30)) / 100, cy = size * (30 + (int)((h >> 18) % 25)) / 100;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int t = y * 256 / (size - 1), r = mixc(r1, r2, t, 256), g = mixc(g1, g2, t, 256), b = mixc(b1, b2, t, 256);
        int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
        if (style == 0) {                                          /* sunset: a sun and three ridges */
            int rad = size * 20 / 100;
            if (d2 <= rad * rad) { r = r3; g = g3; b = b3; }
            for (int k = 0; k < 3; k++) {
                int base = size * (62 + k * 12) / 100 - tri(x + k * size / 2, size / 2) / 3;
                if (y > base) { int sh = 70 - k * 18; r = r2 * sh / 100; g = g2 * sh / 100; b = b2 * sh / 100 + 20 * k; }
            }
        } else if (style == 1) {                                   /* orbits: thin rings round a bright dot */
            int d = 0; while ((d + 1) * (d + 1) <= d2) d++;
            r = r2 / 2; g = g2 / 2; b = b2 / 2 + 30;
            if (d % (size / 9) < 3) { r = r1; g = g1; b = b1; }
            if (d2 <= (size / 14) * (size / 14)) { r = r3; g = g3; b = b3; }
        } else if (style == 2) {                                   /* waves: diagonal gradient with wave bands */
            int w = tri(x * 2 + y / 2 + (int)(h % 40), size / 4);
            int band = (y + w * 2) / (size / 8);
            if (band & 1) { r = mixc(r, r3, 1, 3); g = mixc(g, g3, 1, 3); b = mixc(b, b3, 1, 3); } else { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }
        } else {                                                   /* geometry: two overlapping discs */
            int rad = size * 30 / 100, ex = x - (size - cx), ey = y - (size - cy), e2 = ex * ex + ey * ey;
            int in1 = d2 <= rad * rad, in2 = e2 <= rad * rad;
            if (in1 && in2) { r = r3; g = g3; b = b3; } else if (in1) { r = r1; g = g1; b = b1; } else if (in2) { r = (r1 + r2) / 2 + 40; g = g2 + 40; b = b2 + 90; }
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        }
        px[(size_t)(size - 1 - x) * size + (size - 1 - y)] = rgb565(r, g, b);   /* the thumbnail is shown rotated */
    }
}

void sim_cover_draw(const char *key, int size, uint16_t *px)
{
    const char *tone = getenv("SIM_COVER_TONE");
    if (tone && tone[0] == 'd') {                                  /* "detail": a photo-like picture with 1 px structure at EVERY size (fine grid, thin rings, a checker, diagonal hatching): a sharp decode shows it, a stretched smaller one blurs it (the now page's sharpness shots) */
        uint32_t h = fnv1a(key ? key : "");
        int h1 = (int)(h % 360), r1, g1, b1, r2, g2, b2;
        hsv(h1, 160, 235, &r1, &g1, &b1); hsv((h1 + 150) % 360, 180, 90, &r2, &g2, &b2);
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
            int t = (x + y) * 256 / (2 * size - 2), r = mixc(r1, r2, t, 256), g = mixc(g1, g2, t, 256), b = mixc(b1, b2, t, 256);
            int dx = x - size / 2, dy = y - size / 2, d2 = dx * dx + dy * dy, d = 0;
            while ((d + 1) * (d + 1) <= d2) d++;
            if (x % 16 == 0 || y % 16 == 0) { r = 255 - r / 4; g = 255 - g / 4; b = 255 - b / 4; }          /* 1 px grid */
            if (d % 12 == 0 && d < size / 3) { r = 255; g = 255; b = 255; }                                 /* 1 px rings */
            if (x > size * 5 / 8 && y > size * 5 / 8 && x < size * 7 / 8 && y < size * 7 / 8) { int c = ((x / 2 + y / 2) & 1) ? 255 : 0; r = g = b = c; }   /* 2 px checker */
            if (x < size / 4 && y < size / 4 && (x + y) % 5 == 0) { r = 0; g = 0; b = 0; }                  /* hatching */
            px[(size_t)y * size + x] = rgb565(r, g, b);
        }
        return;
    }                   /* "bright" / "dark": a plain pale or near-black picture with a few stripes (the now page's readability shots; unset: the test covers below) */
    if (tone) {
        const bool bright = tone[0] == 'b';
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
            int v = bright ? 238 + (x + y) % 17 : 8 + (x + y) % 17;
            if (((x - y + 4 * size) / (size / 6)) % 3 == 0) v = bright ? 205 : 40;       /* diagonal bands: some structure under the text */
            px[(size_t)y * size + x] = rgb565(v, bright ? v - 6 : v, bright ? v - 14 : v + 6);
        }
        return;
    }
    if (getenv("SIM_GUIDE")) { guide_draw(fnv1a(key ? key : ""), size, px); return; }
    uint32_t h = fnv1a(key ? key : "");
    int h1 = (int)(h % 360), h2 = (h1 + 80 + (int)((h >> 9) % 140)) % 360;
    int r1, g1, b1, r2, g2, b2;
    hsv(h1, 200, 230, &r1, &g1, &b1);
    hsv(h2, 220, 120, &r2, &g2, &b2);
    int cx = size / 2 + (int)((h >> 16) % 41) * size / 360 - 20 * size / 360, cy = size / 2 + (int)((h >> 20) % 41) * size / 360 - 20 * size / 360;
    int rad = size * 28 / 100, ring = rad + size / 24;
    int dr = (r1 + 255) / 2, dg = (g2 + 255) / 2, db = (b1 + b2 + 255) / 3;   /* the disc: a pale mix of both colours */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int t = (x + y) * 256 / (2 * size - 2);                           /* 0..256 along the diagonal */
            int r = r1 + (r2 - r1) * t / 256, g = g1 + (g2 - g1) * t / 256, b = b1 + (b2 - b1) * t / 256;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 <= rad * rad) { r = dr; g = dg; b = db; }
            else if (d2 <= ring * ring) { r = r / 2; g = g / 2; b = b / 2; }
            px[(size_t)y * size + x] = rgb565(r, g, b);
        }
    }
}
