// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_listpick.h"
#include <string.h>

/* List toys (pc_listpick.h). Weights are the pack's u8 node weights; sums fit in 32 bits (65535 * 255). */

static bool child(const pc_gp_t *gp, uint32_t parent, uint32_t k, pc_gp_node_t *c)
{
    pc_gp_node_t p;
    return !pc_gp_node(gp, parent, &p) && k < p.nchild && !pc_gp_node(gp, p.first_child + k, c);
}

static uint32_t nchild(const pc_gp_t *gp, uint32_t parent)
{
    pc_gp_node_t p;
    return pc_gp_node(gp, parent, &p) ? 0 : p.nchild;
}

/* weight of child k as a candidate (0: not one) */
static uint32_t item_w(const pc_gp_t *gp, uint32_t parent, uint32_t k, const uint8_t *excl)
{
    pc_gp_node_t c;
    if (pc_lp_bit(excl, k) || !child(gp, parent, k, &c)) return 0;
    return c.weight;
}

int pc_lp_count(const pc_gp_t *gp, uint32_t parent, const uint8_t *excl)
{
    int n = 0;
    for (uint32_t k = 0, m = nchild(gp, parent); k < m; k++) n += item_w(gp, parent, k, excl) > 0;
    return n;
}

int pc_lp_items(const pc_gp_t *gp, uint32_t parent, const uint8_t *excl, uint16_t *out, int cap)
{
    int n = 0;
    for (uint32_t k = 0, m = nchild(gp, parent); k < m && n < cap; k++)
        if (item_w(gp, parent, k, excl)) out[n++] = (uint16_t)k;
    return n;
}

int pc_lp_pick(pc_rng_t *r, const pc_gp_t *gp, uint32_t parent, const uint8_t *excl)
{
    uint32_t m = nchild(gp, parent), sum = 0;
    for (uint32_t k = 0; k < m; k++) sum += item_w(gp, parent, k, excl);
    if (!sum) return -1;
    uint32_t x = pc_rng_below(r, sum);
    for (uint32_t k = 0; k < m; k++) {
        uint32_t w = item_w(gp, parent, k, excl);
        if (x < w) return (int)k;
        x -= w;
    }
    return -1;   /* not reached */
}

/* ---- 午餐 (lunch) ---- */
static uint32_t shop_w(const pc_gp_t *gp, uint32_t k, const uint8_t *skip)
{
    pc_gp_node_t root, c;
    if (pc_lp_bit(skip, k) || pc_gp_node(gp, 0, &root) || k >= root.nchild || pc_gp_node(gp, root.first_child + k, &c)) return 0;
    return c.weight && pc_lp_count(gp, root.first_child + k, NULL) ? c.weight : 0;
}

int pc_lp_shop_count(const pc_gp_t *gp, const uint8_t *skip)
{
    int n = 0;
    for (uint32_t k = 0, m = nchild(gp, 0); k < m; k++) n += shop_w(gp, k, skip) > 0;
    return n;
}

int pc_lp_shops(const pc_gp_t *gp, const uint8_t *skip, uint16_t *out, int cap)
{
    int n = 0;
    for (uint32_t k = 0, m = nchild(gp, 0); k < m && n < cap; k++)
        if (shop_w(gp, k, skip)) out[n++] = (uint16_t)k;
    return n;
}

int pc_lp_pick_shop(pc_rng_t *r, const pc_gp_t *gp, const uint8_t *skip)
{
    uint32_t m = nchild(gp, 0), sum = 0;
    for (uint32_t k = 0; k < m; k++) sum += shop_w(gp, k, skip);
    if (!sum) return -1;
    uint32_t x = pc_rng_below(r, sum);
    for (uint32_t k = 0; k < m; k++) {
        uint32_t w = shop_w(gp, k, skip);
        if (x < w) return (int)k;
        x -= w;
    }
    return -1;
}

/* ---- 抽人 (pick a person) ---- */
int pc_lp_left(const pc_gp_t *gp, uint32_t group, const uint8_t *drawn) { return pc_lp_count(gp, group, drawn); }

int pc_lp_draw(pc_rng_t *r, const pc_gp_t *gp, uint32_t group, uint8_t *drawn, bool norepeat, bool *new_round)
{
    if (new_round) *new_round = false;
    if (!pc_lp_count(gp, group, NULL)) return -1;
    if (norepeat && !pc_lp_count(gp, group, drawn)) {          /* everyone has had a turn: a new round */
        memset(drawn, 0, pc_lp_bitbytes(nchild(gp, group)));
        if (new_round) *new_round = true;
    }
    int k = pc_lp_pick(r, gp, group, norepeat ? drawn : NULL);
    if (k >= 0) pc_lp_setbit(drawn, (uint32_t)k);
    return k;
}

/* ---- strips ---- */
static void shuffle(pc_rng_t *r, uint16_t *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)pc_rng_below(r, (uint32_t)i + 1);
        uint16_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

int pc_lp_strip(pc_rng_t *r, const uint16_t *items, int n, int cap, int min_len, uint16_t *map)
{
    if (n <= 0 || cap <= 0) return 0;
    if (n > cap) {                                    /* reservoir sample of cap, then shuffled */
        for (int i = 0; i < cap; i++) map[i] = items[i];
        for (int i = cap; i < n; i++) {
            uint32_t j = pc_rng_below(r, (uint32_t)i + 1);
            if (j < (uint32_t)cap) map[j] = items[i];
        }
        shuffle(r, map, cap);
        return cap;
    }
    int k = min_len > n ? (min_len + n - 1) / n : 1;  /* copies of one shuffled order: no item beside itself */
    if (k * n > cap) k = cap / n > 0 ? cap / n : 1;
    for (int i = 0; i < n; i++) map[i] = items[i];
    shuffle(r, map, n);
    for (int c = 1; c < k; c++) memcpy(map + c * n, map, (size_t)n * sizeof *map);
    return k * n;
}

int pc_lp_strip_fixed(pc_rng_t *r, const uint16_t *items, int n, int len, uint16_t *map)
{
    if (n <= 0 || len < 1) return 0;
    if (n > len) return pc_lp_strip(r, items, n, len, len, map);
    int k = pc_lp_strip(r, items, n, n, n, map);         /* one shuffled order (k == n) */
    for (int i = k; i < len; i++) map[i] = map[i % n];
    if (n >= 3 && len % n == 1) {                       /* it ends on map[0]: swap the last two */
        uint16_t t = map[len - 1]; map[len - 1] = map[len - 2]; map[len - 2] = t;
    }
    return len;
}

int pc_lp_slot(pc_rng_t *r, uint16_t *map, int len, uint16_t item, int top, int rows, bool *replaced)
{
    if (replaced) *replaced = false;
    if (len < 1) return -1;
    int have = 0;
    for (int i = 0; i < len; i++) have += map[i] == item;
    if (have) {
        uint32_t x = pc_rng_below(r, (uint32_t)have);
        for (int i = 0; i < len; i++) if (map[i] == item && x-- == 0) return i;
    }
    int t = ((top % len) + len) % len, vis = rows < 1 ? 0 : rows;
    int s;
    if (vis >= len) s = (int)pc_rng_below(r, (uint32_t)len);
    else s = (t + vis + (int)pc_rng_below(r, (uint32_t)(len - vis))) % len;   /* past the window, cyclic */
    map[s] = item;
    if (replaced) *replaced = true;
    return s;
}
