// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* BTVOL: one volume per output. The wired output (3.5 mm; also speaker mode and Auracast listening, and the value the Auracast
 * broadcast uses) keeps cfg "volume"; every Bluetooth headset has its own value, by address, in cfg "bt_vols" (pc_volsel: at most 15, the least
 * recently used one dropped; a headset never seen starts at PC_VOLSEL_BT_DEFAULT). app_volume() / app_set_volume() (cfg.h) are the CURRENT output's.
 * The current output changes in exactly one place, vol_out_switch(), which also stores the sink: the order there is what keeps every block at or
 * below the volume of the output it goes to (see vol_out.c). */
#include <stdbool.h>
#include <stdint.h>
#include "audio_svc.h"
#include "pc/pc_volsel.h"

pc_vout_t vol_out_current(void);
bool vol_out_is_bt(void);                               /* any task: the current output is a headset (only then may a value go to the headset's link) */
int  vol_out_wired(void);                               /* the wired value, whatever the current output */
int  vol_out_spk(void);                                 /* the jack's value in speaker / Auracast-listening mode (cfg volume_spk, default PC_VOLSEL_SPK_DEFAULT), whatever the current output */
int  vol_out_set_spk(int v);                            /* stores it (clamped, returned) */
void vol_out_mode_changed(void);                        /* the bt mode is known / changed: the jack takes the wired or the speaker value (vol_out_switch) */
int  vol_out_set_wired(int v);                          /* stores it (clamped, returned); the caller applies it when the wired value is the current one */
int  vol_out_headset(const uint8_t bda[6]);             /* that headset's stored value or the default */
int  vol_out_set_headset(const uint8_t bda[6], int v);  /* stores it for that headset (clamped, returned) */
void vol_out_forget(const uint8_t bda[6]);              /* the headset was unpaired: its value goes */
bool vol_out_limit(int max);                            /* volume_max changed: stored values above it come down; true: the current output's was above */
/* LVGL task: the output changes (or the same output again). Sets the device volume and the sink in the safe order; fn as audio_svc_set_sink. */
void vol_out_switch(pc_vout_t out, const uint8_t bda[6], aud_sink_fn fn);
void vol_out_print(void);                               /* APP vol out=<wired|speaker|bt:<addr>|cast> value=<n> wired=<n>, then APP vol spk=<n> */
