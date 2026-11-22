// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fmtreason.h"
#include "pc_test.h"

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void vgm_head(uint8_t *h, uint32_t ver, uint32_t data_off)
{
    memset(h, 0, 0x100);
    memcpy(h, "Vgm ", 4);
    put32(h + 8, ver);
    put32(h + 0x0C, 3579545);        /* SN76489 */
    put32(h + 0x10, 3579545);        /* YM2413 */
    put32(h + 0x2C, 7670454);        /* YM2612 */
    put32(h + 0x34, data_off);
}

static void test_vgm(void)
{
    uint8_t h[0x100];
    char out[128];
    vgm_head(h, 0x171, 0xCC);                                  /* header 0x100 bytes: all fields exist */
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), 0);
    put32(h + 0x30, 3579545);                                  /* YM2151 */
    put32(h + 0x9C, 1789773 | 0x40000000u);                    /* K051649, dual bit set */
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), 2);
    CHECK_STR(out, "YM2151, K051649");
    put32(h + 0x9C, 0x40000000u);                              /* only the dual bit: not a clock */
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), 1);
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, 6), 1);   /* a short buffer truncates, never overruns */
    CHECK_EQ_INT((int)strlen(out), 5);
    vgm_head(h, 0x150, 0x0C);                                  /* data at 0x40: a 1.50 header; bytes past it are data, not clocks */
    put32(h + 0x9C, 1789773);
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), 0);
    vgm_head(h, 0x110, 0);
    put32(h + 0x30, 3579545);
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), 1);
    h[0] = 'X';
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, sizeof h, out, sizeof out), -1);
    CHECK_EQ_INT(pc_vgm_unsupported_chips(h, 8, out, sizeof out), -1);
}

static void test_s3m(void)
{
    uint8_t f[0x200];
    memset(f, 0, sizeof f);
    memcpy(f + 0x2C, "SCRM", 4);
    f[0x20] = 2; f[0x22] = 2;                                  /* 2 orders, 2 instruments */
    f[0x60 + 2] = 0x08; f[0x60 + 4] = 0x09;                    /* parapointers 8 and 9: file offsets 0x80 and 0x90 */
    f[0x80] = 1;                                               /* a sample */
    pc_reader_t r;
    pc_reader_mem(&r, f, sizeof f);
    CHECK_EQ_INT(pc_refuse_reason(&r, ".s3m", NULL, 0), PC_REFUSE_NONE);
    f[0x90] = 2;                                               /* an OPL voice */
    CHECK_EQ_INT(pc_refuse_reason(&r, ".S3M", NULL, 0), PC_REFUSE_OPL_MODULE);
    CHECK_EQ_INT(pc_refuse_reason(&r, ".mod", NULL, 0), PC_REFUSE_NONE);
}

static void test_aiff_tta_mpc(void)
{
    uint8_t f[64];
    char d[16];
    pc_reader_t r;
    memset(f, 0, sizeof f);
    memcpy(f, "FORM", 4);
    memcpy(f + 8, "AIFC", 4);
    memcpy(f + 12, "COMM", 4);
    f[19] = 38;
    memcpy(f + 20 + 18, "ima4", 4);
    pc_reader_mem(&r, f, sizeof f);
    CHECK_EQ_INT(pc_refuse_reason(&r, ".aifc", d, sizeof d), PC_REFUSE_AIFF_CODEC);
    CHECK_STR(d, "ima4");
    memcpy(f + 20 + 18, "sowt", 4);
    CHECK_EQ_INT(pc_refuse_reason(&r, ".aif", d, sizeof d), PC_REFUSE_NONE);
    memset(f, 0, sizeof f);
    memcpy(f, "TTA1", 4);
    f[4] = 2;
    CHECK_EQ_INT(pc_refuse_reason(&r, ".tta", NULL, 0), PC_REFUSE_TTA_ENCRYPTED);
    f[4] = 1;
    CHECK_EQ_INT(pc_refuse_reason(&r, ".tta", NULL, 0), PC_REFUSE_NONE);
    memcpy(f, "MP+", 3);
    f[3] = 0x05;
    CHECK_EQ_INT(pc_refuse_reason(&r, ".mpc", NULL, 0), PC_REFUSE_MPC_OLD);
    f[3] = 0x07;
    CHECK_EQ_INT(pc_refuse_reason(&r, ".mpc", NULL, 0), PC_REFUSE_NONE);
}

int main(void)
{
    RUN(test_vgm);
    RUN(test_s3m);
    RUN(test_aiff_tta_mpc);
    return 0;
}
