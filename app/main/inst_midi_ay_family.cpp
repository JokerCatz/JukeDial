// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_ay_family.cpp (JC). */
#include "inst_midi_ay_family.h"

#include <math.h>

/* See midi_ay_family.h's top comment for what this file is and why it
 * exists (factored out of midi_fme7_chip.cpp so [ZX:AY] can share the
 * same AY/YM2149 driving logic). Every function body below is a direct,
 * unmodified-behavior extraction of midi_fme7_chip.cpp's original
 * file-scope logic, now parameterized on ay_family_ctx_t instead of
 * reading fixed file-scope statics -- the REGISTER WRITES THEMSELVES are
 * byte-for-byte identical to the pre-extraction version (verified by
 * [NES:5B]'s own register-peek tests, which still pass unchanged against
 * this factored version). */

void ay_family_write_reg(ay_family_ctx_t *ctx, int reg, int data)
{
    ctx->apu->write(0, reg, data);
    if (reg >= 0 && (unsigned)reg < Ay_Apu::reg_count) {
        ctx->shadow_regs[reg] = (uint8_t)data;
    }
}

int ay_family_fold_note(const ay_family_ctx_t *ctx, int note)
{
    while (note < ctx->note_min) {
        note += 12;
    }
    while (note > ctx->note_max) {
        note -= 12;
    }
    if (note < ctx->note_min) {
        note = ctx->note_min;
    }
    if (note > ctx->note_max) {
        note = ctx->note_max;
    }
    return note;
}

int ay_family_velocity_to_volume(int velocity)
{
    int v = velocity >> 3;
    if (v == 0) {
        v = 1;
    }
    return v;
}

double ay_family_note_freq_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void ay_family_trigger_channel(ay_family_ctx_t *ctx, int channel, int note, int velocity, int timbre)
{
    int folded = ay_family_fold_note(ctx, note);
    int period = ctx->tone_period[folded - ctx->note_min];
    int vol = ay_family_velocity_to_volume(velocity);
    bool buzz = (timbre % 2) == 1;

    ay_family_write_reg(ctx, channel * 2 + 0, period & 0xFF);
    ay_family_write_reg(ctx, channel * 2 + 1, (period >> 8) & 0x0F);

    unsigned tone_bit = AY_MIXER_TONE_A << channel; /* channel 0/1 only, see caller */
    if (buzz) {
        /* Envelope-DAC buzz -- see midi_fme7_chip.cpp's top comment's
         * "Why tone is now DISABLED during buzz" section. */
        double freq = ay_family_note_freq_hz(folded);
        int env_period = (int)((double)ctx->clock_hz / (AY_ENVELOPE_DIVISOR * freq) + 0.5);
        if (env_period < 1) {
            env_period = 1; /* real hardware treats a programmed EP of 0 as acting like 1 */
        }
        if (env_period > 0xFFFF) {
            env_period = 0xFFFF;
        }
        ctx->mixer |= tone_bit;
        ay_family_write_reg(ctx, 7, ctx->mixer);
        ay_family_write_reg(ctx, 11, env_period & 0xFF);
        ay_family_write_reg(ctx, 12, (env_period >> 8) & 0xFF);
        ay_family_write_reg(ctx, 13, 0x0E); /* continue+attack+alternate -- classic repeating buzz-bass ramp */
        ay_family_write_reg(ctx, 8 + channel, 0x10); /* envelope-enable bit */
    } else {
        ctx->mixer &= ~tone_bit;
        ay_family_write_reg(ctx, 7, ctx->mixer);
        ay_family_write_reg(ctx, 8 + channel, vol & 0x0F); /* constant volume, envelope-enable bit clear */
    }
}

void ay_family_silence_channel(ay_family_ctx_t *ctx, int channel)
{
    ay_family_write_reg(ctx, 8 + channel, 0x00);
}

void ay_family_percussion_on(ay_family_ctx_t *ctx, int note, int velocity)
{
    int noise_period;
    if (note < 40) {
        noise_period = AY_NOISE_PERIOD_KICK;
    } else if (note < 50) {
        noise_period = AY_NOISE_PERIOD_SNARE;
    } else {
        noise_period = AY_NOISE_PERIOD_HIHAT;
    }
    int vol = ay_family_velocity_to_volume(velocity);

    ay_family_write_reg(ctx, 6, noise_period & 0x1F);
    ay_family_write_reg(ctx, 10, vol & 0x0F); /* channel C volume, constant-volume mode */
}

void ay_family_silence_percussion(ay_family_ctx_t *ctx)
{
    ay_family_write_reg(ctx, 10, 0x00);
}

void ay_family_reset_mixer(ay_family_ctx_t *ctx)
{
    /* Tone A/B enabled (bits clear), tone C disabled (channel C is
     * percussion-only), noise A/B disabled, noise C enabled -- see
     * midi_fme7_chip.cpp's top comment's "Percussion" section. Channels
     * A/B's tone bits are then flipped dynamically per note by
     * ay_family_trigger_channel() (buzz mutes tone); channel C's bits are
     * never touched again after this. */
    ctx->mixer = (uint8_t)(AY_MIXER_TONE_C | AY_MIXER_NOISE_A | AY_MIXER_NOISE_B);
    ay_family_write_reg(ctx, 7, ctx->mixer);
}
