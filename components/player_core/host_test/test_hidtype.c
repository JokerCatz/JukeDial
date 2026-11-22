// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_hidtype.h"
#include "pc_test.h"

static void test_url(void)
{
    uint8_t k[64][2];
    int n = pc_hid_type("http://Ab9:/.-_?=&#%~\n", k, 64);
    CHECK_EQ_INT(n, 22);
    CHECK_EQ_INT(k[0][0], 0); CHECK_EQ_INT(k[0][1], 0x0B);          /* h */
    CHECK_EQ_INT(k[4][0], PC_HID_SHIFT); CHECK_EQ_INT(k[4][1], 0x33); /* : */
    CHECK_EQ_INT(k[5][1], 0x38);                                     /* / */
    CHECK_EQ_INT(k[7][0], PC_HID_SHIFT); CHECK_EQ_INT(k[7][1], 0x04); /* A */
    CHECK_EQ_INT(k[9][1], 0x26);                                     /* 9 */
    CHECK_EQ_INT(k[12][1], 0x37);                                    /* . */
    CHECK_EQ_INT(k[13][1], 0x2D);                                    /* - */
    CHECK_EQ_INT(k[14][0], PC_HID_SHIFT); CHECK_EQ_INT(k[14][1], 0x2D);   /* _ */
    CHECK_EQ_INT(k[15][0], PC_HID_SHIFT); CHECK_EQ_INT(k[15][1], 0x38);   /* ? */
    CHECK_EQ_INT(k[16][1], 0x2E);                                    /* = */
    CHECK_EQ_INT(k[17][0], PC_HID_SHIFT); CHECK_EQ_INT(k[17][1], 0x24);   /* & */
    CHECK_EQ_INT(k[18][0], PC_HID_SHIFT); CHECK_EQ_INT(k[18][1], 0x20);   /* # */
    CHECK_EQ_INT(k[19][0], PC_HID_SHIFT); CHECK_EQ_INT(k[19][1], 0x22);   /* % */
    CHECK_EQ_INT(k[20][0], PC_HID_SHIFT); CHECK_EQ_INT(k[20][1], 0x35);   /* ~ */
    CHECK_EQ_INT(k[21][1], PC_HID_ENTER);
}

static void test_refuses(void)
{
    uint8_t k[8][2];
    CHECK_EQ_INT(pc_hid_type("設定", k, 8), -1);                    /* not on a US keyboard */
    CHECK_EQ_INT(pc_hid_type("abcdefghij", k, 8), -1);               /* does not fit */
    CHECK_EQ_INT(pc_hid_type("", k, 8), 0);
}

int main(void)
{
    RUN(test_url);
    RUN(test_refuses);
    return 0;
}
