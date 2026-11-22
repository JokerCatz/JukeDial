// SPDX-License-Identifier: LGPL-2.1-or-later
/* The firmware's start order (app/main/app_main.c, display branch) minus what the board needs for hardware: the serial half of the
 * console, soak log, guard, the radio memory test. Services are the fakes in fake_*.c. Called once, after the display exists. */
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "speed_svc.h"
#include "album_svc.h"
#include "bt_svc.h"
#include "cfg.h"
#include "clock_svc.h"
#include "timers_svc.h"
#include "console.h"
#include "font.h"
#include "fx_state.h"
#include "inst_beat.h"
#include "inst_ble.h"
#include "inst_chip.h"
#include "inst_engine.h"
#include "inst_fm.h"
#include "inst_kit.h"
#include "inst_ui.h"
#include "library_svc.h"
#include "lowmem.h"
#include "bt_meta.h"
#include "player_ctl.h"
#include "power_svc.h"
#include "psram_alloc.h"
#include "screens.h"
#include "sim.h"
#include "sleep_timer.h"
#include "stats_svc.h"
#include "gamesave_svc.h"
#include "sysmode.h"
#include "thumb_svc.h"
#include "ui.h"
#include "ui_safe.h"
#include "usb_mode.h"
#include "ext_mode.h"
#include "cast_mode.h"
#include "bsp/bsp_display.h"
#include "boot_front.h"
#include "wifi_svc.h"

/* The instrument app (--mode inst): instrument_run()'s start order without the board (no I2C, haptics, BLE stack, console task, guard). */
static void boot_inst(lv_display_t *disp)
{
    static pc_instr_cfg_t cfg;
    psram_alloc_hooks();
    g_sd_ok = sim_state_int("sd", 1) != 0;
    cfg_load();
    const char *saved = cfg_str("inst", NULL);
    pc_instr_cfg_parse(saved, &cfg);
    inst_fm_set_program_all(cfg.fm);
    if (!saved) cfg.volume = app_volume();
    printf("INST engine=%d\n", inst_engine_start(&cfg, inst_chip_find(cfg.chip)));
    inst_beat_start(cfg.beat_haptic, cfg_haptic_mv());
    if (g_sd_ok && cfg.kit[0]) inst_kit_load(cfg.kit);
    lv_indev_t *touch = sim_input_start(disp);
    font_init();
    if (!font_has_cjk()) printf("SIM warn no CJK font\n");
    inst_ui_start(disp, touch, &cfg);
    lowmem_start();
    printf("INST ble=%d\n", inst_ble_start());
    console_app_cmd = inst_cmd_ui;
    sim_on_command(console_exec);
}

/* The external mode (--mode ext, alias usb): usb_mode_run()'s start order without the board (no I2C, haptics, MSC, TinyUSB, key task, its loop);
 * the screen is the shared usb_mode_screen_start (ui/scr_ext.c), the state is fake_usb_mode.c (simusb). The console is the player's (test only:
 * the board's USB mode has none). */
static void boot_ext(lv_display_t *disp)
{
    psram_alloc_hooks();
    g_sd_ok = sim_state_int("sd", 1) != 0;
    cfg_load();
    lv_indev_t *touch = sim_input_start(disp);
    fake_ext_start();                                   /* the config from <root>/_player/config/keys.txt (the board: usb_mode_run reads it before the card goes to the computer), the HID sender */
    ext_screen_start(disp, touch);
    if (!font_has_cjk()) printf("SIM warn no CJK font\n");
    if (!sim_state_int("usb_sd", 0)) g_sd_ok = false;  /* the card is the computer's from here on (bsp_sdcard_unmount on the board); SIM_STATE usb_sd=1 (test lever) keeps it ours,
                                                        * so the only thing that stops a settings write is the host's cfg_writes=false (t12_usb_no_cfg_write) */
    sim_on_command(console_exec);
}

/* The broadcast radio (--mode cast): cast_mode_run()'s start order without the board (no I2C, haptics, Wi-Fi, Bluetooth stack, console task); the page is the real ui/scr_cast.c. */
static void boot_cast(lv_display_t *disp)
{
    psram_alloc_hooks();
    g_sd_ok = sim_state_int("sd", 1) != 0;
    cfg_load();
    audio_svc_start();
    audio_svc_set_volume(app_volume());
    lv_indev_t *touch = sim_input_start(disp);
    cast_screen_start(disp, touch);
    if (!font_has_cjk()) printf("SIM warn no CJK font\n");
    player_ctl_start();
    ui_volume_hook = audio_svc_set_volume;
    fake_cast_start();
    console_app_cmd = fake_cast_cmd;
    sim_on_command(console_exec);
}

/* Safe mode (--mode safe): guard_safe_mode's screen path without the board: the display at 40 %, then ui_safe_show, nothing else (no cfg, card, touch, knob,
 * services). The console is the player's (test only). `SIM timers pre=<n>` just before the call lets a test count the timers the screen adds. */
static void boot_safe(lv_display_t *disp)
{
    int pre = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) pre++;
    bsp_display_backlight_set(40);
    printf("SIM timers pre=%d\n", pre);
    ui_safe_show(disp, "3 consecutive crashes", 3);
    sim_on_command(console_exec);
}

void sim_boot(lv_display_t *disp)
{
    if (strcmp(g_sim_mode, "safe")) boot_front_mode(!strcmp(g_sim_mode, "ext") ? "usb" : g_sim_mode);   /* app_main.c: boot_front_run() after the recovery window, every mode but the safe one (the board's dispatch finds its mode by flags) */
    if (!strcmp(g_sim_mode, "safe")) { boot_safe(disp); return; }
    if (!strcmp(g_sim_mode, "inst")) { boot_inst(disp); return; }
    if (!strcmp(g_sim_mode, "ext")) { boot_ext(disp); return; }
    if (!strcmp(g_sim_mode, "cast")) { boot_cast(disp); return; }
    psram_alloc_hooks();
    g_sd_ok = sim_state_int("sd", 1) != 0;             /* the root directory is the card; an empty one is a new card; SIM_STATE sd=0: no card */
    cfg_load();
    sim_rotation_latch();
    audio_svc_start();
    audio_svc_set_volume(app_volume());
    speed_svc_init();                   /* Track B: the stored speed and crossfade go to the audio service */
    power_svc_start();
    lv_indev_t *touch = sim_input_start(disp);
    font_init();
    if (!font_has_cjk()) printf("SIM warn no CJK font\n");
    ui_start(disp, touch);
    library_svc_load();
    stats_svc_start();
    gamesave_start();
    player_ctl_start();
    bt_meta_start();
    thumb_svc_start();
    album_svc_start(!strcmp(cfg_str("now_view", "cover"), "album"));
    fx_svc_start();
    sleep_timer_start();
    wifi_svc_init();
    bt_svc_start();
    ui_volume_hook = audio_svc_set_volume;
    usb_mode_watch_start();
    clock_svc_start();
    timers_start();
    ui_push(&scr_menu, NULL);                          /* after the service timers (app_main.c order) */
    if (langpick_first_boot()) ui_push(&scr_langpick, NULL);   /* i18n B: no "lang" key in the config = first boot: the picker over the menu (safe mode and the USB drive mode never get here) */
    else ui_restore_last();                                 /* after every service timer exists: the leak tripwire counts timers from its push */
    sysmode_resume();
    lowmem_start();
    sim_on_command(console_exec);
}

/* Shutdown, first half: the screens go while the fonts they use still exist (deleting a focused label after its font was freed reads freed memory).
 * ui_pop destroys one screen per call and is a no-op at the bottom; the bottom screen and the top layer are cleaned by hand. */
void sim_ui_close(void)
{
    if (!lv_display_get_default()) return;
    for (int i = 0; i < 64; i++) ui_pop();
    lv_obj_clean(lv_screen_active());
    lv_obj_clean(lv_layer_top());
}
