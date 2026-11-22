// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_sms_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Multi_Buffer.h"
#include "Sms_Apu.h"

#include "pc/pc_gm_table.h"
#include "inst_midi_sms_tables.h"
#include "pc/pc_voicepool.h"

/* The [SMS:PSG] adapter -- drives a
 * standalone Sms_Apu (the Sega Master System's SN76489-compatible PSG)
 * directly, same driving pattern/blip-time-0 discipline as every other
 * adapter here (see midi_nes_chip.cpp's top comment for the full
 * derivation this file doesn't repeat).
 *
 * -- Register write protocol (gme/Sms_Apu.cpp's write_data(), reverse-
 * engineered bit-by-bit, not assumed from datasheet prose
 * -- see RP2350_player/tools/gen_sms_note_table.py's top comment for the full byte-math
 * derivation) --
 * Sms_Apu is a single write_data(time, byte) port with an internal
 * latch, standard SN76489 protocol:
 *   Latch byte (bit7=1): 1 cc t dddd
 *     cc   = channel select (0/1/2 = tone A/B/C, 3 = noise)
 *     t    = register type (0 = tone/noise period, 1 = volume)
 *     dddd = low 4 bits of the period (period writes only; ignored/
 *            reused as the volume value itself for volume writes, see
 *            below)
 *   Continuation byte (bit7=0, tone/period writes only): 0 - dddddd
 *     dddddd = high 6 bits of the 10-bit period register N
 * A volume write is a SINGLE latch byte (t=1): the byte's own low 4 bits
 * ARE the 4-bit attenuation code directly (see "Attenuation-based
 * volume" below) -- no continuation byte, matching Sms_Apu::write_data's
 * `if (latch & 0x10) oscs[index]->volume = volumes[data & 15];` branch,
 * which reads `data` (this same latch byte), not a following byte.
 * write_reg_period()/write_reg_volume() below implement these two shapes;
 * write_reg_noise() is a single latch byte too (channel 3, type 0).
 *
 * -- Attenuation-based volume, INVERTED vs the AY-style volume --
 * Unlike Ay_Apu's amp_table (0=silent, 15=loudest, ASCENDING amplitude --
 * see midi_ay_family.h/midi_fme7_chip.cpp), the SN76489's 4-bit volume
 * field is an ATTENUATION code: 0=0dB (loudest), 15=full attenuation
 * (silent) -- DESCENDING amplitude as the register value rises. Source:
 * gme/Sms_Apu.cpp's own `volumes[16] = {64, 50, 39, 31, 24, 19, 15, 12,
 * 9, 7, 5, 4, 3, 2, 1, 0}` lookup table the emulator applies to whatever
 * 4-bit code we write -- monotonically DECREASING output as the written
 * code increases, ~2dB/step (`pow(1.26, ...)` per the table's own
 * generating-comment), the well-documented SN76489/SN76496 datasheet
 * attenuator convention. velocity_to_atten() below inverts this project's
 * usual `velocity>>3` mapping (higher velocity -> LOWER attenuation code)
 * to get louder-with-higher-velocity, the musically correct direction,
 * despite the register itself running backwards.
 *
 * -- Voice topology --
 * harmony_count=2 (channels A/B) + bass_count=1 (channel C) + channel 3
 * (noise) dedicated to ch10 percussion -- the SAME 2+1+percussion shape
 * GB/NES/[NES:5B]/[Atari:POKEY] all use. Disclosed: unlike GB's real
 * wave-vs-pulse asymmetry, SMS's 3 tone channels are the IDENTICAL
 * circuit (same situation as [NES:5B]/[Atari:POKEY] -- no hardware
 * timbral distinction), so "channel C is bass" is a stylistic/topology
 * convention for consistency with the rest of this roster, not a
 * hardware-forced choice.
 *
 * -- No duty/timbre dimension (disclosed, same situation as [NES:5B]) --
 * The SN76489 has no duty-cycle or waveform register at all -- always a
 * fixed 50% square per tone channel. program_change is accepted (stored,
 * per the chip_synth_t contract) but has NO audible effect on melodic
 * voices; only percussion's hit-type bucketing varies by note range (see
 * below), same disclosed limitation midi_fme7_chip.cpp documents for its
 * own AY-family tone channels.
 *
 * -- Percussion: real hardware noise register variety --
 * The noise channel's register (latch byte, channel=3, type=0) packs a
 * 2-bit rate select (0=N/512 fastest/brightest, 1=N/1024, 2=N/2048
 * slowest/dullest, 3=sync-to-channel-C -- unused here) and a feedback
 * bit (bit2: 0=periodic/buzzy "looped" LFSR, 1=white/hissy proper LFSR --
 * see Sms_Apu::reset()'s `noise_feedback` vs `looped_feedback` and
 * write_data()'s `noise.feedback = (data&0x04) ? noise_feedback :
 * looped_feedback;`). Coarse 3-way hit-type split (same idiom as every
 * other percussion-lacking-a-tone-generator chip here):
 *   kick  -- rate=2 (slowest/dullest), periodic (buzzy low thump)
 *   snare -- rate=1 (mid), white (hissy body)
 *   hihat -- rate=0 (fastest/brightest), white (bright hiss)
 * No auto-decay (disclosed, matches [NES:5B]/[Atari:POKEY]'s own
 * percussion -- constant volume, sustains until note_off). */

#define SMS_CLOCK_RATE 3579545u /* see RP2350_player/tools/gen_sms_note_table.py's top comment */
static long s_bend_clock = SMS_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9

/* SN76489 noise register bits (latch byte, channel=3 type=0): bits0-1 =
 * rate select, bit2 = feedback (0=periodic, 1=white) -- see top comment's
 * "Percussion" section. */
#define SMS_NOISE_RATE_FAST 0x00
#define SMS_NOISE_RATE_MID 0x01
#define SMS_NOISE_RATE_SLOW 0x02
#define SMS_NOISE_FB_WHITE 0x04
#define SMS_NOISE_FB_PERIODIC 0x00

static Sms_Apu s_sms;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0/1 = channel A/B (harmony), 2 = channel C (bass) -- harmony_count=2,
 * bass_count=1, the shape pc_vp_init() reproduces exactly (see
 * this file's top comment). Channel 3 (noise) is driven directly outside
 * the pool, same idiom as every other chip adapter here. */
static pc_vp_t s_pool;
static int s_channel_program[16]; /* stored, GM program tracking only -- no audible timbre effect, see top comment */

static void write_reg_period(int channel, int period)
{
    int n = period;
    if (n < 0) {
        n = 0;
    }
    if (n > 1023) {
        n = 1023;
    }
    s_sms.write_data(0, 0x80 | (channel << 5) | (n & 0x0F));
    s_sms.write_data(0, (n >> 4) & 0x3F);
}

/* atten: 0..15, 0=loudest per the SN76489's own attenuator convention --
 * see top comment's "Attenuation-based volume" section. Single latch
 * byte, channel 0-2 tone or 3 noise. */
static void write_reg_volume(int channel, int atten)
{
    s_sms.write_data(0, 0x80 | (channel << 5) | 0x10 | (atten & 0x0F));
}

static void write_reg_noise(int rate, int fb)
{
    s_sms.write_data(0, 0x80 | (3 << 5) | (fb & 0x04) | (rate & 0x03));
}

static int fold_note(int note)
{
    while (note < MIDI_SMS_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_SMS_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_SMS_NOTE_MIN) {
        note = MIDI_SMS_NOTE_MIN;
    }
    if (note > MIDI_SMS_NOTE_MAX) {
        note = MIDI_SMS_NOTE_MAX;
    }
    return note;
}

/* Inverted vs every other chip adapter's velocity_to_volume() here -- see
 * top comment's "Attenuation-based volume" section: higher velocity must
 * produce a LOWER attenuation code. v ranges 1..15 (same floor-at-1 idiom
 * as every other adapter, to avoid a velocity=1..7 note going fully
 * silent), atten = 15-v ranges 0..14 (never the full-silence code 15,
 * which silence_channel() writes explicitly instead). */
static int velocity_to_atten(int velocity)
{
    int v = velocity >> 3;
    if (v == 0) {
        v = 1;
    }
    return 15 - v;
}

static void trigger_channel(int channel, int note, int velocity, int timbre)
{
    (void)timbre; /* no duty/timbre dimension on this chip, see top comment */
    int folded = fold_note(note);
    int period = midi_sms_tone_period[folded - MIDI_SMS_NOTE_MIN];
    int atten = velocity_to_atten(velocity);
    write_reg_period(channel, period);
    write_reg_volume(channel, atten);
}

static void silence_channel(int channel)
{
    write_reg_volume(channel, 0x0F); /* max attenuation code = silent */
}

static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    trigger_channel(slot, note, velocity, timbre);
}

static void silence_slot(int slot)
{
    silence_channel(slot);
}

static void sms_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, 0);
}

static void sms_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group, 0, 0);
}

static void sms_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

static void sms_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return;
    }
    pc_vp_note_off(&s_pool, channel, note);
}

static void sms_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, 0);
}

static void sms_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* Percussion (channel 10 -> the noise channel): coarse 3-way hit-type
 * split by real hardware register variety, see top comment. */
static void percussion_on(int note, int velocity)
{
    int rate, fb;
    if (note < 40) {
        rate = SMS_NOISE_RATE_SLOW;
        fb = SMS_NOISE_FB_PERIODIC;
    } else if (note < 50) {
        rate = SMS_NOISE_RATE_MID;
        fb = SMS_NOISE_FB_WHITE;
    } else {
        rate = SMS_NOISE_RATE_FAST;
        fb = SMS_NOISE_FB_WHITE;
    }
    write_reg_noise(rate, fb);
    write_reg_volume(3, velocity_to_atten(velocity));
}

static void silence_percussion(void)
{
    write_reg_volume(3, 0x0F);
}

static void sms_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        percussion_on(note, velocity);
    } else {
        sms_note_on(channel, note, velocity);
    }
}

static void sms_program_change(int channel, int program)
{
    (void)channel;
    (void)program; /* stored per-channel via program_change_gm only; no audible melodic effect, see top comment */
}

static void sms_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_percussion();
}

static int sms_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = SMS_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_sms.reset();
    s_sms.output(s_buf.center()); /* mono-to-all-three, matching every other adapter here -- no per-voice stereo positioning */
    s_sms.treble_eq(blip_eq_t(-1.0));

    pc_vp_init(&s_pool, trigger_slot, silence_slot); /* 2 harmony + 1 bass, GB/NES's own shape */
    memset(s_channel_program, 0, sizeof s_channel_program);
    silence_channel(0);
    silence_channel(1);
    silence_channel(2);
    silence_percussion();
    return 0;
}

static void sms_render(int16_t *stereo_frames, size_t frame_count)
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
            s_sms.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

#define SMS_VOICE_COUNT 4

static int sms_voice_count(void)
{
    return SMS_VOICE_COUNT;
}

static const char *sms_voice_name(int i)
{
    static const char *const names[SMS_VOICE_COUNT] = {"Square A", "Square B", "Square C", "Noise"};
    if (i < 0 || i >= SMS_VOICE_COUNT) {
        return NULL;
    }
    return names[i];
}

/* Signed: square/noise last_amp swings +-volume (see gme/Sms_Apu.h's own
 * voice-peek doc comment, components/gme/patches). Take abs(), same convention
 * as every other signed-peek chip here. Magnitude bounded 0-64 (the
 * volumes[] table's own max entry). */
static void sms_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= SMS_VOICE_COUNT) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_sms.osc_last_amp(i);
    if (amp < 0) {
        amp = -amp;
    }
    if (amp > 64) {
        amp = 64;
    }
    int scaled = (amp * 255) / 64;
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- SMS's 2-wide tone
 * harmony pool; the third bass tone voice is a different instrument. */
static int sms_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(SMS_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_sms = {
    .id = "sms",
    .name = "Sega SMS",
    .reset = sms_reset,
    .note_on = sms_note_on_dispatch,
    .note_off = sms_note_off,
    .program_change = sms_program_change,
    .all_off = sms_all_off,
    .note_on_direct = sms_note_on_direct,
    .note_off_direct = sms_note_off_direct,
    .note_on_gm = sms_note_on_gm,
    .program_change_gm = sms_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = sms_render,
    .voice_count = sms_voice_count,
    .voice_name = sms_voice_name,
    .voice_state = sms_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = sms_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_sms(void)
{
    return &k_chip_sms;
}

extern "C" int midi_sms_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}
