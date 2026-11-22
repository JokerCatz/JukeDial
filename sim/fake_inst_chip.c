// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_chip (the 11 chip adapters of the instrument: GME APUs driven note by note). */
/* Behaviour: the real table of ids, names and voice counts; a voice shows level 200 while a note held by the engine fake maps to it, else 0. */
/* Empty: render, note_on/off, program change (the engine fake makes no sound). */
#include <string.h>
#include "inst_chip.h"
#include "sim.h"

typedef struct { const char *id, *name; int voices; } fake_chip_t;
static const fake_chip_t k_tab[] = {
    { "nes", "NES 2A03", 4 }, { "gb", "Game Boy", 4 }, { "sms", "Sega SMS", 4 }, { "ay", "ZX AY", 3 }, { "scc", "MSX SCC", 5 }, { "pce", "PC Engine", 6 },
    { "n163", "NES N163", 4 }, { "vrc6", "NES+VRC6", 7 }, { "5b", "NES 5B", 3 }, { "pokey", "Atari POKEY", 3 }, { "fm", "YM2612 FM", 6 },
};
#define NCHIP ((int)(sizeof k_tab / sizeof k_tab[0]))
static pc_synth_t s_syn[NCHIP];

#define VOICE_FN(i) \
    static int vc##i(void) { return k_tab[i].voices; }
VOICE_FN(0) VOICE_FN(1) VOICE_FN(2) VOICE_FN(3) VOICE_FN(4) VOICE_FN(5) VOICE_FN(6) VOICE_FN(7) VOICE_FN(8) VOICE_FN(9) VOICE_FN(10)
static int (*const k_vc[NCHIP])(void) = { vc0, vc1, vc2, vc3, vc4, vc5, vc6, vc7, vc8, vc9, vc10 };

static const char *vname(int i) { static const char *const n[] = { "1", "2", "3", "4", "5", "6", "7", "8" }; return i >= 0 && i < 8 ? n[i] : NULL; }
static void vstate(int i, uint8_t *level, int *active)
{
    int note = fake_inst_engine_held(i);                  /* sim.h: the note held for voice i (the engine fake), -1 */
    *active = note >= 0;
    *level = note >= 0 ? 200 : 0;
}
static int vnote(int i) { return fake_inst_engine_held(i); }
static void noop_i2(int a, int b) { (void)a; (void)b; }
static void noop_i3(int a, int b, int c) { (void)a; (void)b; (void)c; }
static void noop_v(void) {}
static int reset(uint32_t r) { (void)r; return 0; }
static void render(int16_t *s, size_t n) { memset(s, 0, n * 4); }
static int harm(void) { return 1; }

int inst_chip_count(void) { return NCHIP; }
const pc_synth_t *inst_chip(int i)
{
    if (i < 0 || i >= NCHIP) return NULL;
    pc_synth_t *s = &s_syn[i];
    if (!s->id) {
        *s = (pc_synth_t){ .id = k_tab[i].id, .name = k_tab[i].name, .reset = reset, .note_on = noop_i3, .note_off = noop_i2, .program_change = noop_i2,
                           .all_off = noop_v, .note_on_direct = noop_i3, .note_off_direct = noop_i2, .note_on_gm = noop_i3, .program_change_gm = noop_i2,
                           .render = render, .voice_count = k_vc[i], .voice_name = vname, .voice_state = vstate, .voice_note = vnote, .harmony_count = harm };
    }
    return s;
}
int inst_chip_find(const char *id)
{
    for (int i = 0; i < NCHIP; i++) if (id && !strcmp(id, k_tab[i].id)) return i;
    return 0;
}
