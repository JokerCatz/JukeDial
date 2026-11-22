// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_nes_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Multi_Buffer.h"
#include "Nes_Apu.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_nes_tables.h"
#include "pc/pc_voicepool.h"

/* The NES (2A03) chip adapter -- second implementation
 * of the chip_synth.h vtable, proving its pluggability (see that header's
 * top comment). Drives a STANDALONE Nes_Apu instance directly, bypassing
 * gme's Nsf_Emu/Classic_Emu file-player machinery entirely, mirroring
 * midi_gb_chip.cpp's own approach for Gb_Apu -- same "the only C++ file
 * for this chip in the MIDI seam" role, same plain-C chip_synth.h
 * boundary to player_backend_midi.c.
 *
 * -- Driving pattern (derived from reading gme/Nsf_Emu.cpp/Nes_Oscs.cpp/
 * Nes_Apu.h/.cpp, same posture as midi_gb_chip.cpp's own
 * derivation) --
 * Nsf_Emu::init_sound() wires a fresh Nes_Apu roughly like this:
 *   - apu.dmc_reader(pcm_read, this) -- a memory-read callback for the
 *     DMC oscillator's sample-fetch DMA. This adapter installs an inert
 *     stub (nes_dmc_stub_reader below) purely for driving-pattern
 *     fidelity/safety -- DMC is never enabled (see "DMC" section below),
 *     so it is never actually called.
 *   - No apu.irq_notifier() call: Nes_Apu::reset() defaults
 *     irq_notifier_ to NULL and irq_changed() no-ops when it's NULL (see
 *     Nes_Apu.cpp), so frame-sequencer IRQs (unused here -- no CPU to
 *     interrupt) need no stub at all, unlike DMC's DMA reader.
 *   - apu.reset(pal_mode, initial_dmc_dac) then
 *     apu.write_register(0, 0x4015, 0x0F) -- enables the 4 channels this
 *     adapter drives (pulse1/pulse2/triangle/noise bits 0-3) while
 *     leaving DMC (bit 4, 0x10) OFF, exactly like Nsf_Emu's own NSF
 *     player init.
 *   - apu.osc_output(i, buf) per oscillator (NOT a single apu.output()
 *     call the way Gb_Apu offers -- Nes_Apu has no such convenience;
 *     each oscillator's real 2A03 hardware output is genuinely mono, one
 *     Blip_Buffer per oscillator, see Nes_Apu.h's osc_output doc
 *     comment). This adapter routes every driven oscillator to the
 *     Stereo_Buffer's CENTER channel (same "every oscillator centered,
 *     no panning feature" choice midi_gb_chip.cpp's NR51
 *     comment documents for GB).
 * render() below reproduces this same buffer-draining SHAPE as
 * midi_gb_chip.cpp's gb_render(): drain the Stereo_Buffer via
 * read_samples(), and on a shortfall advance apu.end_frame()+
 * s_buf.end_frame() by exactly the current shortfall (not a fixed ~50ms
 * chunk) -- see gb_render()'s comment for why. Register writes happen at
 * blip_time 0 for the identical reason midi_gb_chip.cpp's top comment
 * documents (drain_midi_events() dispatches every pending MIDI event
 * before calling render() for that block).
 *
 * -- Voice roles (game-system idiom, mirrors GB's own) --
 * Pulse 1 + Pulse 2 (oscs 0/1): harmony pool (sticky, oldest-steal).
 * Triangle (osc 2): bass voice, press-time-sticky-lowest, SAME
 * midi_voice_pool.h mechanics as GB's wave channel (physical slot 2 in
 * both adapters). Noise (osc 3): channel 10 percussion, 3-way coarse
 * kick/snare/hihat split by note range, same idiom as GB's NR43 split.
 * DMC (osc 4): UNUSED -- never written, its Blip_Buffer
 * output is left NULL (Nes_Apu::osc_output's own doc: a NULL buffer
 * mutes that oscillator), and its status-register enable bit is never
 * set. voice_count() below reports 4, not Nes_Apu::osc_count (5) --
 * DMC is entirely hidden from the Channels page so NES shows the same 4
 * live columns GB does, not a 5th always-silent one.
 *
 * -- Triangle has NO volume control (real 2A03 hardware fact, unlike
 * GB's wave channel which has a 2-bit volume code) --
 * Nes_Triangle::run's amplitude is driven purely by phase (calc_amp()),
 * with no envelope/volume field anywhere in its register layout ($4008's
 * bits 0-6 are a LINEAR COUNTER RELOAD, not a volume). So triangle notes
 * are on/off only -- `velocity` is accepted for interface uniformity
 * (matches every other trigger callback's signature) but otherwise
 * IGNORED for triangle, disclosed here and at trigger_triangle()'s own
 * comment. This is also why triangle sounds an OCTAVE LOWER than a
 * pulse for "the same period register value" -- see
 * RP2350_player/tools/gen_nes_note_table.py's top comment for the exact formula
 * derivation and why this adapter uses two independently-computed period
 * tables (midi_nes_tables.h) rather than sharing GB's "reuse the bass
 * table's numbers" temptation.
 *
 * -- Sustain-until-off registers (mirrors GB's "period=0, sustain,
 * DAC-on" trigger idiom with the NES-native equivalent of each) --
 * Pulse: control byte = duty<<6 | 0x30 | volume -- bit4 (0x10) is
 * CONSTANT VOLUME (skip the hardware envelope, `Nes_Envelope::volume()`
 * returns the raw nibble instead of the decaying `envelope` value); bit5
 * (0x20) is LENGTH-COUNTER HALT (`Nes_Osc::clock_length`'s halt_mask --
 * freezes length_counter, which the "$x003 length-load" write below must
 * still load to a nonzero table entry once so `Nes_Envelope::volume()`'s
 * `length_counter == 0 -> 0` guard doesn't silence it; halted, that
 * value is then never touched again). Silencing writes the SAME control
 * byte with volume forced to 0 -- the standard NES-tracker "note off ==
 * write volume 0" idiom, which fades cleanly via Blip_Synth's own
 * delta-offset machinery exactly like GB's DAC-off write does, without
 * touching length/phase at all (no retrigger click).
 * Triangle: $4008 bit7 doubles as BOTH the length-counter halt bit
 * (`clock_length(0x80)`, a DIFFERENT bit position than the pulses'/
 * noise's 0x20) AND the linear-counter's own hold flag
 * (`Nes_Triangle::clock_linear_counter`: while bit7 is set,
 * `reg_written[3]` never clears, so `linear_counter` is FORCE-RELOADED
 * to bits0-6 every ~1/240s frame tick forever -- indefinite sustain,
 * never actually depleting). Silencing writes $4008 = 0x00: bit7 drops
 * (length counter now free to decay, harmless since the next check
 * below already zeros it), AND -- because `reg_written[3]` is still
 * true from the note-on's $400B write and stays true until the NEXT
 * clock_linear_counter() sees bit7 clear -- the very next frame tick
 * (<=~4.2ms, one NES frame-sequencer period at NTSC) reloads
 * `linear_counter = regs[0] & 0x7F` using the NEW (now zero) value,
 * silencing the triangle within about one frame tick, then finally
 * clears reg_written[3]. Verified by tracing Nes_Triangle::
 * clock_linear_counter()/run() in Nes_Oscs.cpp line by line -- not
 * assumed.
 * Noise (percussion only, one-shot): uses the REAL hardware envelope
 * (bit4=0, non-constant) rather than pulses'/triangle's constant-
 * sustain trick, since percussion is meant to decay -- see
 * nes_percussion_on()'s own comment for why this means NES percussion
 * volume can't take velocity the way GB's does (a genuine, disclosed
 * hardware difference, not an oversight).
 *
 * -- Duty mapping --
 * The 2A03 pulse duty-select field (`regs[0]` bits 6-7, `Nes_Square::
 * run`'s `duty_select`) is numerically IDENTICAL to the GB pulse duty
 * code both chips already use 0-3 for: 0=12.5%, 1=25%, 2=50%,
 * 3=75% (NES's duty_select==3 is a "negated 25%" hardware trick that
 * plays out to a 75% duty cycle -- see Nes_Square::run's `amp = volume`
 * special case for duty_select==3). GB has no true 75% duty at all (its
 * 2-bit NRx1 field only encodes 12.5/25/50/75%... actually GB's field is
 * ALSO 0-3 = 12.5/25/50/75%, so both chips' hardware duty encodings
 * happen to line up exactly). Because of this coincidence, this adapter
 * reuses pc_gm_table.h's `pulse_duty` field VERBATIM as NES's
 * duty_select (`timbre % 4` in trigger_pulse(), same "mod 4" convention
 * midi_gb_chip.cpp's trigger_square() uses) -- no remapping table is
 * needed. This is disclosed as a fortunate hardware coincidence specific
 * to these two chips' pulse duty encodings, not a general guarantee a
 * future third chip's timbre space could rely on. */

#define NES_CLOCK_RATE 1789773u /* NTSC 2A03 -- see gen_nes_note_table.py's top comment */
static long s_bend_clock = NES_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9 /* MIDI channel 10, 0-based -- same value as midi_gb_chip.cpp's */

static Nes_Apu s_apu;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0=pulse1, 1=pulse2, 2=triangle -- see midi_voice_pool.h. Noise (channel
 * 10 percussion) is driven directly, outside the pool, exactly like
 * midi_gb_chip.cpp's noise oscillator. */
static pc_vp_t s_pool;

/* Program-change-selected pulse duty index (mod 4) -- NES's equivalent of
 * midi_gb_chip.cpp's s_timbre_index, minus the wave-pattern half (NES's
 * triangle has no comparable timbre dimension, see this file's top
 * comment). */
static int s_timbre_index;

/* MIDI_MAP_GM (reused verbatim -- see midi_gb_chip.cpp's
 * own s_channel_program comment). */
static int s_channel_program[16];

static void write_reg(unsigned addr, int data)
{
    s_apu.write_register(0, (nes_addr_t)addr, data);
}

/* MIDI note -> playable-range fold, identical scheme to midi_gb_chip.cpp's
 * fold_note() (+/-12 semitones until inside [MIDI_NES_NOTE_MIN,
 * MIDI_NES_NOTE_MAX]) -- see gen_nes_note_table.py's top comment for why
 * this always terminates inside the band exactly (96-36=60 is an exact
 * multiple of 12, same as GB's range). */
static int fold_note(int note)
{
    while (note < MIDI_NES_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_NES_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_NES_NOTE_MIN) {
        note = MIDI_NES_NOTE_MIN; /* defensive; never actually engages */
    }
    if (note > MIDI_NES_NOTE_MAX) {
        note = MIDI_NES_NOTE_MAX;
    }
    return note;
}

/* Same "floor at 1" reasoning as midi_gb_chip.cpp's velocity_to_env_
 * volume() -- NES has no DAC-off distinction to protect here (constant-
 * volume 0 is simply silent, not a special disabled state), but flooring
 * at 1 keeps a very-low-velocity (1-7) note-on from landing on an
 * inaudible volume 0, matching GB's own audible-note-on guarantee. */
static int velocity_to_volume(int velocity)
{
    int v = velocity >> 3; /* 0-127 -> 0-15 */
    if (v == 0) {
        v = 1;
    }
    return v;
}

static int nes_dmc_stub_reader(void *user_data, nes_addr_t addr)
{
    (void)user_data;
    (void)addr;
    return 0; /* never actually called -- DMC's enable bit is never set, see this file's top comment */
}

static void silence_pulse(int slot)
{
    unsigned base = 0x4000u + (unsigned)slot * 4u;
    write_reg(base + 0, 0x30); /* halt+constant, volume=0 -- see top comment's "sustain-until-off" section */
}

static void silence_triangle(void)
{
    write_reg(0x4008u, 0x00); /* control flag off, reload=0 -- see top comment's derivation */
}

static void silence_noise(void)
{
    write_reg(0x400Cu, 0x30); /* same idiom as silence_pulse: halt+constant, volume=0 */
}

static void trigger_pulse(int slot, int note, int velocity, int timbre)
{
    unsigned base = 0x4000u + (unsigned)slot * 4u;
    int folded = fold_note(note);
    int period = midi_nes_pulse_period[folded - MIDI_NES_NOTE_MIN];
    int duty = timbre % 4; /* 0=12.5%, 1=25%, 2=50%, 3=75% -- see top comment's duty-mapping section */
    int vol = velocity_to_volume(velocity);

    write_reg(base + 0, (duty << 6) | 0x30 | vol); /* control: duty, halt=1, constant=1, volume */
    write_reg(base + 2, period & 0xFF);             /* timer low */
    write_reg(base + 3, (period >> 8) & 0x07);       /* timer high (bits0-2) + length load idx 0 (top5=0)
                                                       * -- trigger: reloads length_counter, resets phase */
}

/* velocity is intentionally unused -- triangle has no volume control, see
 * this file's top comment. */
static void trigger_triangle(int note, int velocity, int timbre)
{
    (void)velocity;
    (void)timbre; /* no wave-pattern-equivalent timbre dimension for triangle */
    int folded = fold_note(note);
    int period = midi_nes_triangle_period[folded - MIDI_NES_NOTE_MIN];

    write_reg(0x4008u, 0x80 | 0x7Fu);    /* control flag=1 (indefinite linear-counter reload), reload=127 */
    write_reg(0x400Au, period & 0xFF);   /* timer low */
    write_reg(0x400Bu, (period >> 8) & 0x07); /* timer high + length load idx 0 -- trigger */
}

/* pc_vp_trigger_fn/pc_vp_silence_fn callbacks. */
static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    if (slot == 2) {
        trigger_triangle(note, velocity, timbre);
    } else {
        trigger_pulse(slot, note, velocity, timbre);
    }
}

static void silence_slot(int slot)
{
    if (slot == 2) {
        silence_triangle();
    } else {
        silence_pulse(slot);
    }
}

static void nes_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* percussion handled separately, see nes_percussion_on below */
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

/* Same precedence as midi_gb_chip.cpp's gb_note_on_gm() -- see
 * pc_vp_note_on_gm()'s comment. bass_timbre is passed as 0 and
 * ignored by trigger_triangle() (no timbre dimension for triangle, see
 * this file's top comment); harmony_timbre reuses the GM table's
 * pulse_duty field verbatim (see the duty-mapping section above). */
static void nes_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, 0 /* bass_timbre unused -- triangle has no timbre */);
}

static void nes_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void nes_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* percussion is a one-shot decay, see nes_percussion_on */
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void nes_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void nes_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* 3 preset "drums" by note range, same coarse split/thresholds as
 * midi_gb_chip.cpp's gb_percussion_on() (NOT the General MIDI drum map,
 * disclosed there and here). noise_period_table (Nes_Oscs.cpp, NTSC) is
 * INDEXED 0..15, LOW index = SHORT period = HIGH pitch, HIGH index =
 * LONG period = LOW pitch -- opposite of GB's NR43 shift-amount framing
 * but same intent. `mode` (bit7 of $400E, the noise LFSR "mode flag")
 * picks a 93-bit-ish short/metallic tap (1) vs. the full 32767-bit tap
 * (0, the "normal", fuller/less-metallic tap) -- see Nes_Noise::run's
 * `tap = mode_flag ? 8 : 13`. Values below are hand-picked for an
 * audible low/mid/high split, not tuned for acoustic realism, same
 * disclosed posture as GB's NR43 choices.
 *
 * Unlike GB's percussion (which sets the ENVELOPE'S INITIAL VOLUME
 * nibble directly from velocity, decaying from there), this uses the
 * NES's REAL non-constant-volume envelope mode ($400C bit4=0): on real
 * 2A03 hardware that mode's low nibble is the envelope's DECAY PERIOD,
 * not an initial-volume field at all -- `Nes_Envelope::clock_envelope()`
 * always resets `envelope` to 15 (max) on trigger and decays it toward 0
 * at a rate set by that nibble. There is no register field anywhere on
 * this chip that lets a note-on set an initial volume for a decaying
 * envelope; GB's Gb_Env genuinely has an extra field NES's Nes_Envelope
 * does not. So `velocity` is intentionally UNUSED here -- every NES drum
 * hit starts at the same full envelope volume and decays at the same
 * fixed rate (NES_PERC_ENV_PERIOD), a disclosed hardware-driven
 * simplification (not a missed opportunity to apply velocity, as it
 * would be for GB).
 *
 * A second, separate hardware-driven latency: using the real envelope
 * (vs. every other trigger in this file, which uses constant-volume) means
 * the actual AUDIBLE onset is gated by the frame sequencer --
 * `Nes_Envelope::clock_envelope()`'s `envelope=15` reset only applies on
 * a LATER frame-sequencer tick, not synchronously at this function's
 * register writes the way every constant-volume trigger's volume is.
 * Empirically measured (a standalone Nes_Apu repro, not just derived on
 * paper) at up to ~15-20ms from a freshly-
 * reset APU -- longer than one ~4.2ms frame period because
 * Nes_Apu::reset()'s own $4017 write (mode-0 frame counter, which this
 * adapter's nes_reset() also performs) resets frame_delay to a nearly
 * full frame_period, delaying the FIRST tick after reset specifically.
 * This is on top of the ordinary render()-call-boundary quantization
 * this whole backend already accepts (see midi_gb_chip.cpp's "Register
 * writes and clock time" comment) -- small (well under the ~50ms a
 * human perceives as "late"), real, and disclosed, not something this
 * adapter tries to paper over. See platforms/sim/service_test_main.c's
 * NES percussion leg for the measurement this comment is based on. */
#define NES_PERC_ENV_PERIOD 2 /* quick decay, same idiom as GB's NR42 period=2 choice */
static void nes_percussion_on(int note, int velocity)
{
    (void)velocity; /* see this function's own comment */
    int period_index;
    int mode;
    if (note < 40) {
        period_index = 13; /* kick: long period (low pitch), mode 0 (fuller/less metallic) */
        mode = 0;
    } else if (note < 50) {
        period_index = 6; /* snare: mid period, mode 1 (buzzier/metallic) */
        mode = 1;
    } else {
        period_index = 1; /* hihat: short period (high pitch), mode 1 (bright) */
        mode = 1;
    }

    write_reg(0x400Cu, NES_PERC_ENV_PERIOD); /* envelope mode (bit4=0), no loop (bit5=0) -> decays and stays silent */
    write_reg(0x400Eu, (mode << 7) | period_index);
    write_reg(0x400Fu, 0x00); /* length load idx 0 -- trigger: reg_written[3] resets envelope to 15 next frame tick */
}

static void nes_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        nes_percussion_on(note, velocity);
    } else {
        nes_note_on(channel, note, velocity);
    }
}

static void nes_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* no selectable drum kits, same as GB */
    }
    s_timbre_index = program;
}

static void nes_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_noise();
}

static int nes_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* Multi_Buffer capacity mirrors midi_gb_chip.cpp's own choice (just a
     * max-size allocation, not how much nes_render() below actually
     * generates per call -- see that function's comment). */
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = NES_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120); /* same choice as GB's gb_reset(); no NES-specific retuning */
    s_buf.clear();

    s_apu.reset(false /* NTSC, not PAL */, 0);
    s_apu.dmc_reader(nes_dmc_stub_reader, NULL); /* inert -- DMC never enabled, see top comment */
    for (int i = 0; i < 4; i++) {
        s_apu.osc_output(i, s_buf.center()); /* pulse1, pulse2, triangle, noise -- every oscillator
                                               * centered, no panning feature (matches GB) */
    }
    /* osc 4 (DMC) output left unset (NULL) -- muted, unused. */
    s_apu.treble_eq(blip_eq_t(-1.0)); /* matches GB's own choice */

    write_reg(0x4015u, 0x0F); /* enable pulse1/pulse2/triangle/noise; DMC (bit4) left off */
    write_reg(0x4017u, 0x00); /* frame counter, 4-step mode (default); IRQ unused, no notifier installed */
    write_reg(0x4001u, 0x00); /* pulse1 sweep: disabled (bit7=0), written once, never touched again */
    write_reg(0x4005u, 0x00); /* pulse2 sweep: disabled */

    pc_vp_init(&s_pool, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program); /* GM default program 0 per channel */
    silence_noise();
    return 0;
}

/* Same buffer-draining shape as midi_gb_chip.cpp's gb_render() -- see
 * this file's top comment's "Driving pattern" section and that
 * function's own comment for the full "why sized to the current
 * shortfall, not a fixed chunk" rationale (identical reasoning, just
 * NES_CLOCK_RATE in place of GB_CLOCK_RATE). */
static void nes_render(int16_t *stereo_frames, size_t frame_count)
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

/* 4, not Nes_Apu::osc_count (5) -- DMC is hidden from voice_count/name/
 * state entirely, see this file's top comment. */
#define NES_VOICE_COUNT 4

static int nes_voice_count(void)
{
    return NES_VOICE_COUNT;
}

static const char *nes_voice_name(int i)
{
    static const char *const names[NES_VOICE_COUNT] = {"Pulse 1", "Pulse 2", "Triangle", "Noise"};
    if (i < 0 || i >= NES_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Unlike GB's oscillators (whose last_amp is signed for the square/noise
 * pair, needing abs()), every NES oscillator's last_amp is already
 * non-negative: pulses' amp is either 0 or `volume` (Nes_Square::run),
 * triangle's calc_amp() is confirmed in [0, 15] by tracing its phase
 * math (Nes_Oscs.cpp), and noise's amp is either 0 or `volume` --
 * matching Nes_Apu.h's own osc_last_amp doc comment ("osc 0/1 volume
 * range 0-15, osc 2 triangle 0-15, osc 3 noise volume range 0-15"). So
 * no sign-handling branch is needed here, unlike
 * player_backend_gme.cpp's/midi_gb_chip.cpp's GB voice-state code. */
static void nes_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= NES_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_apu.osc_last_amp(i);
    int scaled = (amp * 255) / 15;
    if (scaled > 255) {
        scaled = 255;
    }
    if (scaled < 0) {
        scaled = 0;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- NES's 2-wide pulse
 * harmony pool; the triangle bass voice is a different instrument. */
static int nes_harmony_count(void)
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

static const pc_synth_t k_chip_nes = {
    .id = "nes",
    .name = "NES 2A03",
    .reset = nes_reset,
    .note_on = nes_note_on_dispatch,
    .note_off = nes_note_off,
    .program_change = nes_program_change,
    .all_off = nes_all_off,
    .note_on_direct = nes_note_on_direct,
    .note_off_direct = nes_note_off_direct,
    .note_on_gm = nes_note_on_gm,
    .program_change_gm = nes_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = nes_render,
    .voice_count = nes_voice_count,
    .voice_name = nes_voice_name,
    .voice_state = nes_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = nes_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_nes(void)
{
    return &k_chip_nes;
}

/* Test-only accessors -- same rationale/shape as midi_gb_chip.cpp's
 * midi_gb_debug_voice_note()/midi_gb_debug_voice_timbre(), just against
 * this file's own s_pool (0=pulse1, 1=pulse2, 2=triangle). */
extern "C" int midi_nes_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_nes_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
