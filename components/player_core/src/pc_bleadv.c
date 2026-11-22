// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_bleadv.h"
#include <string.h>

void pc_bleadv_init(pc_bleadv_t *a) { memset(a, 0, sizeof *a); }
void pc_bleadv_want(pc_bleadv_t *a, bool on) { a->want = on; }
void pc_bleadv_conn(pc_bleadv_t *a, bool c) { a->conn = c; if (c) { a->adv = false; a->pending = false; } }
int pc_bleadv_next(pc_bleadv_t *a)
{
    if (a->pending) return PC_ADV_NONE;
    if (a->want && !a->adv && !a->conn) { a->pending = true; return PC_ADV_START; }
    if (!a->want && a->adv) { a->pending = true; return PC_ADV_STOP; }
    return PC_ADV_NONE;
}
void pc_bleadv_started(pc_bleadv_t *a, bool ok) { a->pending = false; if (ok) a->adv = true; }   /* an error leaves a true flag true: the set may still be running */
void pc_bleadv_stopped(pc_bleadv_t *a) { a->pending = false; a->adv = false; }
