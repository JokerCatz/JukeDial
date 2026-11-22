// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_n163_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Multi_Buffer.h"
#include "Nes_Namco_Apu.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_n163_tables.h"
#include "pc/pc_voicepool.h"
#include "inst_midi_wave_library.h"

/* The [NES:N163] adapter -- drives a
 * standalone Nes_Namco_Apu (the Namco 163's wavetable expansion chip for
 * the Famicom), one of three wavetable chip adapters consuming
 * the shared midi_wave_library.h.
 *
 * -- Channel-count tradeoff --
 * Real N163 hardware time-multiplexes ALL active channels through ONE
 * shared DAC each audio cycle -- MORE active channels means a LOWER
 * effective sample rate per channel (the well-documented N163 hardware
 * tradeoff: 8 channels ~= 1/8th a single channel's own update rate).
 * This adapter fixes `active_oscs = 4` (ACTIVE_OSCS below): enough for
 * this project's usual 2-harmony/1-bass/1-percussion shape at a
 * healthier per-channel update rate than 8 would give, at the cost of
 * only using half the chip's maximum polyphony. Real hardware only ever
 * activates the TOP `active_oscs` of its 8 physical oscillators (indices
 * `osc_count-active_oscs .. osc_count-1`, i.e. 4-7 here) -- verified by
 * reading `Nes_Namco_Apu::run_until()`'s own `for (i =
 * osc_count-active_oscs; i < osc_count; ...)` loop bound, not assumed;
 * oscillators 0-3 are simply never touched by this adapter.
 *
 * -- Register model (gme/Nes_Namco_Apu.cpp, reverse-engineered bit-by-bit
 * -- addressed via write_addr()/write_data(), a shared
 * 0x80-byte RAM covering BOTH per-channel control registers (8 bytes
 * each, base 0x40+physIdx*8) and wavetable sample storage; real hardware
 * lets a game freely choose the split, we fix a simple non-overlapping
 * layout, see "Wave upload" below) --
 *   offset+0 : frequency register, low 8 bits
 *   offset+2 : frequency register, mid 8 bits
 *   offset+4 : bits0-1 = frequency register, top 2 bits (18-bit total);
 *              bits2-4 = wavelength selector (0 = full 32 samples, this
 *              adapter's only setting -- `wave_size = 32 -
 *              selector*4`); bits5-7 MUST be nonzero for the emulator to
 *              produce any output at all (`if (!(osc_reg[4]&0xE0))
 *              continue;` -- verified directly from source, these are
 *              real hardware's own phase-accumulator high bits, which
 *              this simplified emulator core doesn't otherwise consult;
 *              this adapter always writes 0xE0 here, a value with no
 *              phase meaning for us since we never read phase back
 *              either)
 *   offset+6 : wavetable sample base, in NIBBLES (0-3 index into a
 *              private 32-nibble/16-byte region, see "Wave upload" below)
 *   offset+7 : volume, 4 bits (bits0-3) -- for the LAST physical
 *              oscillator (index 7) ONLY, bits4-6 ALSO double as the
 *              GLOBAL active-channel-count selector (`(reg[0x7F]>>4&7)+1`
 *              -- a genuine real-hardware quirk, not a bug: channel 7's
 *              own volume register byte IS the count-select register).
 *              This adapter's write_osc7_volume() ORs the fixed count
 *              field back in on every write so the two never fight.
 * offsets+1/+3/+5 (phase accumulator low/mid/high on real hardware) are
 * NEVER written here -- confirmed by reading run_until(): the emulator
 * tracks its own internal `Namco_Osc::wave_pos`/`delay` state instead of
 * consulting these register bytes at all, so real hardware's "seek
 * playback position via phase register" feature is inert in this core
 * and irrelevant to this adapter.
 *
 * -- Wave upload: canonical 0..255 -> N163's UNSIGNED 4-bit wavetable
 * nibbles -- `canonical >> 4` gives 0..15 (unipolar, matching the
 * emulator's own `sample = (sample&15)*volume`, no sign math, same
 * shape as [PCE]'s own `>>3` rescale). Each of the 4 active channels
 * (k=0..3, physical osc 4+k) gets its OWN private 32-nibble/16-byte
 * region at byte offset k*16 (reg[0x00-0x3F], well clear of the channel
 * register block at reg[0x60-0x7F]) -- UNLIKE [MSX:SCC], N163 has no
 * physical wave-RAM-sharing constraint between channels, so percussion
 * (channel k=3, physical osc 7) COULD carry an independently different
 * waveform from the melodic voices; this adapter still reuses the
 * current global s_timbre_index for it anyway (same "no independent
 * percussion timbre" convention every other chip adapter here uses,
 * disclosed as a design choice, not a hardware requirement this time).
 *
 * -- Voice topology: harmony_count=2 (k=0/1, physical osc 4/5) +
 * bass_count=1 (k=2, physical osc 6) + percussion (k=3, physical osc 7)
 * -- the base GB/NES/[SMS:PSG] 2+1+percussion shape, since active_oscs=4
 * exactly matches that shape's 4-voice total with none left over.
 *
 * -- No real noise generator (disclosed, same situation as [MSX:SCC]) --
 * N163 is purely wavetable, no noise/LFSR hardware at all. Percussion is
 * differentiated from melodic notes by a coarse 3-way PITCH bucket only
 * (same idiom as [MSX:SCC]'s own equivalent), reusing this chip's own
 * frequency table's extremes/midpoint. No auto-decay (same disclosed
 * simplification every other chip adapter here uses); sustains until
 * note_off. */

#define N163_CLOCK_RATE 1789773u /* shared NES/Famicom clock, see RP2350_player/tools/gen_n163_note_table.py's top comment */
static long s_bend_clock = N163_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9
#define N163_ACTIVE_OSCS 4
#define N163_FIRST_ACTIVE_OSC (Nes_Namco_Apu::osc_count - N163_ACTIVE_OSCS) /* = 4 */
#define N163_WAVE_SIZE 32
#define N163_CHANNEL_COUNT_REG 0x7F /* also osc7's own volume reg, see top comment */

static Nes_Namco_Apu s_apu;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* k=0/1 = harmony (physical osc 4/5), k=2 = designated bass (physical
 * osc 6) -- harmony_count=2, bass_count=1. k=3 (physical osc 7,
 * percussion) is driven directly, outside the pool, same idiom as every
 * other chip adapter here. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected library waveform, %MIDI_WAVE_COUNT */
static int s_channel_program[16];

/* Absolute (non-auto-incrementing) single-register write. */
static void write_reg(int addr, int data)
{
    s_apu.write_addr(addr & 0x7F);
    s_apu.write_data(0, data);
}

static void write_osc7_volume(int vol)
{
    write_reg(N163_CHANNEL_COUNT_REG, ((N163_ACTIVE_OSCS - 1) & 7) << 4 | (vol & 0x0F));
}

static int fold_note(int note)
{
    while (note < MIDI_N163_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_N163_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_N163_NOTE_MIN) {
        note = MIDI_N163_NOTE_MIN;
    }
    if (note > MIDI_N163_NOTE_MAX) {
        note = MIDI_N163_NOTE_MAX;
    }
    return note;
}

static int velocity_to_volume(int velocity)
{
    int v = velocity >> 3;
    if (v == 0) {
        v = 1;
    }
    return v; /* 1..15, direct into the 4-bit volume field */
}

/* Uploads library waveform `timbre` into channel k's private 16-byte
 * region (reg[k*16 .. k*16+15]), auto-incrementing through write_addr's
 * own 0x80 bit -- see this file's top comment's "Wave upload" section. */
static void upload_wave(int k, int timbre)
{
    int idx = timbre % MIDI_WAVE_COUNT;
    s_apu.write_addr(0x80 | (k * 16)); /* 0x80 = auto-increment */
    for (int i = 0; i < N163_WAVE_SIZE / 2; i++) {
        uint8_t lo = midi_wave_library[idx][2 * i] >> 4;
        uint8_t hi = midi_wave_library[idx][2 * i + 1] >> 4;
        s_apu.write_data(0, (hi << 4) | lo);
    }
}

/* k: 0-3 (physical osc N163_FIRST_ACTIVE_OSC+k). */
static void trigger_channel(int k, int note, int velocity, int timbre)
{
    upload_wave(k, timbre);
    int folded = fold_note(note);
    uint32_t freq = midi_n163_freq_reg[folded - MIDI_N163_NOTE_MIN];
    int base = 0x40 + (N163_FIRST_ACTIVE_OSC + k) * 8;
    write_reg(base + 0, freq & 0xFF);
    write_reg(base + 2, (freq >> 8) & 0xFF);
    write_reg(base + 4, 0xE0 | ((freq >> 16) & 0x03)); /* gate bits + wavelen(0=full32) + freq top2, see top comment */
    write_reg(base + 6, k * 32);                       /* wave nibble base */
    int vol = velocity_to_volume(velocity);
    if (k == 3) {
        write_osc7_volume(vol);
    } else {
        write_reg(base + 7, vol & 0x0F);
    }
}

static void silence_channel(int k)
{
    if (k == 3) {
        write_osc7_volume(0);
    } else {
        int base = 0x40 + (N163_FIRST_ACTIVE_OSC + k) * 8;
        write_reg(base + 7, 0x00);
    }
}

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    trigger_channel(slot, note, velocity, timbre);
}

static void silence_slot(int slot)
{
    silence_channel(slot);
}

static void n163_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void n163_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group, e->pulse_duty,
                                e->pulse_duty);
}

static void n163_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void n163_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void n163_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void n163_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> k=3, physical osc 7): no real noise
 * generator, see top comment -- pitch-bucketed only, reusing this
 * chip's own frequency table's extremes/midpoint. No auto-decay;
 * sustains until note_off. */
static void percussion_on(int note, int velocity)
{
    static const int kBucketIndex[3] = {0, MIDI_N163_NOTE_COUNT / 2, MIDI_N163_NOTE_COUNT - 1};
    int bucket;
    if (note < 40) {
        bucket = 0; /* kick: lowest/slowest -> low pitch thump */
    } else if (note < 50) {
        bucket = 1; /* snare: mid */
    } else {
        bucket = 2; /* hihat: highest/fastest -> bright */
    }
    uint32_t freq = midi_n163_freq_reg[kBucketIndex[bucket]];
    int base = 0x40 + (N163_FIRST_ACTIVE_OSC + 3) * 8;

    upload_wave(3, s_timbre_index);
    write_reg(base + 0, freq & 0xFF);
    write_reg(base + 2, (freq >> 8) & 0xFF);
    write_reg(base + 4, 0xE0 | ((freq >> 16) & 0x03));
    write_reg(base + 6, 3 * 32);
    write_osc7_volume(velocity_to_volume(velocity));
}

static void silence_percussion(void)
{
    write_osc7_volume(0);
}

static void n163_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        n163_note_on(channel, note, velocity);
    }
}

static void n163_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void n163_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_percussion();
}

static int n163_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = N163_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_apu.reset();
    for (int i = N163_FIRST_ACTIVE_OSC; i < Nes_Namco_Apu::osc_count; i++) {
        s_apu.osc_output(i, s_buf.center());
    }
    s_apu.treble_eq(blip_eq_t(-1.0));
    s_apu.volume(0.5); /* instrument app: 1.0 clips with all voices (board bench); explicit, see midi_scc_chip.cpp's top comment for why this project no longer assumes a constructor default */

    write_osc7_volume(0); /* also latches the active-channel-count field, see top comment */

    pc_vp_init_n(&s_pool, 2, 1, trigger_slot, silence_slot); /* GB/NES's own 2-harmony+1-bass shape */
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    silence_channel(0);
    silence_channel(1);
    silence_channel(2);
    silence_percussion();
    return 0;
}

static void n163_render(int16_t *stereo_frames, size_t frame_count)
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
            s_buf.end_frame(clocks);
        }
    }
}

#define N163_VOICE_COUNT 4

static int n163_voice_count(void)
{
    return N163_VOICE_COUNT;
}

static const char *n163_voice_name(int i)
{
    static const char *const names[N163_VOICE_COUNT] = {"Channel 1", "Channel 2", "Channel 3", "Noise"};
    if (i < 0 || i >= N163_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Unipolar 0..225 (sample(0-15)*volume(0-15) max, see gme/Nes_Namco_Apu.h's
 * own voice-peek doc comment). i maps directly to physical osc
 * N163_FIRST_ACTIVE_OSC+i. */
static void n163_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= N163_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_apu.osc_last_amp(N163_FIRST_ACTIVE_OSC + i);
    if (amp < 0) {
        amp = 0;
    }
    int scaled = (amp * 255) / 225;
    if (scaled > 255) {
        scaled = 255;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- N163's 2-wide
 * wavetable harmony pool; the third bass voice is a different instrument. */
static int n163_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(N163_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_n163 = {
    .id = "n163",
    .name = "NES N163",
    .reset = n163_reset,
    .note_on = n163_note_on_dispatch,
    .note_off = n163_note_off,
    .program_change = n163_program_change,
    .all_off = n163_all_off,
    .note_on_direct = n163_note_on_direct,
    .note_off_direct = n163_note_off_direct,
    .note_on_gm = n163_note_on_gm,
    .program_change_gm = n163_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = n163_render,
    .voice_count = n163_voice_count,
    .voice_name = n163_voice_name,
    .voice_state = n163_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = n163_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_nes_n163(void)
{
    return &k_chip_n163;
}

extern "C" int midi_n163_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_n163_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
