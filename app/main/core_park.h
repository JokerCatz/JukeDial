// SPDX-License-Identifier: LGPL-2.1-or-later
/* core_park: the one place where a core is parked (ESP32-S31, IDF v6.1). Rule: a parked core has its branch predictor OFF.
 *
 * Why: a core that spins in an internal-RAM loop while the other core freezes or suspends the external-memory cache must not
 * fetch flash code speculatively; with its branch predictor on it does, and the cache then reports "Core 1 panic'ed (Cache
 * error)" (boot crash of 2026-11-19, the internal notes). IDF v6.1 switches the predictor off
 * only on the core that performs the flash operation, not on the core it parks.
 *
 * What goes through here:
 *  - every flash operation (esp_flash_*, NVS, partitions): the -Wl,--wrap=esp_ipc_call_nonblocking hook in core_park.c swaps
 *    IDF's park function for core_park_flash_block(), which switches the parked core's predictor off first;
 *  - our own code that must hold the other core still for a short step: core_park_begin() / core_park_end() below.
 *  NOT through here (yet): IDF's own esp_ipc_isr_stall_other_cpu() callers (light / deep sleep, MSPI retune at a CPU-frequency
 *  change, gdbstub). None is reachable in this firmware; tools/check/check_flash_park.py fails the build if one appears, or if
 *  light sleep is enabled, before that entry is hooked and tested on the board.
 *
 * core_park_begin() / core_park_end() (built on esp_ipc_isr_stall_other_cpu / esp_ipc_isr_release_other_cpu):
 *  begin: masks this core's interrupts, runs core_park_predictor_off() on the other core (esp_ipc_isr_call_blocking), then
 *         stalls it (it spins in IDF's IPC-ISR loop in IRAM, at interrupt level 5: even inside its own critical sections);
 *  end:   releases it, runs core_park_predictor_on() on it, unmasks this core's interrupts.
 * Rules (the caller's job; breaking one resets the board):
 *  - SHORT: held far below the interrupt watchdog (CONFIG_ESP_INT_WDT_TIMEOUT_MS = 800, CPU1 checked; both cores' ticks stop
 *    being serviced while parked). Aim for microseconds; core_park_end() measures every hold and logs once per boot when one
 *    exceeds CORE_PARK_WARN_US (ROM printf: allowed inside the caller's own critical section).
 *  - NOTHING THAT CAN BLOCK between begin and end: no FreeRTOS wait, mutex, queue, malloc, printf / ESP_LOG, file or flash access
 *    (a flash operation would wait for the stalled core: deadlock), no esp_timer API except esp_timer_get_time(). This core's
 *    interrupts are masked, so a blocking call cannot yield anyway (FreeRTOS asserts or the watchdog fires).
 *  - NO SPINLOCK THE OTHER CORE MAY HOLD: it can be stalled in the middle of its own critical section; taking that lock while it
 *    is parked never returns. Take such locks BEFORE core_park_begin() (then the other core cannot be holding them).
 *  - not reentrant (a second begin on the same core returns CORE_PARK_ERR_NESTED); never from an ISR (CORE_PARK_ERR_ISR);
 *    only from code running with the cache on (normal task code), since the other core runs the predictor helpers from flash.
 *  - one core only (CONFIG_FREERTOS_UNICORE, or before the scheduler runs): returns CORE_PARK_ONE_CORE and parks nothing;
 *    core_park_end() is then a no-op. Callers that need the stall must check for CORE_PARK_OK.
 *  - panic while parked: IDF's panic handler aborts the stall (esp_ipc_isr_stall_abort) and resets; nothing to undo.
 * Cost: no internal RAM (state in PSRAM, code in flash: the predictor helpers run with the cache on, so they need no IRAM). */
#pragma once
#include <stdint.h>

typedef enum {
    CORE_PARK_OK = 0,           /* the other core is stalled with its predictor off: call core_park_end() */
    CORE_PARK_ONE_CORE = 1,     /* only this core runs: nothing parked, nothing to end (end is a no-op) */
    CORE_PARK_ERR_ISR = -1,     /* called from an interrupt: refused */
    CORE_PARK_ERR_NESTED = -2,  /* this core already holds the park: refused (not reentrant) */
} core_park_rc_t;

#define CORE_PARK_WARN_US 5000  /* a hold longer than this is logged once per boot (the int WDT fires at 800 ms) */

core_park_rc_t core_park_begin(void);
void core_park_end(void);

/* last and longest hold of this boot, microseconds (diagnostics / board tests) */
uint32_t core_park_last_hold_us(void);
uint32_t core_park_max_hold_us(void);

/* The one primitive: switch the predictor (return stack, branch prediction, BTB: MHCR) of the core that runs it off / on.
 * Signature of an esp_ipc_isr function; the argument is unused. Runs with the cache on only (lives in flash). */
void core_park_predictor_off(void *unused);
void core_park_predictor_on(void *unused);
