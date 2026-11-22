// SPDX-License-Identifier: LGPL-2.1-or-later
/* JC3636F118 music player (sub-project 2). Boot order per spec 4.2: guard, 3 s grace, TF, config,
 * display last. */
#include <stdio.h>
#include "pc/pc_paths.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_knob.h"
#include "bsp/bsp_sdcard.h"
#include "bsp/bsp_touch.h"
#include "audio_svc.h"
#include "speed_svc.h"
#include "console.h"
#include "fx_state.h"
#include "sleep_timer.h"
#include "library_svc.h"
#include "bt_meta.h"
#include "player_ctl.h"
#include "stats_svc.h"
#include "gamesave_svc.h"
#include "thumb_svc.h"
#include "album_svc.h"
#include "cfg.h"
#include "lcd_clock.h"
#include <dirent.h>
#include <string.h>
#include "font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guard.h"
#include "imu_svc.h"
#include "keys_svc.h"
#include "crashlog.h"
#include "pack_stamp.h"
#include "crashnet.h"
#include "pc/pc_crash.h"
#include "soak_log.h"
#include "bt_svc.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "psram_alloc.h"
#include "memlog.h"
#include "lowmem.h"
#include "power_svc.h"
#include "power_pol.h"
#include "charge_mode.h"
#include "wifi_svc.h"
#include "sysmode.h"
#include "boot_front.h"
#include "usb_mode.h"
#include "instrument.h"
#include "cast_mode.h"
#include "clock_svc.h"
#include "timers_svc.h"
#include "screens.h"
#include "sdkconfig.h"
#include "ui.h"
#include "cpu_clock.h"
#include "ui/ui_tour.h"

#if CONFIG_APP_TEST_PLAY
static void close_file(void *f) { fclose(f); }
static void test_play(void)
{
    char path[300] = "";
    DIR *d = opendir(PC_EXPORT_DIR);
    struct dirent *e;
    while (d && !path[0] && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char sub[280];
        snprintf(sub, sizeof sub, PC_EXPORT_DIR "/%s", e->d_name);
        DIR *d2 = opendir(sub);
        struct dirent *f;
        while (d2 && (f = readdir(d2)))
            if (strstr(f->d_name, ".mp3")) { snprintf(path, sizeof path, "%s/%s", sub, f->d_name); break; }
        if (d2) closedir(d2);
    }
    if (d) closedir(d);
    FILE *fp = path[0] ? fopen(path, "rb") : NULL;
    pc_reader_t r;
    if (fp) setvbuf(fp, NULL, _IONBF, 0);   /* before any I/O; the backend has its own 16 KB buffer */
    if (!fp || pc_reader_file(&r, fp)) { printf("APP test_play none\n"); return; }
    printf("APP test_play %s gen=%lu\n", path, (unsigned long)audio_svc_play(&pc_backend_mp3, &r, 0, 0, close_file, fp));
}
#endif

void app_main(void)
{
    memlog_boot_take();                 /* MEMB: the console `memboot` flag (RTC), default off */
    memlog_step("app_main");
    const bool boot_ok = guard_boot();  /* RTC memory only, no pin: a crash inside the board probe is still counted */
    guard_board_check();                /* then the wrong board's image touches no other pin (the two boards' pins collide); safe mode drives pins, so it comes after */
    crashnet_early(guard_last_reset_was_crash());   /* crash safety net: the track that was playing, from RTC memory (cleared here for every boot mode); the card part follows the mount */
    if (!boot_ok) guard_safe_mode("3 consecutive crashes");
    crashlog_boot();                    /* the last crash's breadcrumbs -> pending (written after the card mount) */
#if CONFIG_BT_ENABLED
    {   /* NVS (Bluetooth bonds, PHY calibration) on this task: its stack is internal RAM, flash writes freeze the cache */
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    }
#endif
    psram_alloc_hooks();
    memlog_step("psram_hooks");
    lcd_clock_escape_poll();            /* recovery window before anything risky (3 s; the BOOT key held in it clears the experimental display clock, lcd_clock.h) */
    boot_front_run();                   /* the boot front (boot_front.h): the power-on picture of the mode about to run, before the dispatch below; flags only peeked */
    if (usb_mode_take_request()) { crashlog_mode(PC_CRASH_MODE_USB); usb_mode_run(); }   /* one shot, never returns (restarts into normal mode); no crash flush: kept for the next boot */
    if (instrument_take_request()) { crashlog_mode(PC_CRASH_MODE_INST); instrument_run(); }   /* one shot, never returns (exit = restart) */
    if (cast_mode_take_request()) { crashlog_mode(PC_CRASH_MODE_CAST); cast_mode_run(); }   /* one shot, never returns (exit = restart) */
    if (charge_mode_wanted()) { crashlog_mode(PC_CRASH_MODE_NORMAL); charge_mode_run(); }   /* FakePod only: the cable switched it on, not the power key: the charge screen, no audio / Bluetooth; never returns (the power key restarts into the player) */
    crashlog_mode(PC_CRASH_MODE_NORMAL);
    printf("APP boot\n");
    memlog("boot");
    {   /* the last USB mode's internal RAM (its console is on the other side of the plug): printed once, then the RTC note is cleared (pre-flight T12-2) */
        uint32_t uf, um;
        if (usb_mode_mem_last(&uf, &um)) printf("APP usb mem last free=%lu min=%lu\n", (unsigned long)uf, (unsigned long)um);
    }
#if CONFIG_APP_TEST_NO_SD
    g_sd_ok = false;
#else
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;
#endif
    crashnet_card(g_sd_ok);             /* the marks on the card + the track found above */
    crashlog_flush(g_sd_ok);            /* the last crash: one line, printed, and appended to /sdcard/_player/logs/crash.txt */
    printf("APP sd mounted=%d\n", g_sd_ok);
    if (g_sd_ok) pack_stamp_boot();    /* the card pack's stamp: logged; the Status page warns when it is missing or older than PC_PACK_MIN */
    memlog_step("sd_mount");
    cfg_load();
    memlog_step("cfg_load");
    printf("APP risky=%d\n", app_risky_enabled());
    audio_svc_start();
    memlog_step("audio_svc");
    audio_svc_set_volume(app_volume());
    speed_svc_init();                   /* Track B: the stored speed and crossfade go to the audio service */
    esp_err_t ei = bsp_i2c_init();      /* before power_svc_start: the FakePod PMU task needs this bus at once */
    memlog_step("i2c");
    power_svc_start();
    memlog_step("power_svc");
    lv_display_t *disp = NULL;
    bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(true);   /* experimental QSPI clock (lcd_clock.h); the line above: screen rotation from the card (0 without one); the panel, the touch and the keys follow */
    bsp_display_set_task_stack(24 * 1024, true);   /* UI-side work (GME probe, JPEG, FreeType) needs a deep stack */
    esp_err_t ed = bsp_display_start_ex(&disp, (uint8_t)cfg_brightness());
    uint8_t hst = 0;
    esp_err_t eh = ei == ESP_OK ? bsp_haptic_init(&hst) : ei;
    printf("APP hw i2c=%s display=%s haptic=%s\n", esp_err_to_name(ei), esp_err_to_name(ed), esp_err_to_name(eh));
    memlog("display");
    memlog_step("display");
    if (ed == ESP_OK && bsp_display_lock(-1)) {
        lv_indev_t *touch = NULL;
        bsp_touch_start(disp, &touch);
        memlog_step("touch");
        bsp_knob_start(disp, false, NULL);
        memlog_step("knob");
        keys_svc_start();                      /* the FakePod's two keys as the knob; nothing on a board with a knob */
        memlog_step("keys");
        imu_svc_start();                       /* the FakePod's tilt source for the maze; nothing without an IMU */
        memlog_step("imu");
        font_init();
        memlog_step("font_init");
        ui_start(disp, touch);
        memlog_step("ui_start");
        if (power_pol_boot_check()) {          /* FakePod only: below 2 % on battery: the page says so, the unit powers off in 5 s; no library, no Bluetooth, nothing else starts */
            bsp_display_unlock();
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        library_svc_load();
        memlog_step("library");
        stats_svc_start();
        memlog_step("stats");
        gamesave_start();                      /* games.txt: the games' bests and the tools' values (ideas G2) */
        memlog_step("gamesave");
        player_ctl_start();
        memlog_step("player_ctl");
        bt_meta_start();                       /* speaker mode's AVRCP track: two LVGL timers, paused until a phone */
        memlog_step("bt_meta");
        thumb_svc_start();
        memlog_step("thumb");
        album_svc_start(!strcmp(cfg_str("now_view", "cover"), "album"));   /* photos: built ~30 s after boot when shown while playing */
        memlog_step("album");
        fx_svc_start();
        memlog_step("fx");
        sleep_timer_start();
        memlog_step("sleep_timer");
        soak_log_start((int)esp_reset_reason());
        memlog_step("soak_log");
        wifi_svc_init();                       /* radio stays off until asked */
        memlog_step("wifi_init");
        guard_bt_reset_apply(g_sd_ok);         /* an abnormal restart or the safe mode: the stored Bluetooth mode goes back to off first */
        bt_svc_start();                        /* the stored Bluetooth mode (spec bluetooth 4.1) */
        memlog_step("bt_start");
        ui_volume_hook = audio_svc_set_volume;
        usb_mode_watch_start();                /* the plug on the OTG side: offer the USB drive mode */
        memlog_step("usb_watch");
        clock_svc_start(); timers_start();                     /* SNTP, alarm, pomodoro */
        memlog_step("clock");
        power_pol_start();                     /* FakePod only: auto power-off, the 15 % warning, the 60 s below 5 % rule */
        ui_push(&scr_menu, NULL);             /* after the service timers too: the root's leak snapshot (checked on rebuild) holds them */
        memlog_step("ui_menu");
        if (langpick_first_boot()) ui_push(&scr_langpick, NULL);   /* i18n B: no "lang" key in the config = first boot: the picker over the menu (safe mode and the USB drive mode never get here) */
        else if (tour_needed()) ui_push(&scr_tour, NULL);   /* GS: a card with no tour_done: the gesture tutorial first (safe, USB and external boots never get here) */
        else ui_restore_last();                     /* after every service timer exists: the leak tripwire counts timers from its push */
        sysmode_resume();                      /* what played before a mode-switch reboot */
        memlog_step("sysmode_resume");
        bsp_display_unlock();
    }
    lowmem_start();
    memlog_step("lowmem");
    guard_arm_healthy_timer();
    console_start();
    cpu_clock_boot_done();                 /* experimental CPU clock: only here, at the end of a normal player boot (the other modes never get here and stay at 320) */
    memlog_step("console");
    memlog_boot_end();
#if CONFIG_APP_TEST_PLAY
    test_play();
#endif
#if CONFIG_APP_TEST_PANIC
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("APP test panic\n");
    abort();
#endif
}
