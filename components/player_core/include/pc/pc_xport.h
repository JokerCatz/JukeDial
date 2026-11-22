// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 外接模式 T3 (spec 2026-11-15-external-mode.md sections 4, 11): which transport carries the keys, USB or Bluetooth (BLE HID), in the one boot of the mode.
 * Pure: no RTOS, no stack calls. The caller feeds the cable (edges of "the computer enumerated the device"), the user's answers and a millisecond clock,
 * and acts on what the machine says: sender (who may send now), advertise (the BLE stack advertises). Both can never be true for two transports:
 * one enum says who sends. USB side: starts here, the cable not yet seen; an unplug asks "switch to Bluetooth?"; yes = advertise and send over BLE,
 * no / timeout = stay on the USB side (asked again only at the next unplug). Bluetooth side: a plug-in asks "back to USB?"; yes = stop advertising
 * (the caller also drops the BLE link), keys over USB; no / timeout = stay (asked again only at the next plug-in). While a question is open on the
 * Bluetooth side the Bluetooth link keeps sending (nothing sends while the USB side's question is open: the cable is gone). */
#define PC_XPORT_ASK_MS 30000

typedef enum { PC_XP_USB = 0, PC_XP_ASK_BT, PC_XP_BT, PC_XP_ASK_USB } pc_xport_state_t;
typedef enum { PC_XS_NONE = 0, PC_XS_USB, PC_XS_BLE } pc_xport_sender_t;

typedef struct { uint8_t state; bool usb; uint32_t ask_at; } pc_xport_t;

void pc_xport_init(pc_xport_t *x);                        /* the USB side, no cable yet */
void pc_xport_usb(pc_xport_t *x, bool plugged, uint32_t now_ms);   /* every poll: acts on a change only */
void pc_xport_request(pc_xport_t *x, uint32_t now_ms);    /* the user asks for the other side now (a cell): only when the cable state allows it */
void pc_xport_answer(pc_xport_t *x, bool yes);            /* the question: yes = switch; no change when no question is open */
void pc_xport_tick(pc_xport_t *x, uint32_t now_ms);       /* an open question runs out after PC_XPORT_ASK_MS (= no) */
int pc_xport_sender(const pc_xport_t *x);                 /* pc_xport_sender_t */
bool pc_xport_advertise(const pc_xport_t *x);
int pc_xport_ask(const pc_xport_t *x);                    /* 0 none, 1 "switch to Bluetooth?", 2 "back to USB?" */

#ifdef __cplusplus
}
#endif
