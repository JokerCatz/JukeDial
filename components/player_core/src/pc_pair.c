// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pair.h"
#include <stdio.h>
#include <string.h>

pc_pair_decision_t pc_pair_request(pc_pair_t *p, const uint8_t bda[6], uint32_t num, bool we_started, uint32_t now_ms)
{
    if (we_started) return PC_PAIR_ACCEPT;
    if (p->asking && (int32_t)(now_ms - p->deadline) >= 0) p->asking = false;   /* an unanswered request whose page was left: expired, not pending */
    if (p->asking && memcmp(p->bda, bda, 6)) return PC_PAIR_REJECT;
    p->asking = true;
    p->num = num;
    memcpy(p->bda, bda, 6);
    p->deadline = now_ms + PC_PAIR_TIMEOUT_MS;
    return PC_PAIR_PROMPT;
}

bool pc_pair_expired(pc_pair_t *p, uint32_t now_ms)
{
    if (!p->asking || (int32_t)(now_ms - p->deadline) < 0) return false;
    p->asking = false;
    return true;
}

bool pc_pair_answer(pc_pair_t *p, uint8_t bda_out[6])
{
    if (!p->asking) return false;
    memcpy(bda_out, p->bda, 6);
    p->asking = false;
    return true;
}

void pc_pair_digits(uint32_t num, char out[7]) { snprintf(out, 7, "%06u", (unsigned)(num % 1000000u)); }
