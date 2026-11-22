// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* Crash log (spec ui-kit-diagnostics 3.5): breadcrumbs in RTC no-init memory, turned into one line on the next boot,
 * printed as "APP crash ..." and appended to /sdcard/_player/logs/crash.txt. The panic entry is wrapped (-Wl,--wrap) to add
 * PC / RA / mcause; the wrapper only writes RTC memory. Nothing here allocates while running or keeps a task / timer. */
void crashlog_boot(void);               /* app_main, right after guard_boot(): last reset a crash and live valid -> pending; live cleared */
void crashlog_mode(int mode);           /* PC_CRASH_MODE_* */
void crashlog_crumb(const char *screen, int bt_mode, int src);   /* UI tick; also refreshes uptime, internal_min, wall */
void crashlog_flush(bool sd_ok);        /* after the card mount: pending -> one printed line (+ the file when sd_ok); pending cleared */
void crashlog_print_tail(void);         /* console crashlog: the file's last 10 lines */
void crashlog_test(const char *arg);    /* console crashtest [nosd|keep|fail|early|safe]: only with /sdcard/.crashtest */
