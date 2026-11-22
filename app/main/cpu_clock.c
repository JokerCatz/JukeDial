// SPDX-License-Identifier: LGPL-2.1-or-later
/* Experimental run-time CPU clock (cpu_clock.h). Code in flash, state in PSRAM: no internal RAM of our own.
 * esp_pm_configure freezes the caches for a moment, so it must run on a task whose stack is in internal RAM: most of ours have PSRAM stacks (the LVGL task, console, audio).
 * It is handed to a task that exists only for the call (internal stack, 4 KB, freed when it ends: no permanent RAM; the chipbench task deletes itself the same way). */
#include <stdio.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cfg.h"
#include "cpu_clock.h"
#include "guard.h"

EXT_RAM_BSS_ATTR static struct { volatile bool ready, busy; volatile int want, cur; } S;   /* PSRAM BSS is zeroed at boot: no initialiser here, cpu_clock_boot_done sets cur */

int cpu_clock_mhz(void) { return (int)(esp_clk_cpu_freq() / 1000000); }

static void task(void *arg)
{
    (void)arg;
    while (S.want != S.cur) {
        const int w = S.want;
        esp_pm_config_t c = { .max_freq_mhz = w, .min_freq_mhz = w, .light_sleep_enable = false };
        esp_err_t e = esp_pm_configure(&c);
        if (e != ESP_OK) { printf("APP cpuclk %d MHz failed rc=%s\n", w, esp_err_to_name(e)); S.want = S.cur; break; }
        S.cur = w;
        printf("APP cpuclk set %d MHz real=%d\n", w, cpu_clock_mhz());
    }
    S.busy = false;
    vTaskDeleteWithCaps(NULL);
}

void cpu_clock_apply(void)
{
    if (!S.ready) return;                                   /* boot (and safe mode, the USB modes) run at 320 */
    S.want = cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ));
    if (S.want == S.cur || S.busy) return;                  /* a running task re-reads want before it ends */
    S.busy = true;
    if (xTaskCreatePinnedToCoreWithCaps(task, "cpuclk", 4096, NULL, 2, NULL, 0, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        S.busy = false;
        S.want = S.cur;
        printf("APP cpuclk no task (internal RAM)\n");
    }
}

void cpu_clock_boot_done(void)
{
    if (guard_last_reset_was_crash() && cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ) != CPU_CLOCK_DEFAULT_MHZ) {
        printf("APP cpuclk: the last reset was a crash, the clock setting goes back to %d MHz\n", CPU_CLOCK_DEFAULT_MHZ);
        cfg_set_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ);
    }
    S.cur = S.want = cpu_clock_mhz();                        /* the boot clock (320) */
    S.ready = true;
    cpu_clock_apply();
}

void cpu_clock_cmd(const char *arg)
{
    while (*arg == ' ') arg++;
    int n;
    if (sscanf(arg, "%d", &n) == 1) cfg_set_int("cpu_mhz", cpu_clock_norm(n));   /* the hook in cfg_set_str applies it */
    printf("APP cpuclk want=%d cur=%d busy=%d real=%d\n", cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ)), S.cur, S.busy, cpu_clock_mhz());
}
