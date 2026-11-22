// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_fmpatch.h"

static void test_tfi_round_trip(void)
{
    for (int i = 0; i < PC_FM_BANK_N; i++) {
        uint8_t b[PC_FM_TFI_SIZE];
        pc_fm_patch_t p;
        pc_fm_tfi_write(&pc_fm_bank[i], b);
        CHECK_EQ_INT(pc_fm_tfi_parse(b, sizeof b, &p), 0);
        CHECK_EQ_INT(p.alg, pc_fm_bank[i].alg);
        CHECK_EQ_INT(p.fb, pc_fm_bank[i].fb);
        CHECK(!memcmp(p.op, pc_fm_bank[i].op, sizeof p.op));
        CHECK(pc_fm_bank[i].name[0]);
    }
}

static void test_tfi_layout(void)
{
    uint8_t b[PC_FM_TFI_SIZE] = { 4, 6 };
    for (int k = 0; k < 4; k++) b[2 + k * 10] = (uint8_t)(k + 1);   /* MUL = file slot + 1 */
    b[2 + 1] = 0;                                                    /* file slot 0 DT: -3 */
    b[2 + 2] = 99;                                                   /* TL 99 */
    b[2 + 3 * 10 + 4] = 200;                                         /* last slot AR: clamped */
    pc_fm_patch_t p;
    CHECK_EQ_INT(pc_fm_tfi_parse(b, sizeof b, &p), 0);
    CHECK_EQ_INT(p.alg, 4);
    CHECK_EQ_INT(p.fb, 6);
    CHECK_EQ_INT(p.op[0].mul, 1);        /* file order OP1, OP3, OP2, OP4 */
    CHECK_EQ_INT(p.op[2].mul, 2);
    CHECK_EQ_INT(p.op[1].mul, 3);
    CHECK_EQ_INT(p.op[3].mul, 4);
    CHECK_EQ_INT(p.op[0].tl, 99);
    CHECK_EQ_INT(p.op[3].ar, 31);
    CHECK_EQ_INT(pc_fm_tfi_parse(b, 41, &p), -1);
    CHECK_EQ_INT(pc_fm_tfi_parse(b, 43, &p), -1);
    b[0] = 9;                                                        /* algorithm out of range */
    CHECK_EQ_INT(pc_fm_tfi_parse(b, sizeof b, &p), -1);
}

static void test_carriers(void)
{
    CHECK_EQ_INT(pc_fm_carriers(0), 0x8);
    CHECK_EQ_INT(pc_fm_carriers(3), 0x8);
    CHECK_EQ_INT(pc_fm_carriers(4), 0xA);
    CHECK_EQ_INT(pc_fm_carriers(5), 0xE);
    CHECK_EQ_INT(pc_fm_carriers(6), 0xE);
    CHECK_EQ_INT(pc_fm_carriers(7), 0xF);
    CHECK_EQ_INT(pc_fm_op_offset(0), 0);
    CHECK_EQ_INT(pc_fm_op_offset(1), 8);
    CHECK_EQ_INT(pc_fm_op_offset(2), 4);
    CHECK_EQ_INT(pc_fm_op_offset(3), 12);
}

static int find(const pc_fm_reg_t *r, int n, int reg)
{
    for (int i = 0; i < n; i++) if (r[i].reg == reg) return r[i].val;
    return -1;
}

static void test_regs(void)
{
    pc_fm_patch_t p = { "t", 4, 5, {
        { 1, 3, 30, 1, 31, 5, 2, 7, 3, 0 },     /* OP1 modulator */
        { 2, 2, 10, 2, 28, 6, 1, 8, 2, 0 },     /* OP2 carrier, DT -1 */
        { 3, 6, 40, 0, 20, 4, 0, 6, 1, 9 },     /* OP3 modulator, DT +3, SSG */
        { 4, 0, 120, 3, 31, 3, 3, 5, 4, 0 },    /* OP4 carrier, DT -3 */
    } };
    pc_fm_reg_t r[PC_FM_REGS];
    int n = pc_fm_patch_regs(&p, 127, r);
    CHECK_EQ_INT(n, 30);
    CHECK_EQ_INT(find(r, n, 0x30), 0x01);            /* OP1: DT 0, MUL 1 */
    CHECK_EQ_INT(find(r, n, 0x38), 0x52);            /* OP2 at +8: DT -1 = 5 */
    CHECK_EQ_INT(find(r, n, 0x34), 0x33);            /* OP3 at +4: DT +3 */
    CHECK_EQ_INT(find(r, n, 0x3C), 0x74);            /* OP4 at +12: DT -3 = 7 */
    CHECK_EQ_INT(find(r, n, 0x40), 30);
    CHECK_EQ_INT(find(r, n, 0x48), 10);
    CHECK_EQ_INT(find(r, n, 0x58), 0x80 | 28);       /* RS 2, AR 28 */
    CHECK_EQ_INT(find(r, n, 0x68), 6);
    CHECK_EQ_INT(find(r, n, 0x78), 1);
    CHECK_EQ_INT(find(r, n, 0x88), 0x28);            /* SL 2, RR 8 */
    CHECK_EQ_INT(find(r, n, 0x94), 9);
    CHECK_EQ_INT(find(r, n, 0xB0), (5 << 3) | 4);
    CHECK_EQ_INT(find(r, n, 0xB4), 0xC0);
    /* velocity: carriers only, clamped */
    n = pc_fm_patch_regs(&p, 1, r);
    int a = pc_fm_vel_att(1);
    CHECK(a > 10 && a <= 40);
    CHECK_EQ_INT(find(r, n, 0x40), 30);
    CHECK_EQ_INT(find(r, n, 0x44), 40);
    CHECK_EQ_INT(find(r, n, 0x48), 10 + a);
    CHECK_EQ_INT(find(r, n, 0x4C), 127);
    pc_fm_reg_t t[4];
    CHECK_EQ_INT(pc_fm_tl_regs(&p, 64, t), 2);
    CHECK_EQ_INT(find(t, 2, 0x48), 10 + pc_fm_vel_att(64));
    CHECK_EQ_INT(pc_fm_vel_att(127), 0);
    for (int v = 1; v < 127; v++) CHECK(pc_fm_vel_att(v) >= pc_fm_vel_att(v + 1));
}

static void test_fnum(void)
{
    for (int note = 12; note < 120; note++) {
        float hz = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        int b, f;
        pc_fm_fnum(hz, &b, &f);
        CHECK(b >= 0 && b <= 7);
        CHECK(f > 0 && f < 2048);
        double back = f * pow(2.0, b - 1) * 7670453.0 / (144.0 * 1048576.0);
        if (note >= 24 && note < 108) {
            CHECK(f >= 600 || b == 0);
            CHECK_NEAR(back / hz, 1.0, 0.002);
        }
    }
    int b, f;
    pc_fm_fnum(20000.0f, &b, &f);                    /* out of range: clamped, never wraps */
    CHECK_EQ_INT(b, 7);
    CHECK_EQ_INT(f, 2047);
}

static void test_gm_and_drums(void)
{
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(0)].name, "E.Piano");
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(33)].name, "Bass");
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(19)].name, "Organ");
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(48)].name, "Strings");
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(61)].name, "Brass");
    CHECK_STR(pc_fm_bank[pc_fm_gm_patch(73)].name, "Flute");
    for (int p = 0; p < 128; p++) CHECK(pc_fm_gm_patch(p) >= 0 && pc_fm_gm_patch(p) < PC_FM_BANK_N);
    int n;
    const pc_fm_patch_t *k = pc_fm_drum(36, &n), *s = pc_fm_drum(38, &n), *h = pc_fm_drum(42, &n);
    CHECK(k && s && h && k != s && s != h);
    CHECK_STR(k->name, "Kick");
    CHECK_STR(s->name, "Snare");
    CHECK_STR(h->name, "Hat");
    pc_fm_drum(42, &n);
    int n2;
    pc_fm_drum(46, &n2);
    CHECK(n > 60 && n2 > 60);
    pc_fm_drum(41, &n);
    pc_fm_drum(50, &n2);
    CHECK(n2 > n);                                   /* toms keep their pitch order */
}

int main(void)
{
    RUN(test_tfi_round_trip);
    RUN(test_tfi_layout);
    RUN(test_carriers);
    RUN(test_regs);
    RUN(test_fnum);
    RUN(test_gm_and_drums);
    return 0;
}
