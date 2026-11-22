// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_exthid.h"
#include "pc_test.h"

/* the map is well formed: short items only, collections balance, the report ids come 1, 2, 3, and the Input bits of each report add up to the payload size constants */
static void test_map(void)
{
    int depth = 0, rid = 0, order = 0, rsize = 0, rcount = 0, in_bits[4] = { 0 };
    for (int i = 0; i < pc_exthid_map_len;) {
        uint8_t b = pc_exthid_map[i];
        int n = b & 3; n = n == 3 ? 4 : n;
        int tag = b & 0xFC;
        uint32_t v = 0;
        for (int k = 0; k < n; k++) v |= (uint32_t)pc_exthid_map[i + 1 + k] << (8 * k);
        CHECK(i + 1 + n <= pc_exthid_map_len);
        CHECK(b != 0xFE);                                                       /* no long items */
        if (tag == 0xA0) depth++;
        else if (tag == 0xC0) { CHECK(depth > 0); depth--; }
        else if (tag == 0x84) { rid = (int)v; CHECK_EQ_INT(rid, ++order); }
        else if (tag == 0x74) rsize = (int)v;
        else if (tag == 0x94) rcount = (int)v;
        else if (tag == 0x80) in_bits[rid] += rsize * rcount;                   /* Input */
        i += 1 + n;
    }
    CHECK_EQ_INT(depth, 0);
    CHECK_EQ_INT(order, 3);
    CHECK_EQ_INT(in_bits[1], PC_EXTHID_LEN_CONSUMER * 8);
    CHECK_EQ_INT(in_bits[2], PC_EXTHID_LEN_KEYBOARD * 8);
    CHECK_EQ_INT(in_bits[3], PC_EXTHID_LEN_MOUSE * 8);
}
int main(void) { RUN(test_map); return 0; }
