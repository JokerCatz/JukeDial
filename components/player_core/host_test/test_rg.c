// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rg.h"
#include "pc_test.h"

static void test_rg_parse(void)
{
    int16_t g;
    uint16_t p;
    CHECK_EQ_INT(pc_rg_parse_gain("-6.53 dB", &g), 0); CHECK_EQ_INT(g, -653);
    CHECK_EQ_INT(pc_rg_parse_gain(" +1.2dB", &g), 0); CHECK_EQ_INT(g, 120);
    CHECK_EQ_INT(pc_rg_parse_gain("0.00 dB", &g), 0); CHECK_EQ_INT(g, 0);
    CHECK_EQ_INT(pc_rg_parse_gain("-0.006 dB", &g), 0); CHECK_EQ_INT(g, -1);
    CHECK_EQ_INT(pc_rg_parse_gain("-12", &g), 0); CHECK_EQ_INT(g, -1200);
    CHECK(pc_rg_parse_gain("loud", &g) < 0);
    CHECK(pc_rg_parse_gain("", &g) < 0);
    CHECK(pc_rg_parse_gain("-99 dB", &g) < 0);         /* beyond +-60 dB: a broken tag */
    CHECK(pc_rg_parse_gain("nan", &g) < 0);
    CHECK_EQ_INT(pc_rg_parse_peak("0.988739", &p), 0); CHECK_EQ_INT(p, 9887);
    CHECK_EQ_INT(pc_rg_parse_peak("1.5", &p), 0); CHECK_EQ_INT(p, 15000);
    CHECK(pc_rg_parse_peak("0", &p) < 0);                /* 0 means unknown */
    CHECK(pc_rg_parse_peak("-1", &p) < 0);
    CHECK(pc_rg_parse_peak("x", &p) < 0);
    CHECK(pc_rg_parse_peak("9", &p) < 0);                /* more than 6.5: not a sample peak */
}

static pc_rg_t rg(int16_t t, uint16_t tp, int16_t a, uint16_t ap) { pc_rg_t r = { t, a, tp, ap }; return r; }

static void test_rg_gain(void)
{
    float db;
    pc_rg_t none = rg(PC_RG_NONE, 0, PC_RG_NONE, 0);
    CHECK_NEAR(pc_rg_linear(&none, PC_RG_OFF, 0, &db), 1.0, 1e-6);
    CHECK_NEAR(db, 0, 1e-6);
    pc_rg_t r = rg(-602, 5000, -300, 8000);
    CHECK_NEAR(pc_rg_linear(&r, PC_RG_OFF, 0, &db), 1.0, 1e-6);           /* off: tags ignored */
    CHECK_NEAR(pc_rg_linear(&r, PC_RG_TRACK, 0, &db), 0.5, 1e-3);
    CHECK_NEAR(db, -6.02, 1e-4);
    CHECK_NEAR(pc_rg_linear(&r, PC_RG_ALBUM, 0, &db), 0.7079, 1e-3);
    CHECK_NEAR(db, -3.0, 1e-4);
    /* untagged: the default pre-amp */
    CHECK_NEAR(pc_rg_linear(&none, PC_RG_TRACK, 0, &db), 1.0, 1e-6);
    CHECK_NEAR(pc_rg_linear(&none, PC_RG_TRACK, -3.0f, &db), 0.7079, 1e-3);
    CHECK_NEAR(db, -3.0, 1e-4);
    /* a missing kind falls back to the other one */
    pc_rg_t ta = rg(PC_RG_NONE, 0, -400, 0), tt = rg(-500, 0, PC_RG_NONE, 0);
    CHECK_NEAR(pc_rg_linear(&ta, PC_RG_TRACK, 0, &db), 0.631, 1e-3);
    CHECK_NEAR(pc_rg_linear(&tt, PC_RG_ALBUM, 0, &db), 0.562, 1e-3);
    /* clip prevention: gain * peak <= 1 */
    pc_rg_t loud = rg(600, 9000, 600, 6000);
    float g = pc_rg_linear(&loud, PC_RG_TRACK, 0, &db);
    CHECK_NEAR(g, 1.0 / 0.9, 1e-4);
    CHECK_NEAR(db, 20 * log10(1.0 / 0.9), 1e-3);
    CHECK_NEAR(pc_rg_linear(&loud, PC_RG_ALBUM, 0, &db), 1.0 / 0.6, 1e-4);
    pc_rg_t hot = rg(-100, 15000, PC_RG_NONE, 0);                          /* a peak above full scale caps even a cut */
    CHECK_NEAR(pc_rg_linear(&hot, PC_RG_TRACK, 0, &db), 1.0 / 1.5, 1e-4);
    pc_rg_t nopeak = rg(600, 0, PC_RG_NONE, 0);                           /* peak unknown: no cap */
    CHECK_NEAR(pc_rg_linear(&nopeak, PC_RG_TRACK, 0, &db), 1.995, 1e-3);
    /* the album peak is used with the album gain, the track peak with the track gain */
    pc_rg_t mix = rg(300, 9000, 300, 0);
    CHECK_NEAR(pc_rg_linear(&mix, PC_RG_ALBUM, 0, &db), 1.4125, 1e-3);
}

int main(void)
{
    RUN(test_rg_parse);
    RUN(test_rg_gain);
    return 0;
}
