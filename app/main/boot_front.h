// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The boot front: a pre-boot entry that runs once in app_main, after the guard / safe-mode checks and the recovery window, BEFORE the mode dispatch.
 * It finds out which mode is about to run (the one-shot flags are only PEEKED: the dispatch still consumes them exactly as before), and for a normal boot of that mode:
 * mounts the card, loads the config, starts the display (the way the mode would), shows the mode's power-on picture (splash.h) and hands over. The mode entries are
 * unchanged: their own bsp_sdcard_mount / cfg_load / bsp_display_start_ex find the work done (all three are safe to call again); the USB drive mode finds the card released. */
void boot_front_run(void);   /* the board only; the simulator calls boot_front_mode */
/* the same for a named mode (the simulator, which picks its mode on the command line): player usb inst cast charge */
void boot_front_mode(const char *mode);
