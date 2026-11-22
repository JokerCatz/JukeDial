// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_pokey_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Multi_Buffer.h"
#include "Sap_Apu.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_pokey_tables.h"
#include "pc/pc_voicepool.h"

/* The [Atari:POKEY] adapter -- drives a
 * standalone Sap_Apu directly (the same class the regular .sap file
 * player uses, PLAYER_CHIP_SAP -- reused verbatim for this MIDI chip's
 * family, not a new enum value, since it genuinely is the same core).
 * Same driving pattern/blip-time-0 discipline as every other adapter
 * here (see midi_nes_chip.cpp's top comment for the full derivation this
 * file doesn't repeat) -- this comment covers what's specific to POKEY.
 *
 * -- Voice topology: the 8-bit-vs-16-bit tradeoff -- see
 * RP2350_player/tools/gen_pokey_note_table.py's top comment for the full derivation
 * (read from gme/Sap_Apu.cpp's calc_periods(), not assumed). Summary:
 * POKEY's 4 physical oscillators are pairable into 16-bit-precise
 * channels two at a time (AUDCTL bits), at the cost of the "slave" of
 * each pair becoming permanently silent (its own register still supplies
 * the paired voice's low byte on every audio block -- retriggering it
 * for anything else would instantly mispitch the paired voice, verified
 * by tracing calc_periods()). This adapter spends exactly ONE such pair:
 *   - oscillator index 1 ("Lead", harmony slot 0): 16-bit-paired,
 *     pitch-accurate across the FULL 36-96 band (midi_pokey_lead_period[]).
 *   - oscillator index 0 (the pair's silent slave): NEVER independently
 *     triggered, output routed to NULL (same "hidden, never rendered"
 *     idiom midi_nes_chip.cpp uses for DMC) -- only ever written to
 *     supply the Lead voice's low byte.
 *   - oscillator index 2 ("Support", harmony slot 1): left UNPAIRED, a
 *     plain 8-bit channel -- a genuinely usable second melodic voice,
 *     just coarser and with a physically narrower playable band (MIDI
 *     60-96 only, midi_pokey_support_period[] -- an 8-bit register at
 *     this divider cannot represent anything below ~247 Hz at all, a
 *     real hardware ceiling, not folded around the way every other
 *     table here folds a chip's full band).
 *   - oscillator index 3 ("Noise", channel-10 percussion): left UNPAIRED
 *     too, dedicated to percussion (every other chip adapter here keeps
 *     one; pitch precision doesn't matter for drums, so spending the 4th
 *     oscillator on a 2nd accurate pair instead was rejected -- see the
 *     generator's own comment for the "why not 2 accurate pairs, 0
 *     spare" alternative that was considered).
 * harmony_count=2 (Lead, Support), bass_count=0 -- unlike every other
 * chip adapter here, POKEY's oscillators are ALL the identical circuit
 * (no wave/triangle-style timbral asymmetry to justify a dedicated bass
 * slot, matching [NES:5B]'s own "no natural bass voice" reasoning); a
 * symmetric 2-wide harmony-only pool (via midi_voice_pool.h's generalized
 * N-wide-pool support) is the honest choice.
 *
 * -- No true duty control; distortion-poly modes as program-selectable
 * timbre -- studied from Sap_Apu.cpp's run_until() (the AUDCx
 * register's upper 3 bits, 0x20/0x40/0x80, select between POKEY's
 * several noise/poly generators; the LOWER 4 bits are volume, 0-15,
 * doubled to 0-30 by the emulator). Distinct AUDIBLE combinations,
 * traced through run_until()'s wave-selection logic bit by bit (not
 * assumed from POKEY documentation prose):
 *   0x00 -- 17-bit poly AND 5-bit poly gating: the dirtiest/noisiest tone.
 *   0x20 -- 5-bit poly gating only (bit5 routes to a pure square wave
 *           instead of the 17/9-bit poly, see the `!(osc_control&0x20)`
 *           branch): a mildly gritty tone.
 *   0x40 -- 4-bit poly AND 5-bit poly gating: short/buzzy, percussion-like.
 *   0x80 -- 17-bit poly, NO 5-bit gating (bit7 disables the poly5 AND):
 *           smoother continuous noise.
 *   0xA0 -- pure square, NO 5-bit gating (bit5 AND bit7 both set): a
 *           completely clean tone -- POKEY's "pure tone" distortion,
 *           the most melodic-sounding setting and this adapter's DEFAULT
 *           (program 0).
 *   0xC0 -- 4-bit poly only, no 5-bit gating: short buzzy noise, no gate.
 * `k_pokey_distortion[6] = {0xA0, 0x20, 0x00, 0x40, 0x80, 0xC0}`, indexed
 * by `timbre % 6` (program-change selected, shared across both melodic
 * voices like every other chip adapter's single timbre index here) --
 * ordered from cleanest to dirtiest, program 0 = pure tone.
 *
 * -- Percussion has no auto-decay (mirrors [NES:5B]'s own situation) --
 * POKEY's AUDCx volume field is a flat, non-
 * decaying 4-bit value (confirmed by run_until(): volume is used
 * directly, no envelope/decay state machine anywhere in this file) --
 * real POKEY hardware itself has no per-voice envelope generator at all
 * (unlike GB/NES's real hardware envelopes, or AY/YM2149's decay-capable
 * one), so this is a genuine chip characteristic, not a gme emulation
 * gap. A percussion hit sustains until note_off. */

#define POKEY_CLOCK_RATE 1773447u /* Sap_Emu.cpp's own setup_buffer() value, see the generator's top comment */
static long s_bend_clock = POKEY_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9

static Sap_Apu s_apu;
static Sap_Apu_Impl s_impl;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0 = Lead (osc index1, 16-bit paired), 1 = Support (osc index2,
 * unpaired 8-bit) -- harmony_count=2, bass_count=0, see top comment. */
static pc_vp_t s_pool;

static int s_timbre_index; /* program-change-selected distortion index, timbre%6 */
static int s_channel_program[16];

static const uint8_t k_pokey_distortion[6] = {0xA0, 0x20, 0x00, 0x40, 0x80, 0xC0}; /* see top comment */

static void write_reg(unsigned addr, int data)
{
    s_apu.write_data(0, addr, data);
}

static int fold_note_lead(int note)
{
    while (note < MIDI_POKEY_LEAD_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_POKEY_LEAD_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_POKEY_LEAD_NOTE_MIN) {
        note = MIDI_POKEY_LEAD_NOTE_MIN;
    }
    if (note > MIDI_POKEY_LEAD_NOTE_MAX) {
        note = MIDI_POKEY_LEAD_NOTE_MAX;
    }
    return note;
}

/* Narrower fold band than every other chip's fold_note() here -- see
 * this file's top comment / the generator's comment for why Support's
 * playable band bottoms out at MIDI 60, a real 8-bit-register hardware
 * ceiling, not an arbitrary choice. */
static int fold_note_support(int note)
{
    while (note < MIDI_POKEY_SUPPORT_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_POKEY_SUPPORT_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_POKEY_SUPPORT_NOTE_MIN) {
        note = MIDI_POKEY_SUPPORT_NOTE_MIN;
    }
    if (note > MIDI_POKEY_SUPPORT_NOTE_MAX) {
        note = MIDI_POKEY_SUPPORT_NOTE_MAX;
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

static void trigger_lead(int note, int velocity, int timbre)
{
    int folded = fold_note_lead(note);
    uint32_t combined = midi_pokey_lead_period[folded - MIDI_POKEY_LEAD_NOTE_MIN];
    int vol = velocity_to_volume(velocity);
    int dist = k_pokey_distortion[timbre % 6];

    write_reg(0xD200u, combined & 0xFFu);        /* AUDF1 (osc0, low byte -- the pair's silent slave) */
    write_reg(0xD202u, (combined >> 8) & 0xFFu);  /* AUDF2 (osc1, high byte -- the audible master) */
    write_reg(0xD203u, dist | vol);               /* AUDC2 */
}

static void silence_lead(void)
{
    write_reg(0xD203u, 0x00);
}

static void trigger_support(int note, int velocity, int timbre)
{
    int folded = fold_note_support(note);
    int reload = midi_pokey_support_period[folded - MIDI_POKEY_SUPPORT_NOTE_MIN];
    int vol = velocity_to_volume(velocity);
    int dist = k_pokey_distortion[timbre % 6];

    write_reg(0xD204u, reload);       /* AUDF3 (osc2) */
    write_reg(0xD205u, dist | vol);   /* AUDC3 */
}

static void silence_support(void)
{
    write_reg(0xD205u, 0x00);
}

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    if (slot == 0) {
        trigger_lead(note, velocity, timbre);
    } else {
        trigger_support(note, velocity, timbre);
    }
}

static void silence_slot(int slot)
{
    if (slot == 0) {
        silence_lead();
    } else {
        silence_support();
    }
}

static void pokey_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

static void pokey_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    /* No bass slot exists (bass_count=0) -- BASS/EITHER both degrade to
     * the harmony pool, see pc_vp_note_on_gm()'s comment;
     * `pulse_duty` is reused verbatim as the distortion timbre (mod 6
     * applied in trigger_lead/trigger_support above). */
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, e->pulse_duty);
}

static void pokey_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void pokey_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void pokey_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void pokey_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> oscillator index3): coarse 3-way split by
 * note range (same idiom as every other chip adapter here), using the
 * noisier distortion settings rather than pitch-table lookups (drum
 * pitch is not precision-critical) -- see this file's top comment for
 * why it sustains until note_off (no hardware auto-decay on this chip). */
static void percussion_on(int note, int velocity)
{
    int reload;
    int dist;
    if (note < 40) {
        reload = 200; /* kick: long period (low pitch) */
        dist = 0x40;  /* 4-bit+5-bit poly: buzzy */
    } else if (note < 50) {
        reload = 80; /* snare: mid period */
        dist = 0x00; /* full noisy (17-bit+5-bit poly) */
    } else {
        reload = 10; /* hihat: short period (high pitch) */
        dist = 0xC0; /* 4-bit poly only, no gating: bright/short */
    }
    int vol = velocity_to_volume(velocity);

    write_reg(0xD206u, reload);
    write_reg(0xD207u, dist | vol);
}

static void silence_percussion(void)
{
    write_reg(0xD207u, 0x00);
}

static void pokey_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        pokey_note_on(channel, note, velocity);
    }
}

static void pokey_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    s_timbre_index = program;
}

static void pokey_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_percussion();
}

static int pokey_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = POKEY_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_impl.volume(0.5);   /* instrument app: 1.0 clips with all voices (board bench) */
    s_impl.synth.treble_eq(blip_eq_t(-1.0));
    s_apu.reset(&s_impl);
    s_apu.osc_output(0, NULL);       /* the Lead pair's silent slave -- never independently rendered */
    s_apu.osc_output(1, s_buf.center()); /* Lead (audible master) */
    s_apu.osc_output(2, s_buf.center()); /* Support */
    s_apu.osc_output(3, s_buf.center()); /* Noise/percussion */

    write_reg(0xD208u, 0x10); /* AUDCTL: pair osc0+osc1 into the 16-bit Lead voice (fast_bits[1]=0x10), see top comment */
    write_reg(0xD209u, 0x00); /* STIMER: reset delay/phase bookkeeping */

    pc_vp_init_n(&s_pool, 2, 0, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program);
    silence_percussion();
    return 0;
}

static void pokey_render(int16_t *stereo_frames, size_t frame_count)
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

#define POKEY_VOICE_COUNT 3

static int pokey_voice_count(void)
{
    return POKEY_VOICE_COUNT;
}

static const char *pokey_voice_name(int i)
{
    static const char *const names[POKEY_VOICE_COUNT] = {"Lead", "Support", "Noise"};
    if (i < 0 || i >= POKEY_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* i=0(Lead)->physical osc1, i=1(Support)->osc2, i=2(Noise)->osc3 -- see
 * this file's top comment for the UI-index-vs-physical-oscillator
 * mapping. osc_last_amp is signed (0-30 magnitude, see Sap_Apu.h's own
 * doc comment added by the voice-peek patch); take abs() like every
 * other signed-peek chip here. */
static void pokey_voice_state(int i, uint8_t *level, int *active)
{
    static const int osc_for_ui[POKEY_VOICE_COUNT] = {1, 2, 3};
    if (i < 0 || i >= POKEY_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_apu.osc_last_amp(osc_for_ui[i]);
    if (amp < 0) {
        amp = -amp;
    }
    int scaled = (amp * 255) / 30;
    if (scaled > 255) {
        scaled = 255;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- 2-wide harmony
 * pool, no bass slot (POKEY's two accurate voices are the same circuit). */
static int pokey_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(POKEY_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_pokey = {
    .id = "pokey",
    .name = "Atari POKEY",
    .reset = pokey_reset,
    .note_on = pokey_note_on_dispatch,
    .note_off = pokey_note_off,
    .program_change = pokey_program_change,
    .all_off = pokey_all_off,
    .note_on_direct = pokey_note_on_direct,
    .note_off_direct = pokey_note_off_direct,
    .note_on_gm = pokey_note_on_gm,
    .program_change_gm = pokey_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = pokey_render,
    .voice_count = pokey_voice_count,
    .voice_name = pokey_voice_name,
    .voice_state = pokey_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = pokey_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_atari_pokey(void)
{
    return &k_chip_pokey;
}

extern "C" int midi_pokey_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

extern "C" int midi_pokey_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
