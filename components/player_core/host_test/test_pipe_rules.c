// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pipe_rules.h"
#include "pc_test.h"

/* hires T5b: the audio task's decisions (audio_svc.c) and the simulator's fake take these; built with PC_SRC_MAX_RATE=384000 */

static pc_pipe_t plan(uint32_t raw, uint32_t burst, bool sink, uint32_t cap, uint32_t hw_cap)   /* as audio_svc.c plan_of / the fake */
{
    pc_plan_args_t a = pc_plan_args(raw, burst, sink, cap, hw_cap);
    return pc_pipe_plan(raw, a.out_max, burst, a.limit_why);
}
#define CHECK_PLAN(p, P, H, W) do { pc_pipe_t pp_ = (p); CHECK_EQ_INT(pp_.pipe, P); CHECK_EQ_INT(pp_.halvings, H); CHECK_EQ_INT(pp_.why, W); } while (0)
#define CHECK_ARGS(a, OM, W) do { pc_plan_args_t pa_ = (a); CHECK_EQ_INT(pa_.out_max, OM); CHECK_EQ_INT(pa_.limit_why, W); } while (0)

/* out_max = pc_out_max_rate(sink ? SINK : WIRED, min(cap, hw_cap)); why = sink ? SINK : hw_cap < cap ? CLOCK : CAP */
static void test_plan_args(void)
{
    CHECK_ARGS(pc_plan_args(96000, 0, false, 96000, 96000), 96000, PC_PIPE_CAP);
    CHECK_ARGS(pc_plan_args(44100, 0, false, 96000, 96000), 96000, PC_PIPE_CAP);
    CHECK_ARGS(pc_plan_args(96000, 0, false, 96000, 48000), 48000, PC_PIPE_CLOCK);   /* this boot's clock was refused */
    CHECK_ARGS(pc_plan_args(96000, 0, false, 48000, 96000), 48000, PC_PIPE_CAP);     /* the user's ceiling (T6) */
    CHECK_ARGS(pc_plan_args(96000, 0, false, 48000, 48000), 48000, PC_PIPE_CAP);     /* hw_cap == cap: the cap */
    CHECK_ARGS(pc_plan_args(192000, 0, false, 192000, 192000), 192000, PC_PIPE_CAP); /* hires T12: 192k native on the APLL */
    CHECK_ARGS(pc_plan_args(384000, 0, false, 384000, 384000), 384000, PC_PIPE_CAP); /* hires T13: 384k native */
    CHECK_ARGS(pc_plan_args(768000, 0, false, 768000, 768000), 384000, PC_PIPE_CAP); /* the wire never above PC_AUDIO_MAX_RATE */
    CHECK_ARGS(pc_plan_args(96000, 0, true, 96000, 96000), 48000, PC_PIPE_SINK);
    CHECK_ARGS(pc_plan_args(96000, 0, true, 96000, 48000), 48000, PC_PIPE_SINK);     /* a sink: its reason wins over the clock */
    CHECK_PLAN(plan(96000, 0, false, 96000, 96000), 96000, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(88200, 0, false, 96000, 96000), 88200, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(192000, 0, false, 96000, 96000), 96000, 1, PC_PIPE_CAP);
    CHECK_PLAN(plan(192000, 0, false, 192000, 192000), 192000, 0, PC_PIPE_NATIVE);  /* hires T12 */
    CHECK_PLAN(plan(192000, 16384, false, 192000, 192000), 192000, 0, PC_PIPE_NATIVE);   /* the burst limit is inclusive */
    CHECK_PLAN(plan(192000, 32768, false, 192000, 192000), 96000, 1, PC_PIPE_BURST);     /* a block too large for the DMA timing: halved, with the reason */
    CHECK_PLAN(plan(176400, 0, false, 192000, 192000), 176400, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(192000, 0, false, 192000, 96000), 96000, 1, PC_PIPE_CLOCK);          /* the APLL clock failed this boot: one tier down */
    CHECK_PLAN(plan(384000, 0, false, 96000, 96000), 96000, 2, PC_PIPE_CAP);
    CHECK_PLAN(plan(384000, 4096, false, 384000, 384000), 384000, 0, PC_PIPE_NATIVE);   /* hires T13: the burst limit is inclusive */
    CHECK_PLAN(plan(384000, 8192, false, 384000, 384000), 384000, 0, PC_PIPE_NATIVE);   /* bigframe: the reservoir takes it */
    CHECK_PLAN(plan(384000, 32768, false, 384000, 384000), 384000, 0, PC_PIPE_NATIVE);  /* ffmpeg-default FLAC frame, inclusive limit */
    CHECK_PLAN(plan(384000, 32769, false, 384000, 384000), 96000, 2, PC_PIPE_BURST);   /* over the limit: halved to 192k, whose 16384 limit halves again */
    CHECK_PLAN(plan(352800, 0, false, 384000, 384000), 352800, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(352800, 32768, false, 384000, 384000), 352800, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(352800, 65536, false, 384000, 384000), 88200, 2, PC_PIPE_BURST);
    CHECK_PLAN(plan(352800, 32768, true, 384000, 384000), 44100, 3, PC_PIPE_SINK);       /* a Bluetooth sink: out_max 48k wins first */
    CHECK_PLAN(plan(352800, 0, true, 384000, 384000), 44100, 3, PC_PIPE_SINK);
    CHECK_PLAN(plan(384000, 16384, false, 384000, 384000), 384000, 0, PC_PIPE_NATIVE);
    CHECK_PLAN(plan(384000, 0, false, 192000, 192000), 192000, 1, PC_PIPE_CAP);
    CHECK_PLAN(plan(384000, 0, true, 384000, 384000), 48000, 3, PC_PIPE_SINK);          /* a Bluetooth sink: three halvings */
    CHECK_PLAN(plan(96000, 0, false, 96000, 48000), 48000, 1, PC_PIPE_CLOCK);       /* hires off: the source is halved */
    CHECK_PLAN(plan(176400, 20000, false, 96000, 96000), 88200, 1, PC_PIPE_CAP);    /* the cap halves first; the burst does not matter at 88.2k */
}

/* the no-row rule: a wired pipeline above 48k without a clock row plans as for a crystal-only DAC (48k, why clock) */
static void test_plan_args_no_row(void)
{
    CHECK_ARGS(pc_plan_args(64000, 0, false, 96000, 96000), 48000, PC_PIPE_CLOCK);
    CHECK_PLAN(plan(64000, 0, false, 96000, 96000), 32000, 1, PC_PIPE_CLOCK);
    CHECK_ARGS(pc_plan_args(128000, 0, false, 96000, 96000), 48000, PC_PIPE_CLOCK);  /* 128k -> 64k (cap) has no row either */
    CHECK_PLAN(plan(128000, 0, false, 96000, 96000), 32000, 2, PC_PIPE_CLOCK);
    CHECK_ARGS(pc_plan_args(50000, 0, false, 96000, 96000), 48000, PC_PIPE_CLOCK);
    CHECK_PLAN(plan(50000, 0, false, 96000, 96000), 25000, 1, PC_PIPE_CLOCK);
    CHECK_EQ_INT(plan(50001, 0, false, 96000, 96000).pipe, 0);                     /* odd above 48k: refused by the plan */
    CHECK_ARGS(pc_plan_args(64000, 0, true, 96000, 96000), 48000, PC_PIPE_SINK);     /* a sink takes 32k from 48k: no row needed */
    CHECK_PLAN(plan(64000, 0, true, 96000, 96000), 32000, 1, PC_PIPE_SINK);
    CHECK_ARGS(pc_plan_args(48000, 0, false, 96000, 96000), 96000, PC_PIPE_CAP);     /* <= 48k: the crystal rows */
    CHECK_ARGS(pc_plan_args(32000, 0, false, 96000, 96000), 96000, PC_PIPE_CAP);
}

/* review mutation 2: a missing re-plan on a sink change. With a sink the plan is at most 48k whatever the ceilings say */
static void test_plan_sink_replan(void)
{
    const uint32_t caps[] = { 48000, 96000, 192000, 384000 };
    const uint32_t raws[] = { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    for (unsigned c = 0; c < sizeof caps / sizeof *caps; c++)
        for (unsigned r = 0; r < sizeof raws / sizeof *raws; r++) {
            CHECK_ARGS(pc_plan_args(raws[r], 0, true, caps[c], caps[c]), 48000, PC_PIPE_SINK);
            pc_pipe_t p = plan(raws[r], 0, true, caps[c], caps[c]);
            CHECK(p.pipe > 0 && p.pipe <= 48000);
        }
    CHECK_PLAN(plan(96000, 0, true, 96000, 96000), 48000, 1, PC_PIPE_SINK);
    CHECK_PLAN(plan(192000, 0, true, 96000, 96000), 48000, 2, PC_PIPE_SINK);
    CHECK_PLAN(plan(96000, 0, false, 96000, 96000), 96000, 0, PC_PIPE_NATIVE);    /* and back on the wire */
}

static void test_hw_cap_rearm(void)
{
    CHECK_EQ_INT(pc_hw_cap_after_cap_change(48000, 96000, 48000), PC_AUDIO_MAX_RATE);   /* raised: the DAC is tried again */
    CHECK_EQ_INT(pc_hw_cap_after_cap_change(48000, 96000, 96000), PC_AUDIO_MAX_RATE);
    CHECK_EQ_INT(pc_hw_cap_after_cap_change(96000, 48000, 48000), 48000);              /* lowered: unchanged */
    CHECK_EQ_INT(pc_hw_cap_after_cap_change(96000, 48000, 96000), 96000);
    CHECK_EQ_INT(pc_hw_cap_after_cap_change(96000, 96000, 48000), 48000);              /* same: unchanged (the failure stands) */
}

static void test_tier_down(void)
{
    CHECK_EQ_INT(pc_hires_tier_down(96000), 48000);
    CHECK_EQ_INT(pc_hires_tier_down(88200), 48000);
    CHECK_EQ_INT(pc_hires_tier_down(192000), 96000);
    CHECK_EQ_INT(pc_hires_tier_down(176400), 96000);
    CHECK_EQ_INT(pc_hires_tier_down(384000), 192000);
    CHECK_EQ_INT(pc_hires_tier_down(48000), 48000);
    CHECK_EQ_INT(pc_hires_tier_down(44100), 48000);   /* never below 48000 */
    CHECK_EQ_INT(pc_hires_tier_down(0), 48000);
}

/* the fix-round M4 rule: only clock-type failures lower the ceiling */
static void test_failure_lowers_cap(void)
{
    CHECK(pc_hires_failure_lowers_cap(PC_ERR_NOT_SUPPORTED, false));
    CHECK(pc_hires_failure_lowers_cap(PC_ERR_INVALID_ARG, false));
    CHECK(pc_hires_failure_lowers_cap(PC_ERR_INVALID_RESPONSE, true));      /* the read-back mismatch */
    CHECK(pc_hires_failure_lowers_cap(0, true));
    CHECK(pc_hires_failure_lowers_cap(PC_ERR_INVALID_RESPONSE, false));     /* the read-back code alone: a caller that forgets the flag still lowers it */
    CHECK(!pc_hires_failure_lowers_cap(PC_ERR_NO_MEM, false));
    CHECK(!pc_hires_failure_lowers_cap(-1, false));                         /* ESP_FAIL: a disable / enable error */
    CHECK(!pc_hires_failure_lowers_cap(0x103, false));                      /* ESP_ERR_INVALID_STATE */
    CHECK(!pc_hires_failure_lowers_cap(0x107, false));                      /* ESP_ERR_TIMEOUT */
    CHECK(!pc_hires_failure_lowers_cap(0, false));
}

static void test_join_kind(void)
{
    pc_pipe_t w96 = plan(96000, 0, false, 96000, 96000), w48 = plan(48000, 0, false, 96000, 96000), w441 = plan(44100, 0, false, 96000, 96000);
    pc_pipe_t s96 = plan(96000, 0, true, 96000, 96000), s48 = plan(48000, 0, true, 96000, 96000);
    pc_pipe_t w192 = plan(192000, 0, false, 96000, 96000);
    CHECK_EQ_INT(pc_join_kind(96000, w96, 96000, w96), PC_JOIN_GAPLESS);          /* 96k -> 96k, the same plan */
    CHECK_EQ_INT(pc_join_kind(96000, s96, 48000, s48), PC_JOIN_SEAMLESS);         /* 96k -> 48k with a sink: both 48k */
    CHECK_EQ_INT(pc_join_kind(192000, w192, 96000, w96), PC_JOIN_SEAMLESS);       /* 192k -> 96k wired: both 96k */
    CHECK_EQ_INT(pc_join_kind(44100, w441, 96000, w96), PC_JOIN_RATE_CHANGE);     /* 44.1k -> 96k wired */
    CHECK_EQ_INT(pc_join_kind(96000, w96, 48000, w48), PC_JOIN_RATE_CHANGE);
    pc_pipe_t nat = { 96000, 0, PC_PIPE_NATIVE }, clk = plan(96000, 0, false, 96000, 48000);
    CHECK_EQ_INT(pc_join_kind(96000, w96, 96000, clk), PC_JOIN_RATE_CHANGE);      /* same raw, the ceiling dropped in between: another plan */
    CHECK_EQ_INT(pc_join_kind(96000, s96, 96000, s96), PC_JOIN_GAPLESS);
    CHECK_EQ_INT(pc_join_kind(96000, nat, 96000, (pc_pipe_t){ 96000, 0, PC_PIPE_CAP }), PC_JOIN_GAPLESS);   /* the reason word is not part of the plan */
    CHECK_STR(pc_join_name(PC_JOIN_GAPLESS), "gapless");
    CHECK_STR(pc_join_name(PC_JOIN_SEAMLESS), "seamless");
    CHECK_STR(pc_join_name(PC_JOIN_RATE_CHANGE), "rate change");
    CHECK_STR(pc_join_name((pc_join_t)7), "?");
}

/* the write-or-drop rule (spec 4.5) and the review's mutations 1 and 3 */
static void test_block_route(void)
{
    CHECK_EQ_INT(pc_block_route(false, true, 96000, 96000), PC_BLOCK_WRITE);
    CHECK_EQ_INT(pc_block_route(false, true, 44100, 44100), PC_BLOCK_WRITE);
    CHECK_EQ_INT(pc_block_route(true, true, 96000, 96000), PC_BLOCK_SINK);
    CHECK_EQ_INT(pc_block_route(true, false, 96000, 48000), PC_BLOCK_SINK);       /* a sink takes any rate (the block says it) */
    CHECK_EQ_INT(pc_block_route(false, false, 96000, 96000), PC_BLOCK_DROP_WAIT); /* no channel (released, or a failed switch released it: m1) */
    CHECK_EQ_INT(pc_block_route(false, true, 48000, 96000), PC_BLOCK_DROP_WAIT);  /* the sink cleared: a 48k block, the wire re-planned to 96k */
    /* mutation 1, a skipped halving: the plan says 192k -> 96k (1 halving), a block made at the source rate is never written */
    pc_pipe_t p = plan(192000, 0, false, 96000, 96000);
    CHECK_PLAN(p, 96000, 1, PC_PIPE_CAP);
    CHECK_EQ_INT(pc_block_route(false, true, 192000, p.pipe), PC_BLOCK_DROP_WAIT);
    CHECK_EQ_INT(pc_block_route(false, true, p.pipe, p.pipe), PC_BLOCK_WRITE);
    /* mutation 3, a block before the rate switch: made at the old 44.1k while the channel was switched to 96k */
    CHECK_EQ_INT(pc_block_route(false, true, 44100, 96000), PC_BLOCK_DROP_WAIT);
    CHECK_EQ_INT(pc_block_route(false, true, 96000, 44100), PC_BLOCK_DROP_WAIT);
    /* m1: a failed switch at <= 48k releases the channel (s_rate = the new rate): nothing is written until the reinit at that rate */
    CHECK_EQ_INT(pc_block_route(false, false, 44100, 44100), PC_BLOCK_DROP_WAIT);
}

/* hires T10b fix 1: the queued next track's rate as the sink plans it (the Bluetooth resampler's look-ahead input rate) */
static void test_sink_pipe_rate(void)
{
    CHECK_EQ_INT(pc_sink_pipe_rate(44100, 0), 44100);
    CHECK_EQ_INT(pc_sink_pipe_rate(48000, 0), 48000);
    CHECK_EQ_INT(pc_sink_pipe_rate(32000, 0), 32000);
    CHECK_EQ_INT(pc_sink_pipe_rate(88200, 0), 44100);      /* halved for the sink (48k at most) */
    CHECK_EQ_INT(pc_sink_pipe_rate(96000, 0), 48000);
    CHECK_EQ_INT(pc_sink_pipe_rate(192000, 0), 48000);
    CHECK_EQ_INT(pc_sink_pipe_rate(384000, 0), 48000);
    CHECK_EQ_INT(pc_sink_pipe_rate(0, 0), 0);               /* none queued */
    CHECK_EQ_INT(pc_sink_pipe_rate(768000, 0), 0);          /* above PC_SRC_MAX_RATE: refused, no table */
    for (uint32_t r = 8000; r <= 384000; r += 100)          /* the same as the audio task's sink plan (pc_plan_args + pc_pipe_plan) */
        CHECK_EQ_INT(pc_sink_pipe_rate(r, 0), plan(r, 0, true, PC_AUDIO_MAX_RATE, PC_AUDIO_MAX_RATE).pipe);
}

/* fix 2: the builder's hints (bt_svc.c rsq_build_task and the simulator's model share them) */
static void test_rsq_hints(void)
{
    pc_rsq_pair_t now, next;
    pc_rsq_hints(true, 44100, 44100, 96000, 48000, &now, &next);
    CHECK_EQ_INT(now.in, 44100); CHECK_EQ_INT(now.out, 48000);
    CHECK_EQ_INT(next.in, 48000); CHECK_EQ_INT(next.out, 48000);      /* a 96k next track: halved for the sink */
    pc_rsq_hints(true, 96000, 96000, 0, 48000, &now, &next);           /* the plan before the sink re-plan: not a sink rate yet */
    CHECK_EQ_INT(now.in, 0); CHECK_EQ_INT(next.in, 0);
    pc_rsq_hints(true, 0, 44100, 0, 48000, &now, &next);               /* no source loaded */
    CHECK_EQ_INT(now.in, 0);
    pc_rsq_hints(false, 44100, 44100, 32000, 48000, &now, &next);      /* no sink: nothing */
    CHECK_EQ_INT(now.in, 0); CHECK_EQ_INT(next.in, 0);
    pc_rsq_hints(true, 48000, 48000, 88200, 44100, &now, &next);
    CHECK_EQ_INT(now.in, 48000); CHECK_EQ_INT(now.out, 44100);
    CHECK_EQ_INT(next.in, 44100); CHECK_EQ_INT(next.out, 44100);
}

int main(void)
{
    RUN(test_sink_pipe_rate);
    RUN(test_rsq_hints);
    RUN(test_plan_args);
    RUN(test_plan_args_no_row);
    RUN(test_plan_sink_replan);
    RUN(test_hw_cap_rearm);
    RUN(test_tier_down);
    RUN(test_failure_lowers_cap);
    RUN(test_join_kind);
    RUN(test_block_route);
    return 0;
}
