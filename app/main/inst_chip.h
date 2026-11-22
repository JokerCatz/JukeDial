// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Instrument app chip adapters (ported from RP2350_player src/player/midi_*_chip.cpp): GME APUs driven
 * note by note. Each adapter is static storage; one runs at a time, on the instrument audio task. */
#include <math.h>
#include "pc/pc_synth.h"
#include "pc/pc_voicepool.h"
#include "pc/pc_gm_table.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef pc_synth_t chip_synth_t;

const pc_synth_t *midi_chip_nes(void);
const pc_synth_t *midi_chip_gb(void);
const pc_synth_t *midi_chip_sms(void);
const pc_synth_t *midi_chip_zx_ay(void);
const pc_synth_t *midi_chip_msx_scc(void);
const pc_synth_t *midi_chip_pce(void);
const pc_synth_t *midi_chip_nes_n163(void);
const pc_synth_t *midi_chip_nes_vrc6(void);
const pc_synth_t *midi_chip_nes_5b(void);
const pc_synth_t *midi_chip_atari_pokey(void);
const pc_synth_t *midi_chip_ym2612(void);

int inst_chip_count(void);
const pc_synth_t *inst_chip(int i);          /* NULL out of range */
int inst_chip_find(const char *id);          /* index, 0 (NES) when unknown */

/* pitch bend = the APU clock the Blip buffer is told about: every voice bends together, without phase resets */
static inline long inst_bend_clock(long clock, int cents) { return (long)(clock * pow(2.0, cents / 1200.0) + 0.5); }
#ifdef __cplusplus
}
#endif
