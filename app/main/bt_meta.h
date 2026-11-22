// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_btmeta.h"
#include "player_ctl.h"
/* Speaker mode (AVRCP 1.6 CT; fix round 1): what the phone says it plays - title, artist, album, cover art.
 * The decisions are the pure core pc_bma_* (pc_btmeta.h); this file drives it on the LVGL task (a 50 ms tick: the phone's pending events,
 * then the clock; only while a request is open) and keeps what the UI reads. The "phone" is bt_meta_avrc.c on the board (IDF callbacks -> events, actions -> esp_avrc_ct_*)
 * and the fake phone of fake_bt_svc.c in the simulator: both feed the SAME core. Before a title is known nothing changes on the page. */
typedef struct {
    void (*poll)(void);                  /* LVGL task, every tick: hand the pending events to bt_meta_event, in order */
    void (*ask_caps)(void);
    void (*reg_track)(void);
    void (*ask_meta)(uint8_t mask);      /* PC_BTMETA_* bits */
    void (*ca_connect)(void);
    void (*ca_drop)(void);
    void (*get)(const char *handle);     /* the linked thumbnail of a 7-digit handle */
} bt_meta_phone_t;
void bt_meta_start(void);                /* LVGL task, at boot (after player_ctl_start): its two timers, paused */
void bt_meta_attach(const bt_meta_phone_t *p);   /* any task: the tick starts on the LVGL task */
bool bt_meta_asleep(void);               /* any task: the tick is paused (a pushing glue wakes it, again if a wake was lost) */
void bt_meta_wake(void);                 /* any task: a phone queued an event (the tick sleeps while nothing is open: pc_bma_busy) */
void bt_meta_event(pc_bma_ev_t *ev);     /* LVGL task (from poll): now_ms is filled in here */
void bt_meta_bind(now_ops_t *ops);       /* the stream's now_ops: they follow the track (title, cover, details, the low-memory note) */
const pc_btmeta_t *bt_meta_get(void);    /* NULL until a title is known */
bool bt_meta_art(const uint8_t **jpg, uint32_t *len);   /* the cover JPEG of the current handle; false: none (the page's placeholder) */
bool bt_meta_title(char *out, size_t cap);   /* the now page's band ("title  -  artist"); false: no title known */
void bt_meta_open_info(void);            /* the details page of the phone's track (scr_meta, arg -1) */
void bt_meta_print(void);                /* console btmeta: APP btmeta title=.. artist=.. album=.. track=n/m ms=.. art=<handle|-> art_bytes=.. art_state=.. */
