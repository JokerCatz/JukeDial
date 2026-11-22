// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* Experimental display QSPI clock (cfg "lcd_mhz", console `seti lcd_mhz <n>` + reboot; values per board in pc_lcdclk.h; no settings row yet).
 * lcd_clock_prepare(): after cfg_load, before bsp_display_start_ex (every boot mode that starts the display): hands the value to the BSP. The value is the board's default for this boot
 *   after a crash reset (guard_last_reset_was_crash) or when the escape was held; `persist` (the normal player boot only) also writes the default back to cfg.
 * lcd_clock_escape_poll(): the escape that needs no screen or console: the BOOT key (the round board's pin hole, the FakePod's bottom key) held 3 s within the first 5 s of the boot (app_main's
 *   recovery window, extended only while the key is down); clears lcd_mhz at the next lcd_clock_prepare. The FakePod's top key is the PMU power key behind I2C and its long hold powers off: not used. */
void lcd_clock_prepare(bool persist);
void lcd_clock_escape_poll(void);
