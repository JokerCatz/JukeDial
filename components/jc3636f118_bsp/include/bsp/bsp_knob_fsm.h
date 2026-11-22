// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Click decoder for this board's knob: two independent direction contacts (CW pulls B low,
 * CCW pulls A low; the other contact may blip around the release). Pure C, host-tested.
 *
 * Rule: from rest (A=B=1) the first contact to go low decides the click's direction; the click is
 * counted when both contacts are high again (the knob has settled into the detent), so holding
 * the knob half-way does not count yet. A new click may only start once both contacts have been
 * high continuously for `lockout_us` (the other contact blips right after a release).
 * Limit (measured): a half turn that springs back produces the same press/release as a full
 * click, so it still counts on release. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t lockout_us;
    int64_t release_t;   /* time both contacts last became high */
    bool high;           /* both contacts currently high */
    bool first;          /* no click seen yet: no lockout to wait for */
    int pending;         /* direction of the click in progress, 0 if none */
} bsp_knob_fsm_t;

void bsp_knob_fsm_init(bsp_knob_fsm_t *f, int64_t lockout_us);
/* Feed the pin levels after every edge (a, b: 0 = low). Returns +1 (CW) or -1 (CCW) on the
 * release that completes a click, else 0. */
int  bsp_knob_fsm_step(bsp_knob_fsm_t *f, int a, int b, int64_t t_us);
