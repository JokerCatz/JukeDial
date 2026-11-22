// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Which side of the USB-C plug is in (user idea 2026-11-10): power without the debug port's SOF packets means the
 * computer is on the OTG side (or it is a charger) -> offer the USB drive mode, once per plug-in. Called once a second. */
#define PC_PLUG_ASK_AFTER 3
typedef struct { int otg_ticks; bool asked; } pc_plug_t;
bool pc_plug_tick(pc_plug_t *p, bool usb_power, bool debug_port);   /* true: ask now */
/* USB drive mode: where the card is, for the screen (usb_mode_where; the board and sim/fake_usb_mode.c share this rule). mounted: the computer
 * configured the device (tud_mounted); ejected: the last storage event put the card on our side (an eject, or the unplug that followed).
 * -1 waiting for the computer, 1 the computer uses the card (from the first plug-in on: the storage starts at USB, no event), 0 ejected. */
int pc_usb_where(bool mounted, bool ejected);
#ifdef __cplusplus
}
#endif
