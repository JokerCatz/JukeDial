// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fortune / draw series: pure logic (pc_fortune.h). No statics: every state is the caller's. */
#include "pc/pc_fortune.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const uint8_t pc_kuji_weight[PC_KUJI_COUNT] = { 16, 22, 18, 18, 16, 10 };
const uint8_t pc_rarity_rate[PC_RAR_COUNT] = { 30, 35, 25, 10 };   /* N, R, SR, SSR */

static uint64_t mix64(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void pc_rng_seed(pc_rng_t *r, uint64_t seed) { r->s = seed; }
uint32_t pc_rng_next(pc_rng_t *r) { r->s += 0x9e3779b97f4a7c15ull; return (uint32_t)(mix64(r->s) >> 32); }
uint32_t pc_rng_below(pc_rng_t *r, uint32_t n)
{
    if (n <= 1) return 0;
    uint32_t lim = (uint32_t)(0x100000000ull - 0x100000000ull % n);   /* reject the tail: no modulo bias */
    for (;;) {
        uint32_t v = pc_rng_next(r);
        if (lim == 0 || v < lim) return v % n;
    }
}
static double rng_unit(pc_rng_t *r) { return pc_rng_next(r) / 4294967296.0; }   /* [0, 1) */

uint64_t pc_fortune_day_seed(int year, int month, int day, uint32_t salt)
{
    uint64_t ymd = (uint64_t)(year * 10000 + month * 100 + day);
    return mix64(ymd ^ ((uint64_t)salt << 32) ^ 0x5a17c0de00000000ull);
}

/* ---- omikuji ---- */
pc_kuji_t pc_kuji_rank(pc_rng_t *r)
{
    int v = (int)pc_rng_below(r, 100);
    for (int k = 0; k < PC_KUJI_COUNT; k++) { if (v < pc_kuji_weight[k]) return (pc_kuji_t)k; v -= pc_kuji_weight[k]; }
    return PC_KUJI_KYO;
}

void pc_omikuji_draw(pc_rng_t *r, const int8_t *verse_rank, int n, pc_omikuji_t *out)
{
    static const uint8_t BASE_X2[PC_KUJI_COUNT] = { 9, 8, 7, 6, 5, 4 };   /* stars x2: 4.5 ... 2 (const: flash) */
    memset(out, 0, sizeof *out);
    out->rank = pc_kuji_rank(r);
    for (int a = 0; a < PC_KUJI_ASPECTS; a++) {
        int s = (BASE_X2[out->rank] + (int)pc_rng_below(r, 2)) / 2 + (int)pc_rng_below(r, 3) - 1;
        out->stars[a] = (uint8_t)(s < 1 ? 1 : s > 5 ? 5 : s);
    }
    out->verse = -1;
    if (n <= 0 || !verse_rank) return;
    int m = 0;
    for (int i = 0; i < n; i++) m += verse_rank[i] < 0 || verse_rank[i] == (int8_t)out->rank;
    if (!m) { out->verse = (int)pc_rng_below(r, (uint32_t)n); return; }
    int k = (int)pc_rng_below(r, (uint32_t)m);
    for (int i = 0; i < n; i++)
        if ((verse_rank[i] < 0 || verse_rank[i] == (int8_t)out->rank) && k-- == 0) { out->verse = i; return; }
}

int pc_kuji_parse_rank(const char *s)
{
    static const char *const NAMES[PC_KUJI_COUNT] = { "大吉", "吉", "中吉", "小吉", "末吉", "凶" };
    if (!s[0] || !strcmp(s, "*")) return -1;
    for (int k = 0; k < PC_KUJI_COUNT; k++) if (!strcmp(s, NAMES[k])) return k;
    return -2;
}

/* ---- text lists ---- */
int pc_fortune_lines(char *text, char **out, int max, size_t max_len)
{
    int n = 0;
    char *p = text;
    if (!strncmp(p, "\xEF\xBB\xBF", 3)) p += 3;
    while (*p && n < max) {
        char *e = strchr(p, '\n'), *next = e ? e + 1 : p + strlen(p);
        if (e) *e = 0;
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        size_t len = strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r')) p[--len] = 0;
        if (len && p[0] != '#') {
            if (len > max_len) {
                size_t cut = max_len;
                while (cut && ((unsigned char)p[cut] & 0xC0) == 0x80) cut--;
                p[cut] = 0;
                while (cut && (p[cut - 1] == ' ' || p[cut - 1] == '\t')) p[--cut] = 0;
            }
            if (p[0]) out[n++] = p;
        }
        p = next;
    }
    return n;
}

/* ---- moon blocks ---- */
void pc_jiao_throw(pc_rng_t *r, pc_jiao_throw_t *out)
{
    uint32_t v = pc_rng_next(r);
    out->flat_up[0] = v & 1;
    out->flat_up[1] = (v >> 1) & 1;
    out->result = out->flat_up[0] != out->flat_up[1] ? PC_JIAO_SHENG : out->flat_up[0] ? PC_JIAO_XIAO : PC_JIAO_YIN;
}

/* ---- the wheel ---- */
int pc_wheel_index(double angle_deg, int n)
{
    if (n <= 1) return 0;
    double a = fmod(angle_deg, 360.0);
    if (a < 0) a += 360.0;
    int i = (int)floor(a / (360.0 / n));
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

void pc_wheel_plan(pc_rng_t *r, int n, double start_deg, pc_wheel_spin_t *out)
{
    if (n < 1) n = 1;
    double seg = 360.0 / n;
    out->target = (int)pc_rng_below(r, (uint32_t)n);
    int turns = 3 + (int)pc_rng_below(r, 3);
    double land = out->target * seg + seg * (0.15 + 0.7 * rng_unit(r));
    double from = fmod(start_deg, 360.0);
    if (from < 0) from += 360.0;
    double extra = fmod(land - from + 720.0, 360.0);
    out->start = start_deg;
    out->total = turns * 360.0 + extra;
}

double pc_wheel_angle_at(const pc_wheel_spin_t *s, double t)
{
    if (t <= 0) return s->start;
    if (t >= 1) return s->start + s->total;
    double u = 1.0 - t;
    return s->start + s->total * (1.0 - u * u * u);
}

/* ---- gacha ---- */
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void pc_gacha_tiers(const uint32_t *plays, int n, uint32_t *scratch, uint8_t *tier_out)
{
    if (n <= 0) return;
    memcpy(scratch, plays, (size_t)n * sizeof *scratch);
    qsort(scratch, (size_t)n, sizeof *scratch, cmp_u32);
    for (int i = 0; i < n; i++) {
        int lo = 0, hi = n;                     /* position = first index with plays >= this one's */
        while (lo < hi) { int mid = (lo + hi) / 2; if (scratch[mid] < plays[i]) lo = mid + 1; else hi = mid; }
        long q = (long)lo * 4;
        tier_out[i] = q < n ? PC_RAR_SSR : q < 2L * n ? PC_RAR_SR : q < 3L * n ? PC_RAR_R : PC_RAR_N;
    }
}

static bool used_has(const int *used, int nu, int i) { for (int k = 0; k < nu; k++) if (used[k] == i) return true; return false; }

/* members of tiers lo..hi (inclusive) not in used: pick one uniformly; -1 when there is none */
static int pick_member(pc_rng_t *r, const uint8_t *tier, int n, int lo, int hi, const int *used, int nu)
{
    int m = 0;
    for (int i = 0; i < n; i++) m += tier[i] >= lo && tier[i] <= hi && !used_has(used, nu, i);
    if (!m) return -1;
    int k = (int)pc_rng_below(r, (uint32_t)m);
    for (int i = 0; i < n; i++)
        if (tier[i] >= lo && tier[i] <= hi && !used_has(used, nu, i) && k-- == 0) return i;
    return -1;
}

static int roll_rarity(pc_rng_t *r, int lo)   /* rarity lo..SSR, by the rates among them */
{
    int sum = 0;
    for (int k = lo; k < PC_RAR_COUNT; k++) sum += pc_rarity_rate[k];
    int v = (int)pc_rng_below(r, (uint32_t)sum);
    for (int k = lo; k < PC_RAR_COUNT; k++) { if (v < pc_rarity_rate[k]) return k; v -= pc_rarity_rate[k]; }
    return PC_RAR_SSR;
}

static int pull_one(pc_rng_t *r, const uint8_t *tier, int n, int lo, const int *used, int nu)
{
    int want = roll_rarity(r, lo);
    for (int k = want; k >= lo; k--) { int i = pick_member(r, tier, n, k, k, used, nu); if (i >= 0) return i; }   /* more common */
    for (int k = want + 1; k < PC_RAR_COUNT; k++) { int i = pick_member(r, tier, n, k, k, used, nu); if (i >= 0) return i; }
    return -1;
}

int pc_gacha_pull(pc_rng_t *r, const uint8_t *tier, int n, int count, int *idx_out)
{
    if (n <= 0 || count <= 0) return 0;
    if (count > 10) count = 10;
    bool unique = n >= count;
    int got = 0;
    for (int c = 0; c < count; c++) {
        int i = pull_one(r, tier, n, PC_RAR_N, idx_out, unique ? got : 0);
        if (i < 0) break;
        idx_out[got++] = i;
    }
    if (count >= 10 && got) {
        bool good = false;
        for (int c = 0; c < got; c++) good |= tier[idx_out[c]] >= PC_RAR_SR;
        if (!good) {                             /* the guarantee: the last card becomes SR or better */
            int i = pull_one(r, tier, n, PC_RAR_SR, idx_out, unique ? got - 1 : 0);
            if (i >= 0 && tier[i] >= PC_RAR_SR) idx_out[got - 1] = i;
        }
    }
    return got;
}

/* ---- the collection file ---- */
int pc_cards_update(FILE *in, FILE *out, const char *const *ids, int n, uint32_t *counts_out)
{
    char line[640];
    bool found[16] = { false };
    if (n > 16) n = 16;
    int distinct = 0;
    while (in && fgets(line, sizeof line, in)) {
        size_t len = strlen(line);
        if (len && line[len - 1] != '\n' && !feof(in)) {   /* too long: skip the rest of it */
            int ch;
            while ((ch = fgetc(in)) != EOF && ch != '\n') {}
            continue;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        char *eq = strrchr(line, '=');
        if (!eq || eq == line || !eq[1]) continue;
        *eq = 0;
        uint32_t cnt = (uint32_t)strtoul(eq + 1, NULL, 10);
        for (int i = 0; i < n; i++) if (!strcmp(ids[i], line)) cnt++;
        for (int i = 0; i < n; i++) if (!strcmp(ids[i], line)) { counts_out[i] = cnt; found[i] = true; }
        if (fprintf(out, "%s=%lu\n", line, (unsigned long)cnt) < 0) return -1;
        distinct++;
    }
    for (int i = 0; i < n; i++) {
        if (found[i]) continue;
        uint32_t cnt = 0;
        for (int j = i; j < n; j++) if (!strcmp(ids[i], ids[j])) cnt++;
        for (int j = i; j < n; j++) if (!strcmp(ids[i], ids[j])) { counts_out[j] = cnt; found[j] = true; }
        if (fprintf(out, "%s=%lu\n", ids[i], (unsigned long)cnt) < 0) return -1;
        distinct++;
    }
    return fflush(out) || ferror(out) ? -1 : distinct;
}

/* ---- tarot ---- */
void pc_tarot_draw(pc_rng_t *r, pc_tarot_t *out)
{
    out->card = (int)pc_rng_below(r, PC_TAROT_N);
    out->reversed = pc_rng_next(r) & 1;
}
