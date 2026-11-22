// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Speaker mode's AVRCP controller (AVRCP 1.6 CT): the phone's title, artist, album and cover art into bt_meta.h.
 * Mode task: start before the A2DP sink init, stop before its deinit (esp_avrc_api.h). Headphones mode has its own (bt_remote.c). */
void bt_meta_avrc_start(void);
void bt_meta_avrc_stop(void);
