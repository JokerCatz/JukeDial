// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_shuffle.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* days since 1970-01-01 (Howard Hinnant's days_from_civil) */
static int32_t days(uint32_t ymd)
{
    int32_t y = (int32_t)(ymd / 10000), m = (int32_t)(ymd / 100 % 100), d = (int32_t)(ymd % 100);
    if (m < 1) m = 1;
    if (d < 1) d = 1;
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int32_t pc_days_between(uint32_t a, uint32_t b) { return days(b) - days(a); }

uint32_t pc_shuffle_weight(uint32_t date, uint32_t now, bool fav)
{
    uint32_t w = 1;
    if (date && now) {
        int32_t age = pc_days_between(date, now);
        if (age <= 30) w = 4;
        else if (age <= 90) w = 2;
    }
    return fav ? w * 2 : w;
}

void pc_recent_push(pc_recent_t *r, int idx)
{
    r->idx[r->head] = idx;
    r->head = (r->head + 1) % PC_RECENT_N;
    if (r->n < PC_RECENT_N) r->n++;
}

bool pc_recent_has(const pc_recent_t *r, int idx)
{
    for (int i = 0; i < r->n; i++)
        if (r->idx[(r->head - 1 - i + PC_RECENT_N) % PC_RECENT_N] == idx) return true;
    return false;
}

int pc_recent_last(const pc_recent_t *r) { return r->n ? r->idx[(r->head - 1 + PC_RECENT_N) % PC_RECENT_N] : -1; }

static uint32_t weight_of(const pc_library_t *lib, int t, const pc_stats_t *st)
{
    bool fav = false;
    if (st) {
        char id[300];
        snprintf(id, sizeof id, "mp3:%s", pc_str(lib, lib->tracks[t].path));
        fav = pc_stats_is_fav(st, id);
    }
    return pc_shuffle_weight(lib->tracks[t].date, lib->newest_date, fav);
}

int pc_shuffle_pick(const pc_library_t *lib, const int *cand, int nc, const pc_stats_t *st, const pc_recent_t *recent, uint32_t rnd)
{
    if (nc <= 0) return -1;
    if (nc == 1) return cand[0];
    int last = recent ? pc_recent_last(recent) : -1;
    for (int pass = 0; pass < 2; pass++) {
        /* pass 0 skips every recent track; pass 1 (all were recent) skips only the last one */
        uint64_t total = 0;
        for (int i = 0; i < nc; i++) {
            bool skip = pass == 0 ? (recent && pc_recent_has(recent, cand[i])) : cand[i] == last;
            if (!skip) total += weight_of(lib, cand[i], st);
        }
        if (!total) continue;
        uint64_t x = rnd % total;
        for (int i = 0; i < nc; i++) {
            bool skip = pass == 0 ? (recent && pc_recent_has(recent, cand[i])) : cand[i] == last;
            if (skip) continue;
            uint32_t w = weight_of(lib, cand[i], st);
            if (x < w) return cand[i];
            x -= w;
        }
    }
    return cand[0];
}

int pc_order_next(pc_order_t o, const int *cand, int n, int cur, int shuffled)
{
    if (n <= 0) return -1;
    if (o == PC_ORDER_REPEAT_ONE) return cur >= 0 ? cur : shuffled;
    if (o != PC_ORDER_SEQUENTIAL) return shuffled;
    for (int i = 0; i < n; i++) if (cand[i] == cur) return cand[(i + 1) % n];
    return cand[0];
}

void pc_recent_remap(pc_recent_t *r, int (*map)(int idx, void *u), void *u)
{
    int keep[PC_RECENT_N], n = 0;
    for (int i = r->n - 1; i >= 0; i--) {                        /* oldest first */
        int m = map(r->idx[(r->head - 1 - i + PC_RECENT_N) % PC_RECENT_N], u);
        if (m >= 0) keep[n++] = m;
    }
    for (int i = 0; i < n; i++) r->idx[i] = keep[i];
    r->n = n;
    r->head = n % PC_RECENT_N;
}

int pc_order_next_range(pc_order_t o, int first, int n, int cur, int shuffled)
{
    if (n <= 0) return -1;
    if (o == PC_ORDER_REPEAT_ONE) return cur >= 0 ? cur : shuffled;
    if (o != PC_ORDER_SEQUENTIAL) return shuffled;
    if (cur >= first && cur < first + n) return first + (cur - first + 1) % n;
    return first;
}

/* ---- weights of one scope ---- */
#define WB 64   /* tracks per block sum */

static bool has_mp3_fav(const pc_stats_t *st)
{
    const char *one[1];
    return st && pc_stats_list_favs(st, "mp3:", one, 1) > 0;
}

void pc_wscope_free(pc_wscope_t *ws)
{
    pc_bigmem_free(ws->w);
    pc_bigmem_free(ws->bsum);
    memset(ws, 0, sizeof *ws);
}

int pc_wscope_build(pc_wscope_t *ws, const pc_library_t *lib, int first, int n, const pc_stats_t *st)
{
    pc_wscope_free(ws);
    ws->first = first;
    ws->lib = lib;
    ws->favgen = st ? pc_stats_fav_gen(st) : 0;
    if (n <= 0) return 0;
    const int nb = (n + WB - 1) / WB;
    ws->w = pc_bigmem_realloc(NULL, (size_t)n);
    ws->bsum = pc_bigmem_realloc(NULL, (size_t)nb * sizeof *ws->bsum);
    if (!ws->w || !ws->bsum) { pc_wscope_free(ws); return -ENOMEM; }
    ws->n = n;
    const bool favs = has_mp3_fav(st);
    char id[300];
    memcpy(id, "mp3:", 4);
    memset(ws->bsum, 0, (size_t)nb * sizeof *ws->bsum);
    uint64_t total = 0;
    for (int i = 0; i < n; i++) {
        const pc_track_t *t = &lib->tracks[first + i];
        bool fav = false;
        if (favs) {
            snprintf(id + 4, sizeof id - 4, "%s", pc_str(lib, t->path));
            fav = pc_stats_is_fav(st, id);
        }
        const uint32_t w = pc_shuffle_weight(t->date, lib->newest_date, fav);
        ws->w[i] = (uint8_t)w;
        ws->bsum[i / WB] += w;
        total += w;
    }
    ws->total = total;
    return 0;
}

bool pc_wscope_fresh(const pc_wscope_t *ws, const pc_library_t *lib, int first, int n, const pc_stats_t *st)
{
    return ws->lib == lib && ws->first == first && ws->n == (n > 0 ? n : 0) && ws->favgen == (st ? pc_stats_fav_gen(st) : 0) && (n <= 0 || ws->w);
}

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

/* the weighted walk skipping the sorted offsets sk[0..m) (distinct, inside the range) */
static int walk(const pc_wscope_t *ws, const int *sk, int m, uint64_t total, uint32_t rnd)
{
    uint64_t x = rnd % total;
    const int nb = (ws->n + WB - 1) / WB;
    int j = 0;
    for (int b = 0; b < nb; b++) {
        const int lo = b * WB, hi = lo + WB;
        uint64_t adj = ws->bsum[b];
        int k = j;
        for (; k < m && sk[k] < hi; k++) adj -= ws->w[sk[k]];
        if (x >= adj) { x -= adj; j = k; continue; }
        for (int i = lo; i < hi && i < ws->n; i++) {
            if (j < m && sk[j] == i) { j++; continue; }
            if (x < ws->w[i]) return ws->first + i;
            x -= ws->w[i];
        }
    }
    return -1;
}

int pc_wscope_pick(const pc_wscope_t *ws, const pc_recent_t *recent, uint32_t rnd)
{
    if (ws->n <= 0 || !ws->w) return -1;
    if (ws->n == 1) return ws->first;
    int sk[PC_RECENT_N] = { 0 }, m = 0;
    uint64_t skipped = 0;
    if (recent)
        for (int i = 0; i < recent->n && m < PC_RECENT_N; i++) {
            const int o = recent->idx[(recent->head - 1 - i + PC_RECENT_N) % PC_RECENT_N] - ws->first;
            if (o < 0 || o >= ws->n) continue;
            bool dup = false;
            for (int k = 0; k < m; k++) if (sk[k] == o) dup = true;
            if (!dup) sk[m++] = o;
        }
    qsort(sk, (size_t)m, sizeof sk[0], cmp_int);
    for (int k = 0; k < m; k++) skipped += ws->w[sk[k]];
    if (ws->total > skipped) {                                   /* pass 0: no recent track */
        int r = walk(ws, sk, m, ws->total - skipped, rnd);
        if (r >= 0) return r;
    }
    const int last = recent ? pc_recent_last(recent) - ws->first : -1;   /* pass 1: all were recent, only the last one is skipped */
    int one[PC_RECENT_N] = { 0 }, m1 = 0;
    uint64_t t1 = ws->total;
    if (last >= 0 && last < ws->n) { one[m1++] = last; t1 -= ws->w[last]; }
    if (t1) { int r = walk(ws, one, m1, t1, rnd); if (r >= 0) return r; }
    return ws->first;
}
