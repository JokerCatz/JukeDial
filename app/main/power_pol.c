// SPDX-License-Identifier: LGPL-2.1-or-later
#include "power_pol.h"
#if BSP_HAS_PMU
#include <stdio.h>
#include "bsp/bsp_audio.h"
#include "bsp/bsp_pmu.h"
#include "bsp/bsp_sdcard.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_svc.h"
#include "charge_mode.h"
#include "i18n.h"
#include "player_ctl.h"
#include "power_svc.h"
#include "sfx.h"
#include "sysmode.h"
#include "ui/font.h"
#include "ui/screens.h"
#include "ui/ui.h"

static EXT_RAM_BSS_ATTR pc_apo_t s_apo;
static EXT_RAM_BSS_ATTR pc_lowbat_t s_lb;
static EXT_RAM_BSS_ATTR uint32_t s_last_ms;
static EXT_RAM_BSS_ATTR bool s_started, s_off;
static EXT_RAM_BSS_ATTR char s_msg[96];   /* the strip / page keeps the pointer while it is up */

/* The shutdown sequence (NightPlayer's order, PMU_RTC_DRIVERS 2.4: place saved, playback stopped, the DAC powered down WHILE the clocks still run, the card unmounted, then the PMU).
 * Our DAC order stays mute first (pc_cs_stop). If the PMU does not cut the power (it answers an error, or an external supply holds it), the unit stays up with the message. */
static void off_task(void *arg)
{
    const bool sound = arg != NULL;
    vTaskDelay(pdMS_TO_TICKS(sound ? 2200 : 1200));      /* the message draws; the warning sound is heard */
    audio_svc_stop(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    bsp_audio_power_down();                              /* mute, ramp, amp and serial port down, crystal off, RESET low */
    bsp_sdcard_unmount();
    esp_err_t e = bsp_pmu_power_off();
    printf("APP power off requested rc=%d\n", (int)e);
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("APP power off did not happen: staying up\n");
    s_off = false;                                       /* the policy may try again later */
    vTaskDelete(NULL);
}

void power_pol_off(const char *msg, bool warn_sound)
{
    if (s_off) return;
    s_off = true;
    sysmode_store_state();                                /* the resume position, the sound, the games, cfg: before anything is stopped */
    if (warn_sound) sfx_play("timer_warn");               /* the synth path (before the device gain, like every toy sound) */
    ui_push_message(msg);
    xTaskCreate(off_task, "poweroff", 3072, warn_sound ? (void *)1 : NULL, 5, NULL);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t now = lv_tick_get();
    const uint32_t dt = s_last_ms ? now - s_last_ms : 0;
    s_last_ms = now ? now : 1;
    if (s_off) return;
    const int pct = power_svc_batt_pct();
    const bool ext = power_svc_state() == PC_PWR_USB;       /* external power present (or no cell at all: then it is the plug that runs it) */
    const int a = pc_lowbat_tick(&s_lb, pct, ext, dt);
    if (a == PC_LB_WARN) { snprintf(s_msg, sizeof s_msg, T(S_LOWBAT_WARN) /* Battery low (%d %%): please charge */, pct); ui_push_warning(s_msg); }
    else if (a == PC_LB_SHUTDOWN) { power_pol_off(T(S_LOWBAT_OFF) /* Battery empty: your place is saved, powering off */, true); return; }
    const now_ops_t *o = player_ctl_now();
    const bool playing = o && o->playing && o->playing();
    const int min = power_pol_autooff_min();
    if (ui_screen_on() && pc_apo_ext_screen_off(min, ext, ui_input_idle_ms())) ui_sleep();   /* OLED on external power: the idle time turns the screen off, the unit stays up */
    if (pc_apo_tick(&s_apo, min, dt, playing, ext, ui_input_idle_ms())) {
        snprintf(s_msg, sizeof s_msg, T(S_AUTO_OFF_MSG) /* Idle for %d min: powering off */, min);
        power_pol_off(s_msg, false);
    }
}

void power_pol_start(void)
{
    if (s_started) return;
    s_started = true;
    lv_timer_create(tick_cb, 1000, NULL);
}

static void boot_off_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));
    bsp_sdcard_unmount();
    bsp_pmu_power_off();
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("APP lowbat boot: power off did not happen\n");
    vTaskDelete(NULL);
}

bool power_pol_boot_check(void)
{
    pc_axp_t a;
    if (bsp_pmu_battery(&a) != ESP_OK || !pc_lowbat_boot_refuse(a.pct, a.vbus)) return false;
    printf("APP lowbat boot refused pct=%d: powering off in 5 s\n", a.pct);
    charge_mode_set_lowbat(a.pct);
    ui_push(&scr_charge, NULL);                          /* the same page as the charge screen, in its low-battery wording; no service has started: no audio, no Bluetooth */
    xTaskCreate(boot_off_task, "lowbat_off", 3072, NULL, 5, NULL);
    return true;
}
#else
void power_pol_start(void) {}
bool power_pol_boot_check(void) { return false; }
void power_pol_off(const char *msg, bool warn_sound) { (void)msg; (void)warn_sound; }
#endif
