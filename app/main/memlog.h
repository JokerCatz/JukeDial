// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Prints "MEM <tag> internal_free=.. internal_min=.. internal_largest=.. psram_free=.." */
void memlog(const char *tag);

/* MEMB: boot-step measurement, a runtime switch (default off). Console `memboot` sets an RTC flag and reboots; that one boot logs
 * "MEM step <tag>" after every init step (memlog_step), then memlog_boot_end() dumps tasks/stacks/heaps/DMA and clears the flag. */
void memlog_boot_take(void);          /* early in app_main: reads and clears the flag, arms the steps for this boot */
void memlog_step(const char *tag);    /* one line when armed, else nothing */
void memlog_boot_end(void);           /* when armed: the full dump, then disarms */
void memlog_boot_request(void);       /* console: set the flag, reboot */
void memlog_dump(void);               /* console `memdump`: the task/stack/heap dump any time (scenarios b, c) */
