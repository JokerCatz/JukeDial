// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sample player for the instrument's kit layer (spec 4 / stage 4): 16 voices, linear interpolation for pitch
 * and rate, 16-bit mono or stereo samples (in PSRAM on the board), mixed into a stereo int32 buffer.
 * Voices: a free one, else the oldest is taken over (instantly). The same note on the same channel again
 * fades the earlier voice out over 2 ms (no pile-up of hi-hats). Note-off fades a sustained voice over
 * 50 ms; one-shot voices ignore it and play to their end. Velocity sets the level (squared, as the GM CC7).
 * Audio task only. */
#define PC_SAMP_VOICES 16
typedef struct { const int16_t *data; uint32_t frames, rate; uint8_t channels; } pc_sample_t;
typedef struct {
    const pc_sample_t *s;
    uint32_t idx, frac;          /* position: frame + 32-bit fraction (32-bit maths only: RISC-V 32) */
    uint32_t step_i, step_f;     /* per output frame */
    int32_t gain;                /* q15 */
    int32_t fade, fade_step;     /* q15 level; fade_step > 0 while fading out */
    uint32_t age;
    uint8_t note, ch;
    bool on, one_shot;
} pc_svoice_t;
typedef struct { pc_svoice_t v[PC_SAMP_VOICES]; uint32_t rate, clock; int max; } pc_sampler_t;   /* max: voices in use */

void pc_sampler_init(pc_sampler_t *s, uint32_t out_rate);
/* pitch: note - center semitones; level = (vel / 127)^2 * 0.5 (-6 dB: a few hits at once stay under full
 * scale); returns the voice, -1 when vel is 0 */
int  pc_sampler_note_on(pc_sampler_t *s, const pc_sample_t *smp, int note, int center, int vel, int ch, bool one_shot);
void pc_sampler_note_off(pc_sampler_t *s, int ch, int note);
void pc_sampler_all_off(pc_sampler_t *s);        /* every voice fades out over 2 ms */
void pc_sampler_kill(pc_sampler_t *s);           /* every voice stops now (the kit's memory goes away) */
void pc_sampler_render(pc_sampler_t *s, int32_t *mix, int frames);
int  pc_sampler_active(const pc_sampler_t *s);
/* polyphony 1..16 (16 after init); voices above a lowered limit fade out over 2 ms */
void pc_sampler_set_voices(pc_sampler_t *s, int n);
int  pc_sampler_voices(const pc_sampler_t *s);
#ifdef __cplusplus
}
#endif
