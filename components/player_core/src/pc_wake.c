// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_wake.h"

int pc_wake_on_knob(pc_wake_t *w, int detents, int64_t now_ms)
{
    if (!w->screen_on) {
        w->screen_on = true;
        w->woke_ms = now_ms;
        return 0;
    }
    if (now_ms - w->woke_ms < PC_WAKE_LOCKOUT_MS) return 0;   /* still the turn that woke the screen */
    return detents;
}

void pc_wake_screen_off(pc_wake_t *w) { w->screen_on = false; }
