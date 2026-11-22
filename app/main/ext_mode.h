// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "pc/pc_extkeys.h"
/* 外接模式 T2 (spec 2026-11-15-external-mode.md sections 5, 6): the layer / key model of the external mode and its screen. Both halves are compiled by the
 * firmware and the simulator. The model reads _player/config/keys.txt at the mode start (card still ours), and again after the computer ejected the card
 * (usb_mode_card_ours: /usbcard) or by the 重讀設定 cell; a file with any error is rejected whole (pc_extkeys: the default 媒體鍵 layer + the error list).
 * Statics in PSRAM (this file is built with -msmall-data-limit=0). LVGL task only (the send queue's producer). */
void ext_mode_start(const char *path);       /* the mode start: send queue empty, config read from `path` (no file = the default layer), layer 0 */
const pc_extkeys_cfg_t *ext_cfg(void);
const pc_extkeys_res_t *ext_res(void);
int ext_layer(void);                         /* the current layer */
void ext_layer_step(int delta);              /* wraps */
bool ext_key(int i);                         /* the key i of the current layer to the computer; false: nothing sent (no such key, queue full) */
bool ext_knob(int detents);                  /* the layer's knob role to the computer; true (always consumed) */
enum { EXT_RL_OK = 0, EXT_RL_IN_USE, EXT_RL_NOT_OURS, EXT_RL_CHANGED };   /* the card is on the computer / not on our side yet / went away during the read */
int ext_reload(void);                        /* 重讀設定: read usb_mode_keys_path() now; only when the card is ours */
void ext_poll(void);                         /* the screen's tick: one reload per eject / unplug */
int ext_generation(void);                    /* +1 at every applied config (the screen rebuilds its cells) */
const char *ext_knob_word(void);             /* the current layer's knob role in words (音量 / 捲動 / 方向鍵 / 無) */
/* the UI half (ui/scr_ext.c): fonts from flash only, the shared host with the external mode's rules (its knob: ext_knob), the page; LVGL lock held */
void ext_screen_start(lv_display_t *d, lv_indev_t *touch);
int ext_screen_cell(const char *name);      /* console: the index of the fixed cell "cfg" | "reload" | "url" | "func" | "bt" | "unpair" | "exit" | "help", -1 none */
void ext_screen_page(int delta);        /* console "ext page": turn the ring one page (the simulator's pictures of cells on the later pages) */
void ext_screen_arm_cell(const char *name); /* console: the first tap of that fixed cell (arms it; "func" shows the USB mix question) */
void ext_screen_arm(bool on);                /* console: the first tap of the exit cell (on) / the centre (off: the arm ends) */
void ext_screen_ask(bool yes);               /* console "ext ask yes|no": the question page's answer */
void ext_screen_tap(int idx);                 /* console "ext key <i>": a tap on cell idx of the page's ring (-1 the centre); the simulator's only */
