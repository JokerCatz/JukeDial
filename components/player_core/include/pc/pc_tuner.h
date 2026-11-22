// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The tuner's pure parts (tools ring: 調音器): a YIN-style pitch detector, the note / cents arithmetic and the reference-tone generator.
 * Pitch: x is mono 16-bit PCM at `rate` (16 kHz on the board). The analysis uses the newest PC_PITCH_WIN + PC_PITCH_TAU_MAX + 1 samples (1425: ~89 ms at
 * 16 kHz), so a 2048-sample ring always holds enough. The difference function and its cumulative-mean normalisation (de Cheveigne & Kawahara 2002) are
 * computed lag by lag from 1 and stop at the first dip below the threshold once its minimum is passed (a clean high note costs a few thousand
 * multiply-adds, noise the full 40 x 1024 x 10); the lag is refined by a parabola through the raw difference at the minimum.
 * Range 40 Hz .. 2 kHz. Below the gate (rms) or without a periodic dip (noise) the result is "no pitch" (hz 0). No allocation: scratch comes from the caller.
 * Tone: sine, square 50 % / 25 %, triangle, saw at an exact frequency (float phase, per-sample increment hz / rate computed per block), a 10 ms amplitude
 * ramp on start and stop, peak PC_TONE_PEAK; added to a stereo block with the toy sound's rule (held at full scale: it yields to the music; pc_sfx.h). */
#define PC_PITCH_WIN       1024
#define PC_PITCH_TAU_MAX   400          /* 40 Hz at 16 kHz */
#define PC_PITCH_NEED      (PC_PITCH_WIN + PC_PITCH_TAU_MAX + 1)
#define PC_PITCH_HZ_MIN    40.0f
#define PC_PITCH_HZ_MAX    2000.0f
#define PC_PITCH_GATE_RMS  40.0f        /* int16 units: below this the signal is silence (the PDM mic's room noise sits around 15..30) */
#define PC_PITCH_THRESHOLD 0.15f        /* YIN's absolute threshold on the normalised difference */
#define PC_PITCH_SCRATCH   (PC_PITCH_NEED + PC_PITCH_TAU_MAX + 3)   /* floats the caller provides */
typedef struct { float hz; float clarity; float rms; } pc_pitch_t;   /* hz 0: no pitch; clarity 0..1 (1 - the normalised dip) */

/* n >= PC_PITCH_NEED; scratch: PC_PITCH_SCRATCH floats. Returns true and hz > 0 when a pitch was found; the newest samples are used (x + n - NEED). */
bool pc_pitch_detect(const int16_t *x, int n, uint32_t rate, float *scratch, pc_pitch_t *out);

/* Notes: MIDI number, A4 = a4 Hz (430..450, 440 default). */
#define PC_A4_MIN 430
#define PC_A4_MAX 450
#define PC_A4_DEFAULT 440
float pc_note_hz(int midi, float a4);
int   pc_note_from_hz(float hz, float a4, float *cents);   /* the nearest MIDI note (can be out of 0..127 only for hz outside the range: clamped), cents -50..+50 */
const char *pc_note_name(int midi, char *buf, int n);      /* "A4", "C#3" (sharps), buf >= 5 */
bool pc_in_tune(float cents);                              /* |cents| <= 5 */
#define PC_TUNE_CENTS 5.0f

enum { PC_TONE_SINE, PC_TONE_SQ50, PC_TONE_SQ25, PC_TONE_TRI, PC_TONE_SAW, PC_TONE_WAVES };
#define PC_TONE_PEAK 0.25f              /* -12 dBFS */
typedef struct {
    uint8_t wave;
    float hz;                           /* the target pitch */
    bool on;                            /* sounding (false: ramping down to silence) */
    float ph;                           /* 0..1 */
    float amp;                          /* 0..1 now (the ramp) */
} pc_tone_t;
void pc_tone_set(pc_tone_t *t, int wave, float hz, bool on);
bool pc_tone_active(const pc_tone_t *t);
/* adds the tone (times g) into lr, frames interleaved stereo frames at rate; false: nothing was added */
bool pc_tone_mix(pc_tone_t *t, float *lr, int frames, uint32_t rate, float g);
