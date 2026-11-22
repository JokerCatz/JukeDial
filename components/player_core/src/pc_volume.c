// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_volume.h"
#include <math.h>

int pc_volume_step(int vol, int detents, int max)
{
    int v = vol + PC_VOL_PER_DETENT * detents;
    if (v < 0) v = 0;
    if (v > max) v = max;
    return v;
}

float pc_volume_gain(int vol)
{
    if (vol <= 0) return 0.0f;
    if (vol > 100) vol = 100;
    return powf(10.0f, (-60.0f + 0.6f * (float)vol) / 20.0f);
}
