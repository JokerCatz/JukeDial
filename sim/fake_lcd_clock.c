// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes lcd_clock (the experimental display QSPI clock): the simulator has no panel clock. */
#include "lcd_clock.h"
void lcd_clock_prepare(bool persist) { (void)persist; }
void lcd_clock_escape_poll(void) {}
