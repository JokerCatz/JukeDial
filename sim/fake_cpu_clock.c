// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes cpu_clock (the experimental run-time CPU clock: esp_pm_configure on the board). */
/* Behaviour: applies at once (no boot gate), prints "APP cpuclk set N MHz (sim)" and reports that value as the real clock. */
/* Empty: no task, no crash reset. */
#include <stdio.h>
#include "cfg.h"
#include "cpu_clock.h"

static int s_cur = CPU_CLOCK_DEFAULT_MHZ;

int cpu_clock_mhz(void) { return s_cur; }
void cpu_clock_boot_done(void) {}
void cpu_clock_apply(void)
{
    int w = cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ));
    if (w == s_cur) return;
    s_cur = w;
    printf("APP cpuclk set %d MHz (sim)\n", w);
}
void cpu_clock_cmd(const char *arg)
{
    int n;
    if (sscanf(arg, "%d", &n) == 1) cfg_set_int("cpu_mhz", cpu_clock_norm(n));
    printf("APP cpuclk want=%d real=%d\n", cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ)), s_cur);
}
