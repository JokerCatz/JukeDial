// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sticky harmony/bass voice allocator, ported from RP2350_player src/player/midi_voice_pool.*.
 * Slots [0, harmony) = interchangeable harmony voices (free, else steal the oldest);
 * slots [harmony, harmony + bass) = bass voices, taken in order by a note not higher than every held harmony
 * note; never stolen. Register writes live behind the trigger/silence callbacks (the chip adapter). */
#define PC_VP_MAX 8

typedef enum { PC_GM_HARMONY = 0, PC_GM_BASS = 1, PC_GM_EITHER = 2 } pc_gm_group_t;

typedef void (*pc_vp_trigger_fn)(int slot, int note, int velocity, int timbre);
typedef void (*pc_vp_silence_fn)(int slot);

typedef struct { bool active; int note, channel, velocity, timbre; uint32_t order; } pc_vp_slot_t;
typedef struct {
    pc_vp_slot_t slot[PC_VP_MAX];
    int harmony, bass;
    uint32_t next_order;
    pc_vp_trigger_fn trigger;
    pc_vp_silence_fn silence;
} pc_vp_t;

void pc_vp_init_n(pc_vp_t *p, int harmony, int bass, pc_vp_trigger_fn t, pc_vp_silence_fn s);
void pc_vp_init(pc_vp_t *p, pc_vp_trigger_fn t, pc_vp_silence_fn s);    /* 2 harmony + 1 bass (GB / NES) */
void pc_vp_note_on(pc_vp_t *p, int ch, int note, int vel, int timbre);  /* Auto */
void pc_vp_note_on_gm(pc_vp_t *p, int ch, int note, int vel, pc_gm_group_t g, int harmony_timbre, int bass_timbre);
void pc_vp_note_off(pc_vp_t *p, int ch, int note);
void pc_vp_note_on_direct(pc_vp_t *p, int voice, int note, int vel, int timbre);   /* mono per voice */
void pc_vp_note_off_direct(pc_vp_t *p, int voice, int note);
void pc_vp_all_off(pc_vp_t *p);
int  pc_vp_harmony_count(const pc_vp_t *p);
int  pc_vp_voice_note(const pc_vp_t *p, int slot);      /* -1 idle */
int  pc_vp_voice_timbre(const pc_vp_t *p, int slot);    /* -1 idle */
int  pc_vp_voice_channel(const pc_vp_t *p, int slot);   /* -1 idle */
#ifdef __cplusplus
}
#endif
