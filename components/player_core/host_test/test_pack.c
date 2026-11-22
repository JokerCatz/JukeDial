// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pack.h"
#include "pc_test.h"
#include <string.h>

static void test_parse(void)
{
    pc_pack_t p; const char *t = "pack=3\nversion=2026.11.18-abc1234\nabcd  _player/faces.zip\n";
    CHECK_EQ_INT(pc_pack_parse(t, strlen(t), &p), 0); CHECK_EQ_INT(p.rev, 3); CHECK_STR(p.version, "2026.11.18-abc1234");
    t = "version=x\r\npack=12\r\n";
    CHECK_EQ_INT(pc_pack_parse(t, strlen(t), &p), 0); CHECK_EQ_INT(p.rev, 12); CHECK_STR(p.version, "x");
    t = "pack=\nversion=y\n"; CHECK_EQ_INT(pc_pack_parse(t, strlen(t), &p), -1); CHECK_EQ_INT(p.rev, -1);
    CHECK_EQ_INT(pc_pack_parse("", 0, &p), -1);
    t = "pack=999999999999999999\n"; CHECK_EQ_INT(pc_pack_parse(t, strlen(t), &p), 0); CHECK_EQ_INT(p.rev >= 0, 1);   /* no overflow */
}

static void test_state(void)
{
    CHECK_EQ_INT(pc_pack_state(-1), PC_PACK_MISSING);
    CHECK_EQ_INT(pc_pack_state(PC_PACK_MIN), PC_PACK_OK);
    CHECK_EQ_INT(pc_pack_state(PC_PACK_MIN + 5), PC_PACK_OK);
    CHECK_EQ_INT(pc_pack_state(PC_PACK_MIN - 1), PC_PACK_OLD);
}

int main(void) { RUN(test_parse); RUN(test_state); return 0; }
