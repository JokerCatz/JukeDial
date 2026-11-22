// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_keys.h"
#include <string.h>

void pc_keys_init(pc_keys_t *k) { memset(k, 0, sizeof *k); }

static int dir(int who) { return who == 0 ? PC_KEYS_UP : PC_KEYS_DOWN; }

bool pc_keys_was_repeat(const pc_keys_t *k) { return k->rep; }

int pc_keys_step(pc_keys_t *k, bool top, bool bottom, uint32_t now)
{
    const bool in[2] = { top, bottom };
    k->rep = false;
    if (!k->init) {
        for (int i = 0; i < 2; i++) { k->raw[i] = in[i]; k->st[i] = in[i]; k->raw_t[i] = now; k->down_t[i] = now; }
        k->init = true;
        k->combo = in[0] || in[1];
        return PC_KEYS_NONE;
    }
    bool pressed[2] = { false, false }, released[2] = { false, false };
    for (int i = 0; i < 2; i++) {
        if (in[i] != k->raw[i]) { k->raw[i] = in[i]; k->raw_t[i] = now; }
        if (k->raw[i] != k->st[i] && (uint32_t)(now - k->raw_t[i]) >= PC_KEYS_DEBOUNCE_MS) {
            k->st[i] = k->raw[i];
            if (k->st[i]) { pressed[i] = true; k->down_t[i] = k->raw_t[i]; } else released[i] = true;
        }
    }
    const int down = (int)k->st[0] + (int)k->st[1];
    if (down == 0) {
        const int r = (k->pending && (released[0] || released[1])) ? dir(k->who) : PC_KEYS_NONE;   /* a click shorter than the combo window: its detent now */
        k->pending = k->clicked = k->combo = false;
        return r;
    }
    if (down == 2) {
        if (k->combo) return PC_KEYS_NONE;
        const bool fresh = k->pending;                  /* the first key had not given its detent yet: a real two-key press */
        k->combo = true; k->pending = k->clicked = false;
        return fresh ? PC_KEYS_BOTH : PC_KEYS_NONE;
    }
    if (k->combo) return PC_KEYS_NONE;                  /* one key of a combo still down, or a key held since the start */
    const int i = k->st[0] ? 0 : 1;
    if (pressed[i]) { k->who = i; k->pending = true; k->clicked = false; k->last_rep = now; return PC_KEYS_NONE; }
    const uint32_t held = now - k->down_t[i];
    if (k->pending && held >= PC_KEYS_COMBO_MS) { k->pending = false; k->clicked = true; k->last_rep = now; return dir(i); }
    if (k->clicked && held >= (k->hold_ms ? k->hold_ms : PC_KEYS_HOLD_MS) && held <= PC_KEYS_CAP_MS && (uint32_t)(now - k->last_rep) >= (k->rep_ms ? k->rep_ms : PC_KEYS_REPEAT_MS)) {
        k->last_rep = now;
        k->rep = true;
        return dir(i);
    }
    return PC_KEYS_NONE;
}
