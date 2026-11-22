// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_outstat.h"
/* Bluetooth (spec 2026-11-09-bluetooth-design.md): one mode at a time. LVGL context. */
typedef enum { BT_MODE_OFF, BT_MODE_SPEAKER, BT_MODE_HEADPHONES, BT_MODE_BROADCAST, BT_MODE_LISTEN, BT_MODE_COUNT } bt_mode_t;
bool bt_svc_available(void);            /* built with Bluetooth */
int  bt_svc_set_mode(bt_mode_t m);      /* 0 ok; stored as cfg bt_mode */
bt_mode_t bt_svc_mode(void);
const char *bt_svc_mode_label(bt_mode_t m);
const char *bt_svc_status(void);        /* one line for the UI: "waiting for a phone", "connected: Pixel", ... */
void bt_svc_start(void);                /* at boot: the stored mode */
/* headphones mode: nearby audio sinks (scan), connect, the remembered one reconnects on entering the mode */
typedef struct { char name[64]; uint8_t bda[6]; } bt_dev_t;
/* pairing confirmation: a phone asks to pair -> scr_pair shows the six digits (spec: no auto-accept for phones) */
bool bt_svc_pair_pending(char digits[7]);   /* true while a prompt is open; digits may be NULL */
void bt_svc_pair_reply(bool accept);
bool bt_svc_pair_check_timeout(void);       /* call every second while shown: true once when it timed out (rejected) */
int  bt_svc_paired_count(void);      /* how many devices forget_all would remove; -1 when Bluetooth is off */
int  bt_svc_forget_all(void);          /* removes every paired device; count, -1 when Bluetooth is off */
void bt_svc_scan(void);
int  bt_svc_devices(bt_dev_t *out, int max);
void bt_svc_connect(const uint8_t bda[6]);
int  bt_svc_ldac_kbps(int kbps);        /* LDAC top rate (JC_LDAC builds): 330/492/660/990, cfg ldac_kbps; -1 if not built */
uint32_t bt_svc_sink_underflows(void);   /* hires T8e: dry blocks of the Bluetooth sink paths (A2DP, Auracast) since boot, any task */
void bt_svc_out_in(pc_os_bt_in_t *in);  /* BTAUDIO: the live link for the output status (connected, the sink's codecs + top rates, LDAC step, Wi-Fi); the caller adds the preferences */
void bt_svc_apply_prefs(void);          /* BTAUDIO: a stored bt_* preference changed: reconnect the sink once (headphones mode, connected), never a reboot */
void bt_svc_applied_prefs(pc_os_bt_in_t *in);   /* FIX4: the preferences of the registered endpoints (a connected link is described with these)  */
bool bt_svc_prefs_pending(void);        /* FIX4: a stored preference differs from the applied one in a way that needs a reconnect (headphones mode) */
const char *bt_svc_codec(void);         /* headphones: the codec in use ("LDAC 660 kbps", "AAC"), "" when not streaming */
/* the broadcast-radio boot (cast_mode.c): no player, no fx; Wi-Fi must be up BEFORE this (spike: the reverse order starves the stream). The boot never leaves it (exit = reboot). */
void bt_svc_lean_begin(void);           /* first thing in that boot: the player's refusals (Wi-Fi in broadcast mode) and hooks (fx, UI sink) stand aside */
int  bt_svc_lean_cast(void);            /* the Bluetooth stack + the Auracast source, sink set up by hand; 0 ok */
bool bt_svc_lean_cast_on(void);         /* the broadcast is running */
const char *bt_svc_cast_name(void);     /* the broadcast's name ("JukeDial"); "" without Auracast */
void bt_svc_test_bypass(bool on);       /* TEST ONLY, console `bcastbypass 0|1`: not in any UI; the broadcast mode no longer refuses Wi-Fi and the radio */
bool bt_svc_wifi_ok(void);              /* the broadcast mode's refusals of Wi-Fi / radio stand aside: the broadcast radio boot, or the test switch */
bool bt_svc_lean(void);                 /* bt_svc_lean_begin() ran */
