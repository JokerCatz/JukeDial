// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Experimental run-time CPU clock: cfg "cpu_mhz" 320 (default, the tested speed) / 240 / 160. CONFIG_PM_ENABLE is on only for esp_pm_configure(max = min = N).
 * Boot always runs at 320: the saved value is applied by cpu_clock_boot_done() at the very end of a normal player boot (never in safe mode, the USB / other reboot modes or the
 * boot picture), and a boot after a crash resets the setting to 320 first. Nothing here blocks or raises the clock for a heavy path: the setting is experimental and says so. */
#define CPU_CLOCK_DEFAULT_MHZ 320
static inline int cpu_clock_norm(int mhz) { return mhz == 240 || mhz == 160 ? mhz : CPU_CLOCK_DEFAULT_MHZ; }
static inline int cpu_clock_step(int cur, int d)     /* 320 -> 240 -> 160 -> 320 for d > 0, the other way for d < 0 */
{
    static const int V[3] = { 320, 240, 160 };
    int i = cpu_clock_norm(cur) == 320 ? 0 : cpu_clock_norm(cur) == 240 ? 1 : 2;
    return V[(i + (d < 0 ? 2 : 1)) % 3];
}
void cpu_clock_boot_done(void);   /* app_main, normal player boot, after everything started: the crash reset, then the saved value */
void cpu_clock_apply(void);       /* cfg "cpu_mhz" changed (cfg_set_str calls it): apply the saved value now (nothing before cpu_clock_boot_done) */
int  cpu_clock_mhz(void);         /* the real CPU clock in MHz (esp_clk_cpu_freq) */
void cpu_clock_cmd(const char *arg);   /* console "cpuclk [320|240|160]": sets (saved) and prints the want and the real clock */
