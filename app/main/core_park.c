// SPDX-License-Identifier: LGPL-2.1-or-later
/* core_park (core_park.h): a parked core has its branch predictor off. Part 1, flash operations (psram-hunt 2026-11-19):
 *
 * For a flash operation (esp_flash_read/write/erase, NVS), cache_utils.c spi_flash_disable_interrupts_caches_and_other_cpu()
 * sends spi_flash_op_block_func() to the other core through esp_ipc_call_nonblocking(), waits until that core spins in the
 * IRAM loop, then suspends the external-memory cache (a hit is still served, a miss raises the cache access-fail interrupt).
 * It switches off the branch predictor of the CALLING core only ("branch predictor will start cache request as well");
 * the parked core keeps its predictor on. Its spin loop sits a few bytes before the tail jump to xTaskResumeAll, which is
 * in flash (FreeRTOS lives in flash by default), and next to the call of vTaskSuspendAll (flash too): a predicted fetch
 * from flash by the parked core misses whenever that line is not cached, and the cache reports it as
 * "Core 1 panic'ed (Cache error)" while core 0 is in the flash read. Whether it is cached depends on the flash text
 * layout, which is why a harmless code change (commit 3d1227c5) crashed every boot and others did not.
 * Board proof (same code bytes, one data flag): predictor left on -> 3 of 4 boots crashed in nvs_flash_init; predictor
 * off -> 5 of 5 booted (an internal note, experiments 6 and 7).
 *
 * Fix: the parked core switches its own predictor off (core_park_predictor_off) before it enters the IDF block function;
 * spi_flash_restore_cache() at the end of that function switches it back on (after the cache is resumed).
 * Both functions below run with the cache on (before the parked core sets s_flash_op_can_start), so they stay in flash:
 * no internal RAM. tools/check/check_flash_park.py checks the built ELF for this hook.
 *
 * Part 2, core_park_begin/end: our own short stalls of the other core (rules in core_park.h). */
#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ipc_isr.h"
#include "esp_private/esp_ipc.h"
#include "sdkconfig.h"
#include "core_park.h"

#ifndef CONFIG_ESP_INT_WDT_TIMEOUT_MS
#define CONFIG_ESP_INT_WDT_TIMEOUT_MS 0
#endif

void core_park_predictor_off(void *unused)
{
    (void)unused;
    esp_cpu_branch_prediction_disable();                   /* this core's MHCR: return stack, branch prediction, BTB off */
}

void core_park_predictor_on(void *unused)
{
    (void)unused;
    esp_cpu_branch_prediction_enable();
}

/* ---- flash operations: the hook verified on the board 2026-11-19 (experiments 6/7), unchanged apart from the names ---- */
void spi_flash_op_block_func(void *arg);                  /* IDF cache_utils.c (IRAM, global) */
esp_err_t __real_esp_ipc_call_nonblocking(uint32_t cpu_id, esp_ipc_func_t func, void *arg);

void core_park_flash_block(void *arg)
{
    core_park_predictor_off(NULL);                         /* first, while the cache is still on */
    spi_flash_op_block_func(arg);                          /* its spi_flash_restore_cache() switches the predictor back on */
}

esp_err_t __wrap_esp_ipc_call_nonblocking(uint32_t cpu_id, esp_ipc_func_t func, void *arg)
{
    if (func == spi_flash_op_block_func) func = core_park_flash_block;   /* its only caller in IDF v6.1 is cache_utils.c */
    return __real_esp_ipc_call_nonblocking(cpu_id, func, arg);
}

/* ---- core_park_begin / core_park_end ---- */
/* State per core (written by its own core with its interrupts masked), in PSRAM: no internal RAM. Read only with the cache on. */
typedef struct { uint32_t held, irq, last_us, max_us; int64_t t0; } park_state_t;
EXT_RAM_BSS_ATTR static park_state_t s_park[portNUM_PROCESSORS];
EXT_RAM_BSS_ATTR static bool s_park_warned;

core_park_rc_t core_park_begin(void)
{
#if CONFIG_FREERTOS_UNICORE || !CONFIG_ESP_IPC_ISR_ENABLE
    return CORE_PARK_ONE_CORE;
#else
    if (xPortInIsrContext()) return CORE_PARK_ERR_ISR;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return CORE_PARK_ONE_CORE;   /* the other core is not up yet */
    park_state_t *p = &s_park[esp_cpu_get_core_id()];
    if (p->held) return CORE_PARK_ERR_NESTED;
    uint32_t irq = portSET_INTERRUPT_MASK_FROM_ISR();       /* no preemption between "predictor off" and "stalled" */
    esp_ipc_isr_call_blocking(core_park_predictor_off, NULL);
    esp_ipc_isr_stall_other_cpu();                         /* it now spins in IDF's IRAM loop, predictor off */
    p->irq = irq;
    p->held = 1;
    p->t0 = esp_timer_get_time();                          /* lock-free systimer read: safe while the other core is stalled */
    return CORE_PARK_OK;
#endif
}

void core_park_end(void)
{
#if !(CONFIG_FREERTOS_UNICORE || !CONFIG_ESP_IPC_ISR_ENABLE)
    if (xPortInIsrContext()) return;
    park_state_t *p = &s_park[esp_cpu_get_core_id()];
    if (!p->held) return;
    int64_t dt = esp_timer_get_time() - p->t0;
    esp_ipc_isr_release_other_cpu();
    esp_ipc_isr_call_blocking(core_park_predictor_on, NULL);
    p->held = 0;
    p->last_us = dt > 0 ? (uint32_t)dt : 0;
    if (p->last_us > p->max_us) p->max_us = p->last_us;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(p->irq);
    if (p->last_us > CORE_PARK_WARN_US && !s_park_warned) {   /* ROM printf, no lock: the caller may itself be in a critical section */
        s_park_warned = true;
        ESP_EARLY_LOGW("core_park", "held %u us (> %u us; the interrupt watchdog fires at %d ms): shorten the parked step",
                 (unsigned)p->last_us, (unsigned)CORE_PARK_WARN_US, CONFIG_ESP_INT_WDT_TIMEOUT_MS);
    }
#endif
}

uint32_t core_park_last_hold_us(void) { return s_park[esp_cpu_get_core_id()].last_us; }

uint32_t core_park_max_hold_us(void)
{
    uint32_t m = 0;
    for (int i = 0; i < portNUM_PROCESSORS; i++) if (s_park[i].max_us > m) m = s_park[i].max_us;
    return m;
}
