// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pipe_rules.h"

pc_plan_args_t pc_plan_args(uint32_t raw, uint32_t burst, bool sink, uint32_t cap, uint32_t hw_cap)
{
    pc_plan_args_t a;
    a.limit_why = sink ? PC_PIPE_SINK : hw_cap < cap ? PC_PIPE_CLOCK : PC_PIPE_CAP;
    a.out_max = pc_out_max_rate(sink ? PC_OUT_SINK : PC_OUT_WIRED, hw_cap < cap ? hw_cap : cap);
    pc_pipe_t p = pc_pipe_plan(raw, a.out_max, burst, a.limit_why);
    if (p.pipe > 48000 && !pc_i2s_clk_for(p.pipe, NULL)) { a.out_max = 48000; a.limit_why = PC_PIPE_CLOCK; }   /* no clock row: as a crystal-only DAC */
    return a;
}

uint32_t pc_hires_tier_down(uint32_t rate)
{
    uint32_t t = 48000;
    while (t * 2 < rate) t *= 2;                          /* 96k -> 48k, 192k -> 96k, 384k -> 192k */
    return t;
}

bool pc_hires_failure_lowers_cap(int err, bool readback_mismatch)
{
    return readback_mismatch || err == PC_ERR_INVALID_RESPONSE || err == PC_ERR_NOT_SUPPORTED || err == PC_ERR_INVALID_ARG;
}

pc_join_t pc_join_kind(uint32_t raw_a, pc_pipe_t plan_a, uint32_t raw_b, pc_pipe_t plan_b)
{
    if (raw_a == raw_b && plan_a.pipe == plan_b.pipe && plan_a.halvings == plan_b.halvings) return PC_JOIN_GAPLESS;
    return plan_a.pipe == plan_b.pipe ? PC_JOIN_SEAMLESS : PC_JOIN_RATE_CHANGE;
}

const char *pc_join_name(pc_join_t k)
{
    return k == PC_JOIN_GAPLESS ? "gapless" : k == PC_JOIN_SEAMLESS ? "seamless" : k == PC_JOIN_RATE_CHANGE ? "rate change" : "?";
}

uint32_t pc_sink_pipe_rate(uint32_t raw, uint32_t burst)
{
    if (!raw) return 0;
    pc_plan_args_t a = pc_plan_args(raw, burst, true, PC_AUDIO_MAX_RATE, PC_AUDIO_MAX_RATE);   /* a sink: the wired ceilings do not matter (48k) */
    return pc_pipe_plan(raw, a.out_max, burst, a.limit_why).pipe;
}

void pc_rsq_hints(bool on, uint32_t src_rate, uint32_t pipe_rate, uint32_t next_rate, uint32_t link, pc_rsq_pair_t *now, pc_rsq_pair_t *next)
{
    now->in = on && src_rate && pipe_rate <= 48000 ? pipe_rate : 0;   /* a sink plan keeps every pipeline rate up to 48k */
    now->out = link;
    next->in = on && next_rate ? pc_sink_pipe_rate(next_rate, 0) : 0;   /* burst 0 is exact for a sink (out_max 48k wins first) */
    next->out = link;
}

pc_block_out_t pc_block_route(bool sink, bool channel_up, uint32_t brate, uint32_t s_rate)
{
    if (sink) return PC_BLOCK_SINK;
    return channel_up && brate == s_rate ? PC_BLOCK_WRITE : PC_BLOCK_DROP_WAIT;
}

uint32_t pc_hw_cap_after_cap_change(uint32_t old_cap, uint32_t new_cap, uint32_t hw_cap)
{
    return new_cap > old_cap ? PC_AUDIO_MAX_RATE : hw_cap;
}
