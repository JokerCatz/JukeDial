// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_ledring.h"
#include <math.h>
#include <string.h>

int pc_led_note_segment(int note)
{
    if (note < 0 || note > 127) return -1;
    return (10 + note % 12) % PC_LED_N;
}

void pc_led_init(pc_ledring_t *r) { memset(r, 0, sizeof *r); }

void pc_led_hit(pc_ledring_t *r, int note, int voice, int velocity)
{
    int k = pc_led_note_segment(note);
    if (k < 0) return;
    int lv = 64 + velocity * 191 / 127;                 /* soft notes still visible */
    if (lv > 255) lv = 255;
    if (lv > r->level[k]) r->level[k] = (uint8_t)lv;
    r->voice[k] = (uint8_t)voice;
}

void pc_led_decay(pc_ledring_t *r, int ms)
{
    int d = ms * 255 / 500;
    for (int k = 0; k < PC_LED_N; k++) r->level[k] = (uint8_t)(r->level[k] > d ? r->level[k] - d : 0);
}

int pc_led_active(const pc_ledring_t *r)
{
    int n = 0;
    for (int k = 0; k < PC_LED_N; k++) n += r->level[k] > 0;
    return n;
}

int pc_led_beat_segment(int beat, int bpb)
{
    if (bpb < 1) bpb = 1;
    beat %= bpb;
    if (beat < 0) beat += bpb;
    int v = (2 * beat + 1) * 12 / (2 * bpb);          /* 0..11 on the visible arc */
    return (10 + v) % PC_LED_N;
}

void pc_led_set(pc_ledring_t *r, int seg, int voice, int level)
{
    if (seg < 0 || seg >= PC_LED_N) return;
    r->level[seg] = (uint8_t)(level < 0 ? 0 : level > 255 ? 255 : level);
    r->voice[seg] = (uint8_t)voice;
}

#define PAD_SPAN (2.0 * PC_PAD_EDGE / PC_PADS)
double pc_pad_angle(int pad) { return -PC_PAD_EDGE + PAD_SPAN * (pad + 0.5); }

int pc_pad_hit(int x, int y)
{
    double dx = x - 180, dy = y - 180, r = sqrt(dx * dx + dy * dy);
    if (r < 56 || r > 165) return -1;
    double a = atan2(dx, -dy) * 180.0 / M_PI;          /* clockwise from the top */
    if (a < -PC_PAD_EDGE || a > PC_PAD_EDGE) return -1;
    int p = (int)((a + PC_PAD_EDGE) / PAD_SPAN);
    return p < 0 ? 0 : p >= PC_PADS ? PC_PADS - 1 : p;
}

int pc_pad_segment(int pad)
{
    if (pad < 0 || pad >= PC_PADS) return -1;
    int s = (int)lround((pc_pad_angle(pad) - 11.25) / 22.5);   /* segment k is centred at k * 22.5 + 11.25 */
    return (s % PC_LED_N + PC_LED_N) % PC_LED_N;
}
