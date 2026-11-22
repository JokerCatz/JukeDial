// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_life.h"
#include <string.h>

#define PLANES 7   /* 4 state + old + chg + mask */
static const struct { uint16_t born, stay; } RULE[PC_LIFE_RULES] = {
    { 1u << 3, (1u << 2) | (1u << 3) },
    { 1u << 3, (1u << 2) | (1u << 3) },
    { (1u << 3) | (1u << 6), (1u << 2) | (1u << 3) },
    { (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8), (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) },
};
static const struct { const char *rle; uint8_t w, h; } PAT[PC_LIFE_PATTERNS] = {
    { "bob$2bo$3o!", 3, 3 },                                                                                                      /* 滑翔機 (glider) */
    { "bo2bo$o4b$o3bo$4o!", 5, 4 },                                                                                               /* 輕型太空船 (lightweight spaceship) */
    { "24bo11b$22bobo11b$12b2o6b2o12b2o$11bo3bo4b2o12b2o$2o8bo5bo3b2o14b$2o8bo3bob2o4bobo11b$10bo5bo7bo11b$11bo3bo20b$12b2o!", 36, 9 },   /* 高斯帕滑翔機槍 (Gosper glider gun) */
    { "2b3o3b3o2$o4bobo4bo$o4bobo4bo$o4bobo4bo$2b3o3b3o2$2b3o3b3o$o4bobo4bo$o4bobo4bo$o4bobo4bo2$2b3o3b3o!", 13, 13 },             /* 脈衝星 (pulsar) */
    { "b2o$2ob$bo!", 3, 3 },                                                                                                      /* R-五格 (R-pentomino) */
    { "bo5b$3bo3b$2o2b3o!", 7, 3 },                                                                                               /* 橡實 (acorn) */
    { "6bo$2o6b$bo3b3o!", 8, 3 },                                                                                                 /* 長壽者 (diehard) */
    { "o3b3o$3o2bo$bo!", 7, 3 },                                                                                                  /* 兔子 (rabbits) */
};

size_t pc_life_mem(int w, int h)
{
    const size_t nw = (size_t)(w + 63) / 64;
    return PLANES * nw * (size_t)h * 8 + 64;
}
static uint64_t rowvalid(const pc_life_t *s, int i) { const int r = s->w - 64 * i; return r >= 64 ? ~0ull : ((1ull << r) - 1); }
/* the field's shape: the whole square (default) or the circle of diameter min(w, h) (the round panel: cells outside it are walls and never drawn); call right after init */
void pc_life_shape(pc_life_t *s, bool round)
{
    if (!s || !s->mask) return;
    const int w = s->w, h = s->h, d = (w < h ? w : h);
    memset(s->mask, 0, (size_t)s->nw * (size_t)h * 8);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int dx = 2 * x + 1 - w, dy = 2 * y + 1 - h;   /* the cell's centre against the circle */
            if (!round || dx * dx + dy * dy <= d * d) s->mask[(size_t)y * s->nw + (size_t)(x >> 6)] |= 1ull << (x & 63);
        }
}
bool pc_life_init(pc_life_t *s, void *mem, size_t bytes, int w, int h, uint32_t seed)
{
    if (!s || !mem || w < 8 || h < 8 || w > 512 || h > 512 || bytes < pc_life_mem(w, h)) return false;
    memset(s, 0, sizeof *s);
    s->w = w; s->h = h; s->nw = (w + 63) / 64;
    s->mem = mem;
    const size_t n = (size_t)s->nw * (size_t)h;
    uint64_t *p = mem;
    memset(mem, 0, pc_life_mem(w, h));
    for (int k = 0; k < 4; k++) { s->pl[k] = p; p += n; }
    s->old = p; p += n;
    s->chg = p; p += n;
    s->mask = p;
    s->rng = seed ? seed : 0x9e3779b9u;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            s->mask[(size_t)y * s->nw + (size_t)(x >> 6)] |= 1ull << (x & 63);   /* the whole grid is the field (a square panel: the round glass hides the corners) */
        }
    return true;
}

static uint64_t eq3(uint64_t b0, uint64_t b1, uint64_t b2, int k);
static size_t at(const pc_life_t *s, int x, int y) { return (size_t)y * (size_t)s->nw + (size_t)(x >> 6); }
static uint64_t bitof(int x) { return 1ull << (x & 63); }
int pc_life_value(const pc_life_t *s, int x, int y)
{
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return 0;
    const size_t k = at(s, x, y);
    const uint64_t b = bitof(x);
    return (s->pl[0][k] & b ? 1 : 0) | (s->pl[1][k] & b ? 2 : 0) | (s->pl[2][k] & b ? 4 : 0) | (s->pl[3][k] & b ? 8 : 0);
}
int pc_life_class(int v)
{
    if (v <= 0) return PC_LIFE_C_EMPTY;
    if (v >= PC_LIFE_TRAIL0) return PC_LIFE_C_TRAIL0 + (v - PC_LIFE_TRAIL0);
    return v >= PC_LIFE_GOLD ? PC_LIFE_C_GOLD : PC_LIFE_C_YOUNG;
}
bool pc_life_in_circle(const pc_life_t *s, int x, int y) { return s && x >= 0 && y >= 0 && x < s->w && y < s->h && (s->mask[at(s, x, y)] & bitof(x)); }
static void put(pc_life_t *s, int x, int y, int v)
{
    const int o = pc_life_value(s, x, y);
    if (o == v) return;
    const size_t k = at(s, x, y);
    const uint64_t b = bitof(x);
    for (int p = 0; p < 4; p++) { if (v >> p & 1) s->pl[p][k] |= b; else s->pl[p][k] &= ~b; }
    if (pc_life_class(o) != pc_life_class(v)) s->chg[k] |= b;
    const int ao = o > 0 && o < PC_LIFE_TRAIL0, an = v > 0 && v < PC_LIFE_TRAIL0;
    s->pop = (uint32_t)((int)s->pop + an - ao);
    if (s->pop > s->peak) s->peak = s->pop;
}
void pc_life_set(pc_life_t *s, int x, int y, bool alive)
{
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    if (alive && !s->torus && !pc_life_in_circle(s, x, y)) return;
    const int o = pc_life_value(s, x, y);
    if (alive) { if (o < 1 || o >= PC_LIFE_TRAIL0) put(s, x, y, 1); }
    else put(s, x, y, 0);
}
void pc_life_config(pc_life_t *s, int rule, int torus, int trail)
{
    if (!s) return;
    s->rule = rule < 0 || rule >= PC_LIFE_RULES ? 0 : rule;
    s->torus = torus != 0;
    const int t = trail <= 0 ? 0 : trail <= 1 ? 1 : trail <= 3 ? 3 : 5;
    if (t < s->trail) {                                       /* a shorter afterglow: the trail cells past its end are gone now (they could never fade out) */
        const size_t n = (size_t)s->nw * (size_t)s->h;
        for (size_t i = 0; i < n; i++) {
            uint64_t over = 0;
            for (int k = t; k < 8; k++) over |= eq3(s->pl[0][i], s->pl[1][i], s->pl[2][i], k);
            const uint64_t kill = s->pl[3][i] & over;
            s->pl[0][i] &= ~kill; s->pl[1][i] &= ~kill; s->pl[2][i] &= ~kill; s->pl[3][i] &= ~kill;
            s->chg[i] |= kill;
        }
    }
    s->trail = t;
}
void pc_life_clear(pc_life_t *s)
{
    if (!s) return;
    const size_t n = (size_t)s->nw * (size_t)s->h;
    for (size_t i = 0; i < n; i++) s->chg[i] |= s->pl[0][i] | s->pl[1][i] | s->pl[2][i] | s->pl[3][i];
    for (int p = 0; p < 4; p++) memset(s->pl[p], 0, n * 8);
    s->gen = 0; s->pop = 0; s->peak = 0;
}
uint32_t pc_life_rand(pc_life_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s->rng = x;
}
void pc_life_fill_random(pc_life_t *s, int pct)
{
    if (!s) return;
    pc_life_clear(s);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            if (pc_life_in_circle(s, x, y) && (int)(pc_life_rand(s) % 100) < pct) put(s, x, y, 1);
}
int pc_life_splash(pc_life_t *s, int cx, int cy, int r, int pct)
{
    if (!s) return 0;
    int n = 0;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
            if ((int)(pc_life_rand(s) % 100) >= pct) continue;
            const int before = pc_life_value(s, x, y);
            pc_life_set(s, x, y, true);
            n += pc_life_value(s, x, y) != before;
        }
    return n;
}

/* ---- the step ---- */
#define FA(a, b, c, sum, carry) do { const uint64_t t_ = (a) ^ (b); (sum) = t_ ^ (c); (carry) = ((a) & (b)) | ((c) & t_); } while (0)
static uint64_t eq(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3, int k)
{
    return ((k & 1) ? b0 : ~b0) & ((k & 2) ? b1 : ~b1) & ((k & 4) ? b2 : ~b2) & ((k & 8) ? b3 : ~b3);
}
static uint64_t eq3(uint64_t b0, uint64_t b1, uint64_t b2, int k) { return ((k & 1) ? b0 : ~b0) & ((k & 2) ? b1 : ~b1) & ((k & 4) ? b2 : ~b2); }
/* the visual key: bit 3 = trail; the low three = the trail step, or for an alive cell 001 young / 111 gold */
static void key(uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t *k)
{
    const uint64_t alive = ~p3 & (p0 | p1 | p2), gold = ~p3 & p0 & p1 & p2;
    k[0] = (p3 & p0) | (~p3 & alive);
    k[1] = (p3 & p1) | (~p3 & gold);
    k[2] = (p3 & p2) | (~p3 & gold);
    k[3] = p3;
}
static void rowshift(const pc_life_t *s, const uint64_t *r, uint64_t *L, uint64_t *R)
{
    const int nw = s->nw;
    const uint64_t last = s->torus ? (r[nw - 1] >> ((s->w - 1) & 63)) & 1 : 0, first = s->torus ? (r[0] & 1) << ((s->w - 1) & 63) : 0;
    for (int i = 0; i < nw; i++) {
        L[i] = (r[i] << 1) | (i ? r[i - 1] >> 63 : last);
        R[i] = (r[i] >> 1) | (i < nw - 1 ? r[i + 1] << 63 : first);
    }
}
int pc_life_step(pc_life_t *s)
{
    static const uint64_t ZERO[8];
    if (!s) return 0;
    const int nw = s->nw, h = s->h, T = s->trail;
    const uint16_t born_m = RULE[s->rule].born, stay_m = RULE[s->rule].stay;
    const uint64_t tick = ((s->gen + 1) % PC_LIFE_AGE_GENS == 0) ? ~0ull : 0;
    for (int y = 0; y < h; y++)
        for (int i = 0; i < nw; i++) {
            const size_t k = (size_t)y * (size_t)nw + (size_t)i;
            s->old[k] = ~s->pl[3][k] & (s->pl[0][k] | s->pl[1][k] | s->pl[2][k]);
        }
    uint32_t pop = 0;
    for (int y = 0; y < h; y++) {
        uint64_t L[3][8], R[3][8];
        const uint64_t *C[3];
        for (int r = 0; r < 3; r++) {
            int yy = y - 1 + r;
            if (yy < 0) yy = s->torus ? h - 1 : -1;
            if (yy >= h) yy = s->torus ? 0 : -1;
            C[r] = yy < 0 ? ZERO : s->old + (size_t)yy * (size_t)nw;
            rowshift(s, C[r], L[r], R[r]);
        }
        for (int i = 0; i < nw; i++) {
            const size_t k = (size_t)y * (size_t)nw + (size_t)i;
            uint64_t s1, c1, s2, c2, b0, c4, t, k1, b1, k2;
            FA(L[0][i], C[0][i], R[0][i], s1, c1);
            FA(L[2][i], C[2][i], R[2][i], s2, c2);
            const uint64_t s3 = L[1][i] ^ R[1][i], c3 = L[1][i] & R[1][i];
            FA(s1, s2, s3, b0, c4);
            FA(c1, c2, c3, t, k1);
            b1 = t ^ c4; k2 = t & c4;
            const uint64_t b2 = k1 ^ k2, b3 = k1 & k2;
            uint64_t born = 0, stay = 0;
            for (int n = 0; n <= 8; n++) {
                const uint64_t e = eq(b0, b1, b2, b3, n);
                if (born_m >> n & 1) born |= e;
                if (stay_m >> n & 1) stay |= e;
            }
            const uint64_t A = s->old[k], valid = s->torus ? rowvalid(s, i) : s->mask[k];
            const uint64_t N = ((~A & born) | (A & stay)) & valid;
            const uint64_t o0 = s->pl[0][k], o1 = s->pl[1][k], o2 = s->pl[2][k], o3 = s->pl[3][k];
            const uint64_t nb = N & ~A, st = N & A, die = A & ~N;
            const uint64_t bump = st & tick & ~(o0 & o1 & o2);
            const uint64_t tr = o3, last = T ? (tr & eq3(o0, o1, o2, T - 1)) : tr;
            const uint64_t tcont = tr & ~last & ~N, tstart = T ? die : 0;
            const uint64_t p0 = nb | (st & (o0 ^ bump)) | (tcont & ~o0);
            const uint64_t p1 = (st & (o1 ^ (bump & o0))) | (tcont & (o1 ^ o0));
            const uint64_t p2 = (st & (o2 ^ (bump & o1 & o0))) | (tcont & (o2 ^ (o1 & o0)));
            const uint64_t p3 = tcont | tstart;
            uint64_t ko[4], kn[4];
            key(o0, o1, o2, o3, ko);
            key(p0, p1, p2, p3, kn);
            s->chg[k] |= (ko[0] ^ kn[0]) | (ko[1] ^ kn[1]) | (ko[2] ^ kn[2]) | (ko[3] ^ kn[3]);
            s->pl[0][k] = p0; s->pl[1][k] = p1; s->pl[2][k] = p2; s->pl[3][k] = p3;
            pop += (uint32_t)__builtin_popcountll(N);
        }
    }
    s->gen++;
    s->pop = pop;
    if (pop > s->peak) s->peak = pop;
    return (int)pop;
}

/* ---- patterns ---- */
const char *pc_life_pattern_rle(int i) { return i >= 0 && i < PC_LIFE_PATTERNS ? PAT[i].rle : ""; }
void pc_life_pattern_size(int i, int *w, int *h)
{
    if (i < 0 || i >= PC_LIFE_PATTERNS) { if (w) *w = 0; if (h) *h = 0; return; }
    if (w) *w = PAT[i].w;
    if (h) *h = PAT[i].h;
}
int pc_life_place(pc_life_t *s, int i, int cx, int cy, int orient)
{
    if (!s || i < 0 || i >= PC_LIFE_PATTERNS) return 0;
    const int tr = orient & 4 ? 1 : 0, pw = tr ? PAT[i].h : PAT[i].w, ph = tr ? PAT[i].w : PAT[i].h;
    int x = 0, y = 0, n = 0, run = 0;
    for (const char *c = PAT[i].rle; *c && *c != '!'; c++) {
        if (*c >= '0' && *c <= '9') { run = run * 10 + (*c - '0'); continue; }
        const int r = run ? run : 1;
        run = 0;
        if (*c == '$') { y += r; x = 0; continue; }
        for (int k = 0; k < r; k++, x++) {
            if (*c != 'o') continue;
            int ax = tr ? y : x, ay = tr ? x : y;                 /* transposed pattern coordinates */
            if (orient & 1) ax = pw - 1 - ax;
            if (orient & 2) ay = ph - 1 - ay;
            const int gx = cx - pw / 2 + ax, gy = cy - ph / 2 + ay;
            const int before = pc_life_value(s, gx, gy);
            pc_life_set(s, gx, gy, true);
            n += pc_life_value(s, gx, gy) != before;
        }
    }
    return n;
}
uint64_t *pc_life_chg(pc_life_t *s) { return s ? s->chg : NULL; }
void pc_life_chg_clear(pc_life_t *s) { if (s) memset(s->chg, 0, (size_t)s->nw * (size_t)s->h * 8); }
int pc_life_cap_gens(int cell_px) { return cell_px <= 1 ? 6 : cell_px == 2 ? 10 : cell_px == 3 ? 15 : 30; }
