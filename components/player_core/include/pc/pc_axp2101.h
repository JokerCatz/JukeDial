// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AXP2101 battery facts (FakePod, F2), pure: the decode of five READ-ONLY registers. Register names and bit meanings are the XPowersLib / datasheet ones, written from
 * memory and the notes in .local/fakepod/absorb-notes.md, never run against the chip: the checklist's FakePod section has the steps that confirm them.
 *   0x00 status 1: bit5 VBUS good, bit3 battery present
 *   0x01 status 2: bits 6:5 battery current direction (1 charging, 2 discharging, 0 standby), bits 2:0 charger state (0 tri, 1 pre, 2 constant current, 3 constant voltage, 4 done, 5 not charging)
 *   0x34 / 0x35 battery voltage ADC (14 bit: (0x34 & 0x3F) << 8 | 0x35, mV)
 *   0xA4 fuel gauge percent (bits 6:0; 0xFF = unusable); the shown percent fuses it with the voltage table (pc_axp_fuse)
 * The FakePod's schematic shows no battery connector: "no battery" is a normal answer (percent -1). Nothing in this file writes a register. */
#define PC_AXP_REG_STATUS1 0x00
#define PC_AXP_REG_STATUS2 0x01
#define PC_AXP_REG_VBAT_H  0x34
#define PC_AXP_REG_VBAT_L  0x35
#define PC_AXP_REG_GAUGE   0xA4
enum { PC_AXP_STANDBY = 0, PC_AXP_CHARGING = 1, PC_AXP_DISCHARGING = 2 };
typedef struct {
    bool vbus;          /* external power present */
    bool batt;          /* a battery is connected */
    uint8_t dir;        /* PC_AXP_* */
    uint8_t chg_state;  /* 0..5, status 2 bits 2:0 */
    int vbat_mv;        /* 0 when the ADC is off / no battery */
    int pct;            /* 0..100; -1 = no battery or no usable reading */
} pc_axp_t;
void pc_axp_decode(pc_axp_t *o, uint8_t s1, uint8_t s2, uint8_t vh, uint8_t vl, uint8_t gauge);
int  pc_axp_pct_from_mv(int mv);   /* the 600 mAh LCO open-circuit table (resting voltage) */
int  pc_axp_fuse(int gauge, int vbat_mv, bool charging, bool done);   /* gauge + table fusion, capped at 99 while charging (see pc_axp2101.c); -1 = nothing usable */
const char *pc_axp_chg_name(uint8_t chg_state);
#ifdef __cplusplus
}
#endif
