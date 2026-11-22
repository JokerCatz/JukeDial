// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The FakePod's two keys as the knob (F1b): pc_keys recognises click / hold / both on the levels bsp_keys reads; a click is one detent through ui_knob_inject
 * (the top key +1, the bottom key -1), holding repeats (capped far below the PMU's power-off hold), both keys together switch the screen on / off. A game page takes the same
 * detent as its own key (and as a tap where the page says so: ui_dial, ui_arena). The key task only posts; the detents are applied by a timer in the LVGL task. Call with the LVGL lock held. No-op on a board with a knob. */
void keys_svc_start(void);
/* F2: the same keys in the other boot modes (external / instrument / broadcast), whose own host takes the knob detents (ext_knob: the layer's keys; the instrument volume; the radio volume): the
 * screen-off blind layer belongs to the player only (it needs a source that plays), here a key wakes a dark screen as the knob does. Call with the LVGL lock held, after bsp_knob_start. */
void keys_svc_start_host(void);
/* The blind layer (screen off, a source owns playback; player mode only): top click volume +, bottom click volume -, a hold repeats; top x2 next track; bottom x2 play / pause; both keys wake
 * the screen. See pc_blind.h and the base design notes (FakePod keys per page). */
