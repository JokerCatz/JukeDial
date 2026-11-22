// SPDX-License-Identifier: LGPL-2.1-or-later
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_game_maze.h"

#define RAD2DEG 57.29578f
#define GMAG    0.30f          /* the force, units a step^2 */
#define REST    0.35f          /* the bounce */
#define HALF_W  (PC_MZ_WALL_HALF + PC_MZ_MARBLE_R)
#define GRID_H  (PC_MZ_N * PC_MZ_CELL * 0.5f)

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static int rndn(pc_mz_t *g, int n) { return (int)((rnd(&g->rng) >> 8) % (uint32_t)n); }
static void ev(pc_mz_t *g, int e, int v)
{
    if (g->n_ev < (int)(sizeof g->ev / sizeof g->ev[0])) { g->ev[g->n_ev] = e; g->ev_v[g->n_ev++] = v; }
}
static int wrap360(int a) { a %= 360; return a < 0 ? a + 360 : a; }
int pc_mz_angdiff(int a, int b) { int d = wrap360(a - b); return d > 180 ? d - 360 : d; }
float pc_mz_ring_r(int k) { return 30.0f + 18.0f * (float)k; }
int pc_mz_gap_half(const pc_mz_t *g) { return PC_MZ_GAP_HALF + (g->gold ? 4 : 0); }
float pc_mz_r(const pc_mz_t *g) { return hypotf(g->x, g->y); }
int pc_mz_angle(const pc_mz_t *g) { return wrap360((int)lroundf(atan2f(g->y, g->x) * RAD2DEG)); }
bool pc_mz_running(const pc_mz_t *g) { return g->started && g->state == PC_MZ_PLAY; }
int pc_mz_cell_of(float v)
{
    int c = (int)floorf((v + GRID_H) / PC_MZ_CELL);
    return c < 0 ? 0 : c > PC_MZ_N - 1 ? PC_MZ_N - 1 : c;
}
static float cell_c(int c) { return -GRID_H + ((float)c + 0.5f) * PC_MZ_CELL; }
/* the rings the marble is clear of (centre past the wall's whole zone, with hysteresis: in a gap it keeps the level it came with) */
static int level_of(float r, int lv)
{
    while (lv < PC_MZ_RINGS && r > pc_mz_ring_r(lv) + HALF_W) lv++;
    while (lv > 0 && r < pc_mz_ring_r(lv - 1) - HALF_W) lv--;
    return lv;
}

/* ---- the tilt maze: a perfect maze (iterative DFS from the start cell), the walls as rects, holes in cells off the solution ---- */
static void gen_tilt(pc_mz_t *g)
{
    bool east[PC_MZ_N * PC_MZ_N] = { 0 }, south[PC_MZ_N * PC_MZ_N] = { 0 }, seen[PC_MZ_N * PC_MZ_N] = { 0 };
    int parent[PC_MZ_N * PC_MZ_N], stack[PC_MZ_N * PC_MZ_N], sp = 0;
    stack[sp++] = 0;
    seen[0] = true;
    parent[0] = -1;
    while (sp > 0) {
        const int c = stack[sp - 1], cx = c % PC_MZ_N, cy = c / PC_MZ_N;
        int nb[4], nn = 0;
        if (cx > 0 && !seen[c - 1]) nb[nn++] = c - 1;
        if (cx < PC_MZ_N - 1 && !seen[c + 1]) nb[nn++] = c + 1;
        if (cy > 0 && !seen[c - PC_MZ_N]) nb[nn++] = c - PC_MZ_N;
        if (cy < PC_MZ_N - 1 && !seen[c + PC_MZ_N]) nb[nn++] = c + PC_MZ_N;
        if (!nn) { sp--; continue; }
        const int n = nb[rndn(g, nn)];
        if (n == c + 1) east[c] = true; else if (n == c - 1) east[n] = true;
        else if (n == c + PC_MZ_N) south[c] = true; else south[n] = true;
        seen[n] = true;
        parent[n] = c;
        stack[sp++] = n;
    }
    const int goal = PC_MZ_N * PC_MZ_N - 1;
    bool on_path[PC_MZ_N * PC_MZ_N] = { 0 };
    for (int c = goal; c >= 0; c = parent[c]) on_path[c] = true;
    const float T = PC_MZ_WALL_T * 0.5f, E = GRID_H;
    g->n_walls = 0;
    g->wall[g->n_walls++] = (pc_mz_rect_t){ -E - T, -E - T, E + T, -E + T };
    g->wall[g->n_walls++] = (pc_mz_rect_t){ -E - T, E - T, E + T, E + T };
    g->wall[g->n_walls++] = (pc_mz_rect_t){ -E - T, -E - T, -E + T, E + T };
    g->wall[g->n_walls++] = (pc_mz_rect_t){ E - T, -E - T, E + T, E + T };
    for (int cy = 0; cy < PC_MZ_N; cy++)
        for (int cx = 0; cx < PC_MZ_N; cx++) {
            const int c = cy * PC_MZ_N + cx;
            const float x1 = -E + (float)(cx + 1) * PC_MZ_CELL, y1 = -E + (float)(cy + 1) * PC_MZ_CELL;
            if (cx < PC_MZ_N - 1 && !east[c] && g->n_walls < PC_MZ_WALLS_MAX)
                g->wall[g->n_walls++] = (pc_mz_rect_t){ x1 - T, y1 - PC_MZ_CELL - T, x1 + T, y1 + T };
            if (cy < PC_MZ_N - 1 && !south[c] && g->n_walls < PC_MZ_WALLS_MAX)
                g->wall[g->n_walls++] = (pc_mz_rect_t){ x1 - PC_MZ_CELL - T, y1 - T, x1 + T, y1 + T };
        }
    int cand[PC_MZ_N * PC_MZ_N], nc = 0;
    for (int c = 1; c < goal; c++) if (!on_path[c]) cand[nc++] = c;
    for (int i = nc - 1; i > 0; i--) { const int j = rndn(g, i + 1), t = cand[i]; cand[i] = cand[j]; cand[j] = t; }
    g->n_holes = nc < 4 ? nc : 4;
    for (int i = 0; i < g->n_holes; i++) { g->hole[i][0] = cell_c(cand[i] % PC_MZ_N); g->hole[i][1] = cell_c(cand[i] / PC_MZ_N); }
    g->start[0] = cell_c(0); g->start[1] = cell_c(0);
    g->goal[0] = cell_c(PC_MZ_N - 1); g->goal[1] = cell_c(PC_MZ_N - 1);
    g->x = g->start[0]; g->y = g->start[1];
}

void pc_mz_init(pc_mz_t *g, int rule, int drift, uint32_t seed, bool gold)
{
    *g = (pc_mz_t){ 0 };
    g->rule = (uint8_t)(rule >= 0 && rule < PC_MZ_RULES ? rule : 0);
    g->drift = (uint8_t)(drift < 0 ? 0 : drift > 2 ? 2 : drift);
    g->gold = gold;
    g->rng = seed * 2654435761u + 54321u;
    for (int i = 0; i < 4; i++) rnd(&g->rng);
    if (g->rule == PC_MZ_TILT) { gen_tilt(g); return; }
    g->theta = 90;
    g->gy = 1.0f;
    int prev = g->theta;
    for (int k = 0; k < PC_MZ_RINGS; k++) {                 /* gaps on multiples of 10, each 60+ degrees from the one inside (the first from the start's gravity) */
        int a;
        do a = rndn(g, 36) * 10; while (abs(pc_mz_angdiff(a, prev)) < 60);
        g->gap[k] = prev = a;
        g->gdir[k] = (int8_t)(rnd(&g->rng) & 0x100 ? 1 : -1);
        g->gper[k] = (uint8_t)(g->drift == 0 ? 0 : g->drift == 1 ? 20 + 4 * k : 8 + 2 * k);
    }
    if (g->rule == PC_MZ_ATTRACT) {
        int d;
        do d = rndn(g, 36) * 10; while (abs(pc_mz_angdiff(d, g->gap[PC_MZ_RINGS - 1])) < 60);   /* not at the first gap: it would be no puzzle */
        const float a = (float)d / RAD2DEG;
        g->x = 92.0f * cosf(a); g->y = 92.0f * sinf(a);
        g->level = PC_MZ_RINGS;
    } else if (g->rule == PC_MZ_REPEL) {
        int d;
        do d = rndn(g, 36) * 10; while (abs(pc_mz_angdiff(d, g->gap[0])) < 60);
        const float a = (float)d / RAD2DEG;
        g->x = 10.0f * cosf(a); g->y = 10.0f * sinf(a);
    }
}
void pc_mz_start(pc_mz_t *g) { if (g->state == PC_MZ_PLAY) g->started = true; }
void pc_mz_set_grav(pc_mz_t *g, float dx, float dy)
{
    if (g->rule != PC_MZ_TILT) return;                      /* the tilt belongs to the tilt maze: the knob owns gravity in 重力, the central rules own their force */
    float l = hypotf(dx, dy);
    if (l > 1.0f) { dx /= l; dy /= l; l = 1.0f; }
    g->gx = dx; g->gy = dy;
    if (l > PC_MZ_TILT_ON && g->state == PC_MZ_PLAY) g->started = true;
}
void pc_mz_set_theta(pc_mz_t *g, int deg)
{
    if (g->rule != PC_MZ_GRAV) return;
    g->theta = wrap360(deg);
    const float a = (float)g->theta / RAD2DEG;
    g->gx = cosf(a); g->gy = sinf(a);
}

/* the ring walls: a marble whose centre is within HALF_W of a ring's radius and outside the gap's window is pushed back to the side it came from (prev_r), its radial speed
 * reflected, its sliding slowed; returns the impact speed (0 when none) */
static float collide_rings(pc_mz_t *g, float prev_r)
{
    float impact = 0;
    for (int k = 0; k < PC_MZ_RINGS; k++) {
        const float R = pc_mz_ring_r(k);
        float r = pc_mz_r(g);
        if (fabsf(r - R) >= HALF_W) continue;
        if (abs(pc_mz_angdiff(pc_mz_angle(g), g->gap[k])) <= pc_mz_gap_half(g)) continue;
        const float side = prev_r >= R ? 1.0f : -1.0f;
        if (r < 0.001f) { g->x = 0.001f; r = 0.001f; }
        const float nx = g->x / r, ny = g->y / r, nr = R + side * HALF_W;
        g->x = nx * nr; g->y = ny * nr;
        const float vr = g->vx * nx + g->vy * ny;
        if (vr * side < 0) {
            const float tx = g->vx - vr * nx, ty = g->vy - vr * ny;
            g->vx = tx * 0.92f - vr * REST * nx;
            g->vy = ty * 0.92f - vr * REST * ny;
            if (-vr * side > impact) impact = -vr * side;
        }
    }
    return impact;
}
static float collide_walls(pc_mz_t *g)
{
    float impact = 0;
    for (int i = 0; i < g->n_walls; i++) {
        const pc_mz_rect_t *w = &g->wall[i];
        const float cx = fminf(fmaxf(g->x, w->x0), w->x1), cy = fminf(fmaxf(g->y, w->y0), w->y1);
        float dx = g->x - cx, dy = g->y - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= PC_MZ_MARBLE_R * PC_MZ_MARBLE_R) continue;
        float nx, ny, push;
        if (d2 > 1e-6f) { const float d = sqrtf(d2); nx = dx / d; ny = dy / d; push = PC_MZ_MARBLE_R - d; }
        else {                                              /* the centre is inside the rect: out by the nearest side */
            const float l = g->x - w->x0, r = w->x1 - g->x, t = g->y - w->y0, b = w->y1 - g->y;
            const float m = fminf(fminf(l, r), fminf(t, b));
            nx = ny = 0;
            if (m == l) nx = -1; else if (m == r) nx = 1; else if (m == t) ny = -1; else ny = 1;
            push = m + PC_MZ_MARBLE_R;
        }
        g->x += nx * push; g->y += ny * push;
        const float vn = g->vx * nx + g->vy * ny;
        if (vn < 0) {
            g->vx -= (1.0f + REST) * vn * nx; g->vy -= (1.0f + REST) * vn * ny;
            if (-vn > impact) impact = -vn;
        }
    }
    return impact;
}

void pc_mz_turn(pc_mz_t *g, int d)
{
    g->n_ev = 0;
    if (!d || g->state != PC_MZ_PLAY || g->rule == PC_MZ_TILT) return;
    g->started = true;
    g->turns += d < 0 ? -d : d;
    if (g->rule == PC_MZ_GRAV) {
        g->theta = wrap360(g->theta + d * PC_MZ_STEP_DEG);
        const float a = (float)g->theta / RAD2DEG;
        g->gx = cosf(a); g->gy = sinf(a);
        return;
    }
    const float a = (float)(d * PC_MZ_STEP_DEG) / RAD2DEG, c = cosf(a), s = sinf(a);   /* round its ring: the place and the speed turn, a gap-side overlap is pushed back */
    const float r0 = pc_mz_r(g), x = g->x, y = g->y, vx = g->vx, vy = g->vy;
    g->x = x * c - y * s; g->y = x * s + y * c;
    g->vx = vx * c - vy * s; g->vy = vx * s + vy * c;
    collide_rings(g, r0);
}

static void accel(const pc_mz_t *g, float *ax, float *ay)
{
    if (g->rule == PC_MZ_REPEL || g->rule == PC_MZ_ATTRACT) {
        const float r = pc_mz_r(g), f = g->rule == PC_MZ_REPEL ? GMAG : -GMAG;
        if (r < 0.5f) { *ax = f; *ay = 0; } else { *ax = f * g->x / r; *ay = f * g->y / r; }
    } else { *ax = g->gx * GMAG; *ay = g->gy * GMAG; }
}

void pc_mz_step(pc_mz_t *g)
{
    g->n_ev = 0;
    if (!pc_mz_running(g)) return;
    g->steps++;
    g->ms = (uint32_t)g->steps * 100u / 3u;
    if (g->rule != PC_MZ_TILT) {                            /* the drift: a gap moves a degree, a marble it now overlaps is pushed out to the nearer side */
        bool moved = false;
        for (int k = 0; k < PC_MZ_RINGS; k++)
            if (g->gper[k] && ++g->gacc[k] >= g->gper[k]) { g->gacc[k] = 0; g->gap[k] = wrap360(g->gap[k] + g->gdir[k]); moved = true; }
        if (moved) collide_rings(g, pc_mz_r(g));
    }
    float ax, ay;
    accel(g, &ax, &ay);
    const float damp = g->gold ? 0.975f : 0.96f;
    g->vx = (g->vx + ax) * damp; g->vy = (g->vy + ay) * damp;
    const float sp = hypotf(g->vx, g->vy);
    if (sp > PC_MZ_VMAX) { g->vx *= PC_MZ_VMAX / sp; g->vy *= PC_MZ_VMAX / sp; }
    float impact = 0;
    for (int sub = 0; sub < 2; sub++) {
        const float prev = pc_mz_r(g);
        g->x += g->vx * 0.5f; g->y += g->vy * 0.5f;
        const float im = g->rule == PC_MZ_TILT ? collide_walls(g) : collide_rings(g, prev);
        if (im > impact) impact = im;
    }
    if (impact > 1.5f) ev(g, PC_MZ_EV_BOUNCE, (int)(impact * 10.0f));
    if (g->rule == PC_MZ_TILT) {
        for (int i = 0; i < g->n_holes; i++)
            if (hypotf(g->x - g->hole[i][0], g->y - g->hole[i][1]) < 5.0f) {
                g->x = g->start[0]; g->y = g->start[1]; g->vx = g->vy = 0;
                g->falls++;
                ev(g, PC_MZ_EV_FALL, i);
                return;
            }
        if (hypotf(g->x - g->goal[0], g->y - g->goal[1]) < 7.0f) { g->state = PC_MZ_DONE; ev(g, PC_MZ_EV_DONE, 0); }
        return;
    }
    const int lv = level_of(pc_mz_r(g), g->level);
    if (lv != g->level) { g->level = lv; ev(g, PC_MZ_EV_GAP, lv); }
    if (g->rule == PC_MZ_ATTRACT ? pc_mz_r(g) <= PC_MZ_CENTRE_R : pc_mz_r(g) >= PC_MZ_EXIT_R) { g->state = PC_MZ_DONE; ev(g, PC_MZ_EV_DONE, 0); }
}
