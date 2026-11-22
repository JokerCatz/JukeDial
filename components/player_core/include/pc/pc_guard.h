// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Boot-loop guard decision (spec 3, safety): consecutive crashes -> safe mode at PC_GUARD_LIMIT.
 * A brownout is a power problem (flat battery), not a firmware crash: it neither counts nor clears. */
#define PC_GUARD_LIMIT 3
typedef enum { PC_BOOT_CLEAN, PC_BOOT_CRASH, PC_BOOT_POWER } pc_boot_t;
uint32_t pc_guard_count(uint32_t prev, pc_boot_t kind);
/* Console `crashtest safe` (card-flag gated): the counter value that makes the NEXT crash reset reach the limit. The only place that derives it; guard_arm_for_test() is the only writer. */
static inline uint32_t pc_guard_arm_for_test(void) { return PC_GUARD_LIMIT - 1; }
static inline bool pc_guard_safe_mode(uint32_t count) { return count >= PC_GUARD_LIMIT; }
/* The safe screen is tried at the limit and once more; a crash inside it costs at most two more boots, then the plain path (spec 4.4).
 * power_reset (this boot's reset reason is a brownout): never lit; lighting the panel on a flat battery can brown out again on every boot, and the count
 * does not move on a brownout, so the device would never reach the plain path (controller ruling U28). The count itself is untouched. */
static inline bool pc_guard_safe_screen(uint32_t count, bool power_reset) { return !power_reset && count >= PC_GUARD_LIMIT && count < PC_GUARD_LIMIT + 2; }

/* Owner 2026-11-16: after an ABNORMAL restart (panic, a watchdog, a brownout, a pin or any other reset that is not a deliberate esp_restart and not power-on) and whenever the boot is the
 * safe mode, the Bluetooth mode goes back to the default (off) before Bluetooth starts. Normal restarts (sysmode_reboot, the 重開機 row, the web reboot are all esp_restart), power-on and a
 * deep-sleep wake are unaffected. guard.c maps esp_reset_reason() to the class and keeps the decision in RTC memory until cfg is loaded (the card is not mounted that early). */
typedef enum { PC_RST_POWERON, PC_RST_SOFTWARE, PC_RST_DEEPSLEEP, PC_RST_OTHER } pc_reset_class_t;
static inline bool pc_guard_bt_default(pc_reset_class_t c, bool safe) { return safe || (c != PC_RST_POWERON && c != PC_RST_SOFTWARE && c != PC_RST_DEEPSLEEP); }
/* What the boot does with a pending decision once cfg_load has run: nothing; reset bt_mode to off; or skip it (no card: cfg cannot be written, one log line, never retried). */
typedef enum { PC_BTRST_NONE, PC_BTRST_RESET, PC_BTRST_SKIP_NO_CARD } pc_btrst_t;
static inline pc_btrst_t pc_guard_bt_apply(bool pending, bool card_ok, int stored_mode)
{
    if (!pending) return PC_BTRST_NONE;
    if (!card_ok) return PC_BTRST_SKIP_NO_CARD;
    return stored_mode != 0 ? PC_BTRST_RESET : PC_BTRST_NONE;
}
#ifdef __cplusplus
}
#endif
