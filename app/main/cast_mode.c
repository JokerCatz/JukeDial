// SPDX-License-Identifier: LGPL-2.1-or-later
#include "keys_svc.h"
#include "cast_mode.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_knob.h"
#include "bsp/bsp_sdcard.h"
#include "bsp/bsp_touch.h"
#include "audio_svc.h"
#include "bt_svc.h"
#include "cfg.h"
#include "lcd_clock.h"
#include "console.h"
#include "crashlog.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guard.h"
#include "memlog.h"
#include "pc/pc_paths.h"
#include "pc/pc_wifihold.h"
#include "player_ctl.h"
#include "radio_svc.h"
#include "sysmode.h"
#include "ui/font.h"
#include "ui/ui.h"
#include "wifi_svc.h"

/* The broadcast radio boot (cast_mode.h). Load list (this boot only): NVS, boot-loop guard, TF card + cfg, audio_svc (MP3 / AAC / OGG decoders, the Bluetooth sink), the display (LVGL
 * in PSRAM), touch, knob, haptics, fonts, the shared UI host + one ring page, wifi_svc (STA, lwIP), the radio pipeline (radio_svc: its 256 KB ring and its network task in PSRAM),
 * the BLE controller + Bluedroid + LE Audio + the LC3 source (bt_svc, the same code as the player's Auracast mode). NOT loaded: library, fx, covers, games, tools, the clock, the
 * power service, the player's pages. This file is built with -msmall-data-limit=0 (CMakeLists): its statics are PSRAM, no initialiser anywhere (an initialised static is .data = internal). */
#define MAGIC 0x43415354u                     /* "CAST" */
static RTC_NOINIT_ATTR uint32_t s_req;
static volatile int s_phase, s_station;
static volatile bool s_active;

bool cast_mode_peek_request(void) { return s_req == MAGIC; }   /* the boot front (boot_front.c): the same flag, not consumed */

bool cast_mode_take_request(void)
{
    bool r = s_req == MAGIC;
    s_req = 0;
    return r;
}

void cast_mode_request(void)
{
    s_req = MAGIC;
    sysmode_reboot();                          /* stores what is playing (the player resumes it afterwards), saves cfg, restarts */
}

bool cast_mode_active(void) { return s_active; }

void cast_mode_status(cast_status_t *o)
{
    memset(o, 0, sizeof *o);
    o->phase = s_phase;
    o->wifi = wifi_svc_connected();
    o->rssi = wifi_svc_rssi();
    o->n_stations = radio_count();
    o->station = radio_current() >= 0 ? radio_current() : s_station;
    const now_ops_t *n = player_ctl_now();
    if (n && n->title) n->title(o->now, sizeof o->now);
    o->volume = app_volume();
    snprintf(o->cast_name, sizeof o->cast_name, "%s", bt_svc_cast_name());
    radio_stats(&o->bytes, &o->reconnects, &o->underruns);
}

void cast_mode_exit(void)
{
    printf("APP cast mode exit\n");
    cfg_save_now();
    sysmode_reboot_msg(T(S_RESTARTING_TO_SWITCH_MODE) /* 重新開機切換模式… */);
}

static bool console_cmd(const char *c) { return cast_mode_cmd(c); }

void cast_mode_run(void)
{
    s_active = true;
    bt_svc_lean_begin();                       /* before anything asks for Wi-Fi */
    printf("APP cast mode\n");
    memlog("cast_boot");
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;
    crashlog_flush(g_sd_ok);
    cfg_load();
    audio_svc_start();
    audio_svc_set_volume(app_volume());
    esp_err_t ei = bsp_i2c_init();
    lv_display_t *disp = NULL;
    bsp_display_set_task_stack(16 * 1024, true);
    bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(false);   /* the experimental QSPI clock (lcd_clock.h) */
    esp_err_t ed = bsp_display_start_ex(&disp, (uint8_t)cfg_brightness());
    uint8_t hst = 0;
    esp_err_t eh = ei == ESP_OK ? bsp_haptic_init(&hst) : ei;
    printf("APP cast hw sd=%d i2c=%s display=%s haptic=%s\n", g_sd_ok, esp_err_to_name(ei), esp_err_to_name(ed), esp_err_to_name(eh));
    memlog("cast_display");
    if (ed != ESP_OK || !bsp_display_lock(-1)) {   /* no screen: no way back from here; the player boots instead */
        printf("APP cast no display (%s): back to the player in 5 s\n", esp_err_to_name(ed));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    lv_indev_t *touch = NULL;
    bsp_touch_start(disp, &touch);
    bsp_knob_start(disp, false, NULL);
    keys_svc_start_host();                     /* the FakePod two keys = the knob detents (the volume); nothing on a board with a knob */
    cast_screen_start(disp, touch);
    player_ctl_start();                        /* the audio events reach the radio (a decoder that gave up is restarted) */
    ui_volume_hook = audio_svc_set_volume;       /* the knob is the volume, as everywhere */
    bsp_display_unlock();
    memlog("cast_ui");
    guard_arm_healthy_timer();
    console_app_cmd = console_cmd;
    console_start();

    /* 1. Wi-Fi: the credentials file must exist (its content is never read here: wifi_svc does, and prints nothing of it) */
    struct stat st;
    wifi_svc_init();
    if (stat(PC_CFG_WIFI, &st)) {
        s_phase = CAST_PH_NO_WIFI;
        printf("APP cast no wifi file: nothing starts\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));      /* the screen is the way out: a hold goes back to the player */
    }
    s_phase = CAST_PH_WIFI;
    if (wifi_svc_hold(PC_WH_RADIO)) { s_phase = CAST_PH_WIFI_FAIL; printf("APP cast wifi hold refused\n"); }   /* kept for the whole boot; refused = the reason is on the screen */
    for (int t = 0; !wifi_svc_connected(); t++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (t == 119 && s_phase == CAST_PH_WIFI) s_phase = CAST_PH_WIFI_FAIL;   /* 60 s: an empty file, no SSID or a wrong password; it keeps trying and goes on if it connects */
        if (t % 20 == 19) printf("APP cast waiting for Wi-Fi %d s\n", (t + 1) / 2);
    }
    memlog("cast_wifi");
    /* 2. the broadcast, second (spike: the other order starves the stream) */
    s_phase = CAST_PH_START;
    int rc = bt_svc_lean_cast();
    printf("APP cast broadcast rc=%d\n", rc);
    memlog("cast_broadcast");
    if (rc) {                                           /* no broadcast: no radio either (nothing may reach the sink path in another way) */
        s_phase = CAST_PH_FAIL;
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    /* 3. the radio: the station chosen in the player before the reboot (cfg radio_last, else the first); no change in this mode */
    bsp_display_lock(-1);
    int n = radio_load();
    s_station = cfg_int("radio_last", 0);
    if (s_station < 0 || s_station >= n) s_station = 0;
    s_phase = CAST_PH_ON;
    if (n > 0) radio_play(s_station);
    bsp_display_unlock();
    memlog("cast_radio");
    for (int t = 1;; t++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (t % 10) continue;
        uint32_t b, r, u;
        radio_stats(&b, &r, &u);
        printf("APP cast t=%d wifi=%d rssi=%d free=%lu largest=%lu min=%lu psram=%lu bytes=%lu reconn=%lu underruns=%lu station=%d\n", t, wifi_svc_connected(), wifi_svc_rssi(),
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
               (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (unsigned long)b, (unsigned long)r, (unsigned long)u, radio_current());
    }
}
