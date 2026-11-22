// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 萬花尺 / Spirograph (ideas G5): the curve and its pen, pure C (no UI, host-tested: test_game_spiro).
 * A wheel of r teeth rolls inside (hypotrochoid) or outside (epitrochoid) a ring of R teeth; the pen sits d = pen * r from the wheel's centre.
 *   hypo  x = (R-r) cos t + d cos((R-r)/r t)    y = (R-r) sin t - d sin((R-r)/r t)
 *   epi   x = (R+r) cos t - d cos((R+r)/r t)    y = (R+r) sin t - d sin((R+r)/r t)
 * With integer R and r the curve CLOSES after t = 2 pi r / gcd(R, r) (r/g turns round the ring), which is what pc_sp_turns() counts: the pen stops there.
 * The curve is drawn as PC_SP_SEG_REV straight segments per t = 2 pi (so a curve is turns * 120 segments) and scaled so that its widest point (with the
 * layer-0 pen) is exactly 1.0: every point is in the unit circle, the page multiplies by the field's radius (pc_sp_clip keeps it there whatever happens).
 * One pc_sp_step() is one fixed step of 1/30 s (ui_arena's tick) and hands the page the segments the pen drew in it (out[]), in the unit circle.
 * Rules (規則, classic first): 0 經典 one curve; 1 層疊 three curves one after the other, same wheels, the pen at 100 %, 65 %, 35 % of the set distance;
 * 2 音樂 one curve whose pen speed follows the music's energy (0..255 given to each step; silence still creeps).
 * Axes (set before or during a curve; a change starts the curve over): gear 0..PC_SP_GEARS-1 (the table in pc_game_spiro.c), pen 0..PC_SP_PENS-1 (40, 70, 100, 130 %
 * of r), speed 0..PC_SP_SPEEDS-1 (slow, normal, fast: segments a step), colour 0..PC_SP_COLOURS-1 (the page's: solid, two-tone along the curve, rainbow).
 * Surprise: the golden curve (a game of 1 in PC_SP_GOLD_N, the page's roll): a gear of eight or more lobes, hypotrochoid, the pen on the wheel's rim (cusps). */
enum { PC_SP_CLASSIC = 0, PC_SP_LAYERS, PC_SP_MUSIC, PC_SP_RULES };
#define PC_SP_GEARS     8
#define PC_SP_PENS      4
#define PC_SP_SPEEDS    3
#define PC_SP_COLOURS   3
#define PC_SP_LAYERS_N  3
#define PC_SP_SEG_REV   120     /* segments per turn of t */
#define PC_SP_BURST     24      /* segments a step draws at most */
#define PC_SP_GOLD_N    20
typedef struct { float x0, y0, x1, y1; float t; uint8_t layer; } pc_sp_seg_t;   /* t: how far along its curve the segment ends (0..1) */
typedef struct {
    uint8_t rule, gear, pen, speed, colour, layers;      /* layers: curves of this game (1, or PC_SP_LAYERS_N for 層疊) */
    bool epi, gold, done;
    uint8_t layer;                                       /* the curve the pen draws now */
    uint32_t seg, total;                                 /* segments drawn of this curve / the curve's segments */
    float carry;                                         /* the fraction of a segment the pen owes */
    uint32_t rng;
    pc_sp_seg_t out[PC_SP_BURST];
    int n_out;                                           /* the segments of the last step */
    bool ev_layer, ev_done;                              /* the last step finished a curve (not the last) / the whole drawing */
} pc_sp_t;
/* a new drawing: the gear, pen and kind (hypo / epi) from the seed (the golden game's as above); the axes speed and colour as given */
void pc_sp_init(pc_sp_t *s, int rule, int speed, int colour, uint32_t seed, bool gold);
/* the same drawing's wheels changed (-1: keep): starts over, nothing drawn */
void pc_sp_set(pc_sp_t *s, int gear, int pen, int epi);
void pc_sp_restart(pc_sp_t *s);
int  pc_sp_step(pc_sp_t *s, int energy);                 /* the pen draws; returns the segments made (out[0..n)); 0 once done */
bool pc_sp_running(const pc_sp_t *s);                    /* a step changes something: not done */
float pc_sp_rate(const pc_sp_t *s, int energy);          /* segments a step (music: follows energy) */
void pc_sp_gear_of(int gear, int *R, int *r);            /* the gear's teeth (a bad index is the first) */
int  pc_sp_turns(int gear);                              /* r / gcd(R, r): turns round the ring until the curve closes */
int  pc_sp_lobes(int gear);                              /* R / gcd(R, r): the curve's points / loops */
float pc_sp_pen_of(int pen);                             /* 0.4 .. 1.3 of r */
/* the point of curve `layer` of this drawing at segment index seg (any real number, 0 .. total), in the unit circle */
void pc_sp_point(const pc_sp_t *s, int layer, double seg, float *x, float *y);
void pc_sp_clip(float *x, float *y, float limit);        /* a point beyond `limit` from the centre goes to it, on the same ray */
