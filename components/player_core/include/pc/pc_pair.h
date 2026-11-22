// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Bluetooth pairing confirmation (requirements notes: a phone pairing shows the same six digits and asks
 * Pair / Reject; the old auto-accept is not kept). One prompt at a time; no answer in time = reject. */
#define PC_PAIR_TIMEOUT_MS 30000
typedef struct { bool asking; uint32_t num; uint8_t bda[6]; uint32_t deadline; } pc_pair_t;   /* zero-initialise */
typedef enum { PC_PAIR_PROMPT, PC_PAIR_ACCEPT, PC_PAIR_REJECT } pc_pair_decision_t;
/* we_started: our own connect (headphones) -> accept at once. A second device while a prompt is open -> reject. */
pc_pair_decision_t pc_pair_request(pc_pair_t *p, const uint8_t bda[6], uint32_t num, bool we_started, uint32_t now_ms);
bool pc_pair_expired(pc_pair_t *p, uint32_t now_ms);     /* true once when the open prompt timed out (caller rejects) */
bool pc_pair_answer(pc_pair_t *p, uint8_t bda_out[6]);   /* closes the prompt; false when none is open */
void pc_pair_digits(uint32_t num, char out[7]);          /* "000042" */
