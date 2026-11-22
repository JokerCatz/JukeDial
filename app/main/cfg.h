// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "pc/pc_paths.h"
#include "pc/pc_kv.h"
/* /sdcard/_player/config/app.txt (spec 5.5). Call from one task at a time (LVGL task after boot). */
#define CFG_PATH PC_CFG_DIR "/app.txt"
#define CFG_MAX 160   /* the keys the code writes: tools/check/cfg_keys.txt (tools/check/check_cfg_keys.py keeps the list within this) */
extern pc_kv_t g_cfg[CFG_MAX];
extern int g_cfg_n;
extern bool g_sd_ok;
void cfg_load(void);
bool cfg_has(const char *key);     /* the key is in the file (a first boot has no "lang": the language picker) */
int  cfg_int(const char *key, int def);
const char *cfg_str(const char *key, const char *def);
void cfg_set_int(const char *key, int v);
void cfg_del(const char *key);   /* removes the key (saved soon) */
void cfg_set_str(const char *key, const char *v);
void cfg_save_soon(void);     /* debounced: written ~3 s after the last change */
int  cfg_save_now(void);   /* before a reboot; 0 ok */
void cfg_tick(void);          /* call periodically; performs the debounced save */
int  cfg_haptic_mv(void);   /* the drive strength in mV: 0 when 震動 is off on the device (haptic_on = 0), else haptic_mv (web: the strength) */
int  app_volume(void);        /* BTVOL: the CURRENT output's volume (vol_out.c): wired, or the connected headset's own */
void app_set_volume(int v);   /* clamps to [0, volume_max]; stored for the current output */
bool app_risky_enabled(void); /* /sdcard/.app_risky exists */
#define CFG_SETUP_URL_MAX 4         /* the settings-page addresses: the primary first, then mirrors */
#define CFG_SETUP_URL_DEF "https://github.com/JokerCatz/JukeDial https://gitee.com/jokercatz/JukeDial"   /* the two public repos (primary GitHub, mirror Gitee) */
int  cfg_setup_urls(char *buf, size_t n, const char *out[CFG_SETUP_URL_MAX]);   /* "setup_url" = addresses split by spaces / commas / semicolons (a list, primary first; none = the default); copies them into buf (>= 256) and points out[] into it; returns how many (>= 1) */
int  cfg_rotation(void);        /* the card's "rotate" (0 upright, 1 turned 180; anything else 0; 0 without a card) */
int  cfg_brightness(void);      /* the card's "brightness" (the backlight percent), default 60 */
