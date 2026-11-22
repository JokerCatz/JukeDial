// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes crashlog (breadcrumbs in RTC memory, a line on the next boot after a crash). */
/* Behaviour: none; the simulator has no RTC memory and never crashes on purpose. */
/* Empty: everything (the UI tick's crumb is dropped). */
#include "crashlog.h"

void crashlog_boot(void) {}
void crashlog_mode(int mode) { (void)mode; }
void crashlog_crumb(const char *screen, int bt_mode, int src) { (void)screen; (void)bt_mode; (void)src; }
void crashlog_flush(bool sd_ok) { (void)sd_ok; }
void crashlog_print_tail(void) {}
void crashlog_test(const char *arg) { (void)arg; }
