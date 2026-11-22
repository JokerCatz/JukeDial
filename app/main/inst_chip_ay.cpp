// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_ay_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Ay_Apu.h"
#include "Multi_Buffer.h"

#include "inst_midi_ay_family.h"
#include "inst_midi_ay_tables.h"
#include "pc/pc_gm_table.h"
#include "pc/pc_voicepool.h"

/* The [ZX:AY] adapter drives a standalone Ay_Apu, using the same
 * register-write mechanics as [NES:5B] (midi_fme7_chip.cpp), factored
 * into midi_ay_family.h/.cpp and reused verbatim here. See
 * midi_fme7_chip.cpp's top comment for the FULL derivation of the
 * register map, the measured envelope-buzz formula, and the percussion
 * noise-bucket idiom -- none of that is repeated here, only what is
 * genuinely SPECIFIC to this adapter.
 *
 * -- The one real difference: clock rate, and therefore the note table --
 * [NES:5B] clocks its Ay_Apu at the NES's own 2A03-derived ~1.789 MHz
 * (the Sunsoft 5B mapper rides the Famicom cartridge bus clock). The ZX
 * Spectrum's AY-3-8912 runs at its OWN, platform-native ~1.7734 MHz
 * (AY_CLOCK_RATE below -- see RP2350_player/tools/gen_ay_note_table.py's top comment
 * for the exact value and source). Both chips are literally the same
 * silicon family (AY-3-8910-compatible), so this is the honest way to
 * differentiate two identically-timbred adapters: by PLATFORM IDENTITY,
 * since the ZX Spectrum genuinely runs its AY at a different clock than
 * the NES's Sunsoft 5B mapper does. midi_ay_tables.h's
 * midi_ay_tone_period[] is regenerated at this clock, genuinely
 * different from midi_5b_tables.h's table (checked: every entry differs
 * by the ~0.9% clock ratio, not just rounding noise -- see the generator
 * script's own diff against gen_5b_note_table.py's output).
 *
 * -- Voice topology: IDENTICAL to [NES:5B] -- 2-wide harmony pool
 * (channels A/B, harmony_count=2, bass_count=0, no natural bass voice --
 * all 3 AY tone channels are the identical circuit) + channel C
 * dedicated to ch10 percussion. Program change selects tone (0) vs
 * envelope-DAC buzz (1), `timbre % 2`, same as [NES:5B] -- the AY-3-8912
 * has the identical envelope generator, so the "buzz-bass" technique is
 * equally authentic on this platform (arguably MORE culturally apt here:
 * ZX Spectrum AY chiptune trackers popularized this exact technique). */

#define AY_CLOCK_RATE 1773400u /* ZX Spectrum AY-3-8912 clock -- see RP2350_player/tools/gen_ay_note_table.py's top comment */
static long s_bend_clock = AY_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9

static Ay_Apu s_ay;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;
static uint8_t s_shadow_regs[Ay_Apu::reg_count];
static ay_family_ctx_t s_ay_ctx;

/* 0/1 = channel A/B, harmony pool (harmony_count=2, bass_count=0 -- see
 * top comment). Channel C (percussion) is driven directly, outside the
 * pool, same idiom as every other chip adapter here. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected: 0=tone, 1=buzz (timbre%2) */
static int s_channel_program[16];

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    ay_family_trigger_channel(&s_ay_ctx, slot, note, velocity, timbre);
}

static void silence_slot(int slot)
{
    ay_family_silence_channel(&s_ay_ctx, slot);
}

static void ay_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void ay_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    /* No bass slot exists (bass_count=0) -- BASS/EITHER both degrade to
     * the harmony pool, see pc_vp_note_on_gm()'s comment;
     * `pulse_duty` is reused verbatim as the tone/buzz timbre, same as
     * [NES:5B]. */
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, e->pulse_duty);
}

static void ay_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void ay_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void ay_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void ay_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

static void ay_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        ay_family_percussion_on(&s_ay_ctx, note, velocity);
    } else {
        ay_note_on(channel, note, velocity);
    }
}

static void ay_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void ay_all_off(void)
{
    pc_vp_all_off(&s_pool);
    ay_family_silence_percussion(&s_ay_ctx);
}

static int ay_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = AY_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_ay.reset();
    s_ay.output(s_buf.center());
    s_ay.treble_eq(blip_eq_t(-1.0));

    memset(s_shadow_regs, 0, sizeof s_shadow_regs);
    s_ay_ctx.apu = &s_ay;
    s_ay_ctx.shadow_regs = s_shadow_regs;
    s_ay_ctx.clock_hz = AY_CLOCK_RATE;
    s_ay_ctx.tone_period = midi_ay_tone_period;
    s_ay_ctx.note_min = MIDI_AY_NOTE_MIN;
    s_ay_ctx.note_max = MIDI_AY_NOTE_MAX;
    ay_family_reset_mixer(&s_ay_ctx);

    pc_vp_init_n(&s_pool, 2, 0, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    ay_family_silence_percussion(&s_ay_ctx);
    return 0;
}

static void ay_render(int16_t *stereo_frames, size_t frame_count)
{
    long count = (long)frame_count * 2;
    long remain = count;
    while (remain > 0) {
        long got = s_buf.read_samples((blip_sample_t *)&stereo_frames[count - remain], remain);
        remain -= got;
        if (remain > 0) {
            long frames_needed = remain / 2;
            blip_time_t clocks = (blip_time_t)(((int64_t)frames_needed * s_bend_clock) /
                                                s_sample_rate_hz) + 64;
            s_ay.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

#define AY_VOICE_COUNT 3

static int ay_voice_count(void)
{
    return AY_VOICE_COUNT;
}

static const char *ay_voice_name(int i)
{
    static const char *const names[AY_VOICE_COUNT] = {"Channel A", "Channel B", "Channel C"};
    if (i < 0 || i >= AY_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

static void ay_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= AY_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_ay.osc_last_amp(i);
    if (amp < 0) {
        amp = 0;
    }
    if (amp > Ay_Apu::amp_range) {
        amp = Ay_Apu::amp_range;
    }
    *level = (uint8_t)amp;
    *active = amp > 0;
}

/* The same-type voice group's width (chip_synth.h) -- 2-wide harmony
 * pool, no bass slot. */
static int ay_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(AY_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_ay = {
    .id = "ay",
    .name = "ZX AY",
    .reset = ay_reset,
    .note_on = ay_note_on_dispatch,
    .note_off = ay_note_off,
    .program_change = ay_program_change,
    .all_off = ay_all_off,
    .note_on_direct = ay_note_on_direct,
    .note_off_direct = ay_note_off_direct,
    .note_on_gm = ay_note_on_gm,
    .program_change_gm = ay_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = ay_render,
    .voice_count = ay_voice_count,
    .voice_name = ay_voice_name,
    .voice_state = ay_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = ay_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_zx_ay(void)
{
    return &k_chip_ay;
}

extern "C" int midi_ay_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_ay_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}

extern "C" int midi_ay_debug_reg(int reg)
{
    if (reg < 0 || (unsigned)reg >= Ay_Apu::reg_count) {
        return -1;
    }
    return s_shadow_regs[reg];
}
