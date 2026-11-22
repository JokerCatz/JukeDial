// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ported from RP2350_player src/player/midi_gb_chip.cpp (JC). Changes: pc_synth_t vtable, pc_vp_* pool,
 * pitch bend through the APU clock, voice_note. */
#include "inst_chip.h"

#include <string.h>

#include "Gb_Apu.h"
#include "Multi_Buffer.h"

#include "inst_midi_gb_tables.h"
#include "pc/pc_gm_table.h"
#include "pc/pc_voicepool.h"

/* The GB chip adapter -- drives a STANDALONE Gb_Apu
 * instance (the same class our voice-peek patch touches,
 * components/gme/patches/0001-voice-peek.patch) directly, bypassing gme's
 * Gbs_Emu/Classic_Emu file-player machinery entirely. This
 * is the only C++ file in the MIDI seam (mirrors player_backend_gme.cpp's
 * "all gme includes/calls live here" role) -- player_backend_midi.c (a C
 * file) talks to this only through chip_synth.h's plain C vtable.
 *
 * -- Driving pattern (derived from reading Gbs_Emu.cpp/Classic_Emu.cpp) --
 * A real gme file player drives Gb_Apu from a CPU emulator's clock; we
 * have no CPU, so render() below reproduces Classic_Emu::play_'s own
 * buffer-draining SHAPE: repeatedly read_samples() from a Stereo_Buffer,
 * and whenever it runs dry, advance the emulated clock (apu.end_frame()
 * then stereo_buf.end_frame(), same order/split Classic_Emu::play_ uses --
 * the APU steps its oscillators AND ends its own internal frame
 * bookkeeping, then the Multi_Buffer drains each oscillator's accumulated
 * Blip_Buffer deltas so they become readable samples). UNLIKE
 * Classic_Emu::play_, each advance is sized to the CURRENT shortfall
 * (roughly this render() call's own frame_count), not a fixed ~50ms
 * buffer-length chunk -- see gb_render()'s own comment for why a fixed
 * chunk would defeat the "note events land within one render()-call
 * boundary" latency goal by letting audio compute ahead of what gets
 * consumed.
 *
 * -- Register writes and clock time --
 * Every note_on/note_off/program_change/all_off call below writes
 * Gb_Apu registers at blip_time 0. This is only valid because
 * player_backend_midi.c's backend_render() drains the pending MIDI byte
 * ring and dispatches every event BEFORE calling render() for that block
 * (see that file's comment) -- Gb_Apu::end_frame() always resets its
 * internal last_time/next_frame_time back to a 0 origin
 * (`next_frame_time -= end_time; last_time -= end_time;`), so time 0 is
 * always valid at the point these functions run. This quantizes note
 * timing to render()-call boundaries (roughly one audio-callback period,
 * e.g. a few ms to a few dozen ms) rather than being sample-accurate --
 * a known simplification appropriate for this use case (live keyboard
 * play and coarse SIM_MIDI_SCRIPT timestamps), not a hard requirement
 * for a synth backend.
 *
 * -- Channel mapping --
 * OMNI melodic: note on/off/program-change from ANY MIDI channel except 9
 * (0-based; MIDI channel 10) feeds the single melodic voice pool below --
 * no per-channel routing, the allocator itself IS the remap. Program
 * change is honored from any melodic channel and applies GLOBALLY to the
 * melodic voices' duty/wave-pattern selection (not tracked per MIDI
 * channel -- one shared "instrument" for whichever notes are sounding).
 * MIDI channel 10 (index 9) routes exclusively to the noise oscillator
 * for percussion; percussion note_on never touches the melodic pool, and
 * the noise oscillator is NEVER a melodic-allocation target (four held
 * melodic keys can never spill a note onto it).
 *
 * -- Voice model (refines a flat "3-voice oldest-steal" scheme into
 * PRESS-TIME-ONLY/STICKY assignment -- see below for why) --
 * A naive oldest-steal pool across all 3 melodic oscillators causes
 * timbre chaos: the wave channel's built-in-pattern timbre is nothing
 * like a pulse's duty-cycle timbre, so a note randomly landing on
 * whichever physical oscillator happens to be free/oldest makes a chord
 * sound inconsistent voice to voice. Instead this adapter follows the
 * classic chiptune-arrangement idiom (wave/triangle channel = bassline,
 * pulse channels = harmony, as popularized by NES/GB tracker music where
 * the triangle/wave channel is conventionally reserved for bass): the
 * WAVE oscillator (2) is a BASS voice; the matched PULSE PAIR (0, 1) is a
 * harmony pool -- allocation and oldest-note-steal happen only within it.
 * The bass note is protected from stealing: a
 * 4th simultaneous melodic key always steals the harmony pool's oldest
 * pulse note, never the bass.
 *
 * "No mid-sustain re-routing" is read as a blanket prohibition, not one
 * scoped to "every note except the one being migrated": assignment is
 * decided ONCE at note-on and NEVER revisited for an already-sustaining
 * note. Wave takes a new note IFF
 * (wave is currently free) AND (the new note is not higher than every
 * currently-held harmony note) -- otherwise the new note goes to the
 * harmony pool (free-else-steal-oldest, bass still protected). No
 * migration, no promotion, no retriggering of a note that didn't just
 * arrive via note_on. Accepted, disclosed consequences of this
 * simplicity: (1) if the bass note releases while a chord is still held,
 * wave goes idle and stays idle -- the harmony notes keep sounding
 * unchanged, no glitch, just an honestly-idle bass voice until the next
 * note_on re-evaluates; (2) if a new note lower than the current bass
 * arrives while wave is busy, it lands on a pulse instead (the "lowest
 * note on wave" property is a press-time heuristic, not a live
 * invariant) -- voicing is imperfect in that case, but zero notes are
 * ever re-triggered on a different oscillator mid-sustain. See
 * gb_note_on()/gb_note_off() below.
 *
 * The allocation MECHANICS described above
 * (which of the 3 melodic slots a note lands on, sticky/no-migration,
 * harmony-pool oldest-steal, the GM group precedence) moved verbatim into
 * a chip-agnostic module, midi_voice_pool.{h,c}, so the NES adapter
 * (midi_nes_chip.cpp) can reuse the exact same logic instead of
 * reimplementing it -- see that header's top comment for the precise
 * moved/stayed split. This file now only owns the GB-specific register
 * writes (trigger_slot/silence_slot below) and timbre bookkeeping
 * (s_timbre_index/s_channel_program), passed to the shared pool via
 * function pointers. Behavior is unchanged; only the code's location
 * moved. */

#define GB_CLOCK_RATE 4194304u
static long s_bend_clock = GB_CLOCK_RATE;
#define MIDI_PERCUSSION_CHANNEL 9 /* MIDI channel 10, 0-based */

static Gb_Apu s_apu;
static Stereo_Buffer s_buf;
static uint32_t s_sample_rate_hz = 44100;

/* 0=square1, 1=square2, 2=wave -- see midi_voice_pool.h. */
static pc_vp_t s_pool;

/* Program-change-selected "instrument" index, shared by both the pulse
 * duty selector (mod 4 hardware duty cycles) and the wave channel's
 * built-in pattern selector (mod MIDI_GB_WAVE_PATTERN_COUNT) -- one
 * global index rather than two, since in Auto/Direct modes GM program
 * numbers have no meaningful mapping to either GB dimension anyway
 * (disclosed simplification, see this file's top comment). MIDI_MAP_GM
 * also funnels through this same variable -- gb_note_on_gm()
 * below sets it to the landing voice's curated timbre index (from
 * pc_gm_table.h) immediately before triggering, so trigger_square()/
 * trigger_wave() need no GM-specific code path at all. */
static int s_timbre_index;

/* MIDI_MAP_GM: each MIDI channel carries its OWN program state
 * (GM's own convention, unlike Auto's single global instrument) --
 * indexed by 0-based MIDI channel byte, default 0 (Acoustic Grand Piano,
 * the correct GM default for a channel that never received a program
 * change). Channel 9 (percussion) is never written here -- see
 * gb_program_change_gm() below. */
static int s_channel_program[16];

static void write_reg(unsigned addr, int data)
{
    s_apu.write_register(0, addr, data);
}

/* MIDI note -> playable-range fold (+/-12 semitones until inside
 * [MIDI_GB_NOTE_MIN, MIDI_GB_NOTE_MAX]) -- see gen_gb_note_table.py's
 * top comment for why this always terminates inside the band exactly. */
static int fold_note(int note)
{
    while (note < MIDI_GB_NOTE_MIN) {
        note += 12;
    }
    while (note > MIDI_GB_NOTE_MAX) {
        note -= 12;
    }
    if (note < MIDI_GB_NOTE_MIN) {
        note = MIDI_GB_NOTE_MIN; /* defensive; never actually engages, see generator comment */
    }
    if (note > MIDI_GB_NOTE_MAX) {
        note = MIDI_GB_NOTE_MAX;
    }
    return note;
}

static int velocity_to_env_volume(int velocity)
{
    int v = velocity >> 3; /* 0-127 -> 0-15 */
    if (v == 0) {
        v = 1; /* keep the DAC on for any audible note-on (see write_register's
                 * "initial volume nibble 0 -> enabled=false" DAC-off rule,
                 * Gb_Env::write_register) -- velocity 0 is already routed to
                 * NOTE_OFF by the MIDI parser and never reaches here. */
    }
    return v;
}

static void write_wave_pattern(int index)
{
    for (int i = 0; i < 16; i++) {
        write_reg(0xFF30u + (unsigned)i, midi_gb_wave_patterns[index][i]);
    }
}

static void silence_square(int slot)
{
    unsigned base = 0xFF10u + (unsigned)slot * 5u;
    write_reg(base + 2, 0x00); /* NRx2 top nibble 0 -> DAC off, instant silence */
}

static void silence_wave(void)
{
    write_reg(0xFF1Au, 0x00); /* NR30 bit7=0 -> DAC off */
}

static void silence_noise(void)
{
    write_reg(0xFF21u, 0x00); /* NR42 top nibble 0 -> DAC off */
}

static void trigger_square(int slot, int note, int velocity, int timbre)
{
    unsigned base = 0xFF10u + (unsigned)slot * 5u;
    int folded = fold_note(note);
    int period = midi_gb_square_period[folded - MIDI_GB_NOTE_MIN];
    int duty = timbre % 4;
    int vol = velocity_to_env_volume(velocity);

    write_reg(base + 1, (duty << 6)); /* NRx1: duty, length=0 (max, unused -- continuous mode) */
    write_reg(base + 2, (vol << 4));  /* NRx2: initial volume, period=0 (sustain, no auto-decay) */
    write_reg(base + 3, period & 0xFF);
    write_reg(base + 4, 0x80 | ((period >> 8) & 0x07)); /* trigger, no length-enable */
}

static void trigger_wave(int note, int velocity, int timbre)
{
    int folded = fold_note(note);
    int period = midi_gb_wave_period[folded - MIDI_GB_NOTE_MIN];
    int pattern = timbre % MIDI_GB_WAVE_PATTERN_COUNT;
    /* NR32 volume code: 00=mute, 01=100%, 10=50%, 11=25% -- 2 bits, not
     * the pulses'/noise's 4-bit envelope (Gb_Wave has no Gb_Env). Coarse
     * 3-way velocity split into the 3 non-mute codes, disclosed. */
    int vol_code = (velocity >= 85) ? 1 : (velocity >= 43) ? 2 : 3;

    write_wave_pattern(pattern);
    write_reg(0xFF1Au, 0x80);            /* NR30: DAC on */
    write_reg(0xFF1Bu, 0x00);            /* NR31: length (unused, continuous) */
    write_reg(0xFF1Cu, vol_code << 5);   /* NR32: volume code */
    write_reg(0xFF1Du, period & 0xFF);   /* NR33 */
    write_reg(0xFF1Eu, 0x80 | ((period >> 8) & 0x07)); /* NR34: trigger, no length-enable */
}

/* pc_vp_trigger_fn/pc_vp_silence_fn callbacks --
 * the pool passes the already-decided slot/note/velocity/timbre
 * (timbre is s_timbre_index for Auto/Direct, or the GM table's curated
 * value for GM -- see gb_note_on()/gb_note_on_gm() below), this file only
 * knows how to turn that into GB register writes. */
static void trigger_slot(int slot, int note, int velocity, int timbre)
{
    if (slot == 2) {
        trigger_wave(note, velocity, timbre);
    } else {
        trigger_square(slot, note, velocity, timbre);
    }
}

static void silence_slot(int slot)
{
    if (slot == 2) {
        silence_wave();
    } else {
        silence_square(slot);
    }
}

/* Press-time-only/sticky assignment (see top comment's "Voice model"
 * section), now delegated to the shared midi_voice_pool -- see
 * pc_vp_note_on()'s comment for the exact mechanics (unchanged
 * from this file's own former gb_note_on()). */
static void gb_note_on(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* percussion handled separately (see gb_percussion_on below) */
    }
    pc_vp_note_on(&s_pool, channel, note, velocity, s_timbre_index);
}

/* MIDI_MAP_GM (midi_map_mode.h): the sticky allocator is
 * reused via pc_vp_note_on_gm(), but the note's
 * CHANNEL's current program (s_channel_program, set by
 * gb_program_change_gm below -- GM's own per-channel program convention,
 * unlike Auto's single global instrument) looks up a pc_gm_table.h
 * entry first, whose voice-group value the pool uses to constrain
 * candidacy -- see pc_vp_note_on_gm()'s comment for the exact
 * precedence (unchanged from this file's own former gb_note_on_gm()).
 * The table entry's pulse_duty/wave_pattern fields are passed straight
 * through as the pool's harmony_timbre/bass_timbre -- the pool applies
 * whichever one matches the slot it actually picks, so trigger_square()/
 * trigger_wave() need no GM-specific code path at all. */
static void gb_note_on_gm(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* GM never consults the table for percussion, see top comment */
    }

    int program = s_channel_program[channel] & 0x7F;
    const pc_gm_entry_t *e = &pc_gm_table[program];
    pc_vp_note_on_gm(&s_pool, channel, note, velocity, (pc_gm_group_t)e->group,
                                e->pulse_duty, e->wave_pattern);
}

/* MIDI_MAP_GM's per-channel program tracking -- see
 * s_channel_program's own comment. Channel 9's program IS recorded when
 * a percussion-channel program change arrives (dispatch_event() forwards
 * before its percussion check) but is never read: gb_note_on_gm()
 * short-circuits channel 9 to percussion first. */
static void gb_program_change_gm(int channel, int program)
{
    if (channel < 0 || channel >= 16) {
        return;
    }
    s_channel_program[channel] = program & 0x7F;
}

/* Sticky assignment's mirror: releasing a note just silences and frees
 * whichever physical slot it was assigned to at note-on time -- no
 * promotion of a harmony note onto a just-vacated wave slot. If a
 * chord's bass note releases while harmony notes are still
 * held, wave simply goes idle until the next note_on re-evaluates;
 * the harmony notes are untouched. Percussion is filtered out here (the
 * pool itself has no percussion awareness, see midi_voice_pool.h). */
static void gb_note_off(int channel, int note)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* percussion is a one-shot decay, see gb_percussion_on */
    }
    pc_vp_note_off(&s_pool, channel, note);
}

/* MIDI_MAP_DIRECT (midi_map_mode.h): the allocator/bass-harmony scheme
 * above is bypassed entirely -- player_backend_midi.c has already mapped
 * a fixed MIDI channel to a physical voice index (0/1/2), dispatched
 * straight to the pool's own direct-mode entry points. Reuses the SAME
 * pool slots 0-2 as Auto mode; safe because player_backend_midi.c calls
 * all_off() (which zeroes every slot, see gb_all_off below) on every mode
 * switch, so a mode's leftover voice state is never visible to the other
 * mode. */
static void gb_note_on_direct(int voice, int note, int velocity)
{
    pc_vp_note_on_direct(&s_pool, voice, note, velocity, s_timbre_index);
}

static void gb_note_off_direct(int voice, int note)
{
    pc_vp_note_off_direct(&s_pool, voice, note);
}

/* 3 preset "drums" by note range -- a coarse 3-way split, NOT the
 * General MIDI drum map. NR43 (LFSR width/shift/divisor) values below are hand-
 * picked for an audible low/mid/high differentiation, not tuned for
 * acoustic realism. Each hit uses a short DECAYING envelope (period != 0,
 * direction=decrease) rather than the melodic voices' sustain-until-off
 * envelope, since percussion is one-shot and typically has no matching
 * note_off in real MIDI usage. */
static void gb_percussion_on(int note, int velocity)
{
    uint8_t nr43;
    if (note < 40) {
        nr43 = 0x53; /* kick: low shift, 15-bit LFSR -- long/low rumble */
    } else if (note < 50) {
        nr43 = 0x36; /* snare: mid shift, 7-bit LFSR -- buzzier/mid */
    } else {
        nr43 = 0x21; /* hihat: short period, 7-bit LFSR -- bright/fast */
    }
    int vol = velocity_to_env_volume(velocity);

    write_reg(0xFF20u, 0x00);              /* NR41: length (unused, continuous) */
    write_reg(0xFF21u, (vol << 4) | 0x02); /* NR42: initial volume, decrease, period=2 (quick decay) */
    write_reg(0xFF22u, nr43);              /* NR43 */
    write_reg(0xFF23u, 0x80);              /* NR44: trigger, no length-enable */
}

static void gb_note_on_dispatch(int channel, int note, int velocity)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        gb_percussion_on(note, velocity);
    } else {
        gb_note_on(channel, note, velocity);
    }
}

static void gb_program_change(int channel, int program)
{
    if (channel == MIDI_PERCUSSION_CHANNEL) {
        return; /* no selectable drum kits */
    }
    s_timbre_index = program;
}

static void gb_all_off(void)
{
    pc_vp_all_off(&s_pool);
    silence_noise();
}

static int gb_reset(uint32_t sample_rate_hz)
{
    s_sample_rate_hz = sample_rate_hz;
    /* Multi_Buffer CAPACITY in ms mirrors Classic_Emu::setup_buffer's own
     * choice (1000/20 = 50ms) -- just a max-size allocation, NOT how much
     * gb_render() below actually generates per call, see its comment. */
    /* blargg_err_t: NULL on success, a message on OOM. An unallocated
     * buffer reports sample rate 0, and nothing below -- nor render()'s
     * arithmetic -- is safe against that, so the reset fails instead of
     * proceeding. */
    if (s_buf.set_sample_rate((long)sample_rate_hz, 1000 / 20) != NULL) {
        return -1;
    }
    s_bend_clock = GB_CLOCK_RATE;
    s_buf.clock_rate(s_bend_clock);
    s_buf.bass_freq(120);
    s_buf.clear();

    s_apu.reset();
    s_apu.output(s_buf.center(), s_buf.left(), s_buf.right());
    s_apu.treble_eq(blip_eq_t(-1.0));

    write_reg(0xFF24u, 0x77); /* NR50: both output volumes max, no external-amp bits needed */
    write_reg(0xFF25u, 0xFF); /* NR51: every oscillator centered (both L+R) -- no panning feature */
    write_reg(0xFF26u, 0x80); /* NR52: power on */

    pc_vp_init(&s_pool, trigger_slot, silence_slot);
    s_timbre_index = 0;
    memset(s_channel_program, 0, sizeof s_channel_program); /* GM default program 0 per channel */
    write_wave_pattern(0);
    return 0;
}

static void gb_render(int16_t *stereo_frames, size_t frame_count)
{
    long count = (long)frame_count * 2;
    long remain = count;
    while (remain > 0) {
        long got = s_buf.read_samples((blip_sample_t *)&stereo_frames[count - remain], remain);
        remain -= got;
        if (remain > 0) {
            /* Generate just enough new samples for the CURRENT shortfall
             * (remain/2 frames) -- not Classic_Emu::play_'s fixed
             * buf->length() (50ms) chunk, see this file's top comment's
             * "Driving pattern" section for why: a fixed-size CPU-driven
             * emulator naturally produces exactly one such chunk per
             * frame of emulation, but sizing this synth's chunks the same
             * way would let up to ~50ms of ALREADY-COMPUTED audio queue
             * up in the Blip_Buffer ahead of whatever render() actually
             * consumes per call -- a note-off's register write (applied
             * before this call, blip_time 0) would then have no effect on
             * that stale backlog until it finally drained, defeating the
             * whole "quantized to one render()-call boundary" latency
             * goal. +64 clocks of slack (~15us) covers rounding so a
             * 1-frame shortfall doesn't spin this loop many times. */
            long frames_needed = remain / 2;
            blip_time_t clocks = (blip_time_t)(((int64_t)frames_needed * s_bend_clock) /
                                                s_sample_rate_hz) + 64;
            s_apu.end_frame(clocks);
            s_buf.end_frame(clocks);
        }
    }
}

static int gb_voice_count(void)
{
    return Gb_Apu::osc_count;
}

static const char *gb_voice_name(int i)
{
    static const char *const names[Gb_Apu::osc_count] = {"Square 1", "Square 2", "Wave", "Noise"};
    if (i < 0 || i >= Gb_Apu::osc_count) {
        return NULL;
    }
    return names[i];
}

/* Same amplitude-range derivation player_backend_gme.cpp's voice_state_gb
 * already documents (square/noise last_amp is signed -15..15, take abs();
 * wave is unipolar 0..30) -- see that file's comment for the full
 * Gb_Oscs.cpp trace. */
static void gb_voice_state(int i, uint8_t *level, int *active)
{
    if (i < 0 || i >= Gb_Apu::osc_count) {
        *level = 0;
        *active = 0;
        return;
    }
    int amp = s_apu.osc_last_amp(i);
    if (amp < 0) {
        amp = -amp;
    }
    int max_amp = (i == 2) ? 30 : 15;
    int scaled = (amp * 255) / max_amp;
    if (scaled > 255) {
        scaled = 255;
    }
    *level = (uint8_t)scaled;
    *active = scaled > 0;
}

/* The same-type voice group's width (chip_synth.h) -- GB's 2-wide pulse
 * harmony pool; the wave bass voice is a different instrument, not in it. */
static int gb_harmony_count(void)
{
    return pc_vp_harmony_count(&s_pool);
}

static void inst_pitch_bend(int ch, int cents)
{
    (void)ch;
    s_bend_clock = inst_bend_clock(GB_CLOCK_RATE, cents);
    s_buf.clock_rate(s_bend_clock);
}
static int inst_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }

static const pc_synth_t k_chip_gb = {
    .id = "gb",
    .name = "Game Boy",
    .reset = gb_reset,
    .note_on = gb_note_on_dispatch,
    .note_off = gb_note_off,
    .program_change = gb_program_change,
    .all_off = gb_all_off,
    .note_on_direct = gb_note_on_direct,
    .note_off_direct = gb_note_off_direct,
    .note_on_gm = gb_note_on_gm,
    .program_change_gm = gb_program_change_gm,
    .pitch_bend = inst_pitch_bend,
    .render = gb_render,
    .voice_count = gb_voice_count,
    .voice_name = gb_voice_name,
    .voice_state = gb_voice_state,
    .voice_note = inst_voice_note,
    .harmony_count = gb_harmony_count,
};

extern "C" const chip_synth_t *midi_chip_gb(void)
{
    return &k_chip_gb;
}

/* Test-only accessor (headless midi suite, platforms/sim/service_test_
 * main.c) -- mirrors platforms/sim/led_backend_capture.c's own
 * led_capture_frame()/led_capture_write_count() test-only accessors: not
 * part of chip_synth.h since no other caller needs it, just forward-
 * declared directly in the test file. Returns the MIDI note currently
 * held by physical melodic voice `slot` (0=pulse1, 1=pulse2, 2=wave), or
 * -1 if that voice is inactive or slot is out of range -- lets a test
 * verify WHICH voice a note landed on (the bass/harmony allocation
 * scheme's whole point) without adding any production-code observability
 * this backend doesn't otherwise need. */
extern "C" int midi_gb_debug_voice_note(int slot)
{
    return pc_vp_voice_note(&s_pool, slot);
}

/* Test-only accessor (same rationale/shape as
 * midi_gb_debug_voice_note above). Returns the raw s_timbre_index
 * snapshotted at trigger time for physical voice `slot`, or -1 if that
 * voice is inactive or slot is out of range -- lets a MIDI_MAP_GM test
 * verify a note's CURATED TIMBRE actually got applied (mod 4 is the
 * effective pulse duty code for slot 0/1; mod MIDI_GB_WAVE_PATTERN_COUNT
 * is the effective wave pattern index for slot 2 -- same mod convention
 * trigger_square()/trigger_wave() themselves use). */
extern "C" int midi_gb_debug_voice_timbre(int slot)
{
    return pc_vp_voice_timbre(&s_pool, slot);
}
