// SPDX-License-Identifier: LGPL-2.1-or-later
#include "charge_mode.h"
#if BSP_HAS_PMU
#include <stdio.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_keys.h"
#include "bsp/bsp_pmu.h"
#include "bsp/bsp_sdcard.h"
#include "bsp/bsp_touch.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cfg.h"
#include "lcd_clock.h"
#include "guard.h"
#include "memlog.h"
#include "pc/pc_pwrpol.h"
#include "ui/ui.h"

/* RTC memory: the power-on decision of this power session. FULL = the real boot was chosen (a plug-in boot already showed the charge screen, or the unit was started with the key): every later
 * software restart boots the player. A cold power-on loses RTC memory, so the next plug-in boot decides afresh. */
#define MAGIC_FULL 0xC4A6E5F1u
static RTC_NOINIT_ATTR uint32_t s_session;
static EXT_RAM_BSS_ATTR charge_status_t s_st;
static EXT_RAM_BSS_ATTR volatile bool s_start_req, s_off_req;

bool charge_mode_wanted(void)
{
    if (s_session == MAGIC_FULL) return false;
    uint8_t r20 = 0, r21 = 0;
    if (bsp_i2c_init() != ESP_OK || bsp_pmu_pwron_source(&r20, &r21) != ESP_OK) { s_session = MAGIC_FULL; return false; }   /* nothing known: the player boots as before */
    printf("APP pwron source r20=%02x r21=%02x\n", r20, r21);
    if (pc_pwron_is_plug_only(r20)) return true;
    s_session = MAGIC_FULL;
    return false;
}

void charge_mode_set_lowbat(int pct) { s_st.lowbat = true; s_st.pct = pct; }

void charge_mode_status(charge_status_t *s)
{
    *s = s_st;
}

void charge_mode_start_player(void) { s_start_req = true; }
void charge_mode_power_off(void) { s_off_req = true; }

static void poll_pmu(void)
{
    pc_axp_t a;
    if (bsp_pmu_battery(&a) != ESP_OK) return;
    s_st.pct = a.pct;
    s_st.plugged = a.vbus;
    s_st.charging = a.dir == PC_AXP_CHARGING;
    s_st.full = a.vbus && a.chg_state == 4;
}

static uint32_t host_idle_ms(void) { return 8000; }     /* the screen dims a few seconds after the last touch / key (the panel's real sleep-in) */
static bool host_knob(int d) { (void)d; return true; }  /* a key click only wakes the screen: nothing here is a volume */
static const ui_host_t k_host = { .name = "charge", .knob_default = host_knob, .idle_ms = host_idle_ms, .cfg_writes = false, .haptics = false };

extern void charge_screen_start(lv_display_t *d, lv_indev_t *touch, const ui_host_t *h);

void charge_mode_run(void)
{
    printf("APP charge mode\n");
    memlog("charge_boot");
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;               /* only for the settings (language, brightness, rotation); nothing is played from it */
    cfg_load();
    esp_err_t ei = bsp_i2c_init();
    lv_display_t *disp = NULL;
    bsp_display_set_task_stack(16 * 1024, true);
    bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(false);   /* the experimental QSPI clock (lcd_clock.h) */
    esp_err_t ed = bsp_display_start_ex(&disp, (uint8_t)cfg_brightness());
    printf("APP charge hw sd=%d i2c=%s display=%s\n", g_sd_ok, esp_err_to_name(ei), esp_err_to_name(ed));
    if (ed != ESP_OK || !bsp_display_lock(-1)) {          /* no screen: the player boots instead (the decision below) */
        printf("APP charge no display (%s): the player boots\n", esp_err_to_name(ed));
        charge_mode_start_player();
    } else {
        lv_indev_t *touch = NULL;
        bsp_touch_start(disp, &touch);
        bsp_keys_init();
        poll_pmu();
        charge_screen_start(disp, touch, &k_host);
        bsp_display_unlock();
    }
    guard_arm_healthy_timer();
    bool top_prev = false, bottom_prev = false;
    int unplugged_s = 0, tick = 0;
    for (;;) {
        bool top = false, bottom = false;
        bsp_keys_read(&top, &bottom);
        if ((top && !top_prev) || s_start_req) {          /* the power key: the real boot */
            printf("APP charge: full boot\n");
            s_session = MAGIC_FULL;
            cfg_save_now();                               /* language / brightness / rotation changed here: before the debounce would have written them */
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }
        if ((bottom && !bottom_prev) && bsp_display_lock(-1)) { ui_knob_inject(1); bsp_display_unlock(); }   /* wakes a dark screen (host_knob swallows the detent) */
        top_prev = top; bottom_prev = bottom;
        if (s_off_req) { s_off_req = false; bsp_sdcard_unmount(); printf("APP charge: power off rc=%d\n", (int)bsp_pmu_power_off()); }
        if (++tick % 20 == 0) {                           /* once a second */
            poll_pmu();
            unplugged_s = s_st.plugged ? 0 : unplugged_s + 1;
            if (unplugged_s >= 5) { printf("APP charge: unplugged, power off\n"); bsp_sdcard_unmount(); bsp_pmu_power_off(); unplugged_s = -30; }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#else
bool charge_mode_wanted(void) { return false; }
void charge_mode_run(void) { for (;;) {} }
void charge_mode_status(charge_status_t *s) { *s = (charge_status_t){ .pct = -1 }; }
void charge_mode_set_lowbat(int pct) { (void)pct; }
void charge_mode_start_player(void) {}
void charge_mode_power_off(void) {}
#endif
