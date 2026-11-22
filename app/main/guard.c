// SPDX-License-Identifier: LGPL-2.1-or-later
#include "guard.h"
#include <stdio.h>
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "pc/pc_guard.h"
#include "bsp/bsp_board.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_i2c.h"
#include "ui/ui_safe.h"
#include "cast_mode.h"
#include "cfg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAGIC 0x5AFEB007u
#define HEALTHY_US (60LL * 1000 * 1000)
#define BT_MAGIC 0x0B7DEF01u
static RTC_NOINIT_ATTR uint32_t s_magic, s_crashes, s_bt_pending;   /* RTC memory: no internal RAM; the Bluetooth reset waits here until cfg is loaded */
static bool s_last_crash, s_last_power;

bool guard_boot(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    if (s_magic != MAGIC) { s_magic = MAGIC; s_crashes = 0; s_bt_pending = 0; }
    pc_boot_t kind = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_CPU_LOCKUP ? PC_BOOT_CRASH   /* CPU lockup (reason 15): seen 2026-11-19, it was not counted, so a lockup-only loop never reached safe mode */
                   : r == ESP_RST_BROWNOUT ? PC_BOOT_POWER : PC_BOOT_CLEAN;
    s_crashes = pc_guard_count(s_crashes, kind);
    s_last_crash = kind == PC_BOOT_CRASH;
    s_last_power = kind == PC_BOOT_POWER;
    printf("APP guard reset_reason=%d consecutive_crashes=%lu\n", r, (unsigned long)s_crashes);
    const pc_reset_class_t cls = r == ESP_RST_POWERON ? PC_RST_POWERON : r == ESP_RST_SW ? PC_RST_SOFTWARE : r == ESP_RST_DEEPSLEEP ? PC_RST_DEEPSLEEP : PC_RST_OTHER;
    if (pc_guard_bt_default(cls, pc_guard_safe_mode(s_crashes))) s_bt_pending = BT_MAGIC;   /* stays set until applied: a crash before the apply keeps it */
    return !pc_guard_safe_mode(s_crashes);
}

void guard_bt_reset_apply(bool card_ok)
{
    const pc_btrst_t a = pc_guard_bt_apply(s_bt_pending == BT_MAGIC, card_ok, cfg_int("bt_mode", 0));
    if (s_bt_pending == BT_MAGIC) s_bt_pending = 0;          /* once: never a loop */
    if (a == PC_BTRST_SKIP_NO_CARD) printf("APP guard bt reset skipped: no card\n");
    else if (a == PC_BTRST_RESET) { cfg_set_int("bt_mode", 0); cfg_save_soon(); printf("APP guard bt mode reset to off (abnormal restart or safe mode)\n"); }
}

/* The board this image is for, readable straight from the image file: the web flasher (tools/web/flasher.js) and tools/release.py search the app for "JUKEDIAL_BOARD=<id>;"
   and refuse a file whose board is not the one the user picked.  const data = flash rodata (0 B of internal RAM); `used, retain` keeps it past --gc-sections (plain `used` is not enough: the linker dropped it). */
__attribute__((used, retain)) static const char s_board_marker[] = "JUKEDIAL_BOARD=" BSP_BOARD_ID ";";

void guard_board_check(void)
{
    const char *what = "";
    const bool ok = bsp_board_ok(&what);
    uint32_t fid = 0;
    uint8_t mac[6] = {0};
    esp_flash_read_id(NULL, &fid);          /* a pin-free second signal for the log (not gating: the FakePod's flash maker is not certain) */
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("APP board %s (%s) image=%s flash_id=0x%06x mac=%02x:%02x:%02x:%02x:%02x:%02x\n", ok ? "ok" : "WRONG", what, BSP_BOARD_ID, (unsigned)fid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (ok) {
#ifdef BSP_DAC_CS43131                     /* FakePod I1: the right board, before anything else touches audio (every boot mode, safe mode included): the CS43131 is held in reset, so a DAC left running by the last boot is silenced (bsp_dac_init pulses it later) */
        const gpio_config_t g = { .pin_bit_mask = 1ULL << BSP_PIN_DAC_RST, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&g);
        gpio_set_level(BSP_PIN_DAC_RST, 0);
#endif
        return;
    }
    for (;;) {                             /* nothing else runs: no pin, no flash write, no radio */
        printf("APP WRONG BOARD (%s): this image is for the %s; flash the other one\n", what, BSP_BOARD_ID);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

bool guard_last_reset_was_crash(void) { return s_last_crash; }
uint32_t guard_crashes(void) { return s_magic == MAGIC ? s_crashes : 0; }

void guard_arm_for_test(void) { if (s_magic == MAGIC && s_crashes == 0) s_crashes = pc_guard_arm_for_test(); }   /* the only upward write besides pc_guard_count at boot */

static void healthy_cb(void *arg)
{
    (void)arg;
    s_crashes = 0;
    printf("APP guard healthy\n");
}

void guard_arm_healthy_timer(void)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t a = { .callback = healthy_cb, .name = "guard" };
    if (!t && esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, HEALTHY_US);
}

void guard_safe_mode(const char *why)
{
    s_bt_pending = BT_MAGIC;                   /* the safe mode: Bluetooth off at the next normal boot (guard_boot already says so; a reflash keeps it) */
    cast_mode_take_request();                  /* a pending broadcast-radio request must not outlive safe mode (the next boot is the player); one-shot, nothing else */
#ifdef BSP_PIN_LCD_BLK                     /* the FakePod has neither a backlight pin (AMOLED) nor a DAC mute pin (the CS43131 is quiet in reset) */
    gpio_set_direction(BSP_PIN_LCD_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_PIN_LCD_BLK, 0);
#endif
#ifdef BSP_PIN_DAC_XSMT
    gpio_set_direction(BSP_PIN_DAC_XSMT, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_PIN_DAC_XSMT, 0);   /* always first */
#endif
    /* a brownout reset (flat battery) takes the plain path: no panel to light (U28) */
    if (pc_guard_safe_screen(guard_crashes(), s_last_power)) {
        /* one static screen (ui_safe.c): I2C, then the display with its own LVGL task stack (16 KB in internal RAM: safe mode starts nothing
         * else, so internal RAM is free here, and the path does not depend on PSRAM), brightness fixed at 40 %, no touch, no knob, no cfg, no card */
        bsp_i2c_init();
        lv_display_t *disp = NULL;
        bsp_display_set_task_stack(16 * 1024, false);
        bsp_display_start_ex(&disp, 40);
        if (disp) {
            bsp_display_lock(-1);
            ui_safe_show(disp, why, (unsigned)guard_crashes());
            bsp_display_unlock();
        }
    }
    /* Core 1 is left idle here, NOT stalled or parked: safe mode creates nothing on it (the LVGL task is pinned to core 0 by the BSP),
     * so it only runs its idle task. Stalling it (core_park_begin) for the whole of safe mode would hold it far longer than the 800 ms
     * interrupt watchdog (CONFIG_ESP_INT_WDT_CHECK_CPU1), which would itself reset the board: a new crash loop. tools/check/check_safe_mode.py
     * fails the build if this path starts a task off core 0, calls core_park_*, or starts audio / Bluetooth / Wi-Fi / USB / card code. */
    for (;;) {
        printf("APP SAFE MODE (%s): nothing runs; reflash over USB\n", why);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
