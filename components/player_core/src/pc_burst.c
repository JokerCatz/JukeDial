// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_burst.h"

bool pc_burst_wanted(uint32_t rate) { return rate > PC_BURST_ABOVE_RATE; }
uint32_t pc_burst_slot_frames(uint32_t rate) { uint32_t f = rate / 100; return f ? f : 1; }
uint32_t pc_burst_slots_for(uint32_t frames, uint32_t slot_frames) { return slot_frames ? (frames + slot_frames - 1) / slot_frames : 0; }

void pc_bq_init(pc_bq_t *q, uint16_t depth)
{
    q->wr = q->rd = q->gen = 0;
    q->depth = depth;
    q->min_fill = depth;
    q->primed = false;
}
/* indices run 0 .. 2*depth-1 (no wrap trouble at 2^32 with a depth that is not a power of two) */
uint32_t pc_bq_fill(const pc_bq_t *q) { return (q->wr + 2u * q->depth - q->rd) % (2u * q->depth); }
uint32_t pc_bq_space(const pc_bq_t *q) { return q->depth - pc_bq_fill(q); }
uint32_t pc_bq_wr_slot(const pc_bq_t *q) { return q->wr % q->depth; }
void pc_bq_commit(pc_bq_t *q)
{
    q->wr = (q->wr + 1u) % (2u * q->depth);
    if (pc_bq_fill(q) >= q->depth) q->primed = true;
}
bool pc_bq_take(pc_bq_t *q, uint32_t *slot, uint32_t *gen)
{
    uint32_t fill = pc_bq_fill(q);
    if (!fill) return false;
    if (q->primed && fill < q->min_fill) q->min_fill = (uint16_t)fill;
    *slot = q->rd % q->depth;
    *gen = q->gen;
    return true;
}
void pc_bq_done(pc_bq_t *q, uint32_t gen)
{
    if (gen == q->gen && q->rd != q->wr) q->rd = (q->rd + 1u) % (2u * q->depth);
}
void pc_bq_flush(pc_bq_t *q)
{
    q->rd = q->wr;
    q->gen++;
    q->primed = false;
}
uint32_t pc_bq_min_ms(const pc_bq_t *q, bool *valid)
{
    *valid = q->primed || q->min_fill < q->depth;
    return (uint32_t)q->min_fill * 10u;
}
void pc_bq_window_reset(pc_bq_t *q) { q->min_fill = q->depth; }
