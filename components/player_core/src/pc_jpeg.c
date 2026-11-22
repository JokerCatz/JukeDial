// SPDX-License-Identifier: LGPL-2.1-or-later
/* JPEG routes and our own sequential Huffman decoder (hires task 8d; see pc_jpeg.h). Written for the layouts the S31's decoders refuse,
 * so it favours plain, bounds-checked code over speed: a 9-bit Huffman lookup, a separable float IDCT, one MCU row of samples at a time,
 * the output averaged down by 2^shift while it is converted. */
#include "pc/pc_jpeg.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ZZ[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
                                35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

const char *pc_jpeg_route_name(int r)
{
    switch (r) {
    case PC_JPEG_NATIVE: return "native";
    case PC_JPEG_OWN: return "own";
    case PC_JPEG_PROGRESSIVE: return "progressive";
    case PC_JPEG_UNSUPPORTED: return "unsupported";
    case PC_JPEG_TOO_BIG: return "too_big";
    default: return "bad";
    }
}

static bool is_sof(uint8_t m) { return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC; }

int pc_jpeg_probe(const uint8_t *p, size_t n, pc_jpeg_info_t *in)
{
    memset(in, 0, sizeof *in);
    in->sof = -1;
    if (!p || n < 4 || p[0] != 0xFF || p[1] != 0xD8) return PC_JPEG_BAD;
    size_t i = 2;
    bool sos = false;
    while (!sos) {
        if (i + 4 > n || p[i] != 0xFF) return PC_JPEG_BAD;
        uint8_t m = p[i + 1];
        if (m == 0xFF) { i++; continue; }                   /* fill bytes */
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { i += 2; continue; }
        if (m == 0xD9) return PC_JPEG_BAD;                  /* the end before any scan */
        size_t len = (size_t)p[i + 2] << 8 | p[i + 3];
        if (len < 2 || i + 2 + len > n) return PC_JPEG_BAD;
        const uint8_t *s = p + i + 4;
        if (is_sof(m)) {
            if (in->sof >= 0 || len < 8) return PC_JPEG_BAD;
            in->sof = m;
            in->prec = s[0];
            in->h = s[1] << 8 | s[2];
            in->w = s[3] << 8 | s[4];
            in->ncomp = s[5];
            if (m == 0xC2) return PC_JPEG_PROGRESSIVE;
            if (m != 0xC0 && m != 0xC1) return PC_JPEG_UNSUPPORTED;   /* lossless, hierarchical, arithmetic */
            if (in->prec != 8 || (in->ncomp != 1 && in->ncomp != 3)) return PC_JPEG_UNSUPPORTED;
            if (len < 8 + 3 * (size_t)in->ncomp || !in->w || !in->h) return PC_JPEG_BAD;
            for (int c = 0; c < in->ncomp; c++) {
                for (int k = 0; k < c; k++) if (s[6 + 3 * k] == s[6 + 3 * c]) return PC_JPEG_BAD;   /* the same component id twice */
                in->hs[c] = s[7 + 3 * c] >> 4;
                in->vs[c] = s[7 + 3 * c] & 15;
                if (in->hs[c] < 1 || in->hs[c] > 4 || in->vs[c] < 1 || in->vs[c] > 4) return PC_JPEG_BAD;
            }
        } else if (m == 0xDA) {
            if (in->sof < 0 || len < 3) return PC_JPEG_BAD;
            in->scan_comps = s[0];
            sos = true;
        }
        i += 2 + len;
    }
    if (in->scan_comps != in->ncomp) return PC_JPEG_UNSUPPORTED;   /* a sequential file in several scans */
    /* the cap is ours only (fix round 2): NATIVE pictures go to decoders that scale while they decode, with their own limits */
    bool big = in->w > PC_JPEG_MAX_SIDE || in->h > PC_JPEG_MAX_SIDE || (uint32_t)in->w * (uint32_t)in->h > PC_JPEG_MAX_PIXELS;
    if (in->ncomp == 1) return in->sof == 0xC0 && in->hs[0] == 1 && in->vs[0] == 1 ? PC_JPEG_NATIVE : big ? PC_JPEG_TOO_BIG : PC_JPEG_OWN;
    int hm = 0, vm = 0, blocks = 0;
    for (int c = 0; c < 3; c++) { if (in->hs[c] > hm) hm = in->hs[c]; if (in->vs[c] > vm) vm = in->vs[c]; blocks += in->hs[c] * in->vs[c]; }
    if (blocks > 10) return PC_JPEG_UNSUPPORTED;
    for (int c = 0; c < 3; c++) if (hm % in->hs[c] || vm % in->vs[c]) return PC_JPEG_UNSUPPORTED;
    bool chroma11 = in->hs[1] == 1 && in->vs[1] == 1 && in->hs[2] == 1 && in->vs[2] == 1;
    int y = in->hs[0] << 4 | in->vs[0];
    return in->sof == 0xC0 && chroma11 && (y == 0x11 || y == 0x21 || y == 0x22) ? PC_JPEG_NATIVE : big ? PC_JPEG_TOO_BIG : PC_JPEG_OWN;
}

uint64_t pc_jpeg_hw_bytes(const pc_jpeg_info_t *in, uint32_t *pw, uint32_t *ph)
{
    uint64_t mx = in->ncomp == 1 ? 8 : 8u * (in->hs[0] ? in->hs[0] : 1), my = in->ncomp == 1 ? 8 : 8u * (in->vs[0] ? in->vs[0] : 1);
    uint64_t w = ((uint64_t)in->w + mx - 1) / mx * mx, h = ((uint64_t)in->h + my - 1) / my * my;
    *pw = w > UINT32_MAX ? UINT32_MAX : (uint32_t)w;
    *ph = h > UINT32_MAX ? UINT32_MAX : (uint32_t)h;
    return w * h * (in->ncomp == 1 ? 1u : 2u);
}

uint64_t pc_jpeg_min_bytes(const pc_jpeg_info_t *in) { return ((uint64_t)in->w / 8) * ((uint64_t)in->h / 8) * 2u; }

int pc_jpeg_shift_for(int w, int h, int min_short)
{
    int s = w < h ? w : h, k = 0;
    while (k < 3 && (s >> (k + 1)) >= min_short && (s >> (k + 1)) > 0) k++;
    return k;
}

/* ---- the decoder ---- */
typedef struct {
    uint8_t len[512], val[512];   /* codes up to 9 bits: the whole symbol from the next 9 bits */
    int32_t maxcode[18], valoff[17];
    uint8_t vals[256];
    int nvals;
    bool ok;
} huff_t;

typedef struct {
    int id, h, v, tq, td, ta, pred;
    int pw;                        /* plane width (samples) of one MCU row */
    uint8_t *plane;
} comp_t;

typedef struct {
    const uint8_t *p;
    size_t n, pos;
    uint32_t bits;
    int nbits;
    bool marker, broken, row0_broken, dc_only;   /* dc_only: shift 3, a full-resolution block is one output pixel: its mean (DC) is enough */
    int padded;                    /* zero bytes fed after the data ended (a marker or the buffer's end) */
    uint16_t qt[4][64];
    bool qok[4];
    huff_t dc[4], ac[4];
    comp_t c[3];
    int nc, w, h, hm, vm, mcux, mcuy, ri;
    float cosk[8][8];
} dec_t;

static bool build_huff(huff_t *t, const uint8_t *counts, const uint8_t *vals, int nvals)
{
    memset(t, 0, sizeof *t);
    memcpy(t->vals, vals, (size_t)nvals);
    t->nvals = nvals;
    int32_t code = 0;
    int k = 0;
    for (int l = 1; l <= 16; l++) {
        t->valoff[l] = k - code;
        for (int i = 0; i < counts[l - 1]; i++, k++, code++) {
            if (code >= (1 << l)) return false;            /* more codes than this length has */
            if (l <= 9) {
                int base = code << (9 - l), span = 1 << (9 - l);
                for (int j = 0; j < span; j++) { t->len[base + j] = (uint8_t)l; t->val[base + j] = vals[k]; }
            }
        }
        t->maxcode[l] = counts[l - 1] ? code - 1 : -1;
        code <<= 1;
    }
    t->maxcode[17] = INT32_MAX;
    t->ok = true;
    return true;
}

/* the entropy-coded bytes: 0xFF 0x00 is a 0xFF, any other marker (or the end) stops the data and feeds zeros */
static void fill(dec_t *d)
{
    while (d->nbits <= 24) {
        uint32_t b = 0;
        bool real = false;
        if (!d->marker && d->pos < d->n) {
            b = d->p[d->pos];
            if (b == 0xFF) {
                uint8_t nx = d->pos + 1 < d->n ? d->p[d->pos + 1] : 0xD9;
                if (nx == 0x00) { d->pos += 2; real = true; }
                else { d->marker = true; b = 0; }
            } else { d->pos++; real = true; }
        }
        if (!real) d->padded++;
        d->bits |= b << (24 - d->nbits);
        d->nbits += 8;
    }
}
static uint32_t peek(dec_t *d, int k) { fill(d); return d->bits >> (32 - k); }
static void skip(dec_t *d, int k) { d->bits <<= k; d->nbits -= k; }
static int getbits(dec_t *d, int k) { if (!k) return 0; uint32_t v = peek(d, k); skip(d, k); return (int)v; }
static int extend(int v, int k) { return k && v < (1 << (k - 1)) ? v - (1 << k) + 1 : v; }

static int symbol(dec_t *d, const huff_t *t)
{
    uint32_t look = peek(d, 9);
    if (t->len[look]) { skip(d, t->len[look]); return t->val[look]; }
    for (int l = 10; l <= 16; l++) {
        int32_t code = (int32_t)peek(d, l);
        if (code <= t->maxcode[l]) {
            int idx = t->valoff[l] + code;
            if (idx < 0 || idx >= t->nvals) return -1;
            skip(d, l);
            return t->vals[idx];
        }
    }
    return -1;
}

/* one 8x8 block into out (stride), dequantised and inverse transformed; a damaged code marks the rest of the picture broken (grey) */
static void block(dec_t *d, comp_t *c, uint8_t *out, int stride)
{
    if (d->padded > 8) d->broken = true;                     /* the data ran out (more than the bit buffer of zeros fed) */
    if (d->broken) {                                         /* the rest grey, no transform (review I-1) */
        for (int y = 0; y < 8; y++) memset(out + y * stride, 128, 8);
        return;
    }
    float co[64];
    memset(co, 0, sizeof co);
    {
        const uint16_t *q = d->qt[c->tq];
        int s = symbol(d, &d->dc[c->td]);
        if (s < 0 || s > 11) d->broken = true;
        else {
            c->pred += extend(getbits(d, s), s);
            if (c->pred > 32767) c->pred = 32767;            /* a valid file stays far inside; keeps the product in int (review M-1) */
            if (c->pred < -32767) c->pred = -32767;
            co[0] = (float)(c->pred * q[0]);
            if (d->dc_only && c->h == d->hm && c->v == d->vm) {   /* a full-resolution component (subsampled chroma keeps its detail):
                                                                  * the AC codes are still read (to stay in step), not transformed */
                for (int k = 1; k < 64;) {
                    int rs = symbol(d, &d->ac[c->ta]);
                    if (rs < 0) { d->broken = true; break; }
                    int r = rs >> 4, sz = rs & 15;
                    if (!sz) { if (r == 15) { k += 16; continue; } break; }
                    k += r + 1;
                    if (k > 64) { d->broken = true; break; }
                    (void)getbits(d, sz);
                }
                if (d->broken) { for (int y = 0; y < 8; y++) memset(out + y * stride, 128, 8); return; }
                int v = (int)lrintf(co[0] / 8.0f) + 128;
                uint8_t m = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                for (int y = 0; y < 8; y++) memset(out + y * stride, m, 8);
                return;
            }
            for (int k = 1; k < 64;) {
                int rs = symbol(d, &d->ac[c->ta]);
                if (rs < 0) { d->broken = true; break; }
                int r = rs >> 4, sz = rs & 15;
                if (!sz) { if (r == 15) { k += 16; continue; } break; }
                k += r;
                if (k > 63) { d->broken = true; break; }
                co[ZZ[k]] = (float)(extend(getbits(d, sz), sz) * q[ZZ[k]]);
                k++;
            }
        }
        if (d->broken) { for (int y = 0; y < 8; y++) memset(out + y * stride, 128, 8); return; }
    }
    float tmp[64];
    for (int x = 0; x < 8; x++)                              /* columns */
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += d->cosk[y][v] * co[v * 8 + x];
            tmp[y * 8 + x] = s;
        }
    for (int y = 0; y < 8; y++)                              /* rows */
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += d->cosk[x][u] * tmp[y * 8 + u];
            int v = (int)lrintf(s) + 128;
            out[y * stride + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

/* the restart marker after ri MCUs: the bit buffer is dropped, the next RSTn looked for, the DC predictions reset; found, a broken picture
 * decodes again from there (as libjpeg resynchronises; review M-2) */
static void restart(dec_t *d)
{
    d->bits = 0; d->nbits = 0; d->marker = false; d->padded = 0;
    while (d->pos + 1 < d->n && !(d->p[d->pos] == 0xFF && d->p[d->pos + 1] >= 0xD0 && d->p[d->pos + 1] <= 0xD7)) {
        if (d->p[d->pos] == 0xFF && d->p[d->pos + 1] != 0x00 && d->p[d->pos + 1] != 0xFF) break;   /* another marker: the data ended */
        d->pos++;
    }
    if (d->pos + 1 < d->n && d->p[d->pos] == 0xFF && d->p[d->pos + 1] >= 0xD0 && d->p[d->pos + 1] <= 0xD7) { d->pos += 2; d->broken = false; }
    for (int i = 0; i < d->nc; i++) d->c[i].pred = 0;
}

/* the tables and the frame up to the first scan's data: 0, or a PC_JPEG_E_ code */
static int parse(dec_t *d)
{
    const uint8_t *p = d->p;
    size_t i = 2, n = d->n;
    for (;;) {
        if (i + 4 > n || p[i] != 0xFF) return PC_JPEG_E_FORMAT;
        uint8_t m = p[i + 1];
        if (m == 0xFF) { i++; continue; }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { i += 2; continue; }
        size_t len = (size_t)p[i + 2] << 8 | p[i + 3];
        if (len < 2 || i + 2 + len > n) return PC_JPEG_E_FORMAT;
        const uint8_t *s = p + i + 4, *e = p + i + 2 + len;
        if (m == 0xDB) {
            while (s < e) {
                int pq = s[0] >> 4, tq = s[0] & 15;
                size_t need = 1 + (pq ? 128 : 64);
                if (tq > 3 || pq > 1 || (size_t)(e - s) < need) return PC_JPEG_E_FORMAT;
                for (int k = 0; k < 64; k++) d->qt[tq][ZZ[k]] = pq ? (uint16_t)(s[1 + 2 * k] << 8 | s[2 + 2 * k]) : s[1 + k];
                d->qok[tq] = true;
                s += need;
            }
        } else if (m == 0xC4) {
            while (s < e) {
                if (e - s < 17) return PC_JPEG_E_FORMAT;
                int tc = s[0] >> 4, th = s[0] & 15, total = 0;
                for (int k = 1; k <= 16; k++) total += s[k];
                if (tc > 1 || th > 3 || total > 256 || e - s < 17 + total) return PC_JPEG_E_FORMAT;
                if (!build_huff(tc ? &d->ac[th] : &d->dc[th], s + 1, s + 17, total)) return PC_JPEG_E_FORMAT;
                s += 17 + total;
            }
        } else if (m == 0xDD) {
            if (len < 4) return PC_JPEG_E_FORMAT;
            d->ri = s[0] << 8 | s[1];
        } else if (m == 0xC0 || m == 0xC1) {                 /* checked by pc_jpeg_probe */
            for (int c = 0; c < d->nc; c++) {
                d->c[c].id = s[6 + 3 * c];
                d->c[c].h = d->nc == 1 ? 1 : s[7 + 3 * c] >> 4;
                d->c[c].v = d->nc == 1 ? 1 : s[7 + 3 * c] & 15;
                d->c[c].tq = s[8 + 3 * c];
                if (d->c[c].tq > 3) return PC_JPEG_E_FORMAT;
            }
        } else if (m == 0xDA) {
            int ns = s[0];
            if (ns != d->nc || len < 6 + 2 * (size_t)ns) return PC_JPEG_E_FORMAT;
            for (int k = 0; k < ns; k++) {
                int id = s[1 + 2 * k], tt = s[2 + 2 * k], c = 0;
                while (c < d->nc && d->c[c].id != id) c++;
                if (c == d->nc) return PC_JPEG_E_FORMAT;
                d->c[c].td = tt >> 4;
                d->c[c].ta = tt & 15;
                if (d->c[c].td > 3 || d->c[c].ta > 3 || !d->dc[d->c[c].td].ok || !d->ac[d->c[c].ta].ok || !d->qok[d->c[c].tq]) return PC_JPEG_E_FORMAT;
            }
            d->pos = i + 2 + len;
            return 0;
        }
        i += 2 + len;
    }
}

int pc_jpeg_decode(const uint8_t *p, size_t n, int shift, const pc_jpeg_mem_t *mem, uint16_t **out, int *ow, int *oh)
{
    *out = NULL;
    *ow = *oh = 0;
    pc_jpeg_info_t in;
    int route = pc_jpeg_probe(p, n, &in);
    if (route == PC_JPEG_PROGRESSIVE) return PC_JPEG_E_PROGRESSIVE;
    if (route == PC_JPEG_UNSUPPORTED) return PC_JPEG_E_UNSUPPORTED;
    if (route == PC_JPEG_BAD) return PC_JPEG_E_FORMAT;
    if (route == PC_JPEG_TOO_BIG || in.w > PC_JPEG_MAX_SIDE || in.h > PC_JPEG_MAX_SIDE || (uint32_t)in.w * (uint32_t)in.h > PC_JPEG_MAX_PIXELS)
        return PC_JPEG_E_TOO_BIG;                            /* also a NATIVE picture handed to us after the chip's decoders refused it */
    if (shift < 0) shift = 0;
    if (shift > 3) shift = 3;
    int w = in.w >> shift, h = in.h >> shift;
    if (w < 1 || h < 1) return PC_JPEG_E_FORMAT;
    void *(*al)(size_t) = mem && mem->alloc ? mem->alloc : malloc;
    void (*fr)(void *) = mem && mem->free ? mem->free : free;
    void (*poll)(void) = mem ? mem->poll : NULL;
    dec_t *d = al(sizeof *d);
    if (!d) return PC_JPEG_E_NOMEM;
    memset(d, 0, sizeof *d);
    d->p = p; d->n = n; d->nc = in.ncomp; d->w = in.w; d->h = in.h; d->dc_only = shift == 3;
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++) d->cosk[x][u] = (u ? 0.5f : 0.35355339f) * cosf((float)((2 * x + 1) * u) * 3.14159265f / 16.0f);
    int rc = parse(d);
    uint8_t *planes = NULL;
    uint16_t *px = NULL;
    if (!rc) {
        d->hm = d->vm = 1;
        for (int c = 0; c < d->nc; c++) { if (d->c[c].h > d->hm) d->hm = d->c[c].h; if (d->c[c].v > d->vm) d->vm = d->c[c].v; }
        d->mcux = (d->w + 8 * d->hm - 1) / (8 * d->hm);
        d->mcuy = (d->h + 8 * d->vm - 1) / (8 * d->vm);
        size_t total = 0;
        for (int c = 0; c < d->nc; c++) { d->c[c].pw = d->mcux * d->c[c].h * 8; total += (size_t)d->c[c].pw * d->c[c].v * 8; }
        if ((uint64_t)w * (uint64_t)h > (1u << 28) || total > (1u << 28)) rc = PC_JPEG_E_NOMEM;
        else if (!(planes = al(total)) || !(px = al((size_t)w * h * 2))) rc = PC_JPEG_E_NOMEM;
    }
    if (!rc) {
        uint8_t *q = planes;
        for (int c = 0; c < d->nc; c++) { d->c[c].plane = q; q += (size_t)d->c[c].pw * d->c[c].v * 8; }
        int k = 1 << shift, kk = k * k, band = 8 * d->vm, mcu = 0;
        for (int my = 0; my < d->mcuy; my++) {
            for (int mx = 0; mx < d->mcux; mx++, mcu++) {
                if (d->ri && mcu && mcu % d->ri == 0) restart(d);
                for (int c = 0; c < d->nc; c++) {
                    comp_t *cc = &d->c[c];
                    for (int v = 0; v < cc->v; v++)
                        for (int u = 0; u < cc->h; u++)
                            block(d, cc, cc->plane + (size_t)(v * 8) * cc->pw + (size_t)(mx * cc->h + u) * 8, cc->pw);
                }
                if (poll) poll();                               /* night3 B: the caller's chance to yield (an MCU is ~0.1-0.3 ms on the S31) */
                if (!my) d->row0_broken = d->broken;            /* as it stands at the end of the row: a restart marker later in the row recovers it (fix round 3) */
            }
            if (d->row0_broken) { rc = PC_JPEG_E_FORMAT; break; }   /* nothing of the picture decoded: no all-grey cover (review M-2) */
            int y0 = my * band;                                  /* the output rows whose square lies in this band */
            for (int oy = y0 / k; oy < h && oy * k < y0 + band; oy++) {
                int ly = oy * k - y0;
                uint16_t *o = px + (size_t)oy * w;
                for (int ox = 0; ox < w; ox++) {
                    int sum[3] = { 0, 0, 0 };
                    for (int c = 0; c < d->nc; c++) {
                        const comp_t *cc = &d->c[c];
                        for (int dy = 0; dy < k; dy++) {
                            const uint8_t *row = cc->plane + (size_t)((ly + dy) * cc->v / d->vm) * cc->pw;
                            for (int dx = 0; dx < k; dx++) sum[c] += row[(ox * k + dx) * cc->h / d->hm];
                        }
                    }
                    int Y = sum[0] / kk, r, g, b;
                    if (d->nc == 1) r = g = b = Y;
                    else {
                        int cb = sum[1] / kk - 128, cr = sum[2] / kk - 128;
                        r = Y + ((91881 * cr + 32768) >> 16);
                        g = Y - ((22554 * cb + 46802 * cr - 32768) >> 16);
                        b = Y + ((116130 * cb + 32768) >> 16);
                        r = r < 0 ? 0 : r > 255 ? 255 : r;
                        g = g < 0 ? 0 : g > 255 ? 255 : g;
                        b = b < 0 ? 0 : b > 255 ? 255 : b;
                    }
                    o[ox] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
                }
                if (poll) poll();                               /* one converted output row: ~(w x k x k x comps) adds */
            }
        }
    }
    if (planes) fr(planes);
    fr(d);
    if (rc) { if (px) fr(px); return rc; }
    *out = px;
    *ow = w;
    *oh = h;
    return PC_JPEG_OK;
}
