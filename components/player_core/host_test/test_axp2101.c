// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_axp2101.h"
#include "pc_test.h"

static void test_no_battery(void)
{
    pc_axp_t a; pc_axp_decode(&a, 0x20, 0x00, 0x10, 0x00, 0x55);
    CHECK(a.vbus); CHECK(!a.batt); CHECK_EQ_INT(a.pct, -1); CHECK_EQ_INT(a.vbat_mv, 0);          /* the gauge byte is ignored without a cell */
    CHECK_EQ_INT(a.dir, PC_AXP_STANDBY);
}

static void test_charging_and_discharging(void)
{
    pc_axp_t a; pc_axp_decode(&a, 0x28, 0x23, 0x0F, 0xA0, 87);   /* VBUS + battery, direction 01, state 3 (cv), 0x0FA0 = 4000 mV */
    CHECK(a.vbus && a.batt); CHECK_EQ_INT(a.dir, PC_AXP_CHARGING); CHECK_EQ_INT(a.chg_state, 3); CHECK_EQ_INT(a.vbat_mv, 4000); CHECK_EQ_INT(a.pct, 87);
    CHECK_STR(pc_axp_chg_name(a.chg_state), "cv");
    pc_axp_decode(&a, 0x08, 0x45, 0x0E, 0x10, 41);               /* battery only, direction 10, state 5 (not charging) */
    CHECK(!a.vbus && a.batt); CHECK_EQ_INT(a.dir, PC_AXP_DISCHARGING); CHECK_EQ_INT(a.chg_state, 5); CHECK_EQ_INT(a.vbat_mv, 3600); CHECK_EQ_INT(a.pct, 12);   /* gauge 41 vs 3.6 V (12 %): too far apart, the voltage wins */
}

static void test_gauge_fallback(void)
{
    pc_axp_t a; pc_axp_decode(&a, 0x08, 0x40, 0x0E, 0xA6, 0);    /* the gauge reads 0 with the cell at 3750 mV: suspicious endpoint, the voltage alone */
    CHECK(a.pct >= 30 && a.pct <= 33);
    pc_axp_decode(&a, 0x08, 0x40, 0x00, 0x00, 0);                /* the ADC is off too: the gauge's 0 stands (no better number) */
    CHECK_EQ_INT(a.pct, 0);
    pc_axp_decode(&a, 0x08, 0x40, 0x0F, 0xA0, 0x7F);             /* 127: not a percentage; the voltage alone */
    CHECK(a.pct >= 70 && a.pct <= 75);
    pc_axp_decode(&a, 0x08, 0x40, 0x0F, 0xA0, 0xFF);             /* 0xFF: unusable gauge */
    CHECK(a.pct >= 70 && a.pct <= 75);
}

static void test_fusion(void)
{
    CHECK_EQ_INT(pc_axp_fuse(50, 3860, false, false), 50);       /* agree: 0.6 g + 0.4 ocv */
    CHECK_EQ_INT(pc_axp_fuse(56, 3860, false, false), 54);       /* 0.6*56 + 0.4*50 = 53.6, rounded */
    CHECK_EQ_INT(pc_axp_fuse(70, 3860, false, false), 50);       /* 20 apart: the voltage alone */
    CHECK_EQ_INT(pc_axp_fuse(100, 3900, false, false), pc_axp_pct_from_mv(3900));   /* gauge 100 under 4.0 V: suspicious */
    CHECK_EQ_INT(pc_axp_fuse(0, 3700, false, false), pc_axp_pct_from_mv(3700));     /* gauge 0 above 3.6 V: suspicious */
    CHECK_EQ_INT(pc_axp_fuse(-1, 3980, false, false), 70);       /* no gauge: the table */
    CHECK_EQ_INT(pc_axp_fuse(64, 0, false, false), 64);          /* no voltage: the gauge */
    CHECK_EQ_INT(pc_axp_fuse(-1, 0, false, false), -1);
    CHECK_EQ_INT(pc_axp_fuse(100, 4190, true, false), 99);       /* charging: never 100 ... */
    CHECK_EQ_INT(pc_axp_fuse(87, 4100, true, false), 87);
    CHECK_EQ_INT(pc_axp_fuse(-1, 4190, true, false), 99);
    CHECK_EQ_INT(pc_axp_fuse(100, 4190, true, true), 100);       /* ... until the charger says done */
    pc_axp_t a; pc_axp_decode(&a, 0x28, 0x24, 0x10, 0x4E, 100); /* VBUS, standby, state 4 (done), 4190 mV */
    CHECK_EQ_INT(a.pct, 100);
    pc_axp_decode(&a, 0x28, 0x23, 0x10, 0x4E, 100);             /* VBUS, dir 01 charging, state 3 */
    CHECK_EQ_INT(a.pct, 99);
}

static void test_curve(void)
{
    CHECK_EQ_INT(pc_axp_pct_from_mv(3000), 0); CHECK_EQ_INT(pc_axp_pct_from_mv(4300), 100); CHECK_EQ_INT(pc_axp_pct_from_mv(3980), 70);
    int last = -1;
    for (int mv = 3300; mv <= 4200; mv += 10) { int p = pc_axp_pct_from_mv(mv); CHECK(p >= last); last = p; }
}

int main(void)
{
    RUN(test_no_battery);
    RUN(test_charging_and_discharging);
    RUN(test_gauge_fallback);
    RUN(test_fusion);
    RUN(test_curve);
    return 0;
}
