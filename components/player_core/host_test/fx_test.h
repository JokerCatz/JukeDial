// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Shared helpers of the fx host tests (test-only). The one definition of the click bound (spec §10.2):
 * a switch is click-free when its largest sample-to-sample step is at most twice the larger of the two
 * steady runs' steps, plus a slack (per-app set(): 0.01; chain on/off, mode change, REBUILD swap: 0.02). */
#include <math.h>
#include <stdbool.h>
#include "pc/pc_fxapp.h"

#define FX_CLICK_SLACK_APP   0.01f   /* §10.2: set() mid-stream, one detent or full range */
#define FX_CLICK_SLACK_CHAIN 0.02f   /* §10.2: on/off, mode change, REBUILD swap through the chain */

/* largest |y[n] - y[n-1]| over both sides of an interleaved stereo run */
static inline float fx_maxstep(const float *lr, int frames)
{
    float m = 0;
    for (int i = 1; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            float d = fabsf(lr[2 * i + c] - lr[2 * (i - 1) + c]);
            if (d > m) m = d;
        }
    return m;
}

static inline bool fx_click_ok(float step_switch, float step_old, float step_new, float slack)
{
    return step_switch <= 2.0f * fmaxf(step_old, step_new) + slack;
}

/* first registry index of that class, -1 none */
static inline int fx_first_app_of(pc_fxclass_t cls)
{
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a);
    for (int i = 0; i < n; i++)
        if (a[i]->cls == cls) return i;
    return -1;
}
