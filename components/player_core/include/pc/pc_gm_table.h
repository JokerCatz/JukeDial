// SPDX-License-Identifier: LGPL-2.1-or-later
/* GM program -> voice group + timbre (from RP2350_player src/player/midi_gm_table.h, generated there by
 * RP2350_player/tools/gen_midi_gm_table.py). Not GM synthesis: it arranges GM content onto a chip's few voices. */
#ifndef PC_GM_TABLE_H
#define PC_GM_TABLE_H
#include <stdint.h>
#include "pc/pc_voicepool.h"   /* pc_gm_group_t */

typedef struct {
    uint8_t group;        /* pc_gm_group_t */
    uint8_t pulse_duty;   /* 0-3 duty code when the note lands on a pulse (harmony) voice */
    uint8_t wave_pattern; /* wave index when it lands on the bass voice */
} pc_gm_entry_t;

#define PC_GM_PROGRAM_COUNT 128
static const pc_gm_entry_t pc_gm_table[PC_GM_PROGRAM_COUNT] = {
    /* 0-7: Piano */
    {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0},
    /* 8-15: Chromatic Percussion */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 16-23: Organ */
    {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1}, {PC_GM_EITHER, 2, 1},
    /* 24-31: Guitar */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 32-39: Bass */
    {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1}, {PC_GM_BASS, 0, 1},
    /* 40-47: Strings */
    {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0},
    /* 48-55: Ensemble */
    {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0},
    /* 56-63: Brass */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 64-71: Reed */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 72-79: Pipe */
    {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0}, {PC_GM_HARMONY, 2, 0},
    /* 80-87: Synth Lead */
    {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0},
    /* 88-95: Synth Pad */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 96-103: Synth Effects */
    {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0},
    /* 104-111: Ethnic */
    {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0}, {PC_GM_HARMONY, 1, 0},
    /* 112-119: Percussive */
    {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0},
    /* 120-127: Sound Effects */
    {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0}, {PC_GM_HARMONY, 0, 0},
};

#endif /* PC_GM_TABLE_H */
