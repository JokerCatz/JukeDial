// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_game_tower.h"
#include <math.h>

#define SWING_MAX 1.15f                 /* the pendulum's largest angle (rad) */
#define SWING_LIFT 12.0f                /* the lift of the ends over the middle, units */
#define GRAVITY 0.45f                   /* debris: units / step^2 */
#define OFF_Y 85.0f                     /* a piece below this screen y is gone */

static uint32_t rnd(pc_tw_t *g) { uint32_t x = g->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->rng = x; }
static void ev(pc_tw_t *g, int kind, float x, float y, int value)
{
    if (g->n_ev < PC_TW_EVENTS) g->ev[g->n_ev++] = (pc_tw_ev_t){ (uint8_t)kind, x, y, value };
}
static int top_i(const pc_tw_t *g) { return (g->n - 1) % PC_TW_VIS; }

float pc_tw_cap(const pc_tw_t *g, int height)
{
    if (g->rule != PC_TW_NARROW) return PC_TW_W0;
    float c = PC_TW_W0 * exp2f(-(float)height / (float)PC_TW_MILE);
    return c < PC_TW_MINW ? PC_TW_MINW : c;
}
int pc_tw_height(const pc_tw_t *g) { return g->n - 1; }
int pc_tw_level(const pc_tw_t *g) { return pc_tw_height(g) / PC_TW_MILE + 1; }
float pc_tw_tol(const pc_tw_t *g) { return 2.0f + 0.5f * g->vx; }
float pc_tw_floor_y(const pc_tw_t *g, int f) { return PC_TW_BASE_Y - (float)(f + 1) * PC_TW_H + g->cam; }
float pc_tw_slider_y(const pc_tw_t *g) { return pc_tw_floor_y(g, g->n) + g->dy; }

static void set_swing(pc_tw_t *g)
{
    float th = SWING_MAX * sinf(g->phase);
    g->sx = PC_TW_RANGE * sinf(th) / sinf(SWING_MAX);
    g->dy = -SWING_LIFT * (1.0f - cosf(th)) / (1.0f - cosf(SWING_MAX));
}
/* the next block: as wide as the top (within the cap), starting at the side the last one did not, a golden one now and then */
static void spawn(pc_tw_t *g)
{
    float cap = pc_tw_cap(g, pc_tw_height(g)), tw = g->w[top_i(g)];
    g->sw = tw < cap ? tw : cap;
    g->gold = rnd(g) % PC_TW_GOLD_N == 0;
    const bool right = g->n & 1;
    g->dy = 0;
    if (g->rule == PC_TW_SWING) { g->phase = right ? 1.5707963f : -1.5707963f; set_swing(g); }
    else { g->sx = right ? PC_TW_RANGE : -PC_TW_RANGE; g->dir = right ? -1.0f : 1.0f; }
}
void pc_tw_init(pc_tw_t *g, int rule, uint32_t seed)
{
    *g = (pc_tw_t){ 0 };
    g->rule = (uint8_t)(rule >= 0 && rule < PC_TW_RULES ? rule : PC_TW_CLASSIC);
    g->rng = seed * 2654435761u + 12345u;
    if (!g->rng) g->rng = 0x9E3779B9u;
    for (int i = 0; i < 4; i++) rnd(g);
    g->speed = 1.0f;
    g->n = 1;
    g->w[0] = PC_TW_W0;
    spawn(g);
}
bool pc_tw_running(const pc_tw_t *g)
{
    if (g->state == PC_TW_PLAY) return true;
    for (int i = 0; i < PC_TW_DEBRIS; i++) if (g->deb[i].live) return true;
    return g->cam != g->cam_to;
}

void pc_tw_step(pc_tw_t *g)
{
    g->n_ev = 0;
    if (g->state == PC_TW_PLAY) {
        const float before = g->sx, h = (float)pc_tw_height(g);
        if (g->rule == PC_TW_SWING) {
            float w = 0.075f + 0.0025f * h;
            g->phase += (w > 0.16f ? 0.16f : w) * g->speed;
            set_swing(g);
        } else {
            float v = 1.2f + 0.05f * h;
            g->sx += g->dir * (v > 3.4f ? 3.4f : v) * g->speed;
            if (g->sx >= PC_TW_RANGE) { g->sx = PC_TW_RANGE; g->dir = -1.0f; }
            else if (g->sx <= -PC_TW_RANGE) { g->sx = -PC_TW_RANGE; g->dir = 1.0f; }
        }
        g->vx = fabsf(g->sx - before);
    }
    float d = g->cam_to - g->cam;
    g->cam = (d > -0.05f && d < 0.05f) ? g->cam_to : g->cam + d * 0.25f;
    for (int i = 0; i < PC_TW_DEBRIS; i++) {
        pc_tw_deb_t *b = &g->deb[i];
        if (!b->live) continue;
        b->vy += GRAVITY;
        b->wy -= b->vy;
        if (PC_TW_BASE_Y - (b->wy + PC_TW_H) + g->cam > OFF_Y) b->live = false;
    }
}

static void debris(pc_tw_t *g, float x, float w)
{
    if (w < 0.1f) return;
    int k = 0;
    for (int i = 0; i < PC_TW_DEBRIS; i++) {            /* a free slot, else the one that has fallen the furthest */
        if (!g->deb[i].live) { k = i; break; }
        if (g->deb[i].wy < g->deb[k].wy) k = i;
    }
    g->deb[k] = (pc_tw_deb_t){ true, x, w, (float)g->n * PC_TW_H, 0 };
}

bool pc_tw_tap(pc_tw_t *g)
{
    g->n_ev = 0;
    if (g->state == PC_TW_WAIT) { g->state = PC_TW_PLAY; return true; }
    if (g->state != PC_TW_PLAY) return false;
    const int t = top_i(g), f = g->n % PC_TW_VIS;
    const float tx = g->cx[t], tw = g->w[t], d = g->sx - tx, ad = fabsf(d);
    const float left = fmaxf(g->sx - g->sw / 2, tx - tw / 2), right = fminf(g->sx + g->sw / 2, tx + tw / 2);
    const float ov = right - left, cut = g->sw - (ov > 0 ? ov : 0);
    const float y = pc_tw_floor_y(g, g->n);
    if (ov < 1.0f) {                                     /* nothing (or a sliver) touches: the whole block falls */
        debris(g, g->sx, g->sw);
        g->state = PC_TW_OVER;
        g->combo = 0;
        ev(g, PC_TW_EV_OVER, g->sx, y, pc_tw_height(g));
        return true;
    }
    float cx, w;
    int kind = PC_TW_K_NORMAL;
    const bool perfect = cut <= pc_tw_tol(g);
    const float cap = pc_tw_cap(g, pc_tw_height(g));
    if (g->gold) {                                       /* the golden block: the full width back, no cut */
        cx = (left + right) / 2;
        w = cap;
        kind = PC_TW_K_GOLD;
        g->golds++;
        g->combo = perfect ? g->combo + 1 : 0;
        if (g->combo > g->combo_best) g->combo_best = g->combo;
    } else if (perfect) {
        cx = ad <= pc_tw_tol(g) ? tx : g->sx;
        w = g->sw;
        kind = PC_TW_K_PERFECT;
        g->combo++;
        g->perfects++;
        if (g->combo > g->combo_best) g->combo_best = g->combo;
        if (g->combo >= 2) w = fminf(w + PC_TW_GROW, cap);
    } else {
        cx = (left + right) / 2;
        w = ov;
        g->combo = 0;
        if (d > 0) debris(g, (right + g->sx + g->sw / 2) / 2, g->sx + g->sw / 2 - right);
        else debris(g, (left + g->sx - g->sw / 2) / 2, left - (g->sx - g->sw / 2));
    }
    g->cx[f] = cx; g->w[f] = w; g->kind[f] = (uint8_t)kind;
    g->n++;
    g->cam_to = fmaxf(0.0f, (float)g->n * PC_TW_H - (PC_TW_BASE_Y - PC_TW_TOP_Y));
    ev(g, PC_TW_EV_DROP, cx, y, (int)lroundf(cut));
    if (kind == PC_TW_K_PERFECT) {
        ev(g, PC_TW_EV_PERFECT, cx, y, g->combo);
        if (g->combo >= 2) ev(g, PC_TW_EV_COMBO, cx, y, g->combo);
    }
    if (kind == PC_TW_K_GOLD) ev(g, PC_TW_EV_GOLD, cx, y, g->golds);
    if (pc_tw_height(g) % PC_TW_MILE == 0) ev(g, PC_TW_EV_MILE, 0, y, pc_tw_height(g));
    spawn(g);
    return true;
}
