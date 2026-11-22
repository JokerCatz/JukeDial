// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rate.h"
#include "pc_test.h"

static void chk(uint32_t src, uint32_t out_max, uint32_t burst, uint8_t why, uint32_t pipe, int halv, int rwhy)
{
    pc_pipe_t p = pc_pipe_plan(src, out_max, burst, why);
    CHECK_EQ_INT(p.pipe, pipe);
    if (pipe) { CHECK_EQ_INT(p.halvings, halv); CHECK_EQ_INT(p.why, rwhy); }
}

static void test_pipe_plan(void)
{
    chk(96000, 96000, 4096, PC_PIPE_CAP, 96000, 0, PC_PIPE_NATIVE);
    chk(96000, 48000, 0, PC_PIPE_SINK, 48000, 1, PC_PIPE_SINK);
    chk(192000, 96000, 4096, PC_PIPE_CAP, 96000, 1, PC_PIPE_CAP);
    chk(192000, 192000, 16384, PC_PIPE_CAP, 192000, 0, PC_PIPE_NATIVE);
    chk(192000, 192000, 16385, PC_PIPE_CAP, 96000, 1, PC_PIPE_BURST);
    chk(384000, 384000, 4096, PC_PIPE_CAP, 384000, 0, PC_PIPE_NATIVE);
    chk(384000, 384000, 32768, PC_PIPE_CAP, 384000, 0, PC_PIPE_NATIVE);
    chk(384000, 384000, 32769, PC_PIPE_CAP, 96000, 2, PC_PIPE_BURST);
    chk(352800, 48000, 0, PC_PIPE_SINK, 44100, 3, PC_PIPE_SINK);
    chk(384000, 44100, 0, PC_PIPE_SINK, 0, 0, 0);
    chk(768000, 384000, 0, PC_PIPE_CAP, 0, 0, 0);
    chk(100001, 48000, 0, PC_PIPE_CAP, 0, 0, 0);
    chk(0, 48000, 0, PC_PIPE_CAP, 0, 0, 0);
    chk(44100, 48000, 0, PC_PIPE_CAP, 44100, 0, PC_PIPE_NATIVE);
    chk(384000, 192000, 32768, PC_PIPE_CAP, 96000, 2, PC_PIPE_BURST);
}

int main(void)
{
    RUN(test_pipe_plan);
    return 0;
}
