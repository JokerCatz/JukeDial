// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The instrument's 16-segment rim "LED" ring (spec 7, 18): segment k is centred k * 22.5 + 11.25 degrees clockwise
 * from the top (so the 12 visible ones sit symmetric about the top). A note-on flashes its pitch class: C..B on 12
 * segments rising clockwise from lower left (C = 10 at -123.75 degrees, B = 5 at +123.75); the bottom four (6..9)
 * are left to the page icons. */
#define PC_LED_N 16
typedef struct { uint8_t level[PC_LED_N], voice[PC_LED_N]; } pc_ledring_t;

int  pc_led_note_segment(int note);              /* -1 outside 0..127 */
void pc_led_init(pc_ledring_t *r);
void pc_led_hit(pc_ledring_t *r, int note, int voice, int velocity);
void pc_led_decay(pc_ledring_t *r, int ms);      /* full to dark in 500 ms */
int  pc_led_active(const pc_ledring_t *r);       /* segments lit */
/* metronome: beat i of a bar of n on the 12 visible segments, each beat centred in its share of the arc
 * (lower left = first, clockwise; 4/4 -> 11, 14, 1, 4) */
int  pc_led_beat_segment(int beat, int bpb);
void pc_led_set(pc_ledring_t *r, int seg, int voice, int level);   /* light one segment (beats, pads) */

/* Pads page: 8 sector pads over the upper 252 degrees of the round screen (the bottom holds the page icons, which
 * reach +-131.5 degrees), centre (180, 180), radius 56..165; pad 0 starts at lower left (-126 degrees), clockwise. */
#define PC_PADS 8
#define PC_PAD_EDGE 126.0
int  pc_pad_hit(int x, int y);                   /* pad under a touch, -1 */
int  pc_pad_segment(int pad);                    /* the rim segment nearest the pad's centre */
double pc_pad_angle(int pad);                    /* the pad's centre, degrees clockwise from the top */
#ifdef __cplusplus
}
#endif
