// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_castq.h"
#include "pc/pc_i18n.h"

int pc_cast_quality(int stored) { return stored >= 0 && stored <= 2 ? stored : PC_CAST_Q_DEFAULT; }

int pc_cast_q_step(int cur, int d) { return (((pc_cast_quality(cur) + d) % 3) + 3) % 3; }

int pc_cast_q_rate(int q) { return pc_cast_quality(q) == 0 ? 24000 : 48000; }
int pc_cast_q_nbyte(int q) { q = pc_cast_quality(q); return q == 0 ? 60 : q == 2 ? 155 : 120; }

const char *pc_cast_q_label(int q, bool zh)
{
    switch (pc_cast_quality(q)) {
    case 0: return zh ? TZ("24k 立體聲") : "24k stereo";
    case 2: return zh ? TZ("48k 高位元率") : "48k high";
    default: return zh ? TZ("48k 立體聲") : "48k stereo";
    }
}
