// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_svc.h"
/* Who owns playback (MP3, chiptune, noise) and what the now-playing screen shows.
 * All calls from the LVGL task; audio events are polled from an LVGL timer. */
typedef void (*player_event_fn)(const aud_event_t *e);
enum { NOW_SRC_OTHER = 0, NOW_SRC_MP3, NOW_SRC_CHIP, NOW_SRC_NOISE, NOW_SRC_COLOR, NOW_SRC_RADIO, NOW_SRC_BT };   /* now_ops_t.src; OTHER: no menu entry of its own (clock alarm, test tone); BT: the stream that comes in over Bluetooth (speaker mode, Auracast listening): the main menu's Bluetooth cell */
enum { NOW_MODE_SHUFFLE, NOW_MODE_ORDER, NOW_MODE_ONE };   /* now_ops_t.mode_kind: shuffle / in order (loops) / repeat one */
typedef struct {
    void (*title)(char *out, size_t cap);
    bool (*cover)(char *path, size_t cap, uint32_t *apic_off, uint32_t *apic_len);   /* NULL / false: no cover; true with apic_len 0: no embedded picture, an image file next to path may be the cover (thumb_svc, hires T8c) */
    bool (*fav_id)(char *out, size_t cap);                                            /* false: nothing to favour */
    void (*next)(void);
    void (*prev)(void);   /* NULL: this source has no previous item */
    uint8_t src;          /* who this is (NOW_SRC_*): the main menu reopens the playing page of its own source */
    void (*stop)(void);
    bool (*playing)(void);
    void (*open_list)(void);     /* NULL: no list item in the menu */
    void (*open_info)(void);     /* NULL: no details item */
    const char *(*list_label)(void);   /* NULL: "list" */
    const char *(*mode_label)(void);   /* NULL: no play-order item */
    void (*mode_step)(int detents);    /* the play order by detents (signed, wraps); the playing ring's tap is +1 */
    uint8_t (*mode_kind)(void);        /* NULL: unknown. NOW_MODE_* of the play order now (the square panel's now-playing page shows it as an icon) */
    uint8_t modes;                     /* how many play orders: 3 or more -> the play-order cell is an axis (the knob steps it for 4 s, ui_axis.h); 2 = a plain tap */
    bool speed;                        /* a file source: the playing ring has the speed cell (Track B, speed_svc.h: 0.75 .. 2.0, pitch kept) */
    bool no_art;                 /* white noise: no cover, the name in the middle, nothing turns */
    bool (*cover_jpeg)(const uint8_t **jpg, uint32_t *len);   /* a cover held in memory (the phone's AVRCP cover art), used when cover gives none; thumb_svc copies it */
    void (*cover_done)(bool shown, bool low_mem);   /* NULL: not told. The decode of cover_jpeg ended: shown, or refused for low internal memory (else broken) */
    const char *(*cover_note)(void);   /* NULL / returns NULL: no note. A short dim text under the cover (the honest state of a cover that cannot be shown) */
} now_ops_t;

void player_ctl_start(void);
/* new owner: the previous owner's forget() runs (its state is reset; audio keeps going) */
void player_ctl_take(const now_ops_t *ops, void (*forget)(void), player_event_fn fn);
void player_ctl_release(void);          /* the owner stopped */
const now_ops_t *player_ctl_now(void);  /* NULL when nothing owns playback */
void player_ctl_changed(void);          /* owner tells the UI that track/state changed */
extern void (*player_ctl_on_change)(void);
/* Speaker mode is an exclusive source (ui/scr_spkask.c). The play function of a LOCAL source (songs, chiptune, radio, white noise) calls this first, with
 * a function that repeats the same play: false = go ahead; true = the device is a Bluetooth speaker: nothing may start now, the user is asked whether to
 * leave speaker mode (a reboot) and play; on yes redo(a, b) runs with the gate open. Alarms, toy sounds, the tuner and the test tone do not ask. */
typedef void (*spk_redo_fn)(int a, int b);
bool spk_excl_gate(spk_redo_fn redo, int a, int b);
