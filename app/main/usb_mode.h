// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* USB mode (user decision 2026-11-09: modes by reboot): the TF card as a USB drive and the knob as the computer's
 * volume keys, on the OTG port (the USB-C plug turned over; USB-Serial-JTAG is a separate PHY and keeps working).
 * One shot: the request lives in RTC memory and is cleared at boot, so any later reset is a normal boot.
 * Two halves (UI unify T12, spec 4.2): usb_mode.c is the hardware (MSC, the HID key queue, the state below); ui/scr_ext.c is the
 * screen, a page on the shared stack, compiled by the firmware and the simulator (whose fake_usb_mode.c implements the state). */
bool usb_mode_take_request(void);   /* boot: true once after usb_mode_request() */
bool usb_mode_peek_request(void);     /* the same flag, not consumed (boot_front.c) */
void usb_mode_request(void);        /* LVGL: saves cfg + what is playing, restarts into USB mode */
void usb_mode_run(void);            /* never returns; ends with a restart into normal mode */
bool usb_mode_active(void);         /* this boot is the USB drive mode (usb_mode_run has started): the card is the computer's, never ours to format */
void usb_mode_watch_start(void);    /* normal mode: plugged in on the OTG side -> ask once to switch (LVGL timer) */
/* normal boot: the internal RAM that the last USB mode read 10 s after the computer mounted it (spec 6: its console is on the other side
 * of the plug). true once (the note is cleared by this call); false when there is none (or RTC memory holds no valid note: a cold start). */
bool usb_mode_mem_last(uint32_t *free_b, uint32_t *min_b);

/* ---- the state the screen shows and changes (LVGL task) ---- */
int usb_mode_where(void);           /* -1 waiting for the computer, 1 the computer uses the card (from the first plug-in on), 0 the computer ejected it: pc_usb_where */
bool usb_mode_released(void);       /* cardsafe: the card was released (the computer went away without an eject, or the mode is being left): the drive is gone until the mode is entered again */
int usb_mode_reconnect(void);       /* cardsafe: the Reconnect cell after a release: 0 attached again, 1 no host yet (plug the cable in), 2 could not; never automatic */
int usb_mode_link(void);            /* pc_usb_link_t (pc_sdguide.h): WAIT, IN_USE, EJECTED, LOST; the status line */
bool usb_mode_mounted(void);        /* the computer enumerated the device (cable in, the HID keys reach it) */
void usb_mode_type_url(void);       /* the keyboard types the settings page address into the computer's focused window */
int usb_mode_combo(void);           /* 外接模式 T4: the function mix of this boot (pc_extcombo_t: 0 drive + sound card + keys, 1 drive + keys, 2 sound card + keys) */
bool usb_mode_has_msc(void);        /* the mix has the drive */
bool usb_mode_has_uac(void);        /* the mix has the sound card */
void usb_mode_set_combo(int combo); /* keep the mix and restart into the mode: the computer enumerates the new descriptor (the board restarts; the simulator applies it at once) */
void usb_mode_card_lock(void);      /* 外接模式 fix: held by ext_reload across its check / fopen / fread / fclose; the card's MOUNT_START (the computer takes it back: esp_tinyusb frees the FATFS next) waits for it and then marks the card not ours */
void usb_mode_card_unlock(void);
bool usb_mode_card_ours(void);      /* 外接模式 T2: the card is on our side (the computer ejected it, or the cable went): /usbcard is readable; false while the computer may have it */
const char *usb_mode_keys_path(void);   /* where _player/config/keys.txt is read from once the card is ours (PC_USB_CFG_KEYS on the board) */
bool usb_mode_exit(bool force);     /* back to the player (a restart; never returns on the board): false = the computer still uses the card and not `force` */
/* the knob and the keys go to the computer through ext_mode.h (ext_key / ext_knob: the HID send queue); the screen is ui/scr_ext.c (ext_screen_start) */

void usb_mode_console(const char *a);    /* console "ext ...": the peer bench build with ext_diag only */
