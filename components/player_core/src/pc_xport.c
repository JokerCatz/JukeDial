// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_xport.h"

void pc_xport_init(pc_xport_t *x) { x->state = PC_XP_USB; x->usb = false; x->ask_at = 0; }

void pc_xport_usb(pc_xport_t *x, bool plugged, uint32_t now_ms)
{
    if (plugged == x->usb) return;
    x->usb = plugged;
    if (plugged) {
        if (x->state == PC_XP_ASK_BT) x->state = PC_XP_USB;                       /* back before the answer: the question is moot */
        else if (x->state == PC_XP_BT) { x->state = PC_XP_ASK_USB; x->ask_at = now_ms; }
    } else {
        if (x->state == PC_XP_USB) { x->state = PC_XP_ASK_BT; x->ask_at = now_ms; }
        else if (x->state == PC_XP_ASK_USB) x->state = PC_XP_BT;                  /* the cable went again: stay on Bluetooth */
    }
}
void pc_xport_request(pc_xport_t *x, uint32_t now_ms)
{
    if (x->state == PC_XP_USB && !x->usb) { x->state = PC_XP_ASK_BT; x->ask_at = now_ms; }
    else if (x->state == PC_XP_BT && x->usb) { x->state = PC_XP_ASK_USB; x->ask_at = now_ms; }
}
void pc_xport_answer(pc_xport_t *x, bool yes)
{
    if (x->state == PC_XP_ASK_BT) x->state = yes ? PC_XP_BT : PC_XP_USB;
    else if (x->state == PC_XP_ASK_USB) x->state = yes ? PC_XP_USB : PC_XP_BT;
}
void pc_xport_tick(pc_xport_t *x, uint32_t now_ms)
{
    if ((x->state == PC_XP_ASK_BT || x->state == PC_XP_ASK_USB) && now_ms - x->ask_at >= PC_XPORT_ASK_MS) pc_xport_answer(x, false);
}
int pc_xport_sender(const pc_xport_t *x)
{
    switch (x->state) {
    case PC_XP_USB: return PC_XS_USB;
    case PC_XP_BT: case PC_XP_ASK_USB: return PC_XS_BLE;
    default: return PC_XS_NONE;
    }
}
bool pc_xport_advertise(const pc_xport_t *x) { return x->state == PC_XP_BT || x->state == PC_XP_ASK_USB; }
int pc_xport_ask(const pc_xport_t *x) { return x->state == PC_XP_ASK_BT ? 1 : x->state == PC_XP_ASK_USB ? 2 : 0; }
