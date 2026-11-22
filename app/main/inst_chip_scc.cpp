// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_scc_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Kss_Scc_Apu.h"
#include "Multi_Buffer.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_scc_tables.h"
#include "pc/pc_voicepool.h"
#include "inst_midi_wave_library.h"

/* The [MSX:SCC] adapter -- drives a
 * standalone Scc_Apu (Konami's SCC wavetable expansion chip for the
 * MSX), one of three wavetable chip adapters consuming the
 * shared midi_wave_library.h.
 *
 * -- Register map (gme/Kss_Scc_Apu.cpp's run_until(), reg_count=0x90) --
 *   0x00-0x1F : channel 0's 32-byte SIGNED (int8_t) wavetable RAM
 *   0x20-0x3F : channel 1's own wavetable
 *   0x40-0x5F : channel 2's own wavetable
 *   0x60-0x7F : channel 3's own wavetable -- ALSO channel 4's (see below)
 *   0x80/0x81 : channel 0 tone period, low byte / high nibble (bits8-11)
 *   0x82/0x83 : channel 1 tone period
 *   0x84/0x85 : channel 2 tone period
 *   0x86/0x87 : channel 3 tone period
 *   0x88/0x89 : channel 4 tone period
 *   0x8A-0x8E : channel 0-4 volume (4 bits, 0-15, linear -- NOT an AY-style
 *               log table; `volume = (regs[0x8A+i]&0x0F) * (amp_range/256/15)`)
 *   0x8F      : per-channel enable mask, bit i = 1 enables channel i's
 *               output (ACTIVE-HIGH, unlike the AY family's active-low
 *               mixer -- verified by reading the `if (regs[0x8F] &
 *               (1<<index))` gate directly, not assumed)
 *
 * -- Hardware quirk: channel 4 has NO wavetable RAM of its own --
 * `Scc_Apu::run_until()`'s own wave-pointer code: `int8_t const* wave =
 * (int8_t*)regs + index*wave_size; if (index==osc_count-1) wave -=
 * wave_size;` -- channel 4 (index 4, the last of 5) ALWAYS reads channel
 * 3's wavetable RAM (0x60-0x7F), a real SCC silicon limitation (5
 * channels, only 4 independent wavetables), not an emulation gap. This
 * adapter's topology (below) puts the designated BASS voice on channel 3
 * and PERCUSSION on channel 4 specifically because of this constraint:
 * both always upload the SAME globally-selected library waveform
 * (s_timbre_index, shared across every melodic voice here, same as
 * every other chip adapter's single-timbre-index convention) whenever
 * either one triggers, so the shared physical RAM never actually needs
 * to hold two DIFFERENT waveforms at once -- re-uploading identical
 * bytes from whichever channel triggers second is a harmless no-op, not
 * a glitch. (An earlier design considered giving percussion its own
 * "harsh" library waveform, distinct from the melodic program; that
 * would have corrupted channel 3's currently-sounding tone the instant
 * percussion re-triggered mid-note, since the upload is a real register
 * write to shared RAM -- rejected once this constraint was traced
 * through the source, not caught by ear.) Percussion is differentiated
 * from melodic notes by PITCH bucket only (see "Percussion" below), the
 * same "no independent timbre dimension for drums" situation
 * [NES:5B]/[Atari:POKEY]'s noise percussion already has for a different
 * hardware reason.
 *
 * -- Wave upload: canonical 0..255 -> SCC's signed int8_t RAM --
 * midi_wave_library.h's tables are unsigned 0..255 (this project's
 * pre-existing wave-table convention, see that header's own comment).
 * SCC wave RAM is SIGNED -128..127. `(uint8_t)(canonical - 128)`
 * reinterpreted as int8_t gives exactly that range (0->-128, 128->0,
 * 255->127) -- verified by hand: any V in 0..255, `(V-128)&0xFF` read
 * back as int8_t equals V-128 (two's-complement bit pattern is
 * identical either way), so this is a bit-exact remap, not an
 * approximation.
 *
 * -- Voice topology: harmony_count=3 (channels 0/1/2) + bass_count=1
 * (channel 3) + channel 4 percussion -- the SAME 4-melodic-plus-
 * percussion family shape (2-harmony-plus-1-bass, here widened to
 * 3-harmony-plus-1-bass to use all 5 physical channels) every non-AY
 * chip adapter in this project uses; a topology convention
 * (SCC's 5 channels are the identical circuit, no natural bass timbre)
 * exactly like [SMS:PSG]'s own equivalent choice. */

#define SCC_CLOCK_RATE 3579545u /* MSX Z80 clock, see RP2350_player/tools/gen_scc_note_table.py's top comment */
static long s_bend_clock = SCC_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9
#define SCC_WAVE_SIZE 32
#define SCC_ENABLE_MASK_REG 0x8F
#define SCC_PERCUSSION_CHANNEL 4 /* shares channel 3's wavetable RAM, see top comment */

static Scc_Apu s_scc;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0/1/2 = harmony, 3 = designated bass -- harmony_count=3, bass_count=1.
 * Channel 4 (percussion) is driven directly, outside the pool, same
 * idiom as every other chip adapter here. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected library waveform, %MIDI_WAVE_COUNT */
static int s_channel_program[16];

/* Test-only register shadow -- Scc_Apu keeps its `regs[]` private with no
 * public accessor (same situation midi_fme7_chip.cpp/midi_ay_chip.cpp
 * solve for Ay_Apu; see midi_ay_family.h's own doc comment), so
 * write_reg() below mirrors every write, letting suite_midi verify the
 * wave-upload bytes exactly rather than only inferring them from audio. */
static uint8_t s_shadow_regs[Scc_Apu::reg_count];

static void write_reg(int reg, int data)
{
    s_scc.write(0, reg, data);
    if (reg >= 0 && (unsigned)reg < Scc_Apu::reg_count) {
        s_shadow_regs[reg] = (uint8_t)data;
    }
}

static int fold_note(int note)
{
    while (note < MIDI_SCC_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_SCC_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_SCC_NOTE_MIN) {
        note = MIDI_SCC_NOTE_MIN;
    }
    if (note > MIDI_SCC_NOTE_MAX) {
        note = MIDI_SCC_NOTE_MAX;
    }
    return note;
}

static int velocity_to_volume(int velocity)
{
    int v = velocity >> 3;
    if (v == 0) {
        v = 1;
    }
    return v; /* 1..15, direct into the 4-bit linear volume field */
}

/* Uploads library waveform `timbre` into the 32-byte wave RAM region
 * starting at `wave_base` (0x00/0x20/0x40/0x60 -- channel 4 always
 * passes 0x60, see top comment). */
static void upload_wave(int wave_base, int timbre)
{
    int idx = timbre % MIDI_WAVE_COUNT;
    for (int i = 0; i < SCC_WAVE_SIZE; i++) {
        uint8_t signed_byte = (uint8_t)(midi_wave_library[idx][i] - 128);
        write_reg(wave_base + i, signed_byte);
    }
}

/* channel: 0-3 only (the voice pool's 4 melodic slots -- percussion
 * (physical channel 4) is driven separately by percussion_on() below,
 * never through this function). */
static void trigger_channel(int channel, int note, int velocity, int timbre)
{
    upload_wave(channel * 0x20, timbre);
    int folded = fold_note(note);
    int period = midi_scc_tone_period[folded - MIDI_SCC_NOTE_MIN];
    int vol = velocity_to_volume(velocity);
    write_reg(0x80 + channel * 2 + 0, period & 0xFF);
    write_reg(0x80 + channel * 2 + 1, (period >> 8) & 0x0F);
    write_reg(0x8A + channel, vol & 0x0F);
}

static void silence_channel(int channel)
{
    write_reg(0x8A + channel, 0x00);
}

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    trigger_channel(slot, note, velocity, timbre);
}

static void silence_slot(int slot)
{
    silence_channel(slot);
}

static void scc_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void scc_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group, e->pulse_duty,
                                e->pulse_duty);
}

static void scc_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void scc_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void scc_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void scc_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> physical channel 4): no independent timbre
 * (shares channel 3's wave RAM, see top comment) -- differentiated from
 * melodic notes by a coarse 3-way PITCH bucket only, reusing this
 * chip's own note table's extremes/midpoint (lowest/middle/highest
 * playable period) rather than inventing new magic constants. No
 * auto-decay (same disclosed simplification every other chip adapter
 * here uses); sustains until note_off. */
static void percussion_on(int note, int velocity)
{
    static const int kBucketIndex[3] = {0, MIDI_SCC_NOTE_COUNT / 2, MIDI_SCC_NOTE_COUNT - 1};
    int bucket;
    if (note < 40) {
        bucket = 0; /* kick: lowest/slowest period -> low pitch thump */
    } else if (note < 50) {
        bucket = 1; /* snare: mid */
    } else {
        bucket = 2; /* hihat: highest/fastest period -> bright */
    }
    int period = midi_scc_tone_period[kBucketIndex[bucket]];
    int vol = velocity_to_volume(velocity);

    upload_wave(3 * 0x20, s_timbre_index); /* re-upload the current global waveform -- harmless no-op if unchanged, see top comment */
    write_reg(0x80 + SCC_PERCUSSION_CHANNEL * 2 + 0, period & 0xFF);
    write_reg(0x80 + SCC_PERCUSSION_CHANNEL * 2 + 1, (period >> 8) & 0x0F);
    write_reg(0x8A + SCC_PERCUSSION_CHANNEL, vol & 0x0F);
}

static void silence_percussion(void)
{
    write_reg(0x8A + SCC_PERCUSSION_CHANNEL, 0x00);
}

static void scc_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        scc_note_on(channel, note, velocity);
    }
}

static void scc_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void scc_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_percussion();
}

static int scc_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = SCC_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_scc.output(s_buf.center());
    s_scc.treble_eq(blip_eq_t(-1.0));
    /* Unlike Ay_Apu/Sap_Apu (whose constructors call volume(1.0)
     * themselves), Scc_Apu's own constructor does NOT (`Scc_Apu() {
     * output(0); }`, verified by reading gme/Kss_Scc_Apu.h) -- without an
     * explicit call here, the Blip_Synth's internal delta-to-amplitude
     * table is never populated and every register write computes a
     * genuinely correct, nonzero `delta` that nonetheless renders as
     * silence (found empirically: temporary instrumentation of the
     * vendored, gitignored Kss_Scc_Apu.cpp -- reverted before committing,
     * not a patch -- showed osc.last_amp correctly alternating +-12288
     * while the actual PCM output stayed all-zero). gme's own Kss_Emu.cpp
     * calls `scc.volume(g)` for exactly this reason. */
    s_scc.volume(1.0);

    memset(s_shadow_regs, 0, sizeof s_shadow_regs);
    write_reg(SCC_ENABLE_MASK_REG, 0x1F); /* enable all 5 channels' output gate, see top comment's register map */

    pc_vp_init_n(&s_pool, 3, 1, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    upload_wave(0x00, 0);
    upload_wave(0x20, 0);
    upload_wave(0x40, 0);
    upload_wave(0x60, 0); /* also channel 4's, shared -- see top comment */
    silence_channel(0);
    silence_channel(1);
    silence_channel(2);
    silence_channel(3);
    silence_percussion();
    return 0;
}

static void scc_render(int16_t *stereo_frames, size_t frame_count)
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
            s_scc.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

#define SCC_VOICE_COUNT 5

static int scc_voice_count(void)
{
    return SCC_VOICE_COUNT;
}

static const char *scc_voice_name(int i)
{
    static const char *const names[SCC_VOICE_COUNT] = {"Channel 1", "Channel 2", "Channel 3", "Channel 4",
                                                         "Noise"};
    if (i < 0 || i >= SCC_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Signed: wave[phase]*volume (see gme/Kss_Scc_Apu.h's own voice-peek doc
 * comment), magnitude bounded 128*120=15360, NOT the class's own private
 * amp_range constant (already documented by the patch itself -- see
 * components/gme/README.md's "Scc_Apu" field-verification
 * entry). Take abs()+rescale to this project's 0..255 convention, same
 * pattern player_backend_gme.cpp's own SCC voice adapter uses. */
static void scc_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= SCC_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_scc.osc_last_amp(i);
    if (amp < 0) {
        amp = -amp;
    }
    int scaled = (amp * 255) / 15360;
    if (scaled > 255) {
        scaled = 255;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- SCC's 3-wide
 * wavetable harmony pool; the fourth bass voice is a different instrument. */
static int scc_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(SCC_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_scc = {
    .id = "scc",
    .name = "MSX SCC",
    .reset = scc_reset,
    .note_on = scc_note_on_dispatch,
    .note_off = scc_note_off,
    .program_change = scc_program_change,
    .all_off = scc_all_off,
    .note_on_direct = scc_note_on_direct,
    .note_off_direct = scc_note_off_direct,
    .note_on_gm = scc_note_on_gm,
    .program_change_gm = scc_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = scc_render,
    .voice_count = scc_voice_count,
    .voice_name = scc_voice_name,
    .voice_state = scc_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = scc_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_msx_scc(void)
{
    return &k_chip_scc;
}

extern "C" int midi_scc_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_scc_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}

/* Test-only register peek (see s_shadow_regs's own doc comment above). */
extern "C" int midi_scc_debug_reg(int reg)
{
    if (reg < 0 || (unsigned)reg >= Scc_Apu::reg_count) {
        return -1;
    }
    return s_shadow_regs[reg];
}
