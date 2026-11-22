// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Display geometry of this board, and nothing else (no IDF headers, so host code and the simulator can include it).
 * A port to another panel replaces this file; ui/ui_geom.h derives every UI size from it. */
#define BSP_LCD_H_RES            360
#define BSP_LCD_V_RES            360
#define BSP_LCD_ROUND            1      /* 1 = round glass (the UI keeps clear of the corners), 0 = rectangular */
#define BSP_HAS_KNOB             1      /* 1 = a rotary knob exists, 0 = two keys stand in for it (the FakePod) */
#define BSP_HAS_KEYS             0      /* 1 = two keys stand in for the knob (keys_svc) */
#define BSP_HAS_PMU              0      /* 1 = an AXP2101 answers battery percent / charging (the FakePod); the round board only sees its rail on an ADC */
#define BSP_PANEL_OLED           0      /* 1 = an OLED panel: the screen must never stay lit (the round board's LCD may) */
#define BSP_HAS_IMU              0      /* 1 = a tilt sensor feeds the maze (imu_svc); the round board's QMI8658 is not populated */
#define BSP_BOARD_ID             "jc3636f118"   /* what the CTL status op reports as "board" (the web flasher picks its manifest by it); the FakePod port says "fakepod" */
