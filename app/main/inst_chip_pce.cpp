// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_pce_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Hes_Apu.h"
#include "Multi_Buffer.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_pce_tables.h"
#include "pc/pc_voicepool.h"
#include "inst_midi_wave_library.h"

/* The [PCE] adapter -- drives a
 * standalone Hes_Apu (the PC Engine/TurboGrafx-16's PSG), one of three
 * wavetable chip adapters consuming the shared
 * midi_wave_library.h.
 *
 * -- Register map (gme/Hes_Apu.cpp's write_data()/run_until(), addresses
 * 0x800-0x809) --
 *   0x800 : channel latch (0-5 valid, osc_count=6) -- every other address
 *           below targets whichever channel was latched here
 *   0x801 : GLOBAL balance (both stereo channels) -- left untouched
 *           (default 0xFF from reset), see "No panning" below
 *   0x802 : tone period, low 8 bits
 *   0x803 : tone period, high 4 bits (bits 8-11)
 *   0x804 : control -- bit7 = output enable, bit6 = DAC mode (0=wave,
 *           1=direct-DAC-write via 0x806), bits0-4 = 5-bit LINEAR volume
 *           (0-31, verified via balance_changed()'s own math with the
 *           default centered balance: `vol = control&0x1F` passes
 *           straight through to the log-volume table index)
 *   0x805 : per-channel balance -- also left untouched (default 0xFF)
 *   0x806 : wave RAM write (auto-incrementing phase, wave mode only) OR
 *           direct DAC sample (DAC mode) -- see "Wave upload" below
 *   0x807 : noise control, channels 4/5 ONLY (`if (&osc >= &oscs[4])`) --
 *           bit7 = noise enable (overrides wave entirely, checked FIRST
 *           in run_until()'s `if (noise&0x80) {...} else if
 *           (!(control&0x40)) {...wave...}`), bits0-4 = 5-bit rate
 *           (`period = (32 - (noise&0x1F)) * 64` -- LOWER rate value =
 *           LONGER period = SLOWER/lower-pitched noise)
 *
 * -- No panning -- this adapter never writes 0x801/0x805,
 * leaving Hes_Apu's own reset() default (0xFF, centered) -- balance
 * control is left unimplemented (mono-equivalent per-voice output,
 * same posture every other chip adapter here takes).
 *
 * -- Wave upload: canonical 0..255 -> PCE's UNSIGNED 5-bit DAC samples --
 * midi_wave_library.h's tables are unsigned 0..255; `canonical >> 3`
 * gives 0..31, PCE's own native sample range (Hes_Osc::wave[32], a
 * 5-bit DAC per gme/Hes_Apu.h's own `data &= 0x1F` mask in write_data()'s
 * 0x806 case) -- a direct bit-shift rescale, no sign/offset math needed
 * (PCE's wavetable, unlike SCC's, is unipolar). Re-uploaded on every
 * trigger (same idiom as [MSX:SCC]'s upload_wave(), simplest correct
 * choice over tracking "did the timbre change since last upload").
 * Phase is NOT forced to reset on retrigger (disclosed, matches
 * [MSX:SCC]/[NES:N163]'s own equivalent disclosure) -- write_data()'s
 * own phase-reset condition (`if (osc.control&0x40 & ~data) osc.phase=0`)
 * only fires on a DAC-mode-to-wave-mode TRANSITION, which happens once
 * (chip reset() leaves control=0x40) and never again once this adapter
 * settles into wave mode, so later retriggers continue from wherever the
 * phase counter currently sits rather than restarting at sample 0 --
 * audibly negligible for a 32-sample cycle at musical pitches.
 *
 * -- Voice topology: harmony_count=4 + bass_count=1 (channel 4) + channel
 * 5 percussion -- WIDER than the base 2-harmony/1-bass shape (disclosed,
 * same reasoning [MSX:SCC]'s own widened 3+1 pool uses): PCE's 6 physical
 * channels comfortably support a bigger harmony pool than the base shape,
 * and 4+1+1 uses every channel. All 6 channels are the identical
 * wavetable circuit -- "channel 4 is bass" is a topology convention for
 * roster consistency, not hardware-forced.
 *
 * -- Percussion: REAL hardware noise -- adding it was trivial here (one
 * register, 0x807, plus the ordinary control/period writes every channel
 * already needs), so this adapter uses PCE's actual noise generator on
 * channel 5 rather
 * than a library waveform played at bucketed pitches -- more authentic
 * than [MSX:SCC]/[NES:N163]'s workaround (which lack real noise
 * hardware entirely). Coarse 3-way hit-type split via the rate field
 * (same idiom as every noise-capable chip adapter here): kick=2
 * (slowest/dullest), snare=16 (mid), hihat=30 (fastest/brightest). No
 * auto-decay (same disclosed simplification as every other chip adapter
 * here); sustains until note_off. */

#define PCE_CLOCK_RATE 7159091u /* see RP2350_player/tools/gen_pce_note_table.py's top comment */
static long s_bend_clock = PCE_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9
#define PCE_WAVE_SIZE 32
#define PCE_LATCH_REG 0x800
#define PCE_PERIOD_LO_REG 0x802
#define PCE_PERIOD_HI_REG 0x803
#define PCE_CONTROL_REG 0x804
#define PCE_WAVE_DATA_REG 0x806
#define PCE_NOISE_REG 0x807
#define PCE_PERCUSSION_CHANNEL 5

static Hes_Apu s_hes;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0-3 = harmony, 4 = designated bass -- harmony_count=4, bass_count=1.
 * Channel 5 (percussion) is driven directly, outside the pool, same
 * idiom as every other chip adapter here. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected library waveform, %MIDI_WAVE_COUNT */
static int s_channel_program[16];

static void write_reg(int addr, int data)
{
    s_hes.write_data(0, addr, data);
}

static int fold_note(int note)
{
    while (note < MIDI_PCE_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_PCE_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_PCE_NOTE_MIN) {
        note = MIDI_PCE_NOTE_MIN;
    }
    if (note > MIDI_PCE_NOTE_MAX) {
        note = MIDI_PCE_NOTE_MAX;
    }
    return note;
}

/* 1..31, direct into the 5-bit linear volume field. */
static int velocity_to_volume(int velocity)
{
    int v = velocity >> 2;
    if (v == 0) {
        v = 1;
    }
    return v;
}

static void upload_wave(int timbre)
{
    int idx = timbre % MIDI_WAVE_COUNT;
    for (int i = 0; i < PCE_WAVE_SIZE; i++) {
        write_reg(PCE_WAVE_DATA_REG, midi_wave_library[idx][i] >> 3); /* 0..255 -> 0..31, see top comment */
    }
}

static void trigger_channel(int channel, int note, int velocity, int timbre)
{
    write_reg(PCE_LATCH_REG, channel);
    write_reg(PCE_CONTROL_REG, 0x00); /* disable output + wave mode while loading, see top comment */
    upload_wave(timbre);
    int folded = fold_note(note);
    int period = midi_pce_tone_period[folded - MIDI_PCE_NOTE_MIN];
    write_reg(PCE_PERIOD_LO_REG, period & 0xFF);
    write_reg(PCE_PERIOD_HI_REG, (period >> 8) & 0x0F);
    int vol = velocity_to_volume(velocity);
    write_reg(PCE_CONTROL_REG, 0x80 | (vol & 0x1F)); /* enable + wave mode + volume */
}

static void silence_channel(int channel)
{
    write_reg(PCE_LATCH_REG, channel);
    write_reg(PCE_CONTROL_REG, 0x00);
}

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    trigger_channel(slot, note, velocity, timbre);
}

static void silence_slot(int slot)
{
    silence_channel(slot);
}

static void pce_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void pce_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group, e->pulse_duty,
                                e->pulse_duty);
}

static void pce_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void pce_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void pce_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void pce_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> physical channel 5): real hardware noise, see
 * top comment. */
static void percussion_on(int note, int velocity)
{
    int rate;
    if (note < 40) {
        rate = 2; /* kick: slowest/dullest */
    } else if (note < 50) {
        rate = 16; /* snare: mid */
    } else {
        rate = 30; /* hihat: fastest/brightest */
    }
    int vol = velocity_to_volume(velocity);

    write_reg(PCE_LATCH_REG, PCE_PERCUSSION_CHANNEL);
    write_reg(PCE_NOISE_REG, 0x80 | (rate & 0x1F));
    write_reg(PCE_CONTROL_REG, 0x80 | (vol & 0x1F));
}

static void silence_percussion(void)
{
    write_reg(PCE_LATCH_REG, PCE_PERCUSSION_CHANNEL);
    write_reg(PCE_CONTROL_REG, 0x00);
}

static void pce_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        pce_note_on(channel, note, velocity);
    }
}

static void pce_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void pce_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_percussion();
}

static int pce_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = PCE_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_hes.reset();
    for (int i = 0; i < Hes_Apu::osc_count; i++) {
        s_hes.osc_output(i, s_buf.center(), NULL, NULL);
    }
    s_hes.treble_eq(blip_eq_t(-1.0));
    /* Like Scc_Apu, Hes_Apu's own constructor does NOT call volume()
     * itself (verified against gme/Hes_Apu.h; gme/Hes_Emu.cpp calls
     * `apu.volume(gain())` explicitly for exactly this reason) -- see
     * midi_scc_chip.cpp's top comment for the fuller investigation this
     * mirrors (found while building THIS adapter, applied there too). */
    s_hes.volume(1.0);

    pc_vp_init_n(&s_pool, 4, 1, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    for (int c = 0; c < 5; c++) {
        silence_channel(c);
    }
    silence_percussion();
    return 0;
}

static void pce_render(int16_t *stereo_frames, size_t frame_count)
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
            s_hes.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

#define PCE_VOICE_COUNT 6

static int pce_voice_count(void)
{
    return PCE_VOICE_COUNT;
}

static const char *pce_voice_name(int i)
{
    static const char *const names[PCE_VOICE_COUNT] = {"Channel 1", "Channel 2", "Channel 3", "Channel 4",
                                                         "Channel 5", "Noise"};
    if (i < 0 || i >= PCE_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Unipolar 0..amp_range (0x8000) -- see gme/Hes_Apu.h's own voice-peek
 * doc comment (already max()'d across the L/R sub-channels there). */
static void pce_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= PCE_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_hes.osc_last_amp(i);
    if (amp < 0) {
        amp = 0;
    }
    int scaled = (amp * 255) / (int)Hes_Osc::amp_range;
    if (scaled > 255) {
        scaled = 255;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- PCE's 4-wide
 * wavetable harmony pool; the fifth bass voice is a different instrument. */
static int pce_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(PCE_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_pce = {
    .id = "pce",
    .name = "PC Engine",
    .reset = pce_reset,
    .note_on = pce_note_on_dispatch,
    .note_off = pce_note_off,
    .program_change = pce_program_change,
    .all_off = pce_all_off,
    .note_on_direct = pce_note_on_direct,
    .note_off_direct = pce_note_off_direct,
    .note_on_gm = pce_note_on_gm,
    .program_change_gm = pce_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = pce_render,
    .voice_count = pce_voice_count,
    .voice_name = pce_voice_name,
    .voice_state = pce_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = pce_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_pce(void)
{
    return &k_chip_pce;
}

extern "C" int midi_pce_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_pce_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
