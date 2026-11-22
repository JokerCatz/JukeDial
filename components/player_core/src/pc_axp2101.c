// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_axp2101.h"

/* Open-circuit-voltage table of a 600 mAh LCO cell, resting values (NightPlayer (c) 2026 ZYFDroid, MIT: battery curve of the same board's cell). Under load the voltage sags 30-80 mV, so
 * the OCV reads low while playing: the fusion below leans on the gauge when the two agree. */
int pc_axp_pct_from_mv(int mv)
{
    static const struct { int mv, pct; } T[] = { { 3300, 0 }, { 3420, 3 }, { 3490, 5 }, { 3570, 10 }, { 3630, 15 }, { 3680, 20 }, { 3740, 30 }, { 3800, 40 }, { 3860, 50 },
                                                 { 3920, 60 }, { 3980, 70 }, { 4050, 80 }, { 4110, 90 }, { 4150, 95 }, { 4200, 100 } };
    const int n = (int)(sizeof T / sizeof T[0]);
    if (mv <= T[0].mv) return 0;
    if (mv >= T[n - 1].mv) return 100;
    for (int i = 1; i < n; i++)
        if (mv < T[i].mv) return T[i - 1].pct + (T[i].pct - T[i - 1].pct) * (mv - T[i - 1].mv) / (T[i].mv - T[i - 1].mv);
    return 100;
}

/* Gauge + OCV fusion (the approach of NightPlayer's battery_fuse_percent): the AXP2101 gauge is uncalibrated for this cell and can jump, the OCV curve is calibration-free but wrong
 * under load and while charging. gauge < 0 = unusable (0xFF); vbat_mv 0 = the ADC is off. Charging: the gauge capped at 99 (100 only when the charger reports done); the OCV is not
 * used for the number except as a stand-in without a gauge (capped too). Not charging: both agree within 8 points and the endpoints are not suspicious (gauge 0 with the cell above
 * 3.6 V, gauge 100 below 4.0 V) -> 0.6 gauge + 0.4 OCV; else the OCV alone; no usable voltage -> the gauge. -1: nothing usable. */
int pc_axp_fuse(int gauge, int vbat_mv, bool charging, bool done)
{
    const bool vok = vbat_mv >= 2500 && vbat_mv < 4600;
    const int ocv = vok ? pc_axp_pct_from_mv(vbat_mv) : -1;
    if (gauge > 100) gauge = -1;
    if (done) return 100;
    if (charging) {
        const int g = gauge >= 0 ? gauge : ocv;
        return g < 0 ? -1 : (g > 99 ? 99 : g);
    }
    if (gauge < 0) return ocv;
    if (ocv < 0) return gauge;
    const bool suspicious = (gauge == 0 && vbat_mv > 3600) || (gauge == 100 && vbat_mv < 4000);
    const int diff = gauge > ocv ? gauge - ocv : ocv - gauge;
    if (suspicious || diff > 8) return ocv;
    return (6 * gauge + 4 * ocv + 5) / 10;
}

void pc_axp_decode(pc_axp_t *o, uint8_t s1, uint8_t s2, uint8_t vh, uint8_t vl, uint8_t gauge)
{
    o->vbus = (s1 & 0x20) != 0;
    o->batt = (s1 & 0x08) != 0;
    const uint8_t d = (uint8_t)((s2 >> 5) & 3);
    o->dir = d == 1 ? PC_AXP_CHARGING : d == 2 ? PC_AXP_DISCHARGING : PC_AXP_STANDBY;
    o->chg_state = s2 & 7;
    o->vbat_mv = o->batt ? ((vh & 0x3F) << 8) | vl : 0;
    o->pct = -1;
    if (!o->batt) return;
    o->pct = pc_axp_fuse(gauge == 0xFF ? -1 : (gauge & 0x7F), o->vbat_mv, o->dir == PC_AXP_CHARGING, o->vbus && o->chg_state == 4);
}

const char *pc_axp_chg_name(uint8_t s)
{
    static const char *const N[] = { "tri", "pre", "cc", "cv", "done", "stop", "?", "?" };
    return N[s & 7];
}
