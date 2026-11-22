// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_fme7_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Ay_Apu.h"
#include "Multi_Buffer.h"

#include "inst_midi_5b_tables.h"
#include "inst_midi_ay_family.h"
#include "pc/pc_gm_table.h"
#include "pc/pc_voicepool.h"

/* An earlier version of the [NES:5B] adapter was built on gme's
 * Nes_Fme7_Apu; that core turned out to emulate NEITHER the envelope
 * generator NOR the noise generator (`// noise and envelope aren't
 * supported`, confirmed by reading Nes_Fme7_Apu.cpp's run_until() line by
 * line), so the buzz program was PROVABLY SILENT and ch10 percussion was
 * a plain tone hit -- known, documented limitations of that emulator
 * core, not bugs.
 *
 * This adapter now drives gme's OTHER AY-3-8910-family core instead --
 * Ay_Apu (the same class player_backend_gme.cpp already drives for ZX
 * Spectrum .ay file playback, PLAYER_CHIP_AY) -- which DOES implement
 * both, because our gme patches must never diverge on AUDIO (that keeps
 * upgrades cheap and the render-hash canary meaningful forever), so
 * hand-patching envelope/noise emulation into Nes_Fme7_Apu was never the
 * right move. The Sunsoft 5B mapper's sound section is itself a
 * YM2149/AY-3-8910-compatible 3-tone PSG (same silicon family Ay_Apu
 * emulates), so this is not a downgrade in authenticity, it is the MORE
 * faithful choice once a real envelope/noise generator is on the table --
 * and it needs zero gme patches of its own (Ay_Apu is used completely
 * unmodified; the only gme patch touching this chip family, 0004,
 * touches Ay_Emu.cpp, gme's ZX FILE PLAYER, not Ay_Apu itself, and is an
 * upstream-aligned bounds fix, not a divergence -- see
 * components/gme/README.md). File PLAYBACK is untouched: NSF files
 * still play through gme's own Nsf_Emu/Nes_Fme7_Apu path in
 * player_backend_gme.cpp, unaffected by anything in this file.
 *
 * -- Register map (standard AY/YM2149 -- gme/Ay_Apu.cpp's own
 * write_data_()/run_until(), reg_count=16, only 0-13 used here) --
 *   R0/R1, R2/R3, R4/R5 : channel A/B/C tone period, fine/coarse
 *                         (12-bit: R_odd bits0-3 << 8 | R_even). IDENTICAL
 *                         formula to Nes_Fme7_Apu's (both use
 *                         period_factor=16 -- verified by reading both
 *                         run_until()s side by side), so
 *                         midi_5b_tables.h's generated tone-period table
 *                         carries over UNCHANGED, no regeneration needed.
 *   R6                  : noise period (5 bits, 0-31) -- NOW USED, see
 *                         "Percussion" below (Nes_Fme7_Apu never read this
 *                         register at all).
 *   R7                  : mixer (tone/noise enable per channel, active-
 *                         LOW: 1=disabled; bits0-2=tone A/B/C,
 *                         bits3-5=noise A/B/C). UNLIKE the old adapter,
 *                         this is now WRITTEN DYNAMICALLY (see
 *                         s_mixer/AY_MIXER_* below), not set once at
 *                         reset and forgotten -- see "Buzz" and
 *                         "Percussion" below for why.
 *   R8/R9/R10           : channel A/B/C volume -- bits0-3 = volume
 *                         (0-15, through Ay_Apu.cpp's logarithmic
 *                         amp_table, NOT linear); bit4 = envelope-enable.
 *   R11/R12             : envelope period, fine/coarse (16-bit, ONE
 *                         shared envelope generator for all 3 channels).
 *   R13                 : envelope shape (4-bit).
 *
 * -- Buzz: MEASURED, not argued --
 * Ay_Apu.cpp's run_until() is dense, cycle-accurate C++, not prose --
 * hand-parsing its envelope-shape bit-twiddling (env.modes[][48], built
 * from an 8-entry MODE(a0,a1,b0,b1,c0,c1) table) to predict the buzz's
 * perceived pitch from first principles is exactly the kind of "looks
 * right, is subtly wrong" trap a datasheet-derived divisor constant could
 * fall into -- a wrong value that could not be caught by ear on a core
 * (Nes_Fme7_Apu) that never rendered the envelope buzz at all. So rather
 * than deriving a formula from source-reading alone, the
 * actual constant below was found by building a standalone measurement
 * harness (scratch tool, not committed -- links gme's own unmodified
 * Ay_Apu.cpp+Blip_Buffer.cpp directly, no project CMake involved) that:
 *   1. Renders a channel with tone DISABLED (mixer tone-off bit set) and
 *      envelope-enabled (R8 bit4=1, shape R13=0x0E, the CONT+ATT+ALT
 *      "classic repeating buzz-bass ramp" shape) -- i.e. the amplitude
 *      staircase IS the waveform (the well-known AY "envelope DAC"
 *      buzz-bass technique -- see "why tone is now disabled during buzz"
 *      below for why this adapter adopts it).
 *   2. Sweeps the 16-bit envelope-period register (R11/R12) from 2 to
 *      256 and measures the RENDERED PCM's fundamental two independent
 *      ways -- zero-crossing count and windowed autocorrelation -- over a
 *      500ms capture (bass_freq(120) AC-coupled, matching this adapter's
 *      own reset()).
 *   3. Both methods agree, and `clock_ticks_per_cycle / EP_reg` converges
 *      TIGHTLY to 1024.0-1024.2 across the entire swept range (e.g.
 *      EP=64: 1024.123; EP=256: 1024.186) -- i.e. one full envelope
 *      buzz cycle measures out to 1024 * EP_reg clock ticks, REGARDLESS
 *      of EP_reg's value (ruling out an off-by-a-shape-detail error,
 *      since a wrong constant would show a value that DRIFTS with EP_reg,
 *      not converges).
 *   4. Cross-checked against the actual production formula/note range:
 *      rendering MIDI 36/60/84 through the derived formula (below) and
 *      re-measuring confirms the buzz tracks the target note within a
 *      few percent at MIDI 36/60 (EP_reg in the double digits) and with
 *      growing quantization error at MIDI 84 (EP_reg=2, a genuinely
 *      coarse single-digit register at that point, at the high end of
 *      this project's playable band; an expected consequence of the
 *      register's own resolution, not a formula bug).
 * Measured formula (1024, not the datasheet's /256/EP/32-states chain --
 * that reasoning path is exactly the "looks right, is subtly wrong" trap
 * described above for this core/shape combination):
 *     EP_reg = round(clock / (1024 * note_freq_hz))
 * (clamped to [1, 0xFFFF] -- R11/R12's full 16-bit range; real hardware
 * treats a programmed EP of 0 as acting like 1, matching Ay_Apu.cpp's own
 * `if (!env_period) env_period = env_period_factor;` fallback, so this
 * adapter never needs to special-case writing a literal 0 either way).
 * AY_ENVELOPE_DIVISOR below is this measured 1024.0 -- the measurement is
 * the source of truth here, the (very close, and understood to be RIGHT
 * for the right reason --
 * Ay_Apu.cpp's env_period_factor=32, times another 32 states per full
 * ALTERNATE-shape cycle = 1024) structural match to that chain is
 * corroborating, not load-bearing.
 *
 * -- Why tone is now DISABLED during buzz (the AY core "forces a
 * change") --
 * The earlier Nes_Fme7_Apu-based adapter left the tone-period register
 * ALSO written and the tone mixer bit ALSO enabled during buzz -- harmless
 * when Nes_Fme7_Apu silenced everything regardless, but on Ay_Apu that
 * would mix a plain
 * tone-frequency square wave together with the (now genuinely audible)
 * envelope-driven amplitude modulation, an unintentionally muddy hybrid
 * neither this adapter's original author nor real AY trackers ever
 * actually reach for. This adapter now clears the buzzing channel's
 * mixer tone-enable bit for the DURATION of the buzz note (see
 * trigger_channel()/s_mixer below) -- the real, well-documented AY
 * "envelope bass/buzz" technique: mute the tone generator entirely and
 * let the envelope generator's own amplitude staircase (run at an
 * audio-rate period, not a slow ADSR-style fade) BE the waveform. This is
 * the one place this adapter's TOPOLOGY differs from the earlier
 * Nes_Fme7_Apu-based version's: not the voice COUNT/allocation (still
 * 2-wide harmony pool, channel C for percussion, unchanged), but which
 * mixer bits a given note ends up setting.
 *
 * -- Percussion: ch10 now genuinely routes to the AY noise generator --
 * Channel C's mixer bits are fixed at reset (tone OFF, noise ON --
 * AY_MIXER_TONE_C | into the disabled side, never touched again per note,
 * same "set once" idiom used for the old all-tone mixer value) since
 * channel C is NEVER used melodically (no bass slot exists, see
 * "Voice topology" below) -- only for percussion. percussion_on() now
 * writes R6 (noise period, 5 bits) instead of a tone-period register,
 * picked from a coarse 3-way hit-type split (same idiom as the old
 * tone-based version and as every other chip adapter here): kick=28
 * (near the slow/dull end of the 5-bit range), snare=12 (mid), hihat=1
 * (near the fast/bright end) -- disclosed as arbitrary-but-musically-
 * ordered bucket choices (there is only one shared noise generator, no
 * "correct" hardware value to target the way tone pitch has one), not
 * independently measured the way the buzz's envelope-period CONSTANT
 * was (noise doesn't have a "target frequency" to fit against -- it's
 * pseudo-random by design). No envelope-driven decay is applied to
 * percussion (constant volume, sustains until note_off, same disclosed
 * simplification used everywhere else in this adapter) -- percussion is
 * kept to just "noise period + mixer routing".
 *
 * -- Voice topology (3 tone oscillators, unless the AY core forces a
 * change) --
 * UNCHANGED from the earlier Nes_Fme7_Apu-based version: 2-wide harmony
 * pool (channels A/B, harmony_count=2, bass_count=0 -- "GB-without-wave",
 * no AY channel is
 * timbrally distinct enough to be a natural bass voice, all 3 are the
 * identical duty-less tone-generator circuit) + channel C dedicated to
 * ch10 percussion, matching FME-7's 3 physical channels 1:1 with no
 * shared/borrowed voice. The AY core did not force a topology/voice-COUNT
 * change -- only the mixer-bit and register-target changes described
 * above (buzz: dynamic tone-disable; percussion: noise instead of tone).
 *
 * -- No duty dimension (unlike every other chip here) --
 * Real AY/YM2149 tone generators have no duty-cycle register at all --
 * always a fixed ~50% square (Ay_Apu.cpp's run_until() amplitude only
 * ever toggles between 0 and a single `volume` value). So
 * `s_timbre_index`/program-change selects only between the normal tone
 * program (0) and the buzz program (1) -- `timbre % 2`, not the `%4`
 * duty convention every other chip adapter here uses.
 *
 * -- Debug register peek: SHADOW CACHE, not save_state() --
 * Unlike Nes_Fme7_Apu (which exposed a public save_state()/
 * fme7_apu_state_t with a raw regs[] array), Ay_Apu keeps its `byte regs
 * [reg_count]` private with no equivalent public accessor. Rather than
 * adding yet another gme patch just to expose it (which the "patches
 * stay read-only/observability-only" posture would actually tolerate,
 * but isn't needed here), write_reg() below mirrors every register write
 * into a small file-local shadow array; midi_5b_debug_reg() reads that
 * instead. This is exactly as trustworthy as save_state() was for the
 * old adapter (both just report "what did this file last WRITE to that
 * register", not anything Ay_Apu computed internally), and needs no gme
 * patch. NOTE: the buzz program's ctest coverage combines a
 * register-peek-only assertion (the ONLY option when the buzz is
 * provably silent) with a real rendered-PCM fundamental-frequency pin
 * (the buzz genuinely sounds, see service_test_main.c's suite_midi) --
 * a register peek alone cannot catch a wrong envelope-period CONSTANT
 * the way measuring the actual output can. */

#define NES_CLOCK_RATE 1789773u /* shared 2A03/FME-7 clock, see midi_5b_tables.h's generator comment -- Ay_Apu is clock-rate-agnostic, so reusing the same physical chip clock is still correct here */
static long s_bend_clock = NES_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9

/* The register-write mechanics below (mixer bits, the measured
 * envelope-buzz formula, trigger/silence/percussion) moved to
 * midi_ay_family.h/.cpp, shared with midi_ay_chip.cpp ([ZX:AY]) -- see
 * this file's top comment's "Register map"/"Buzz"/"Percussion" sections
 * for the derivations, still documented here since they were WRITTEN
 * against this file's own investigation; midi_ay_family.h just points
 * back at them rather than repeating them. */

static Ay_Apu s_ay;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;
static uint8_t s_shadow_regs[Ay_Apu::reg_count]; /* see top comment's "Debug register peek" section */
static ay_family_ctx_t s_ay_ctx;

/* 0/1 = channel A/B, harmony pool (harmony_count=2, bass_count=0 -- see
 * this file's top comment). Channel C (percussion) is driven directly,
 * outside the pool, same idiom as every other chip adapter here. */
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

static void fme7_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void fme7_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    /* No bass slot exists (bass_count=0) -- PC_GM_BASS/EITHER
     * both degrade to the harmony pool automatically (see
     * pc_vp_note_on_gm()'s comment), so the group value only
     * ever matters here in that it's passed through unchanged; the
     * `pulse_duty` field is reused verbatim as the tone/buzz timbre
     * (mod 2 applied in trigger_channel above). */
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, e->pulse_duty);
}

static void fme7_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void fme7_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void fme7_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void fme7_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> tone channel C, routed to the NOISE
 * generator, not tone -- see top comment's "Percussion" section). Channel
 * C's mixer bits (tone off, noise on) are fixed at reset and never
 * touched again here. No auto-decay (see top comment); sustains until
 * note_off -- out of this task's scope. */
static void fme7_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        ay_family_percussion_on(&s_ay_ctx, note, velocity);
    } else {
        fme7_note_on(channel, note, velocity);
    }
}

static void fme7_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void fme7_all_off(void)
{
    pc_vp_all_off(&s_pool);
    ay_family_silence_percussion(&s_ay_ctx);
}

static int fme7_reset(uint32_t sample_rate_hz)
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

    s_ay.reset();
    s_ay.output(s_buf.center());
    s_ay.treble_eq(blip_eq_t(-1.0));

    memset(s_shadow_regs, 0, sizeof s_shadow_regs);
    s_ay_ctx.apu = &s_ay;
    s_ay_ctx.shadow_regs = s_shadow_regs;
    s_ay_ctx.clock_hz = NES_CLOCK_RATE;
    s_ay_ctx.tone_period = midi_5b_tone_period;
    s_ay_ctx.note_min = MIDI_5B_NOTE_MIN;
    s_ay_ctx.note_max = MIDI_5B_NOTE_MAX;
    ay_family_reset_mixer(&s_ay_ctx); /* see midi_ay_family.h's own doc comment */

    pc_vp_init_n(&s_pool, 2, 0, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    ay_family_silence_percussion(&s_ay_ctx);
    return 0;
}

static void fme7_render(int16_t *stereo_frames, size_t frame_count)
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

#define FME7_VOICE_COUNT 3

static int fme7_voice_count(void)
{
    return FME7_VOICE_COUNT;
}

static const char *fme7_voice_name(int i)
{
    static const char *const names[FME7_VOICE_COUNT] = {"Channel A", "Channel B", "Channel C"};
    if (i < 0 || i >= FME7_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Unipolar 0..255 (Ay_Apu::amp_range -- already matches this project's
 * 0..255 voice-level convention exactly, no rescale needed, unlike
 * Nes_Fme7_Apu's 0..192 amp_range which the old version of this function
 * had to rescale). See gme/Ay_Apu.h's own osc_last_amp doc comment (patch
 * 0001, no further patching needed here). */
static void fme7_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= FME7_VOICE_COUNT) {
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
 * pool, no bass slot (both PSG channels are the identical circuit). */
static int fme7_harmony_count(void)
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

static const pc_synth_t k_chip_fme7 = {
    .id = "5b",
    .name = "NES 5B",
    .reset = fme7_reset,
    .note_on = fme7_note_on_dispatch,
    .note_off = fme7_note_off,
    .program_change = fme7_program_change,
    .all_off = fme7_all_off,
    .note_on_direct = fme7_note_on_direct,
    .note_off_direct = fme7_note_off_direct,
    .note_on_gm = fme7_note_on_gm,
    .program_change_gm = fme7_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = fme7_render,
    .voice_count = fme7_voice_count,
    .voice_name = fme7_voice_name,
    .voice_state = fme7_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = fme7_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_nes_5b(void)
{
    return &k_chip_fme7;
}

extern "C" int midi_5b_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_5b_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}

/* Test-only register peek (see top comment's "Debug register peek"
 * section): reads the shadow cache
 * write_reg() mirrors every write into, since Ay_Apu (unlike
 * Nes_Fme7_Apu) exposes no public save_state()/regs accessor. reg 0-13,
 * matches the AY/YM2149 map documented in this file's top comment. */
extern "C" int midi_5b_debug_reg(int reg)
{
    if (reg < 0 || (unsigned)reg >= Ay_Apu::reg_count) {
        return -1;
    }
    return s_shadow_regs[reg];
}
