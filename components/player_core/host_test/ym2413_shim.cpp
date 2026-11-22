// SPDX-License-Identifier: LGPL-2.1-or-later
// C view of gme's Ym2413_Emu for test_gme_voices.c (the idle skip of JC 0011 is compared with a chip calculated sample by sample).
#include "Ym2413_Emu.h"

extern "C" {
#include "emu2413.h"

void *ym_new(unsigned rate)
{
    Ym2413_Emu *e = new Ym2413_Emu;
    if (e->set_rate(rate, 3579545)) { delete e; return 0; }
    return e;
}
void ym_free(void *e) { delete (Ym2413_Emu *)e; }
void ym_write(void *e, int a, int d) { ((Ym2413_Emu *)e)->write(a, d); }
void ym_run(void *e, int n, short *out) { ((Ym2413_Emu *)e)->run(n, out); }
const OPLL *ym_chip(void *e) { return ((Ym2413_Emu *)e)->chip(); }
}
