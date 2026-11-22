// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Mode switches by reboot (user decision 2026-11-09): Bluetooth modes (and later USB modes) start clean at boot,
 * so nothing has to be torn down and LE Audio's memory comes back. LVGL context. */
void sysmode_reboot(void);     /* stores what is playing (cfg "resume"), saves cfg now, restarts; the message says a mode is being switched */
void sysmode_store_state(void);   /* what plays (cfg "resume"), the fx state, the games, cfg saved now: the part of a reboot / power-off that keeps the user's place; no restart */
void sysmode_reboot_msg(const char *msg);   /* the same with the message shown while restarting (the plain 重開機 row, the web's reboot: no mode switch) */
void sysmode_resume(void);     /* boot, LVGL lock held, after the menu: plays the stored item once */
