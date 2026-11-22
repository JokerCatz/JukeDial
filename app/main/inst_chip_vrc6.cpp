// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_vrc6_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Multi_Buffer.h"
#include "Nes_Apu.h"
#include "Nes_Vrc6_Apu.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_nes_tables.h"
#include "pc/pc_voicepool.h"
#include "inst_midi_vrc6_tables.h"

/* The [NES+VRC6] combo adapter -- a
 * THIRD implementation of the chip_synth.h vtable, and the first to drive
 * TWO standalone gme cores at once from a single adapter: Nes_Apu (the
 * base 2A03, same object midi_nes_chip.cpp drives) alongside
 * Nes_Vrc6_Apu (Konami's cartridge expansion chip -- 2 more pulse
 * channels plus a saw channel), sharing one Stereo_Buffer. Register-write
 * shape/driving pattern/blip-time-0 discipline mirrors midi_nes_chip.cpp
 * exactly (see that file's top comment for the derivation) -- this
 * comment only covers what's NEW for the combo.
 *
 * -- Voice topology --
 * Physically driven oscillators: 2A03 pulse1/pulse2/triangle/noise (4 of
 * Nes_Apu's 5; DMC left unused/hidden, exactly like midi_nes_chip.cpp)
 * plus VRC6 pulse1/pulse2/saw (all 3 of Nes_Vrc6_Apu's oscillators) = 7
 * total, of which 6 are MELODIC (2A03 pulses+triangle, VRC6 pulses+saw)
 * and 1 is percussion-only (2A03 noise). Naively summing Nes_Apu's raw
 * osc_count (5, INCLUDING the unused DMC) plus Nes_Vrc6_Apu's osc_count
 * (3) suggests 8 -- DMC stays unused here for the identical reason
 * midi_nes_chip.cpp's own top comment gives (no CPU to feed its
 * sample-fetch DMA), so the real "melodic-capable" count is 6, not 8;
 * disclosed rather than silently padding voice_count() with a
 * permanently-silent 8th slot.
 *
 * -- Allocation policy --
 * midi_voice_pool.h was generalized from a fixed 2-harmony+
 * 1-bass shape to runtime harmony_count/bass_count specifically to fit
 * this adapter: harmony_count=4 (slots 0-3: 2A03 pulse1, 2A03 pulse2,
 * VRC6 pulse1, VRC6 pulse2 -- all 4 are the SAME "pulse-family" timbre
 * character, sticky/oldest-steal among themselves, no ordering
 * preference), bass_count=2 (slot 4: 2A03 triangle, TRIED FIRST -- same
 * lowest-note-wins press-time heuristic as [NES]'s own single bass slot;
 * slot 5: VRC6 saw, tried SECOND, only when triangle is already taken --
 * a second lead/bass voice, an overflow
 * bass rather than a co-equal one). pc_vp_note_on()'s
 * generalized bass-slot-list loop (see that file's comment) implements
 * this "try in slot order" precedence with no chip-specific code needed
 * here at all. In MIDI_MAP_GM, BASS-group programs get the same
 * triangle-then-saw precedence (both bass slots share pc_gm_table.h's
 * `pulse_duty` field as `bass_timbre`, but see the "Timbre" section below
 * for why neither voice actually reads it).
 *
 * -- Duty mapping (2A03 pulses vs VRC6 pulses) --
 * 2A03 pulses (slots 0/1) reuse pc_gm_table.h's/midi_nes_chip.cpp's
 * `pulse_duty` (0-3 hardware code) VERBATIM, identical to
 * midi_nes_chip.cpp's own trigger_pulse(). VRC6 pulses (slots 2/3) do
 * NOT share that same numeric encoding -- Nes_Vrc6_Apu's duty field
 * (Vrc6_Osc::regs[0] bits 4-6, `run_square`'s `duty = ((regs[0]>>4)&7)+1`)
 * selects a duty FRACTION of (duty)/16 for duty in 1..8, i.e. 6.25% up to
 * a MAXIMUM of 50% -- VRC6 hardware has no 75% duty option at all (unlike
 * the 2A03/GB coincidence midi_nes_chip.cpp's own top comment documents).
 * `k_vrc6_duty_field[4] = {1, 3, 5, 7}` maps the shared 0-3 duty CODE to
 * VRC6's 3-bit field, landing on ~12.5%/25%/37.5%/50% -- code 3 (2A03's
 * 75%) folds down to VRC6's ceiling (50%), a disclosed, deliberate
 * approximation (not a bug), same "closest achievable" spirit as the
 * saw-timbre disclosure below.
 *
 * -- Timbre: triangle and saw both have NO program-selectable dimension
 * (disclosed, matches [NES]'s own triangle precedent) --
 * Triangle: no volume/timbre field on real hardware at all (see
 * midi_nes_chip.cpp's top comment -- unchanged here, reused verbatim).
 * VRC6 saw (Nes_Vrc6_Apu::run_saw): the ONLY per-note field is
 * `amp_step` (Vrc6_Osc::regs[0] bits 0-5, 6-bit), which controls BOTH the
 * ramp's harmonic brightness AND its effective loudness at once (there is
 * no separate volume field for this channel) -- so rather than inventing
 * a meaningless "timbre" mapping for a single physical knob, this adapter
 * maps VELOCITY to amp_step (a genuine "how hard the saw ramp hits"
 * dynamic, the one place saw actually differs from triangle's total
 * on/off-only nature) and ignores `timbre`/program entirely for this
 * voice, exactly like triangle. amp_step is clamped to [4, 36]: values
 * above ~36 make `7 * amp_step` (the ramp's total rise across one cycle,
 * see run_saw's `amp = (amp + amp_step) & 0xFF`) exceed 255 and WRAP,
 * producing an authentic-but-harsh distortion some real VRC6 music uses
 * deliberately -- clamping avoids that by default rather than exposing
 * it as a "feature" no test could distinguish from a bug.
 *
 * -- Saw silencing: a genuine VRC6 hardware quirk, not a workaround --
 * Confirmed by tracing run_saw() (Nes_Vrc6_Apu.cpp): when
 * the channel is disabled (regs[2] bit7=0) OR (`amp_step==0 && amp==0`),
 * the emulator's "disabled" branch computes the audible level from
 * whatever the internal ramp ACCUMULATOR (`amp`, not directly
 * register-writable) currently holds -- it does NOT force that
 * accumulator to 0. Simply clearing the enable bit therefore FREEZES the
 * saw at its last ramp level instead of silencing it (a well-known real
 * VRC6 hardware characteristic, not a gme emulation gap). The accumulator
 * only actually reaches 0 via the phase-wrap reset inside the ENABLED
 * main loop (`if (--phase == 0) { phase = 7; amp = 0; }`, one wrap per
 * ~7 period ticks). So silence_saw() below writes ONLY `amp_step = 0`
 * (regs[0] = 0x00) and leaves the enable bit SET: the ramp stops
 * advancing but keeps ticking through its already-in-flight phase
 * countdown, reaches a wrap within one cycle (a few ms at any note in
 * this adapter's playable range), and lands cleanly on true 0 --
 * riding out the hardware's own reset path instead of fighting it.
 *
 * -- 2A03 side (pulses/triangle/noise): register writes, percussion,
 * fold_note, velocity mapping -- ALL reused verbatim from
 * midi_nes_chip.cpp's own derivation; see that file's top comment for
 * the full rationale (constant-volume sustain-until-off trick, triangle's
 * indefinite linear-counter reload, the 3-way noise percussion split).
 * This file does not re-derive any of that, only re-implements it against
 * this adapter's own physical slot numbering (see k_voice_names below). */

#define NES_CLOCK_RATE 1789773u /* shared 2A03/VRC6 clock -- see midi_vrc6_tables.h's generator comment */
static long s_bend_clock = NES_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9

static Nes_Apu s_apu;
static Nes_Vrc6_Apu s_vrc6;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* Physical slot numbering (see this file's top comment):
 * 0/1 = 2A03 pulse1/pulse2, 2/3 = VRC6 pulse1/pulse2 (harmony pool,
 * harmony_count=4); 4 = 2A03 triangle, 5 = VRC6 saw (bass_count=2, tried
 * in that order). Percussion (2A03 noise) is driven directly, outside
 * the pool, same as every other chip adapter here. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected duty index, harmony pulses only */
static int s_channel_program[16];

static const uint8_t k_vrc6_duty_field[4] = {1, 3, 5, 7}; /* see top comment's duty-mapping section */

static void write_nes_reg(unsigned addr, int data)
{
    s_apu.write_register(0, (nes_addr_t)addr, data);
}

static void write_vrc6(int osc, int reg, int data)
{
    s_vrc6.write_osc(0, osc, reg, data);
}

/* Shared fold/velocity helpers -- one band (MIDI_NES_NOTE_MIN/MAX, 36-96)
 * covers every table this adapter uses (midi_nes_pulse_period/
 * midi_nes_triangle_period from midi_nes_tables.h, midi_vrc6_saw_period
 * from midi_vrc6_tables.h -- all three generators target that exact same
 * band, see midi_vrc6_tables.h's own generator comment). */
static int fold_note(int note)
{
    while (note < MIDI_NES_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_NES_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_NES_NOTE_MIN) {
        note = MIDI_NES_NOTE_MIN;
    }
    if (note > MIDI_NES_NOTE_MAX) {
        note = MIDI_NES_NOTE_MAX;
    }
    return note;
}

static int velocity_to_volume(int velocity)
{
    int v = velocity >> 3;
    if (v == 0) {
        v = 1;
    }
    return v;
}

static int nes_dmc_stub_reader(void *user_data, nes_addr_t addr)
{
    (void)user_data;
    (void)addr;
    return 0;
}

/* -- 2A03 pulse (slots 0/1) -- verbatim from midi_nes_chip.cpp -- */
static void trigger_2a03_pulse(int nes_slot, int note, int velocity, int timbre)
{
    unsigned base = 0x4000u + (unsigned)nes_slot * 4u;
    int folded = fold_note(note);
    int period = midi_nes_pulse_period[folded - MIDI_NES_NOTE_MIN];
    int duty = timbre % 4;
    int vol = velocity_to_volume(velocity);

    write_nes_reg(base + 0, (duty << 6) | 0x30 | vol);
    write_nes_reg(base + 2, period & 0xFF);
    write_nes_reg(base + 3, (period >> 8) & 0x07);
}

static void silence_2a03_pulse(int nes_slot)
{
    unsigned base = 0x4000u + (unsigned)nes_slot * 4u;
    write_nes_reg(base + 0, 0x30);
}

/* -- 2A03 triangle (slot 4, bass primary) -- verbatim from midi_nes_chip.cpp -- */
static void trigger_triangle(int note)
{
    int folded = fold_note(note);
    int period = midi_nes_triangle_period[folded - MIDI_NES_NOTE_MIN];

    write_nes_reg(0x4008u, 0x80 | 0x7Fu);
    write_nes_reg(0x400Au, period & 0xFF);
    write_nes_reg(0x400Bu, (period >> 8) & 0x07);
}

static void silence_triangle(void)
{
    write_nes_reg(0x4008u, 0x00);
}

/* -- VRC6 pulse (slots 2/3, harmony pool) -- see top comment's duty-
 * mapping/no-phase-reset sections. `vrc6_osc` is 0 or 1. */
static void trigger_vrc6_pulse(int vrc6_osc, int note, int velocity, int timbre)
{
    int folded = fold_note(note);
    int period = midi_nes_pulse_period[folded - MIDI_NES_NOTE_MIN]; /* reused verbatim, see top comment */
    int duty = k_vrc6_duty_field[timbre % 4];
    int vol = velocity_to_volume(velocity);

    write_vrc6(vrc6_osc, 0, (duty << 4) | vol); /* regs[0]: gate=0, duty, volume */
    write_vrc6(vrc6_osc, 1, period & 0xFF);
    write_vrc6(vrc6_osc, 2, 0x80 | ((period >> 8) & 0x0F)); /* enable + period high */
}

static void silence_vrc6_pulse(int vrc6_osc)
{
    write_vrc6(vrc6_osc, 0, 0x00); /* volume nibble 0 -- same "write volume 0" idiom as every pulse here */
}

/* -- VRC6 saw (slot 5, bass secondary) -- see top comment's timbre and
 * silencing sections. */
static void trigger_saw(int note, int velocity)
{
    int folded = fold_note(note);
    int period = midi_vrc6_saw_period[folded - MIDI_VRC6_NOTE_MIN];
    int step = 4 + (velocity * 32) / 127; /* clamp [4,36], see top comment */
    if (step > 36) {
        step = 36;
    }

    write_vrc6(2, 0, step & 0x3F);
    write_vrc6(2, 1, period & 0xFF);
    write_vrc6(2, 2, 0x80 | ((period >> 8) & 0x0F));
}

static void silence_saw(void)
{
    write_vrc6(2, 0, 0x00); /* amp_step=0, enable bit left set -- rides out the phase wrap, see top comment */
}

/* pc_vp_trigger_fn/silence_fn callbacks. */
static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    switch (slot) {
    case 0: trigger_2a03_pulse(0, note, velocity, timbre); break;
    case 1: trigger_2a03_pulse(1, note, velocity, timbre); break;
    case 2: trigger_vrc6_pulse(0, note, velocity, timbre); break;
    case 3: trigger_vrc6_pulse(1, note, velocity, timbre); break;
    case 4: trigger_triangle(note); break;
    case 5: trigger_saw(note, velocity); break;
    default: break;
    }
}

static void silence_slot(int slot)
{
    switch (slot) {
    case 0: silence_2a03_pulse(0); break;
    case 1: silence_2a03_pulse(1); break;
    case 2: silence_vrc6_pulse(0); break;
    case 3: silence_vrc6_pulse(1); break;
    case 4: silence_triangle(); break;
    case 5: silence_saw(); break;
    default: break;
    }
}

/* -- Percussion (channel 10 -> 2A03 noise) -- verbatim from
 * midi_nes_chip.cpp's nes_percussion_on(); see that file's own comment
 * for the full envelope/frame-sequencer-latency derivation. */
#define NES_PERC_ENV_PERIOD 2
static void percussion_on(int note, int velocity)
{
    (void)velocity;
    int period_index;
    int mode;
    if (note < 40) {
        period_index = 13;
        mode = 0;
    } else if (note < 50) {
        period_index = 6;
        mode = 1;
    } else {
        period_index = 1;
        mode = 1;
    }

    write_nes_reg(0x400Cu, NES_PERC_ENV_PERIOD);
    write_nes_reg(0x400Eu, (mode << 7) | period_index);
    write_nes_reg(0x400Fu, 0x00);
}

static void silence_noise(void)
{
    write_nes_reg(0x400Cu, 0x30);
}

static void vrc6_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void vrc6_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, 0 /* bass_timbre unused -- neither bass voice has one */);
}

static void vrc6_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void vrc6_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void vrc6_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void vrc6_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

static void vrc6_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        vrc6_note_on(channel, note, velocity);
    }
}

static void vrc6_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void vrc6_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_noise();
}

static int vrc6_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = NES_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_apu.reset(false, 0);
    s_apu.dmc_reader(nes_dmc_stub_reader, NULL);
    for (int i = 0; i < 4; i++) {
        s_apu.osc_output(i, s_buf.center());
    }
    s_apu.treble_eq(blip_eq_t(-1.0));

    s_vrc6.reset();
    s_vrc6.output(s_buf.center());
    s_vrc6.treble_eq(blip_eq_t(-1.0));

    write_nes_reg(0x4015u, 0x0F);
    write_nes_reg(0x4017u, 0x00);
    write_nes_reg(0x4001u, 0x00);
    write_nes_reg(0x4005u, 0x00);

    pc_vp_init_n(&s_pool, 4, 2, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    silence_noise();
    return 0;
}

static void vrc6_render(int16_t *stereo_frames, size_t frame_count)
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
            s_apu.end_frame(clocks);
            s_vrc6.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

#define VRC6_VOICE_COUNT 7

static int vrc6_voice_count(void)
{
    return VRC6_VOICE_COUNT;
}

static const char *vrc6_voice_name(int i)
{
    static const char *const names[VRC6_VOICE_COUNT] = {
        "Pulse 1", "Pulse 2", "VRC6 Pulse 1", "VRC6 Pulse 2", "Triangle", "Saw", "Noise",
    };
    if (i < 0 || i >= VRC6_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

static void vrc6_voice_state(int i, uint8_t *level, int *active)
{
    int amp;
    int max_amp;
    switch (i) {
    case 0: amp = s_apu.osc_last_amp(0); max_amp = 15; break;
    case 1: amp = s_apu.osc_last_amp(1); max_amp = 15; break;
    case 2: amp = s_vrc6.osc_last_amp(0); max_amp = 15; break;
    case 3: amp = s_vrc6.osc_last_amp(1); max_amp = 15; break;
    case 4: amp = s_apu.osc_last_amp(2); max_amp = 15; break;
    case 5: amp = s_vrc6.osc_last_amp(2); max_amp = 31; break; /* saw: amp>>3, 0..31, see top comment */
    case 6: amp = s_apu.osc_last_amp(3); max_amp = 15; break;
    default:
        *level = 0;
        *active = 0;
        return;
    }
    if (amp < 0) {
        amp = -amp;
    }
    int scaled = (amp * 255) / max_amp;
    if (scaled > 255) {
        scaled = 255;
    }
    if (scaled < 0) {
        scaled = 0;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- here the 4-wide
 * pulse-family harmony pool (2A03 pulse1/2 + VRC6 pulse1/2); the triangle
 * and saw bass slots are each a different instrument, so they are not in
 * it. */
static int vrc6_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(NES_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_vrc6 = {
    .id = "vrc6",
    .name = "NES+VRC6",
    .reset = vrc6_reset,
    .note_on = vrc6_note_on_dispatch,
    .note_off = vrc6_note_off,
    .program_change = vrc6_program_change,
    .all_off = vrc6_all_off,
    .note_on_direct = vrc6_note_on_direct,
    .note_off_direct = vrc6_note_off_direct,
    .note_on_gm = vrc6_note_on_gm,
    .program_change_gm = vrc6_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = vrc6_render,
    .voice_count = vrc6_voice_count,
    .voice_name = vrc6_voice_name,
    .voice_state = vrc6_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = vrc6_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_nes_vrc6(void)
{
    return &k_chip_vrc6;
}

/* Test-only accessors -- same rationale/shape as midi_gb_chip.cpp's/
 * midi_nes_chip.cpp's own, against this file's own s_pool (0/1=2A03
 * pulse1/2, 2/3=VRC6 pulse1/2, 4=triangle, 5=saw). */
extern "C" int midi_vrc6_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_vrc6_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
