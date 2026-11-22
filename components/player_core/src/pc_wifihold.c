// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_wifihold.h"

bool pc_wh_hold(pc_wifihold_t *h, pc_wh_who_t who)
{
    h->holders |= (uint8_t)who;
    h->on = true;
    return true;
}

bool pc_wh_release(pc_wifihold_t *h, pc_wh_who_t who, uint32_t now_ms)
{
    if (!(h->holders & (uint8_t)who)) return h->on;
    h->holders &= (uint8_t)~who;
    if (!h->holders && h->on) h->off_at_ms = now_ms + PC_WH_GRACE_MS;
    return h->on;
}

bool pc_wh_tick(pc_wifihold_t *h, uint32_t now_ms)
{
    if (pc_wh_pending(h) && (int32_t)(now_ms - h->off_at_ms) >= 0) h->on = false;
    return h->on;
}

void pc_wh_force_off(pc_wifihold_t *h)
{
    h->holders = 0;
    h->on = false;
}

bool pc_wh_pending(const pc_wifihold_t *h) { return h->on && !h->holders; }
