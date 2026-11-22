// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef MIDI_AY_FAMILY_H
#define MIDI_AY_FAMILY_H

#include <stdint.h>

#include "Ay_Apu.h"

/* AY/YM2149-family driving logic shared between midi_fme7_chip.cpp
 * ([NES:5B]) and midi_ay_chip.cpp ([ZX:AY]) -- both chips are the same
 * AY-3-8910-compatible silicon, so this factors out the identical
 * register-write mechanics rather than duplicating them.
 * midi_fme7_chip.cpp's top comment carries the FULL derivation this
 * header intentionally does not repeat: the measured envelope-buzz
 * formula (AY_ENVELOPE_DIVISOR, found by building a standalone
 * measurement harness against gme's own unmodified Ay_Apu, NOT derived
 * from datasheet prose), the mixer-bit register map, the "why tone is
 * disabled during buzz" envelope-DAC technique, and the percussion
 * noise-bucket idiom. Only [NES:5B] and [ZX:AY]'s CLOCK RATE and TONE-
 * PERIOD TABLE differ (see midi_ay_chip.cpp's own top comment for why the
 * ZX Spectrum's own ~1.7734 MHz clock, not the NES's ~1.789 MHz one,
 * makes the two chips' note tables genuinely different) -- everything
 * else below is IDENTICAL register-write mechanics, now written once.
 *
 * What stays PER-ADAPTER (deliberately NOT moved here): the static
 * Ay_Apu/Stereo_Buffer instances, the shadow-register cache storage, the
 * voice pool, and the chip_synth_t vtable. [NES:5B] and [ZX:AY] are two
 * INDEPENDENTLY SELECTABLE MIDI tracks, each with its own chip state
 * (either could be mid-note when player_backend_midi.c's track-nav
 * switches between them) -- sharing storage across them would be wrong.
 * Only the STATELESS mechanics below are shared, via a small
 * ay_family_ctx_t the caller owns (one instance per adapter, file-scope
 * static) and passes by pointer to every function. */

#define AY_MIXER_TONE_A 0x01
#define AY_MIXER_TONE_B 0x02
#define AY_MIXER_TONE_C 0x04
#define AY_MIXER_NOISE_A 0x08
#define AY_MIXER_NOISE_B 0x10
#define AY_MIXER_NOISE_C 0x20

/* Measured constant -- see midi_fme7_chip.cpp's top comment's "Buzz:
 * MEASURED, not argued" section for the full derivation (unchanged by
 * this factoring; the measurement is against Ay_Apu itself, not any
 * clock rate, so it applies identically to both adapters). */
#define AY_ENVELOPE_DIVISOR 1024.0

/* Noise-period (R6, 5 bits) bucket values for the 3-way percussion split
 * -- see midi_fme7_chip.cpp's top comment's "Percussion" section
 * (arbitrary-but-ordered, no "correct" hardware target). */
#define AY_NOISE_PERIOD_KICK 28
#define AY_NOISE_PERIOD_SNARE 12
#define AY_NOISE_PERIOD_HIHAT 1

typedef struct {
    Ay_Apu *apu;
    uint8_t *shadow_regs; /* caller-owned array[Ay_Apu::reg_count] */
    uint8_t mixer;        /* current R7 shadow -- mutated by the trigger/percussion functions below */
    uint32_t clock_hz;    /* the adapter's own chip clock -- see midi_ay_chip.cpp/midi_fme7_chip.cpp */
    const uint16_t *tone_period; /* indexed by [note - note_min] */
    int note_min;
    int note_max;
} ay_family_ctx_t;

/* Writes reg via ctx->apu->write() (time 0, same "register writes land at
 * the top of the current audio block" idiom every adapter in this
 * project uses) and mirrors it into ctx->shadow_regs -- see
 * midi_fme7_chip.cpp's top comment's "Debug register peek" section for
 * why the shadow cache exists (Ay_Apu exposes no public regs accessor). */
void ay_family_write_reg(ay_family_ctx_t *ctx, int reg, int data);

/* Octave-folds note into [ctx->note_min, ctx->note_max], clamping only if
 * the band is somehow narrower than an octave (never the case for either
 * current adapter). */
int ay_family_fold_note(const ay_family_ctx_t *ctx, int note);

int ay_family_velocity_to_volume(int velocity);
double ay_family_note_freq_hz(int note);

/* channel: 0=A, 1=B, 2=C. timbre odd (timbre%2==1) selects the buzz
 * program; even selects plain tone -- see midi_fme7_chip.cpp's top
 * comment. Writes the tone-period regs (or the envelope regs, for buzz)
 * and updates ctx->mixer's tone-enable bit for this channel. */
void ay_family_trigger_channel(ay_family_ctx_t *ctx, int channel, int note, int velocity, int timbre);
void ay_family_silence_channel(ay_family_ctx_t *ctx, int channel);

/* Percussion, always channel C (R6 noise period + R10 volume) -- see
 * midi_fme7_chip.cpp's top comment's "Percussion" section. Does NOT touch
 * ctx->mixer (channel C's mixer bits -- tone off, noise on -- are set
 * once by ay_family_reset_mixer() below and never touched again). */
void ay_family_percussion_on(ay_family_ctx_t *ctx, int note, int velocity);
void ay_family_silence_percussion(ay_family_ctx_t *ctx);

/* Sets ctx->mixer to the reset-state pattern (tone A/B enabled, tone C
 * disabled, noise A/B disabled, noise C enabled -- channel C is
 * percussion-only) and writes it to R7. Call once from the adapter's own
 * reset(), after ay_family_write_reg's shadow array has been cleared. */
void ay_family_reset_mixer(ay_family_ctx_t *ctx);

#endif /* MIDI_AY_FAMILY_H */
