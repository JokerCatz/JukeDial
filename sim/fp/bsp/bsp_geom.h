// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The square simulator's panel (make sim-fp): the FakePod's 460x460 AMOLED, no knob. Used instead of components/jc3636f118_bsp/include/bsp/bsp_geom.h
 * only in the sim/build-fp build (sim/CMakeLists.txt, SIM_FP); the FakePod's own BSP carries the same four numbers. */
#define BSP_LCD_H_RES            460
#define BSP_LCD_V_RES            460
#define BSP_LCD_ROUND            0
#define BSP_HAS_KNOB             0
#define BSP_BOARD_ID             "fakepod"
#define BSP_HAS_PMU              1      /* the fake power service answers a battery percent (sim state batt=) */
#define BSP_PANEL_OLED           1      /* the FakePod's AMOLED */
