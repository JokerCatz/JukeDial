// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_spiro.h"
#include "pc/pc_dial.h"
#include <math.h>

static const uint8_t GEAR[PC_SP_GEARS][2] = { { 5, 3 }, { 7, 2 }, { 8, 3 }, { 9, 4 }, { 10, 4 }, { 11, 5 }, { 13, 4 }, { 12, 7 } };   /* R, r (teeth) */
static const float PEN[PC_SP_PENS] = { 0.4f, 0.7f, 1.0f, 1.3f };
static const float LAYER_PEN[PC_SP_LAYERS_N] = { 1.0f, 0.65f, 0.35f };
static const float SPEED_SEGS[PC_SP_SPEEDS] = { 2.0f, 5.0f, 12.0f };      /* segments a step */
static const uint8_t GOLD_GEARS[4] = { 2, 3, 5, 7 };                      /* eight or more lobes */
#define MUSIC_MIN 0.25f                                                   /* the music rule: the base speed x 0.25 (silence) .. x 3 (full energy) */
#define MUSIC_SPAN 2.75f

static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
static uint32_t rnd(pc_sp_t *s) { uint32_t x = s->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s->rng = x; }

void pc_sp_gear_of(int gear, int *R, int *r)
{
    const uint8_t *g = GEAR[gear >= 0 && gear < PC_SP_GEARS ? gear : 0];
    if (R) *R = g[0];
    if (r) *r = g[1];
}
int pc_sp_turns(int gear) { int R, r; pc_sp_gear_of(gear, &R, &r); return r / gcd(R, r); }
int pc_sp_lobes(int gear) { int R, r; pc_sp_gear_of(gear, &R, &r); return R / gcd(R, r); }
float pc_sp_pen_of(int pen) { return PEN[pen >= 0 && pen < PC_SP_PENS ? pen : 0]; }

void pc_sp_clip(float *x, float *y, float limit)
{
    float n = sqrtf(*x * *x + *y * *y);
    if (n > limit && n > 0) { *x *= limit / n; *y *= limit / n; }
}

void pc_sp_point(const pc_sp_t *s, int layer, double seg, float *x, float *y)
{
    int R, r;
    pc_sp_gear_of(s->gear, &R, &r);
    const double d0 = (double)pc_sp_pen_of(s->pen) * r;
    const double d = d0 * LAYER_PEN[layer >= 0 && layer < PC_SP_LAYERS_N ? layer : 0];
    const double t = 6.283185307179586 * seg / PC_SP_SEG_REV;
    double px, py, ext;
    if (s->epi) {
        const double a = R + r, k = a / r;
        px = a * cos(t) - d * cos(k * t); py = a * sin(t) - d * sin(k * t);
        ext = a + d0;
    } else {
        const double a = R - r, k = a / r;
        px = a * cos(t) + d * cos(k * t); py = a * sin(t) - d * sin(k * t);
        ext = a + d0;
    }
    *x = (float)(px / ext);
    *y = (float)(py / ext);
    pc_sp_clip(x, y, 1.0f);
}

void pc_sp_restart(pc_sp_t *s)
{
    s->layers = s->rule == PC_SP_LAYERS ? PC_SP_LAYERS_N : 1;
    s->total = (uint32_t)pc_sp_turns(s->gear) * PC_SP_SEG_REV;
    s->layer = 0; s->seg = 0; s->carry = 0; s->done = false;
    s->n_out = 0; s->ev_layer = s->ev_done = false;
}
void pc_sp_set(pc_sp_t *s, int gear, int pen, int epi)
{
    if (gear >= 0) s->gear = (uint8_t)pc_wrapi(gear, PC_SP_GEARS);
    if (pen >= 0) s->pen = (uint8_t)pc_wrapi(pen, PC_SP_PENS);
    if (epi >= 0) s->epi = epi != 0;
    pc_sp_restart(s);
}
void pc_sp_init(pc_sp_t *s, int rule, int speed, int colour, uint32_t seed, bool gold)
{
    *s = (pc_sp_t){ 0 };
    s->rule = (uint8_t)(rule >= 0 && rule < PC_SP_RULES ? rule : PC_SP_CLASSIC);
    s->speed = (uint8_t)(speed >= 0 && speed < PC_SP_SPEEDS ? speed : 0);
    s->colour = (uint8_t)(colour >= 0 && colour < PC_SP_COLOURS ? colour : 0);
    s->rng = seed * 2654435761u + 12345u;
    if (!s->rng) s->rng = 0x9E3779B9u;
    for (int i = 0; i < 4; i++) rnd(s);
    s->gold = gold;
    if (gold) { s->gear = GOLD_GEARS[rnd(s) % 4]; s->pen = 2; s->epi = false; }
    else { s->gear = (uint8_t)(rnd(s) % PC_SP_GEARS); s->pen = (uint8_t)(rnd(s) % PC_SP_PENS); s->epi = rnd(s) % 3 == 0; }
    pc_sp_restart(s);
}

bool pc_sp_running(const pc_sp_t *s) { return !s->done; }
float pc_sp_rate(const pc_sp_t *s, int energy)
{
    float r = SPEED_SEGS[s->speed < PC_SP_SPEEDS ? s->speed : 0];
    if (s->rule == PC_SP_MUSIC) {
        float e = (float)(energy < 0 ? 0 : energy > 255 ? 255 : energy) / 255.0f;
        r *= MUSIC_MIN + MUSIC_SPAN * e;
    }
    return r > (float)PC_SP_BURST ? (float)PC_SP_BURST : r;
}

int pc_sp_step(pc_sp_t *s, int energy)
{
    s->n_out = 0;
    s->ev_layer = s->ev_done = false;
    if (s->done) return 0;
    const float owe = s->carry + pc_sp_rate(s, energy);
    int n = (int)owe;
    s->carry = owe - (float)n;
    if (n > PC_SP_BURST) n = PC_SP_BURST;
    while (n-- > 0 && !s->done) {
        pc_sp_seg_t *o = &s->out[s->n_out++];
        o->layer = s->layer;
        pc_sp_point(s, s->layer, (double)s->seg, &o->x0, &o->y0);
        s->seg++;
        pc_sp_point(s, s->layer, (double)s->seg, &o->x1, &o->y1);
        o->t = (float)s->seg / (float)s->total;
        if (s->seg >= s->total) {
            if (s->layer + 1 < s->layers) { s->layer++; s->seg = 0; s->ev_layer = true; }
            else { s->done = true; s->ev_done = true; s->ev_layer = false; }
        }
    }
    return s->n_out;
}
