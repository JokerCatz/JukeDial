// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* JC3636F118 (ESP32-S31 knob round display) pin map.
 * Source: vendor main/device/pinconfig.h + schematic JC3636F118_GPIO.pdf (see the hardware notes §1).
 * Do not use bare GPIO numbers anywhere else in the code. */
#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/i2s_types.h"
#include "driver/spi_common.h"
#include "soc/soc_caps.h"
#include "bsp/bsp_geom.h"   /* BSP_LCD_H_RES / BSP_LCD_V_RES live there */

/* Display: ST77916 360x360 over QSPI */
#define BSP_LCD_SPI_HOST         SPI2_HOST
#define BSP_PIN_LCD_SCL          GPIO_NUM_12
#define BSP_PIN_LCD_CS           GPIO_NUM_13
#define BSP_PIN_LCD_D0           GPIO_NUM_14
#define BSP_PIN_LCD_D1           GPIO_NUM_15
#define BSP_PIN_LCD_D2           GPIO_NUM_16
#define BSP_PIN_LCD_D3           GPIO_NUM_17
#define BSP_PIN_LCD_RST          GPIO_NUM_18
#define BSP_PIN_LCD_TE           GPIO_NUM_19
#define BSP_PIN_LCD_BLK          GPIO_NUM_5

/* Touch CST816 + DRV2605L share I2C0 */
#define BSP_PIN_TP_SCL           GPIO_NUM_10
#define BSP_PIN_TP_SDA           GPIO_NUM_11
#define BSP_PIN_TP_INT           GPIO_NUM_8
#define BSP_PIN_TP_RST           GPIO_NUM_9

/* IMU QMI8658C on its own I2C1 (factory boot log found nothing on this bus) */
#define BSP_PIN_IMU_SDA          GPIO_NUM_42
#define BSP_PIN_IMU_SCL          GPIO_NUM_43

/* Rotary encoder (no push switch). Vendor pinconfig: A=6, B=7; schematic has them swapped. */
#define BSP_PIN_KNOB_A           GPIO_NUM_6
#define BSP_PIN_KNOB_B           GPIO_NUM_7

/* PCM5102A DAC (no MCLK: SCK tied low, internal PLL) */
#define BSP_PIN_I2S_BCK          GPIO_NUM_53
#define BSP_PIN_I2S_WS           GPIO_NUM_51
#define BSP_PIN_I2S_DOUT         GPIO_NUM_52
#define BSP_PIN_DAC_XSMT         GPIO_NUM_50   /* low = soft mute, 100k pull-down */

/* PDM microphone. GPIO37 is a strapping pin: never enable internal pulls on it. */
#define BSP_PIN_MIC_CLK          GPIO_NUM_39
#define BSP_PIN_MIC_DATA         GPIO_NUM_37

/* TF card, SDMMC 4-bit */
#define BSP_PIN_SD_CLK           GPIO_NUM_24
#define BSP_PIN_SD_CMD           GPIO_NUM_25
#define BSP_PIN_SD_D0            GPIO_NUM_20
#define BSP_PIN_SD_D1            GPIO_NUM_21
#define BSP_PIN_SD_D2            GPIO_NUM_22
#define BSP_PIN_SD_D3            GPIO_NUM_23

#define BSP_PIN_BUZZER           GPIO_NUM_44   /* high = on (Q2 low-side) */
#define BSP_PIN_BATT_ADC         GPIO_NUM_55   /* system rail via 15k/10k */
#define BSP_PIN_BOOT             GPIO_NUM_60   /* strapping; 10k pull-up; read only */

/* I2C */
#define BSP_I2C_PORT_TOUCH       I2C_NUM_0
#define BSP_I2C_PORT_IMU         I2C_NUM_1
#define BSP_I2C_FREQ_HZ          400000
#define BSP_I2C_ADDR_TOUCH       0x15   /* confirmed by factory boot log */
#define BSP_I2C_ADDR_HAPTIC      0x5A   /* confirmed by factory boot log */
#define BSP_I2C_ADDR_IMU_PRIMARY 0x6B
#define BSP_I2C_ADDR_IMU_ALT     0x6A

/* I2S: esp32s31 soc_caps says PDM->PCM RX exists only on I2S0, so the mic owns I2S0
 * and the DAC uses I2S1. The two test pages never run at the same time. */
/* esp32s31 has 2 I2S instances (esp_hal_i2s/esp32s31 i2s_ll.h: I2S_LL_INST_NUM 2). */
#define BSP_I2S_PORT_MIC         I2S_NUM_0
#define BSP_I2S_PORT_DAC         I2S_NUM_1
