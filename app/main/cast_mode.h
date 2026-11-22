// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The broadcast radio boot (spike docs: the internal notes): the internet radio received over Wi-Fi and sent out as an
 * Auracast broadcast, with no player. Another app in the same image, entered by reboot like the instrument and the USB modes: one shot (the request lives in RTC memory and
 * is cleared at boot, so any later reset, a crash included, boots the player; the boot-loop guard runs before this and safe mode never gets here).
 * The order is fixed: Wi-Fi up and connected FIRST, the broadcast second (the reverse order starves the stream, spike §3). Not loaded in this boot: library, fx, covers, games,
 * the player pages. Two halves (as scr_ext.c / usb_mode.c): cast_mode.c is the hardware and the services, ui/scr_cast.c the screen, fake_cast_mode.c the simulator's model. */
bool cast_mode_take_request(void);    /* boot: true once after cast_mode_request() */
bool cast_mode_peek_request(void);    /* the same flag, not consumed (boot_front.c) */
void cast_mode_request(void);         /* LVGL: saves cfg + what is playing, restarts into the mode */
void cast_mode_run(void);             /* never returns; the exit is a restart into the player */
bool cast_mode_active(void);          /* this boot is the broadcast radio */

enum { CAST_PH_WIFI, CAST_PH_NO_WIFI, CAST_PH_START, CAST_PH_ON, CAST_PH_FAIL, CAST_PH_WIFI_FAIL };   /* (WIFI_FAIL: still waiting after 60 s or the hold was refused) waiting for Wi-Fi, no credentials file, broadcast starting, running, the broadcast could not start */
typedef struct {
    int phase;
    bool wifi;                        /* connected */
    int rssi;                         /* dBm, 0 when not connected */
    int station, n_stations;          /* the one playing (the player's current station, cfg radio_last), the list's size */
    char now[200];                    /* the station and its state / the song, as the radio says it ("name\nstate") */
    int volume;
    char cast_name[24];               /* the broadcast's name */
    uint32_t bytes, reconnects, underruns;   /* the radio since boot: bytes read, streams that ended and came back, dry decoder reads */
} cast_status_t;
void cast_mode_status(cast_status_t *o);     /* LVGL task */
void cast_mode_exit(void);                   /* back to the player: a restart (the board never returns; the simulator prints it) */
bool cast_mode_cmd(const char *c);           /* console "cast ...": st | tap N | hold (the bench build and the simulator) */

/* the UI half (ui/scr_cast.c): fonts from the flash pack, the shared host with this mode's rules, the page; LVGL lock held */
void cast_screen_start(lv_display_t *d, lv_indev_t *touch);
