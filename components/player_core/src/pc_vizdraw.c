// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_vizdraw.h"
#include "pc/pc_i18n.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_voice.h"

/* ---- names, sizes ---- */
static const char *const KIND[PC_VD_KINDS] = { "bars", "ring", "scope", "fall", "phase", "voices", "rows", "roll", "lyrics" };
static const int NVAR[PC_VD_KINDS] = { 4, 4, 3, 3, 2, 3, 1, 2, 2 };         /* every build */
static const int NVAR_ROUND[PC_VD_KINDS] = { 6, 4, 3, 3, 2, 3, 1, 2, 2 };   /* a round screen: bars Lens and Arch on top */
static const char *const LAB_ZH[PC_VD_KINDS][6] = {
    { "實心", "分段", "鏡射", "細線", "圓鏡", "拱形" }, { "向外", "向內", "對稱", "點", "", "" }, { "線", "填滿", "圓形", "", "", "" },
    { "往下", "往上", "對稱", "", "", "" }, { "點", "連線", "", "", "", "" }, { "長條", "分段", "環形", "", "", "" },
    { "列表", "", "", "", "", "" }, { "彩色", "音高", "", "", "", "" }, { "三行", "五行", "", "", "", "" } };
static const char *const LAB_EN[PC_VD_KINDS][6] = {
    { "Solid", "Segments", "Mirror", "Line", "Lens", "Arch" }, { "Out", "In", "Mirror", "Dots", "", "" }, { "Line", "Filled", "Circle", "", "", "" },
    { "Down", "Up", "Mirror", "", "", "" }, { "Dots", "Lines", "", "", "", "" }, { "Bars", "Segments", "Ring", "", "", "" },
    { "List", "", "", "", "", "" }, { "Colour", "Pitch", "", "", "", "" }, { "3 lines", "5 lines", "", "", "", "" } };

static const char *const MAP[PC_VD_MAPS] = { "height", "tip", "band", "level" };
static const char *const MAP_ZH[PC_VD_MAPS] = { "高度", "火苗", "頻段", "音量" };
static const char *const MAP_EN[PC_VD_MAPS] = { "Height", "Tip", "Band", "Level" };

const char *pc_vd_kind_name(pc_vd_kind_t k) { return (unsigned)k < PC_VD_KINDS ? KIND[k] : KIND[0]; }
pc_vd_kind_t pc_vd_kind_parse(const char *name)
{
    if (name) for (int i = 0; i < PC_VD_KINDS; i++) if (!strcmp(name, KIND[i])) return (pc_vd_kind_t)i;
    return PC_VD_BARS;
}
const char *pc_vd_map_name(pc_vd_map_t m) { return (unsigned)m < PC_VD_MAPS ? MAP[m] : MAP[0]; }
pc_vd_map_t pc_vd_map_parse(const char *name)
{
    if (name) for (int i = 0; i < PC_VD_MAPS; i++) if (!strcmp(name, MAP[i])) return (pc_vd_map_t)i;
    return PC_VD_MAP_HEIGHT;
}
const char *pc_vd_map_label(pc_vd_map_t m, bool en) { if ((unsigned)m >= PC_VD_MAPS) m = PC_VD_MAP_HEIGHT; return en ? MAP_EN[m] : TZ(MAP_ZH[m]); }
static const char *const ORD[PC_VD_ORDS] = { "seq", "zigzag", "spread" };
static const char *const ORD_ZH[PC_VD_ORDS] = { "順序", "交錯", "打散" };
static const char *const ORD_EN[PC_VD_ORDS] = { "Sequential", "Alternate", "Scatter" };
const char *pc_vd_order_name(pc_vd_order_t o) { return (unsigned)o < PC_VD_ORDS ? ORD[o] : ORD[0]; }
pc_vd_order_t pc_vd_order_parse(const char *name)
{
    if (name) for (int i = 0; i < PC_VD_ORDS; i++) if (!strcmp(name, ORD[i])) return (pc_vd_order_t)i;
    return PC_VD_ORD_SEQ;
}
const char *pc_vd_order_label(pc_vd_order_t o, bool en) { if ((unsigned)o >= PC_VD_ORDS) o = PC_VD_ORD_SEQ; return en ? ORD_EN[o] : TZ(ORD_ZH[o]); }
void pc_vd_set_order(pc_vd_t *d, pc_vd_order_t o)
{
    if (!d) return;
    if ((unsigned)o >= PC_VD_ORDS) o = PC_VD_ORD_SEQ;
    if (d->order != (uint8_t)o) { d->order = (uint8_t)o; d->fresh = true; }
}
/* s and its inverse mod n: 39 * 23 = 897 = 14 * 64 + 1; 19 * 27 = 513 = 16 * 32 + 1 */
int pc_vd_order_pos(pc_vd_order_t o, int band, int n)
{
    if ((n != 64 && n != 32) || band < 0 || band >= n) return band;
    if (o == PC_VD_ORD_ZIGZAG) return band % 2 == 0 ? band / 2 : n - 1 - (band - 1) / 2;
    if (o == PC_VD_ORD_SPREAD) return band * (n == 64 ? 39 : 19) % n;
    return band;
}
int pc_vd_order_band(pc_vd_order_t o, int pos, int n)
{
    if ((n != 64 && n != 32) || pos < 0 || pos >= n) return pos;
    if (o == PC_VD_ORD_ZIGZAG) return pos < n / 2 ? 2 * pos : 2 * (n - 1 - pos) + 1;
    if (o == PC_VD_ORD_SPREAD) return pos * (n == 64 ? 23 : 27) % n;
    return pos;
}
bool pc_vd_kind_has_map(pc_vd_kind_t k) { return k == PC_VD_BARS || k == PC_VD_RING || k == PC_VD_VOICES; }
int pc_vd_variants_on(pc_vd_kind_t k, bool round) { return (unsigned)k < PC_VD_KINDS ? (round ? NVAR_ROUND[k] : NVAR[k]) : 1; }
int pc_vd_variants(pc_vd_kind_t k) { return pc_vd_variants_on(k, PC_VD_ROUND_SCREEN); }
const char *pc_vd_var_label(pc_vd_kind_t k, int var, bool en)
{
    if ((unsigned)k >= PC_VD_KINDS) k = PC_VD_BARS;
    if (var < 0 || var >= pc_vd_variants(k)) var = 0;
    return en ? LAB_EN[k][var] : TZ(LAB_ZH[k][var]);
}
void pc_vd_size(pc_vd_kind_t k, int var, int *w, int *h)
{
    int a = 0, b = 0;
    switch (k) {
    case PC_VD_BARS: a = var >= 4 ? PC_VD_RING_SZ : 300; b = var >= 4 ? PC_VD_RING_SZ : 200; break;   /* Lens and Arch use the round picture */
    case PC_VD_RING: a = b = PC_VD_RING_SZ; break;
    case PC_VD_SCOPE: a = var == 2 ? PC_VD_CIRCLE_SZ : 320; b = var == 2 ? PC_VD_CIRCLE_SZ : 160; break;
    case PC_VD_FALL: a = 256; b = 200; break;
    case PC_VD_PHASE: a = b = PC_VD_PHASE_SZ; break;
    case PC_VD_VOICES: a = var == 2 ? PC_VD_RING_SZ : 300; b = var == 2 ? PC_VD_RING_SZ : 200; break;     /* variants 0 and 1 share the bars picture, 2 the ring picture */
    case PC_VD_ROWS: case PC_VD_ROLL: a = 300; b = 200; break;
    case PC_VD_LYRICS: break;                                                                              /* no picture: LVGL labels */
    default: break;
    }
    if (w) *w = a;
    if (h) *h = b;
}

/* ---- frame ---- */
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)); }

void pc_vd_set_colors(pc_vd_t *d, pc_ramp_t ramp, const pc_theme_t *t)
{
    if (!d) return;
    if (!t) t = pc_theme_find(NULL);
    for (int i = 0; i < 256; i++) d->lut[i] = rgb565(pc_ramp_color(ramp, t, (float)i / 255.0f));
    d->bg = rgb565(t->bg); d->fg = rgb565(t->fg); d->dim = rgb565(t->dim);
    d->quiet = rgb565(t->bg);                  /* the page background on every theme (a light theme: the panel colour showed as a white box on the near-white page) */
    d->fresh = true;
}

void pc_vd_set_map(pc_vd_t *d, pc_vd_map_t m)
{
    if (!d) return;
    if ((unsigned)m >= PC_VD_MAPS) m = PC_VD_MAP_HEIGHT;
    if (d->map != (uint8_t)m) { d->map = (uint8_t)m; d->fresh = true; }
}

void pc_vd_set_peaks(pc_vd_t *d, bool on)
{
    if (!d) return;
    if (d->peaks_off == (uint8_t)on) { d->peaks_off = (uint8_t)!on; d->fresh = true; }
}

static void fill_rect(pc_vd_t *d, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++) {
        uint16_t *p = d->px + (size_t)j * d->w + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

void pc_vd_init(pc_vd_t *d, uint16_t *px, int w, int h)
{
    memset(d, 0, sizeof *d);
    if (!px || w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > PC_VD_MAX_PX) { w = h = 0; px = NULL; }
    d->px = px; d->w = w; d->h = h;
    pc_vd_set_colors(d, PC_RAMP_THEME, NULL);
    if (px) fill_rect(d, 0, 0, w, h, d->bg);
}

void pc_vd_blank(pc_vd_t *d, pc_vd_kind_t k)
{
    (void)k;                                            /* every kind starts as the page background; the kind stays in the signature for the callers */
    if (d && d->px && d->w > 0) fill_rect(d, 0, 0, d->w, d->h, d->bg);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* Ramp colour of row k (0 at the foot) of bar i of nb, bar height h, full length scale. point: the drawn run is itself the bar top
 * (thin line, ring dots): TIP is the hottest colour and LEVEL follows the position like HEIGHT. */
static uint16_t map_col(const pc_vd_t *d, int k, int h, int scale, int i, int nb, bool point)
{
    int idx;
    switch (d->map) {
    case PC_VD_MAP_TIP: {
        if (point) { idx = 255; break; }
        const int tip = imax(scale * 42 / 100, 1), dist = h - 1 - k;
        idx = 255 - imin(dist * 255 / tip, 255);
        break; }
    case PC_VD_MAP_BAND: idx = nb > 1 ? i * 255 / (nb - 1) : 0; break;
    case PC_VD_MAP_LEVEL:
        if (!point) { idx = imin(h * 255 / imax(scale, 1), 255); break; }
        /* fall through */
    default: idx = k * 255 / imax(scale - 1, 1); break;
    }
    return d->lut[idx];
}

static float sane(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }           /* NaN -> 0 */
static int rnd(float v, int n) { return (int)(sane(v) * (float)n + 0.5f); }

static void rect_add(pc_vd_rect_t *r, int x0, int y0, int x1, int y1)   /* [x0,x1) x [y0,y1) */
{
    if (!r->w) { r->x = x0; r->y = y0; r->w = x1 - x0; r->h = y1 - y0; return; }
    int ex = imax(r->x + r->w, x1), ey = imax(r->y + r->h, y1);
    r->x = imin(r->x, x0); r->y = imin(r->y, y0); r->w = ex - r->x; r->h = ey - r->y;
}

/* returns true when the whole image has to be redrawn (and clears it) */
static bool begin(pc_vd_t *d, int tag, int extra)
{
    bool full = d->fresh || d->tag != tag || d->extra != extra;
    if (full) {
        fill_rect(d, 0, 0, d->w, d->h, d->bg);
        d->tag = (int16_t)tag; d->extra = (int16_t)extra;
        d->fresh = false;
    }
    return full;
}

/* ---- bars ---- */
static float seg_max(const float *a, int i, int nb)
{
    float m = 0;
    for (int s = i * 64 / nb; s < (i + 1) * 64 / nb; s++) { float v = sane(a[s]); if (v > m) m = v; }
    return m;
}

/* Lens (var 4) and Arch (var 5) on the PC_VD_RING_SZ picture. Up to RB_COLS columns; the full length of each is in d->lim (computed when the style starts,
 * so no square root per frame). The glass-radius bound (Lens at most 170.2 px, Arch 173.4 px from the picture centre, tested) holds for bar widths 5, 7 and 9, the only ones the settings produce;
 * a wider bar takes its length from its centre column and may reach further (never outside the buffer). One pass per column: the colour of a row once (a single colour for the whole run with BAND and LEVEL). */
#define RB_COLS 48
#define RB_BASE 213                                   /* Arch: the baseline, picture row (screen row 223) */
static pc_vd_rect_t bars_round(pc_vd_t *d, int var, const float *level, const float *peak, int bar_w)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (d->w != PC_VD_RING_SZ || d->h != PC_VD_RING_SZ) return R;
    if (bar_w < 1 || bar_w > 156) bar_w = 7;
    const int W = d->w, mid = W / 2, pitch = bar_w + 2, arch = var == 5;
    int nb = imin(RB_COLS, (314 + 2) / pitch);
    if (arch) nb &= ~1;                                /* bass in the middle: nb / 2 bands, twice */
    const int x0 = (W - (nb * bar_w + (nb - 1) * 2)) / 2, nbands = arch ? nb / 2 : nb;
    const bool full = begin(d, 1 + var, bar_w);
    if (full) for (int i = 0; i < nb; i++) {
        const float dx = (float)(x0 + i * pitch) + (float)bar_w * 0.5f - (float)mid;
        const float len = arch ? 0.92f * ((float)RB_BASE - ((float)mid - sqrtf(176.0f * 176.0f - dx * dx)))
                               : 0.92f * sqrtf(170.0f * 170.0f - dx * dx);
        d->lim[i] = (int16_t)imax((int)len, 2);
    }
    int16_t nh[RB_COLS], np[RB_COLS];
    bool any = false;
    for (int i = 0; i < nb; i++) {
        const int sc = d->lim[i];
        if (!full) {                                   /* prev is the renderer's own memory, but garbage there must not reach outside the image */
            d->prev_a[i] = (int16_t)imax(0, imin(d->prev_a[i], sc));
            d->prev_b[i] = (int16_t)imax(0, imin(d->prev_b[i], sc));
        }
        const int b = arch ? (i < nb / 2 ? nb / 2 - 1 - i : i - nb / 2) : i;
        nh[i] = (int16_t)rnd(seg_max(level, b, nbands), sc);
        const int p = rnd(seg_max(peak, b, nbands), sc);
        np[i] = (int16_t)(p < 2 || d->peaks_off ? 0 : p);
        if (full || nh[i] != d->prev_a[i] || np[i] != d->prev_b[i]) any = true;
    }
    if (full) rect_add(&R, 0, 0, W, d->h);
    if (!any) return R;
    const bool flat = d->map == PC_VD_MAP_BAND || d->map == PC_VD_MAP_LEVEL;    /* one colour for the whole bar */
    for (int i = 0; i < nb; i++) {
        if (!full && nh[i] == d->prev_a[i] && np[i] == d->prev_b[i]) continue;
        const int x = x0 + i * pitch, h = nh[i], p = np[i], sc = d->lim[i];
        const int b = arch ? (i < nb / 2 ? nb / 2 - 1 - i : i - nb / 2) : i;
        const int m = imax(imax(h, p), full ? 0 : imax(d->prev_a[i], d->prev_b[i]));
        const int top = arch ? RB_BASE - m : mid - m, bot = arch ? RB_BASE : mid + m;
        if (!full) fill_rect(d, x, top, bar_w, bot - top, d->bg);
        if (h > 0 && flat) {
            const uint16_t c = map_col(d, 0, h, sc, b, nbands, false);
            if (arch) fill_rect(d, x, RB_BASE - h, bar_w, h, c); else fill_rect(d, x, mid - h, bar_w, 2 * h, c);
        } else for (int k = 0; k < h; k++) {
            const uint16_t c = map_col(d, k, h, sc, b, nbands, false);
            if (arch) fill_rect(d, x, RB_BASE - 1 - k, bar_w, 1, c);
            else { fill_rect(d, x, mid - 1 - k, bar_w, 1, c); fill_rect(d, x, mid + k, bar_w, 1, c); }
        }
        if (p) {
            if (arch) fill_rect(d, x, RB_BASE - p, bar_w, 2, d->fg);
            else { fill_rect(d, x, mid - p, bar_w, 2, d->fg); fill_rect(d, x, mid + p - 2, bar_w, 2, d->fg); }
        }
        if (!full) rect_add(&R, x, top, x + bar_w, bot);
        d->prev_a[i] = h; d->prev_b[i] = (int16_t)p;
    }
    if (full) for (int i = nb; i < RB_COLS; i++) d->prev_a[i] = d->prev_b[i] = 0;
    return R;
}

pc_vd_rect_t pc_vd_bars(pc_vd_t *d, int var, const float *level, const float *peak, int bar_w)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (!d || !d->px || !level || !peak || d->w < 8 || d->h < 4) return R;
    if (var < 0 || var > (PC_VD_ROUND_SCREEN ? 5 : 3)) var = 0;
    if (var >= 4) return bars_round(d, var, level, peak, bar_w);
    if (bar_w < 1 || bar_w > d->w) bar_w = 7;
    const int W = d->w, H = d->h, mid = H / 2, scale = var == 2 ? mid : H, pitch = bar_w + 2;
    int nb = imin(32, (W + 2) / pitch);
    const int x0 = (W - (nb * bar_w + (nb - 1) * 2)) / 2;
    const bool full = begin(d, 1 + var, bar_w);
    int16_t nh[32], np[32];
    bool any = false;
    if (!full) for (int i = 0; i < nb; i++) {        /* prev is the renderer's own memory, but garbage there must not reach outside the image */
        d->prev_a[i] = (int16_t)imax(0, imin(d->prev_a[i], scale));
        d->prev_b[i] = (int16_t)imax(0, imin(d->prev_b[i], scale));
    }
    for (int i = 0; i < nb; i++) {
        nh[i] = (int16_t)rnd(seg_max(level, i, nb), scale);
        int p = rnd(seg_max(peak, i, nb), scale);
        np[i] = (int16_t)((p < 2 || var == 3 || d->peaks_off) ? 0 : p);
        if (full || nh[i] != d->prev_a[i] || np[i] != d->prev_b[i]) any = true;
    }
    if (full) rect_add(&R, 0, 0, W, H);
    if (!any) return R;

    if (var == 3) {                                  /* thin line through the bar tops' centres, 2 px thick */
        int a = nb, b = -1;
        for (int i = 0; i < nb; i++)
            if (full || nh[i] != d->prev_a[i]) { if (a == nb) a = i; b = i; }
        if (b < 0) { for (int i = 0; i < nb; i++) { d->prev_a[i] = nh[i]; d->prev_b[i] = np[i]; } return R; }
        const int lo = imax(a - 1, 0), hi = imin(b + 1, nb - 1);
        const int cx0 = x0 + bar_w / 2;
        int hmin = H, hmax = 0;
        for (int i = lo; i <= hi; i++) {
            hmin = imin(hmin, imin(nh[i], full ? nh[i] : d->prev_a[i]));
            hmax = imax(hmax, imax(nh[i], full ? nh[i] : d->prev_a[i]));
        }
        const int ylo = imax(H - hmax - 1, 0), yhi = imin(H - hmin + 2, H);        /* rows [ylo, yhi) */
        const int xl = nb < 2 ? x0 : cx0 + lo * pitch, xr = nb < 2 ? x0 + bar_w - 1 : cx0 + hi * pitch;   /* columns [xl, xr]; one bar: its whole dash */
        if (!full) fill_rect(d, xl, ylo, xr - xl + 1, yhi - ylo, d->bg);
        for (int x = xl; x <= xr; x++) {
            int s = imin((x - cx0) / pitch, nb - 2);
            int ya, yb;
            if (nb < 2) { ya = yb = H - imax(nh[0], 2); }                  /* a single bar: a flat dash */
            else {
                int y0 = H - imax(nh[s], 2), y1 = H - imax(nh[s + 1], 2), f = x - (cx0 + s * pitch);
                ya = y0 + (y1 - y0) * f / pitch;
                yb = x == cx0 + (nb - 1) * pitch ? ya : y0 + (y1 - y0) * (f + 1) / pitch;
            }
            int t = imin(ya, yb), u = imax(ya, yb) + 2;
            for (int y = imax(t, 0); y < imin(u, H); y++)
                d->px[(size_t)y * W + x] = d->map == PC_VD_MAP_BAND
                    ? d->lut[nb < 2 ? 0 : (x - cx0) * 255 / ((nb - 1) * pitch)] : map_col(d, H - 1 - y, 0, H, 0, 1, true);
        }
        if (!full) rect_add(&R, xl, ylo, xr + 1, yhi);
        for (int i = 0; i < nb; i++) { d->prev_a[i] = nh[i]; d->prev_b[i] = 0; }
        return R;
    }

    for (int i = 0; i < nb; i++) {
        if (!full && nh[i] == d->prev_a[i] && np[i] == d->prev_b[i]) continue;
        const int x = x0 + i * pitch, h = nh[i], p = np[i];
        const int m = imax(imax(h, p), full ? 0 : imax(d->prev_a[i], d->prev_b[i]));
        const int top = var == 2 ? mid - m : H - m, bot = var == 2 ? mid + m : H;
        if (!full) fill_rect(d, x, top, bar_w, bot - top, d->bg);
        if (var == 2) {
            for (int k = 0; k < h; k++) {
                uint16_t c = map_col(d, k, h, mid, i, nb, false);
                fill_rect(d, x, mid - 1 - k, bar_w, 1, c);
                fill_rect(d, x, mid + k, bar_w, 1, c);
            }
            if (p) {
                fill_rect(d, x, mid - p, bar_w, 2, d->fg);
                fill_rect(d, x, mid + p - 2, bar_w, 2, d->fg);
            }
        } else {
            for (int k = 0; k < h; k++) {
                if (var == 1 && k % 7 >= 5) continue;
                fill_rect(d, x, H - 1 - k, bar_w, 1, map_col(d, k, h, H, i, nb, false));
            }
            if (p) fill_rect(d, x, H - p, bar_w, 2, d->fg);
        }
        if (!full) rect_add(&R, x, top, x + bar_w, bot);
        d->prev_a[i] = (int16_t)h; d->prev_b[i] = (int16_t)p;
    }
    if (full) for (int i = nb; i < 32; i++) d->prev_a[i] = d->prev_b[i] = 0;
    return R;
}

/* ---- ring ----
 * Map = uint16 words: [0] magic [1] w [2] h [3] L (distance buckets) [4] the gap in px (0, 2, 4) [5..260] bbox[64][x0,y0,x1,y1] (inclusive)
 * [261..390] wstart[65] (first pixel of each wedge, a uint32 as two words, low first: a 340x340 ring has more than 65535 pixels)
 * [391 ..] cum[64][L+1] (pixels of the wedge in buckets < b) then pix[npx] (sorted by wedge, then bucket). A pixel is stored as
 * its offset from the corner of its wedge's bbox, (y - y0) * w + (x - x0), which stays below 65536 (build checks), so it is still
 * one uint16; the draw adds the corner's index y0 * w + x0 back. Bucket b = floor(dist - r_in). */
#define RM_MAGIC 0x5256
#define RM_BB 5
#define RM_WS 261
#define RM_CUM 391

size_t pc_vd_ringmap_bytes(int w, int h)
{
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > PC_VD_MAX_PX) return 0;
    long lmax = imin(w, h) / 2 + 2;
    return (size_t)2 * (RM_CUM + 64 * (lmax + 1) + (size_t)w * h);
}

static uint32_t ring_ws(const uint16_t *m, int wd) { return (uint32_t)m[RM_WS + 2 * wd] | (uint32_t)m[RM_WS + 2 * wd + 1] << 16; }

static int ring_wedge(int x, int y, int w, int h, float r_in, float r_out, int L, int gap_px, int *bucket)
{
    float dx = (float)x - (float)(w - 1) * 0.5f, dy = (float)y - (float)(h - 1) * 0.5f;
    float r = sqrtf(dx * dx + dy * dy);
    if (r < r_in || r > r_out) return -1;
    /* angle from straight down measured on the left (clockwise on screen); the right half is its exact mirror image */
    const float u = atan2f(fabsf(dx), dy) * (64.0f / 6.28318531f);
    int wd = (int)u;
    if (gap_px > 0) {                                  /* the distance to the nearest wedge boundary (a ray from the centre) is r * sin(the angle to it) */
        const float f = u - (float)wd, e = f < 1.0f - f ? f : 1.0f - f;
        if (r * sinf(e * (6.28318531f / 64.0f)) < (float)gap_px * 0.5f) return -1;
    }
    if (dx > 0) wd = 63 - wd;
    if (wd > 63) wd = 63;
    int b = (int)(r - r_in);
    *bucket = b < L ? b : L - 1;
    return wd > 63 ? 63 : wd;
}

int pc_vd_ringmap_gap(const void *map)
{
    const uint16_t *m = map;
    return m && m[0] == RM_MAGIC && (m[4] == 2 || m[4] == 4) ? m[4] : 0;
}

bool pc_vd_ringmap_build(void *map, size_t cap, int w, int h, float r_in, float r_out, int gap_px)
{
    if (gap_px != 2 && gap_px != 4) gap_px = 0;
    size_t need = pc_vd_ringmap_bytes(w, h);
    if (!map || !need || cap < need || ((uintptr_t)map & 1)) return false;
    float rmax = (float)imin(w, h) * 0.5f;
    if (!(r_in >= 0) || !(r_out > r_in)) return false;
    if (r_out > rmax) r_out = rmax;
    if (!(r_out > r_in)) return false;
    const int L = (int)(r_out - r_in) + 1;
    uint16_t *m = map;
    memset(m, 0, need);
    uint16_t *cum = m + RM_CUM;
    const int C = L + 1;
    int bk;
    uint32_t total = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int wd = ring_wedge(x, y, w, h, r_in, r_out, L, gap_px, &bk);
        if (wd < 0) continue;
        cum[wd * C + bk]++; total++;
        uint16_t *bb = m + RM_BB + wd * 4;
        if (bb[2] == 0 && bb[0] == 0 && bb[1] == 0 && bb[3] == 0) { bb[0] = bb[2] = (uint16_t)x; bb[1] = bb[3] = (uint16_t)y; }
        else { if (x < bb[0]) bb[0] = (uint16_t)x; if (x > bb[2]) bb[2] = (uint16_t)x; if (y < bb[1]) bb[1] = (uint16_t)y; if (y > bb[3]) bb[3] = (uint16_t)y; }
    }
    if (total > (uint32_t)w * (uint32_t)h) return false;
    m[0] = RM_MAGIC; m[1] = (uint16_t)w; m[2] = (uint16_t)h; m[3] = (uint16_t)L; m[4] = (uint16_t)gap_px;
    uint16_t *ws = m + RM_WS;
    uint32_t acc = 0;
    for (int wd = 0; wd < 64; wd++) {
        const uint16_t *bb = m + RM_BB + wd * 4;
        if (bb[2] || bb[3] || bb[0] || bb[1]) {            /* an empty wedge has no pixel; a used one must fit the offsets into 16 bits */
            if ((uint32_t)(bb[3] - bb[1]) * (uint32_t)w + (uint32_t)(bb[2] - bb[0]) > 65535u) { m[0] = 0; return false; }
        }
        ws[2 * wd] = (uint16_t)acc; ws[2 * wd + 1] = (uint16_t)(acc >> 16);
        int run = 0;
        for (int b = 0; b < L; b++) { int c = cum[wd * C + b]; cum[wd * C + b] = (uint16_t)run; run += c; }
        acc += (uint32_t)run;
    }
    ws[128] = (uint16_t)acc; ws[129] = (uint16_t)(acc >> 16);
    uint16_t *pix = m + RM_CUM + 64 * C;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int wd = ring_wedge(x, y, w, h, r_in, r_out, L, gap_px, &bk);
        if (wd < 0) continue;
        const uint16_t *bb = m + RM_BB + wd * 4;
        pix[ring_ws(m, wd) + cum[wd * C + bk]++] = (uint16_t)((y - bb[1]) * w + (x - bb[0]));
    }
    for (int wd = 0; wd < 64; wd++) {                 /* cum[b] now holds the end of bucket b: shift to the start */
        uint16_t *c = cum + wd * C;
        for (int b = L; b >= 1; b--) c[b] = c[b - 1];
        c[0] = 0;
    }
    return true;
}

/* grow coordinate g (0 at the start of the bar) -> bucket: out b = g, in b = L-1-g. [g0,g1) painted with colour c, or the ramp (c < 0) */
static void ring_span(pc_vd_t *d, const uint16_t *m, int wd, bool inv, int g0, int g1, int c, int n, int band, int nbands, bool point)
{
    const int L = m[3], C = L + 1;
    const uint16_t *cum = m + RM_CUM + wd * C, *pix = m + RM_CUM + 64 * C + ring_ws(m, wd);
    const uint16_t *bb = m + RM_BB + wd * 4;
    uint16_t *org = d->px + (size_t)bb[1] * d->w + bb[0];    /* the wedge's pixel offsets count from its bbox corner */
    g0 = imax(g0, 0); g1 = imin(g1, L);
    for (int g = g0; g < g1; g++) {
        int b = inv ? L - 1 - g : g;
        uint16_t col = c >= 0 ? (uint16_t)c : map_col(d, g, n, L, band, nbands, point);
        for (int k = cum[b]; k < cum[b + 1]; k++) org[pix[k]] = col;
    }
}

static void ring_extent(int n, int pb, bool dots, int *lo, int *hi)   /* grow range a wedge with length n, peak arc at pb occupies */
{
    int l = 1 << 20;
    if (n > 0) l = dots ? imax(n - 4, 0) : 0;
    if (pb >= 2) l = imin(l, pb - 2);
    *hi = imax(n, pb);
    *lo = *hi ? l : 0;
}

/* The ring's wedge loop. grp (voices): per wedge the voice it belongs to, 0xFF for a gap wedge; the BAND mapping then goes by voice (ngrp of them). */
static pc_vd_rect_t ring_draw(pc_vd_t *d, int var, int tag, int extra, const void *map, const float *level, const float *peak, const uint8_t *grp, int ngrp, pc_vd_order_t ord)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    const uint16_t *m = map;
    if (!d || !d->px || !m || !level || !peak || m[0] != RM_MAGIC || m[1] != d->w || m[2] != d->h) return R;
    const int L = m[3];
    const bool inv = var == 1, dots = var == 3, mir = var == 2;
    const bool full = begin(d, tag, extra);
    if (full) rect_add(&R, 0, 0, d->w, d->h);
    for (int wd = 0; wd < 64; wd++) {
        const int j = mir ? (wd < 32 ? wd : 63 - wd) : wd;          /* mirror: 32 segments (pairs of bands by max), one wedge each side */
        const int bd = pc_vd_order_band(ord, j, mir ? 32 : 64);     /* the band (pair) this position shows */
        float lv = mir ? fmaxf(sane(level[2 * bd]), sane(level[2 * bd + 1])) : level[bd];
        float pv = mir ? fmaxf(sane(peak[2 * bd]), sane(peak[2 * bd + 1])) : peak[bd];
        int n = rnd(lv, L), pb = rnd(pv, L);
        if (pb < 2 || d->peaks_off) pb = 0;
        if (!full && n == d->prev_a[wd] && pb == d->prev_b[wd]) continue;
        int lo, hi, olo, ohi;
        ring_extent(n, pb, dots, &lo, &hi);
        if (full) { olo = lo; ohi = hi; } else ring_extent(d->prev_a[wd], d->prev_b[wd], dots, &olo, &ohi);
        if (!full) ring_span(d, m, wd, inv, imin(lo, olo), imax(hi, ohi), d->bg, 0, 0, 0, false);
        /* the whole bar from the foot is redrawn on every change (the erase above starts at its foot too), so TIP and LEVEL,
         * whose colours all change with the length, need nothing more than HEIGHT and BAND */
        ring_span(d, m, wd, inv, dots ? n - 4 : 0, n, -1, n, grp ? grp[wd] : bd, grp ? ngrp : mir ? 32 : 64, dots);
        if (pb) ring_span(d, m, wd, inv, pb - 2, pb, d->fg, 0, 0, 0, false);
        d->prev_a[wd] = (int16_t)n; d->prev_b[wd] = (int16_t)pb;
        if (!full) { const uint16_t *bb = m + RM_BB + wd * 4; rect_add(&R, bb[0], bb[1], bb[2] + 1, bb[3] + 1); }
    }
    return R;
}

pc_vd_rect_t pc_vd_ring(pc_vd_t *d, int var, const void *map, const float *level, const float *peak)
{
    if (var < 0 || var > 3) var = 0;
    return ring_draw(d, var, 17 + var, 0, map, level, peak, NULL, 0, d ? (pc_vd_order_t)d->order : PC_VD_ORD_SEQ);
}

/* ---- voices ----
 * Variants 0 and 1: n bars on the bars picture (300x200). The bars' region is the top VB_H rows; the voices' names take the 20 rows under it:
 * a 5x7 glyph at 2x (14 rows, at 1x when the widest name does not fit under its bar), starting 4 rows below the bars. A bar is as wide as its slot (W / n)
 * minus 6 px, at most 40; its centre is the centre of its slot. The names are drawn with the first frame of a style only: a count or a name that
 * changes is part of the style's tag (a 15 bit hash of n and the names), so it redraws everything. Variant 2: the ring picture; the 64 wedges are
 * split into n groups, the last wedge of each is a gap. */
#define VB_NAMES 20
/* the 5x7 font: digits, A..Z, then # and -; one byte per row, bit 4 = the left column. 38 x 7 = 266 bytes of flash. */
static const uint8_t FONT57[38][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
    { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }, { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
    { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A },   /* # */
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   /* - */
};
static int font_idx(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? 10 + c - 'A' : c >= 'a' && c <= 'z' ? 10 + c - 'a' : c == '#' ? 36 : c == '-' ? 37 : -1; }
static int name_len(const char *nm) { int l = 0; while (l < 3 && nm[l]) l++; return l; }
static int text_w(int len, int s) { return len > 0 ? len * 5 * s + (len - 1) * s : 0; }

static void draw_name(pc_vd_t *d, int cx, int y0, int s, const char *nm)       /* centred on cx, top row y0; characters without a glyph are blank; clipped to the picture */
{
    const int len = name_len(nm), x0 = cx - text_w(len, s) / 2;
    for (int c = 0; c < len; c++) {
        const int g = font_idx(nm[c]);
        if (g < 0) continue;
        for (int r = 0; r < 7; r++) for (int b = 0; b < 5; b++) if (FONT57[g][r] >> (4 - b) & 1)
            for (int yy = 0; yy < s; yy++) for (int xx = 0; xx < s; xx++) {
                const int x = x0 + c * 6 * s + b * s + xx, y = y0 + r * s + yy;
                if (x >= 0 && x < d->w && y >= 0 && y < d->h) d->px[(size_t)y * d->w + x] = d->fg;
            }
    }
}

pc_vd_rect_t pc_vd_voices(pc_vd_t *d, int var, const void *map, int n, const float *level, const float *peak, const char (*names)[4])
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (!d || !d->px || !level || !peak || n < 1 || n > PC_VOICES_MAX) return R;
    if (var < 0 || var > 2) var = 0;
    uint32_t hs = (uint32_t)n * 2654435761u;                                   /* the style's extra: the count and the names */
    if (names) for (int i = 0; i < n; i++) for (int c = 0; c < 3 && names[i][c]; c++) hs = hs * 31u + (uint8_t)names[i][c] + 1u;
    const int extra = (int16_t)((hs ^ (hs >> 15)) & 0x7FFF);
    if (var == 2) {
        float lv[64], pk[64];
        uint8_t grp[64];
        memset(grp, 0xFF, sizeof grp); memset(lv, 0, sizeof lv); memset(pk, 0, sizeof pk);
        for (int g = 0; g < n; g++) for (int w = g * 64 / n; w < (g + 1) * 64 / n - 1; w++) { grp[w] = (uint8_t)g; lv[w] = level[g]; pk[w] = peak[g]; }
        return ring_draw(d, 0, 113, extra, map, lv, pk, grp, n, PC_VD_ORD_SEQ);
    }
    if (d->w != 300 || d->h != 200) return R;
    const int W = d->w, H = d->h, BH = H - VB_NAMES, slot = W / n, bw = imin(40, slot - 6);
    int maxlen = 0;
    if (names) for (int i = 0; i < n; i++) maxlen = imax(maxlen, name_len(names[i]));
    const int s = text_w(maxlen, 2) <= slot - 2 ? 2 : 1;
    const bool full = begin(d, 97 + var, extra);
    if (full) { rect_add(&R, 0, 0, W, H); for (int i = 0; i < PC_VOICES_MAX; i++) d->prev_a[i] = d->prev_b[i] = 0; }
    else for (int i = 0; i < n; i++) {                 /* prev is the renderer's own memory, but garbage there must not reach outside the image */
        d->prev_a[i] = (int16_t)imax(0, imin(d->prev_a[i], BH));
        d->prev_b[i] = (int16_t)imax(0, imin(d->prev_b[i], BH));
    }
    for (int i = 0; i < n; i++) {
        const int h = rnd(level[i], BH), p0 = rnd(peak[i], BH), p = p0 < 2 || d->peaks_off ? 0 : p0;
        const int cx = (2 * i + 1) * W / (2 * n), x = cx - bw / 2;
        if (!full && h == d->prev_a[i] && p == d->prev_b[i]) continue;
        const int m = imax(imax(h, p), full ? 0 : imax(d->prev_a[i], d->prev_b[i]));
        if (!full) { fill_rect(d, x, BH - m, bw, m, d->bg); rect_add(&R, x, BH - m, x + bw, BH); }
        for (int k = 0; k < h; k++) {
            if (var == 1 && k % 7 >= 5) continue;
            fill_rect(d, x, BH - 1 - k, bw, 1, map_col(d, k, h, BH, i, n, false));
        }
        if (p) fill_rect(d, x, BH - p, bw, 2, d->fg);
        d->prev_a[i] = (int16_t)h; d->prev_b[i] = (int16_t)p;
    }
    if (full && names) for (int i = 0; i < n; i++) draw_name(d, (2 * i + 1) * W / (2 * n), H - 2 - 7 * s, s, names[i]);
    return R;
}

/* ---- scope ----
 * Circle directions: COS_Q14[i] = round(cos(2*pi*i/256) * 16384), 256 x int16 = 512 bytes of flash (const). sin(i) = cos(i - 64).
 * Angle 0 = right, growing clockwise on screen (y runs down). Everything per frame is integer after the sample -> radius step. */
static const int16_t COS_Q14[256] = {
    16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
    15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
    11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
    6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
    -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
    -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
};
#define SC_COLS 300
#define SC_X0 10
#define SC_MID 79                                    /* a zero sample draws rows 79 and 80 */
#define SC_RQ0 1587                                  /* circle radius 99.2 (70 * 340 / 240) in 1/16 px, + sample * 907 (= 56.7 px, 40 * 340 / 240) */
#define SC_RQA 907
#define SC_CQ 2712                                   /* the circle centre 169.5 in 1/16 px */

static float clamp1(float v) { return v != v ? 0.0f : v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v; }

/* index into mono of the first of cols samples; the samples at/after n count as 0 */
static int scope_start(const float *m, int n, int cols)
{
    if (n <= cols) return 0;
    for (int i = imax(n - 2 * cols, 1); i <= n - cols; i++)
        if (clamp1(m[i - 1]) < 0.0f && clamp1(m[i]) >= 0.0f) return i;
    return n - cols;
}

static void span_rows(const int16_t *y, int x, int var, int *lo, int *hi)     /* rows [lo, hi) of column x */
{
    int a = y[x], b = x > 0 ? y[x - 1] : a;
    *lo = imin(a, b); *hi = imax(a, b) + 2;
    if (var == 1) { *lo = imin(*lo, SC_MID); *hi = imax(*hi, SC_MID + 2); }
}

typedef struct { int x0, y0, x1, y1; } bbox_t;
static void bb_add(bbox_t *b, int x0, int y0, int x1, int y1)
{
    if (x0 < b->x0) b->x0 = x0;
    if (y0 < b->y0) b->y0 = y0;
    if (x1 > b->x1) b->x1 = x1;
    if (y1 > b->y1) b->y1 = y1;
}

/* the circle as a line of 2x2 blocks through 256 points; rq = radius per direction in 1/16 px. The last point is not joined
 * back to the first: a full-scale step at the seam would be a radial spike of up to 80 px, so a small gap stays instead */
static void circle_draw(pc_vd_t *d, const int16_t *rq, uint16_t col, bbox_t *bb)
{
    const int W = d->w, H = d->h;
    int px0 = 0, py0 = 0;
    for (int i = 0; i < 256; i++) {
        const int k = i;
        int x = (SC_CQ + (rq[k] * COS_Q14[k] >> 14)) >> 4, y = (SC_CQ + (rq[k] * COS_Q14[(k - 64) & 255] >> 14)) >> 4;
        x = imax(imin(x, W - 2), 0); y = imax(imin(y, H - 2), 0);
        const int sx = i ? px0 : x, sy = i ? py0 : y;
        const int dx = x - sx, dy = y - sy, st = imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
        for (int s = 0; s <= st; s++) {
            int cx = st ? sx + dx * s / st : x, cy = st ? sy + dy * s / st : y;
            uint16_t *p = d->px + (size_t)cy * W + cx;
            p[0] = col; p[1] = col; p[W] = col; p[W + 1] = col;
            bb_add(bb, cx, cy, cx + 2, cy + 2);
        }
        px0 = x; py0 = y;
    }
}

pc_vd_rect_t pc_vd_scope(pc_vd_t *d, int var, const float *mono, int n)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (var < 0 || var > 2) var = 0;
    if (!d || !d->px || !mono || n < 1) return R;
    if (d->w != (var == 2 ? PC_VD_CIRCLE_SZ : 320) || d->h != (var == 2 ? PC_VD_CIRCLE_SZ : 160)) return R;
    const int cols = var == 2 ? 256 : SC_COLS, st = scope_start(mono, n, cols);
    const bool full = begin(d, 33 + var, 0);
    if (full) rect_add(&R, 0, 0, d->w, d->h);
    int16_t *nw = d->prev_b;                         /* the new frame: row of each column / radius of each direction */
    for (int i = 0; i < cols; i++) {
        float v = st + i < n ? clamp1(mono[st + i]) : 0.0f;
        nw[i] = (int16_t)(var == 2 ? SC_RQ0 + lroundf(v * (float)SC_RQA) : lroundf(79.0f - v * 79.0f));
    }
    if (var == 2) {
        bool any = full;
        for (int i = 0; i < cols && !any; i++) any = nw[i] != d->prev_a[i];
        if (!any) return R;
        bbox_t bb = { d->w, d->h, 0, 0 };
        if (!full) circle_draw(d, d->prev_a, d->bg, &bb);
        circle_draw(d, nw, d->fg, &bb);
        if (!full) rect_add(&R, bb.x0, bb.y0, bb.x1, bb.y1);
        memcpy(d->prev_a, nw, sizeof(int16_t) * 256);
        return R;
    }
    for (int x = 0; x < cols; x++) {
        const bool ch = full || nw[x] != d->prev_a[x] || (x > 0 && nw[x - 1] != d->prev_a[x - 1]);
        if (!ch) continue;
        int lo, hi, olo, ohi;
        span_rows(nw, x, var, &lo, &hi);
        olo = lo; ohi = hi;
        if (!full) { span_rows(d->prev_a, x, var, &olo, &ohi); fill_rect(d, SC_X0 + x, olo, 1, ohi - olo, d->bg); }
        for (int y = lo; y < hi; y++) {
            uint16_t c = d->fg;
            if (var == 1 && (y < nw[x] || y > nw[x] + 1) && !(x > 0 && y >= nw[x - 1] && y <= nw[x - 1] + 1)) {
                int dist = y <= SC_MID ? SC_MID - y : y - SC_MID - 1;         /* 0 at the zero line (rows 79 and 80), 79 at the edge */
                c = d->lut[64 + dist * 191 / 79];
            }
            d->px[(size_t)y * d->w + SC_X0 + x] = c;
        }
        if (!full) rect_add(&R, SC_X0 + x, imin(lo, olo), SC_X0 + x + 1, imax(hi, ohi));
    }
    memcpy(d->prev_a, nw, sizeof(int16_t) * SC_COLS);
    return R;
}

/* ---- waterfall ---- */
#define FALL_FADE 48     /* levels 0..47 of 255 blend bg → ramp start; 48..255 run the whole ramp */

/* level 0 is the page background, 0..FALL_FADE a smooth blend (per RGB565 field, rounded) from the background to the ramp's first colour,
 * FALL_FADE..255 the whole ramp, first colour to last */
static uint16_t fall_color(const pc_vd_t *d, int k)
{
    if (k <= 0) return d->quiet;
    if (k >= FALL_FADE) return d->lut[(k - FALL_FADE) * 255 / (255 - FALL_FADE)];
    const uint16_t a = d->quiet, b = d->lut[0];
    const int r = ((a >> 11) * (FALL_FADE - k) + (b >> 11) * k + FALL_FADE / 2) / FALL_FADE;
    const int g = (((a >> 5) & 63) * (FALL_FADE - k) + ((b >> 5) & 63) * k + FALL_FADE / 2) / FALL_FADE;
    const int bl = ((a & 31) * (FALL_FADE - k) + (b & 31) * k + FALL_FADE / 2) / FALL_FADE;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void pc_vd_size_fall(bool full, int *w, int *h)
{
    const bool big = full && PC_VD_ROUND_SCREEN;
    if (w) *w = big ? PC_VD_RING_SZ : 256;
    if (h) *h = big ? PC_VD_RING_SZ : 200;
}

/* The full-size waterfall. FF_HW[y] = floor(sqrt(170^2 - (y - 169.5)^2)) for the upper half (row 339 - y is its mirror): 170 bytes of flash. */
#define FF_SZ PC_VD_RING_SZ
#define FF_ROWS 170
static const uint8_t FF_HW[FF_ROWS] = {
    13, 22, 29, 34, 38, 42, 46, 49, 53, 56, 58, 61, 63, 66, 68, 70, 73, 75, 77, 79, 80, 82, 84, 86, 87, 89, 91, 92, 94, 95, 97, 98,
    99, 101, 102, 103, 105, 106, 107, 108, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 130, 131, 132,
    133, 134, 134, 135, 136, 137, 137, 138, 139, 139, 140, 141, 141, 142, 143, 143, 144, 145, 145, 146, 146, 147, 148, 148, 149, 149, 150, 150, 151, 151, 152, 152,
    153, 153, 154, 154, 155, 155, 156, 156, 156, 157, 157, 158, 158, 158, 159, 159, 159, 160, 160, 161, 161, 161, 162, 162, 162, 162, 163, 163, 163, 164, 164, 164,
    164, 165, 165, 165, 165, 166, 166, 166, 166, 166, 167, 167, 167, 167, 167, 167, 168, 168, 168, 168, 168, 168, 168, 168, 169, 169, 169, 169, 169, 169, 169, 169,
    169, 169, 169, 169, 169, 169, 169, 169, 169, 169 };

static pc_vd_rect_t fall_full(pc_vd_t *d, int var, const float *level)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (begin(d, 52 + var, 0)) {                       /* a style or ramp change: bg, no history; the colour of every level is built once */
        fill_rect(d, 0, 0, FF_SZ, FF_SZ, d->bg);   /* outside the circle */
        memset(d->fall_hist, 0, sizeof d->fall_hist);
        for (int k = 0; k < 256; k++) d->fall_col[k] = fall_color(d, k);
    }
    memmove(d->fall_hist[1], d->fall_hist[0], sizeof d->fall_hist - sizeof d->fall_hist[0]);
    for (int c = 0; c < 64; c++) {
        float v;
        if (var == 2) { int j = c >= 32 ? c - 32 : 31 - c; v = fmaxf(sane(level[2 * j]), sane(level[2 * j + 1])); }
        else v = sane(level[c]);
        d->fall_hist[0][c] = (uint8_t)(int)(v * 255.0f + 0.5f);
    }
    for (int a = 0; a < FF_ROWS; a++) {                /* every history row into its two picture rows, bin by bin, clipped to the row's chord (no line buffer) */
        const uint8_t *h = d->fall_hist[a];
        for (int r = 0; r < 2; r++) {
            const int y = var == 1 ? FF_SZ - 2 - 2 * a + r : 2 * a + r;
            const int hw = FF_HW[y < FF_ROWS ? y : FF_SZ - 1 - y], lo = FF_SZ / 2 - hw, hi = FF_SZ / 2 + hw;
            uint16_t *row = d->px + (size_t)y * FF_SZ;
            for (int b = 0; b < 64; b++) {
                const int x0 = (b * FF_SZ + 63) >> 6, x1 = ((b + 1) * FF_SZ + 63) >> 6;   /* the x with x * 64 / 340 == b */
                if (x1 <= lo) continue;
                if (x0 >= hi) break;
                const uint16_t c = d->fall_col[h[b]];
                for (int x = imax(x0, lo); x < imin(x1, hi); x++) row[x] = c;
            }
        }
    }
    rect_add(&R, 0, 0, FF_SZ, FF_SZ);
    return R;
}

pc_vd_rect_t pc_vd_fall(pc_vd_t *d, int var, const float *level)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (var < 0 || var > 2) var = 0;
    if (d && d->px && level && PC_VD_ROUND_SCREEN && d->w == FF_SZ && d->h == FF_SZ) return fall_full(d, var, level);
    if (!d || !d->px || !level || d->w != 256 || d->h != 200) return R;
    const int W = 256, H = 200;
    if (begin(d, 49 + var, 0)) fill_rect(d, 0, 0, W, H, d->quiet);   /* a style change clears the image to the page background; then one row is drawn like any other */
    if (var == 1) memmove(d->px, d->px + 2 * W, (size_t)(H - 2) * W * 2);
    else memmove(d->px + 2 * W, d->px, (size_t)(H - 2) * W * 2);
    const int y = var == 1 ? H - 2 : 0;
    for (int c = 0; c < 64; c++) {
        float v;
        if (var == 2) { int j = c >= 32 ? c - 32 : 31 - c; v = fmaxf(sane(level[2 * j]), sane(level[2 * j + 1])); }
        else v = sane(level[c]);
        int k = (int)(v * 255.0f + 0.5f);
        fill_rect(d, c * 4, y, 4, 2, fall_color(d, k));
    }
    rect_add(&R, 0, 0, W, H);
    return R;
}

/* ---- phase scope ---- */
/* Auto gain: the picture is scaled so that the recent peak of (L +- R) reaches 3/4 of the edge (gain >= 1: a loud signal is never shrunk, so a
 * full-scale one still ends 2 px inside; at most 12x, so near silence is not blown up). gq is the gain in 1/256. */
#define PH_TARGET 49152
#define PH_FLOOR 4096
static void ph_point(const int16_t *s, int *x, int *y, int gq)
{
    const int64_t l = s[0], r = s[1], k = (int64_t)(PC_VD_PHASE_SZ - 5) * gq;
    int px = (PC_VD_PHASE_SZ - 1 + (int)(((l - r) * k) >> 24)) >> 1, py = (PC_VD_PHASE_SZ - 1 - (int)(((l + r) * k) >> 24)) >> 1;
    *x = imax(imin(px, PC_VD_PHASE_SZ - 1), 0); *y = imax(imin(py, PC_VD_PHASE_SZ - 1), 0);
}

pc_vd_rect_t pc_vd_phase(pc_vd_t *d, int var, const int16_t *lr, int frames)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (!d || !d->px || d->w != PC_VD_PHASE_SZ || d->h != PC_VD_PHASE_SZ) return R;
    if (var != 1) var = 0;
    const int W = PC_VD_PHASE_SZ;
    begin(d, 81 + var, 0);
    {   /* pull every pixel towards bg: per channel (c - bg) * 3 / 4 truncated towards zero, through three small tables */
        const uint16_t bg = d->bg;
        const int br = bg >> 11, bgc = (bg >> 5) & 63, bb = bg & 31;
        uint8_t tr[32], tg[64], tb[32];
        for (int i = 0; i < 32; i++) { tr[i] = (uint8_t)(br + ((i - br) * 3) / 4); tb[i] = (uint8_t)(bb + ((i - bb) * 3) / 4); }
        for (int i = 0; i < 64; i++) tg[i] = (uint8_t)(bgc + ((i - bgc) * 3) / 4);
        uint16_t *p = d->px;
        for (int i = 0, n = W * W; i < n; i++) {
            const uint16_t v = p[i];
            if (v != bg) p[i] = (uint16_t)((tr[v >> 11] << 11) | (tg[(v >> 5) & 63] << 5) | tb[v & 31]);
        }
    }
    if (!lr || frames < 1) frames = 0;
    if (frames > 1024) { lr += (ptrdiff_t)2 * ((ptrdiff_t)frames - 1024); frames = 1024; }       /* the newest 1024; no int overflow for any frames */
    int gq = 256;
    {   /* the recent peak: a quick rise, a slow fall (about 1.5 s at 30 fps) */
        int m = d->ph_peak * 31 / 32;
        for (int i = 0; i < frames; i++) {
            const int a = abs(lr[2 * i] - lr[2 * i + 1]), b = abs(lr[2 * i] + lr[2 * i + 1]);
            m = imax(m, imax(a, b));
        }
        d->ph_peak = (uint16_t)imin(m, 65535);
        gq = imin(PH_TARGET * 256 / imax(m, PH_FLOOR), 12 * 256);
        if (gq < 256) gq = 256;
    }
    if (frames) {
        int x, y;
        if (var == 0) {
            for (int i = 0; i < frames; i++) { ph_point(lr + 2 * i, &x, &y, gq); d->px[y * W + x] = d->fg; }
        } else {
            const int segs = imin(frames - 1, 128);
            int ox, oy;
            ph_point(lr, &ox, &oy, gq);
            d->px[oy * W + ox] = d->fg;
            for (int s = 1; s <= segs; s++) {
                ph_point(lr + 2 * (int)((int32_t)s * (frames - 1) / segs), &x, &y, gq);
                const int dx = x - ox, dy = y - oy, n = imax(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
                for (int k = 1; k <= n; k++) d->px[(oy + dy * k / n) * W + ox + dx * k / n] = d->fg;
                ox = x; oy = y;
            }
        }
    }
    R.x = 0; R.y = 0; R.w = W; R.h = W;
    return R;
}

/* ---- voice rows and piano roll (visualiser phase 1) ---- */
static uint16_t mix565(uint16_t a, uint16_t b, int t)            /* a at t = 0, b at t = 255 */
{
    const int ar = a >> 11, ag = a >> 5 & 63, ab = a & 31, br = b >> 11, bg = b >> 5 & 63, bb = b & 31;
    return (uint16_t)((((ar * (255 - t) + br * t) / 255) << 11) | (((ag * (255 - t) + bg * t) / 255) << 5) | ((ab * (255 - t) + bb * t) / 255));
}

static void put_px(pc_vd_t *d, int x, int y, uint16_t c) { if (x >= 0 && x < d->w && y >= 0 && y < d->h) d->px[(size_t)y * d->w + x] = c; }

static int draw_txt(pc_vd_t *d, int x0, int y0, int s, const char *t, uint16_t col)   /* left aligned; returns the x after the text */
{
    int x = x0;
    for (; *t; t++, x += 6 * s) {
        const int g = font_idx(*t);
        if (g < 0) continue;
        for (int r = 0; r < 7; r++) for (int b = 0; b < 5; b++) if (FONT57[g][r] >> (4 - b) & 1)
            for (int yy = 0; yy < s; yy++) for (int xx = 0; xx < s; xx++) put_px(d, x + b * s + xx, y0 + r * s + yy, col);
    }
    return x;
}

void pc_vd_vstate_reset(pc_vd_vstate_t *st) { if (st) memset(st, 0, sizeof *st); }

void pc_vd_vstate_feed(pc_vd_vstate_t *st, const pc_voices_t *v, uint32_t now_ms, uint32_t dt_ms)
{
    if (!st || !v || v->n < 1 || v->n > PC_VOICES_MAX) return;
    const int n = v->n;
    if (st->n != n) { pc_vd_vstate_reset(st); st->n = (uint8_t)n; st->col_ms = now_ms; for (int i = 0; i < n; i++) st->last_strike[i] = v->strikes[i]; }
    const int fall = (int)(dt_ms * 255u / 180u) + 1;                       /* 255 -> 0 in 180 ms */
    int strike_cols[PC_VOICES_MAX] = { 0 };
    for (int i = 0; i < n; i++) {
        if (v->strikes[i] != st->last_strike[i]) { st->last_strike[i] = v->strikes[i]; st->flash[i] = 255; strike_cols[i] = 1; }
        else st->flash[i] = (uint8_t)(st->flash[i] > fall ? st->flash[i] - fall : 0);
    }
    int pushed = 0;
    while (now_ms - st->col_ms >= PC_VD_ROLL_MS && pushed < PC_VD_ROLL_COLS) {   /* time-based: a slow frame adds the columns it missed (the same data) */
        st->col_ms += PC_VD_ROLL_MS;
        st->head = (int16_t)((st->head + 1) % PC_VD_ROLL_COLS);
        for (int i = 0; i < PC_VOICES_MAX; i++) {
            uint8_t nt = 0, l = 0;
            if (i < n && v->level[i] >= 16) {
                if (v->flags[i] & PC_VF_PITCH && v->cents[i] >= 0) {
                    int m = (v->cents[i] + 50) / 100 - 23;                  /* MIDI 24 (C1) = 1 */
                    nt = (uint8_t)(m < 1 ? 1 : m > 84 ? 84 : m);
                } else nt = 255;
                l = (uint8_t)(v->level[i] >> 1);
                if (pushed == 0 && strike_cols[i]) l |= 0x80;
            }
            st->note[st->head][i] = nt;
            st->lv[st->head][i] = l;
        }
        pushed++;
    }
    if (now_ms - st->col_ms >= PC_VD_ROLL_MS * PC_VD_ROLL_COLS) st->col_ms = now_ms;   /* a very long stall: restart the clock */
}

static uint16_t voice_col(const pc_vd_t *d, int i, int n) { return d->lut[n > 1 ? 90 + i * 165 / (n - 1) : 255]; }   /* the upper part of the ramp: the dark start of most ramps would not read on the page */

pc_vd_rect_t pc_vd_rows(pc_vd_t *d, pc_vd_vstate_t *st, const pc_voices_t *v, const float *level, const float *peak, const uint8_t *mstate, int sel)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (!d || !d->px || !st || !v || !level || !peak || v->n < 1 || v->n > PC_VOICES_MAX || d->w != 300 || d->h != 200) return R;
    const int n = v->n, cols = n <= 8 ? 1 : 2, rpc = n <= 8 ? n : n <= 16 ? 8 : 16, cw = 300 / cols;
    const int rh = imin(24, 200 / rpc), s = rh >= 18 && cw >= 280 ? 2 : 1;
    const int lamp = s * 3, tw = 3 * 6 * s - s, bh = s == 2 ? 9 : rh >= 20 ? 9 : 5;
    fill_rect(d, 0, 0, d->w, d->h, d->bg);
    for (int i = 0; i < n; i++) {
        const int c = i / rpc, r = i % rpc, x0 = c * cw + 3, y0 = r * rh + (rh - (bh + 3 + (s == 2 ? 2 : 0))) / 2;
        const int ms = mstate ? mstate[i] : 0, solo_on = mstate && ms == 2;
        const bool dead = ms == 1;                                              /* muted: everything dim */
        const uint16_t vc = dead ? d->dim : voice_col(d, i, n), txt = dead ? d->dim : d->fg;
        const int ty = y0 + (bh + 3 - 7 * s) / 2;
        /* key-on lamp */
        { int t = dead ? 20 : 40 + st->flash[i] * 215 / 255; fill_rect(d, x0, ty + (7 * s - lamp) / 2, lamp, lamp, mix565(d->dim, vc, t)); }
        int x = x0 + lamp + 3;
        char nm[4] = { v->name[i][0], v->name[i][1], v->name[i][2], 0 };
        draw_txt(d, x, ty, s, nm, txt);
        x += tw + 4;
        /* the note and the mark are at the right end of the column; the bar takes what is left */
        const int mark_w = 6 * s - s, note_w = tw, right = x0 - 3 + cw - 6;
        const int nx = right - mark_w - 3 - note_w, bx = x, bw = nx - 4 - bx;
        char nn[4];
        pc_voice_note_name((v->flags[i] & PC_VF_PITCH) && v->level[i] >= 8 ? v->cents[i] : PC_VOICE_NOPITCH, nn);
        draw_txt(d, nx, ty, s, nn, txt);
        if (ms) draw_txt(d, right - mark_w, ty, s, ms == 1 ? "M" : "S", solo_on ? d->fg : d->dim);
        if (bw > 8) {
            const int bw2 = rnd(level[i], bw), pw = rnd(peak[i], bw);
            fill_rect(d, bx, y0, bw, bh, mix565(d->bg, vc, 40));                 /* the track */
            fill_rect(d, bx, y0, bw2, bh, vc);
            if (pw > 2 && !d->peaks_off) fill_rect(d, bx + pw - 2, y0, 2, bh, d->fg);
            if (v->pan[i] != PC_PAN_UNKNOWN) {                                  /* a 1 px track under the bar with a marker */
                const int py = y0 + bh + 1, mx = bx + (v->pan[i] + 127) * (bw - 3) / 254;
                fill_rect(d, bx, py, bw, 1, d->dim);
                fill_rect(d, mx, py, 3, s == 2 ? 2 : 1, txt);
            }
        }
        if (i == sel) {                                                          /* the ring's cursor: a box round the row */
            const int bx0 = c * cw + 1, by0 = r * rh, bwid = cw - 2, bhei = rh - 1;
            fill_rect(d, bx0, by0, bwid, 1, d->fg); fill_rect(d, bx0, by0 + bhei - 1, bwid, 1, d->fg);
            fill_rect(d, bx0, by0, 1, bhei, d->fg); fill_rect(d, bx0 + bwid - 1, by0, 1, bhei, d->fg);
        }
    }
    d->fresh = false;
    rect_add(&R, 0, 0, d->w, d->h);
    return R;
}

pc_vd_rect_t pc_vd_roll(pc_vd_t *d, int var, const pc_vd_vstate_t *st, const pc_voices_t *v, const uint8_t *mstate, int sel)
{
    pc_vd_rect_t R = { 0, 0, 0, 0 };
    if (!d || !d->px || !st || !v || v->n < 1 || v->n > PC_VOICES_MAX || d->w != 300 || d->h != 200) return R;
    if (var < 0 || var > 1) var = 0;
    const int n = v->n, LANE = 24;
    fill_rect(d, 0, 0, d->w, d->h, d->bg);
    const uint16_t guide = mix565(d->bg, d->dim, 90);
    for (int o = 0; o < 7; o++) fill_rect(d, 0, 197 - 2 * (12 * o), d->w, 1, guide);           /* every C: C1 .. C7 */
    fill_rect(d, 0, LANE - 1, d->w, 1, guide);                                              /* the unpitched lane's foot */
    for (int o = 0; o < 7; o += 2) { char lb[3] = { 'C', (char)('1' + o), 0 }; draw_txt(d, 1, 197 - 2 * (12 * o) - 8, 1, lb, mix565(d->bg, d->dim, 150)); }   /* C1 C3 C5 C7 at the left */
    for (int a = 0; a < PC_VD_ROLL_COLS; a++) {
        const int ci = ((st->head - a) % PC_VD_ROLL_COLS + PC_VD_ROLL_COLS) % PC_VD_ROLL_COLS, x = 298 - 2 * a;
        for (int i = 0; i < n; i++) {
            const uint8_t nt = st->note[ci][i], lv = st->lv[ci][i];
            if (!nt) continue;
            const int lvl = (lv & 0x7F) * 2 + 1, strike = lv >> 7;
            if (mstate && mstate[i] == 1) continue;
            const uint16_t base = nt == 255 || var == 0 ? voice_col(d, i, n) : d->lut[(nt - 1) * 255 / 83];
            const uint16_t col = mix565(d->bg, base, 80 + lvl * 175 / 255);
            if (nt == 255) {                                                    /* unpitched: its own thin row in the lane */
                const int y = (i % 6) * 4;
                fill_rect(d, x, y, 2, 3, col);
            } else {
                const int y = 196 - 2 * (nt - 1), th = i == sel ? 3 : 2;
                fill_rect(d, x, y - (th - 2), 2, th, col);
                if (strike) { put_px(d, x, y - 2, d->fg); put_px(d, x + 1, y - 2, d->fg); }   /* a note start: a bright cap */
            }
        }
    }
    d->fresh = false;
    rect_add(&R, 0, 0, d->w, d->h);
    return R;
}
