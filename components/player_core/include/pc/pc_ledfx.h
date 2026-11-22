// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_viz.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Rim "LED" effects of the instrument (spec 18): the 12 visible segments of the ring (pc_ledring), index v = 0
 * at lower left, clockwise to 11 at lower right (16-space segment (10 + v) % 16). Driven by notes (pitch class,
 * velocity, a colour from the caller: the voice's), or by the output spectrum. Beat and pad marks are a second
 * layer shown in every mode. Colours 0xRRGGBB. Pure logic; the caller draws. Ideas from RP2350_player
 * src/led/led_effects.c (ripple / comet / rainbow / spectrum), re-done for note events on 12 segments. */
#define PC_FX_SEGS 12
typedef enum { PC_FX_PITCH, PC_FX_RAINBOW, PC_FX_RIPPLE, PC_FX_COMET, PC_FX_SPECTRUM, PC_FX_OFF, PC_FX_COUNT } pc_fx_mode_t;
const char *pc_fx_name(pc_fx_mode_t m);          /* cfg value: "pitch", "rainbow", ... */
pc_fx_mode_t pc_fx_parse(const char *s);          /* unknown / NULL -> PC_FX_PITCH */

#define PC_FX_WAVES 8
#define PC_FX_COMETS 6
typedef struct { float pos, age, amp, dir; uint32_t col; } pc_fx_mover_t;   /* a ripple or a comet; amp 0 = free */
typedef struct {
    pc_fx_mode_t mode;
    float lvl[PC_FX_SEGS];                       /* note layer (pitch / rainbow), 0..1 */
    uint32_t col[PC_FX_SEGS];
    float mark[PC_FX_SEGS];                      /* beat / pad layer, 0..1 */
    uint32_t mcol[PC_FX_SEGS];
    pc_fx_mover_t wave[PC_FX_WAVES], comet[PC_FX_COMETS];
    float band[PC_FX_SEGS];                      /* spectrum, smoothed, 0..1 */
    int last_note;                               /* comet direction: a higher note goes clockwise */
} pc_ledfx_t;

void pc_fx_init(pc_ledfx_t *f, pc_fx_mode_t m);
void pc_fx_set_mode(pc_ledfx_t *f, pc_fx_mode_t m);   /* clears the note layer */
int  pc_fx_note_seg(int note);                   /* visible segment of a pitch class, -1 */
uint32_t pc_fx_pc_color(int note);               /* rainbow: a hue per pitch class (C red, around the wheel) */
void pc_fx_note(pc_ledfx_t *f, int note, int vel, uint32_t col);
void pc_fx_mark(pc_ledfx_t *f, int seg, uint32_t col, float level);   /* visible segment; out of range ignored */
void pc_fx_spectrum(pc_ledfx_t *f, const float *bars, int n);         /* n bars low..high (e.g. 64) */
void pc_fx_tick(pc_ledfx_t *f, int ms);
bool pc_fx_busy(const pc_ledfx_t *f);            /* anything lit (or moving) */
uint32_t pc_fx_ink(const pc_theme_t *th, uint32_t rgb);   /* a lit LED's colour on the theme's page: a light theme darkens it to 3:1 against the page, a dark one leaves it */
/* every segment's colour over the unlit one; ramp / theme colour the spectrum */
void pc_fx_render(const pc_ledfx_t *f, uint32_t unlit, pc_ramp_t ramp, const pc_theme_t *th, uint32_t out[PC_FX_SEGS]);
uint32_t pc_fx_mix(uint32_t a, uint32_t b, float t);   /* a -> b by t 0..1 */
#ifdef __cplusplus
}
#endif
