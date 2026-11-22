// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_fortune.h"     /* pc_rng_t */
#include "pc/pc_glyphpack.h"
#ifdef __cplusplus
extern "C" {
#endif
/* List toys over a glyph pack (小工具: 午餐, 抽人轉盤, 點子產生器; spec an internal note
 * section 7). Pure logic, no statics, no allocation. A "group" is a node of the pack's second level (a shop, a group
 * of people, a column); its items are its children. Picks honour the node weights (0 = never picked, left out of
 * the reels). Bit sets (skip, drawn) are one bit per child, (nchild + 7) / 8 bytes, owned by the caller.
 * Return values are child offsets (0 .. nchild-1) or -1 when there is nothing to pick. */

static inline bool pc_lp_bit(const uint8_t *b, uint32_t i) { return b && (b[i >> 3] >> (i & 7) & 1); }
static inline void pc_lp_setbit(uint8_t *b, uint32_t i) { b[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static inline uint32_t pc_lp_bitbytes(uint32_t n) { return (n + 7) / 8; }

/* children of node parent with weight > 0 and not in excl (NULL: none excluded) */
int pc_lp_count(const pc_gp_t *gp, uint32_t parent, const uint8_t *excl);
/* one of them, with probability weight / sum */
int pc_lp_pick(pc_rng_t *r, const pc_gp_t *gp, uint32_t parent, const uint8_t *excl);
/* their child offsets in order into out (at most cap); returns how many were written */
int pc_lp_items(const pc_gp_t *gp, uint32_t parent, const uint8_t *excl, uint16_t *out, int cap);

/* 午餐: a shop (child of the root) counts when its weight > 0, it is not skipped and it has a dish to pick */
int pc_lp_shop_count(const pc_gp_t *gp, const uint8_t *skip);
int pc_lp_pick_shop(pc_rng_t *r, const pc_gp_t *gp, const uint8_t *skip);
int pc_lp_shops(const pc_gp_t *gp, const uint8_t *skip, uint16_t *out, int cap);

/* 抽人: one draw from group. norepeat: only people not drawn yet this round; when everyone has been drawn a new
 * round starts (drawn cleared, *new_round set) first. The pick is marked in drawn either way. */
int pc_lp_draw(pc_rng_t *r, const pc_gp_t *gp, uint32_t group, uint8_t *drawn, bool norepeat, bool *new_round);
int pc_lp_left(const pc_gp_t *gp, uint32_t group, const uint8_t *drawn);   /* pickable and not drawn yet */

/* ---- reel strips for lists ----
 * A strip is a cycle of len slots, map[slot] = an item. n items: more than cap -> a random cap of them (shuffled);
 * else one shuffled order repeated so the strip has at least min_len slots (no item next to itself for n >= 2,
 * cyclic too). len <= max(cap, n) and <= cap when n > cap. Returns len (0 for n == 0). */
int pc_lp_strip(pc_rng_t *r, const uint16_t *items, int n, int cap, int min_len, uint16_t *map);
/* exactly len slots (reels of one view share a length): more than len items -> a random len of them; fewer -> one
 * shuffled order repeated and cut; no item next to itself (cyclic) when possible (n >= 3, or n == 2 and len even).
 * Returns len (0 for n == 0 or len < 1). */
int pc_lp_strip_fixed(pc_rng_t *r, const uint16_t *items, int n, int len, uint16_t *map);
/* the slot to land on for item: a random slot that holds it; if none does (a large list), a random slot outside
 * the visible window top .. top+rows-1 (cyclic; any slot when len <= rows) gets the item (map changed, *replaced
 * set: redraw that cell). -1 when len < 1. */
int pc_lp_slot(pc_rng_t *r, uint16_t *map, int len, uint16_t item, int top, int rows, bool *replaced);

#ifdef __cplusplus
}
#endif
