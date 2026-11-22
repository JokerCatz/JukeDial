// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
/* PCF85063(A) real-time clock (FakePod, I2C 0x51), pure: the register block <-> a broken-down UTC time. Written from the datasheet's register map from memory and never run
 * against the chip (checklist: the FakePod RTC section):
 *   0x00 Control_1: bit5 STOP, bit1 12_24 (0 = 24 h), bit0 CAP_SEL (0 = 7 pF, 1 = 12.5 pF), bit2 CIE; the reset value is 0x00
 *   0x04 Seconds: bit7 OS (oscillator stopped: the time is not trustworthy; cleared by writing the register with the bit 0), bits 6:0 BCD 00..59
 *   0x05 Minutes BCD, 0x06 Hours BCD (24 h: bits 5:0), 0x07 Days BCD 01..31, 0x08 Weekdays 0..6 (any fixed assignment; Sunday = 0 here), 0x09 Months BCD 01..12, 0x0A Years BCD 00..99
 * The year register covers 2000..2099. The chip keeps whatever the firmware wrote: this firmware stores UTC. */
#define PC_RTC_ADDR      0x51
#define PC_RTC_REG_CTRL1 0x00
#define PC_RTC_REG_SEC   0x04
#define PC_RTC_BLOCK     7           /* seconds .. years */
#define PC_RTC_CTRL1_STOP  0x20
#define PC_RTC_CTRL1_12_24 0x02
/* r[0..6] = the registers 0x04..0x0A. Returns true when every field is in range (a calendar-valid date, 24 h hours); *os (may be NULL) = the oscillator-stop flag. t is filled
 * (tm_year/tm_mon as struct tm, tm_wday computed from the date, never read from the chip) whenever the fields are in range, else left alone. */
bool pc_rtc85063_decode(const uint8_t r[PC_RTC_BLOCK], struct tm *t, bool *os);
/* The block to write: seconds with bit7 = 0 (clears OS). false for a time outside 2000..2099 or with a field out of range (r is not written). */
bool pc_rtc85063_encode(const struct tm *t, uint8_t r[PC_RTC_BLOCK]);
/* Control_1 as it must be for normal running: STOP and 12_24 clear, every other bit (CAP_SEL, CIE) kept. */
uint8_t pc_rtc85063_ctrl1_run(uint8_t ctrl1);
#ifdef __cplusplus
}
#endif
