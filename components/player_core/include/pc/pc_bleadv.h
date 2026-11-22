// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 外接模式 fix round 1: the BLE advertising bookkeeping as a pure state machine. The stack answers start / stop later with an event; until then no second command is
 * sent (a second start before the first completed made the stack answer "already advertising" with an error, and the error cleared the flag while the set still ran:
 * the advertising was never stopped on the way back to USB). An error answer to a start never clears a flag that is already true. */
typedef enum { PC_ADV_NONE = 0, PC_ADV_START, PC_ADV_STOP } pc_bleadv_action_t;
typedef struct { bool want, adv, pending, conn; } pc_bleadv_t;

void pc_bleadv_init(pc_bleadv_t *a);
void pc_bleadv_want(pc_bleadv_t *a, bool on);
void pc_bleadv_conn(pc_bleadv_t *a, bool connected);     /* a connection ends the advertising set; a disconnect may start it again */
int pc_bleadv_next(pc_bleadv_t *a);                      /* the command to send now (sets pending), PC_ADV_NONE when none is needed or one is outstanding */
void pc_bleadv_started(pc_bleadv_t *a, bool ok);         /* the start event; ok = status 0 */
void pc_bleadv_stopped(pc_bleadv_t *a);                  /* the stop event */
#ifdef __cplusplus
}
#endif
