// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "inst_chip.h"

/* order of the chip page and of the knob on the play page: NES, GB, the other square-wave kinds, FM last */
static const pc_synth_t *chip_at(int i)
{
    switch (i) {
    case 0: return midi_chip_nes();
    case 1: return midi_chip_gb();
    case 2: return midi_chip_sms();
    case 3: return midi_chip_zx_ay();
    case 4: return midi_chip_msx_scc();
    case 5: return midi_chip_pce();
    case 6: return midi_chip_nes_n163();
    case 7: return midi_chip_nes_vrc6();
    case 8: return midi_chip_nes_5b();
    case 9: return midi_chip_atari_pokey();
    case 10: return midi_chip_ym2612();
    default: return NULL;
    }
}

int inst_chip_count(void) { return 11; }
const pc_synth_t *inst_chip(int i) { return chip_at(i); }

int inst_chip_find(const char *id)
{
    for (int i = 0; i < inst_chip_count(); i++) {
        const pc_synth_t *c = chip_at(i);
        if (id && c && !strcmp(c->id, id)) return i;
    }
    return 0;
}
