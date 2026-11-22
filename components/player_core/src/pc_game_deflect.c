// SPDX-License-Identifier: LGPL-2.1-or-later
/* 旋轉擋板 / Rotating deflector: rules and geometry (pc_game_deflect.h). */
#include "pc/pc_game_deflect.h"
#include <math.h>
#include <string.h>

#define DEG 57.2957795f
#define RAD 0.0174532925f
static const float SPEED[PC_DF_SPEEDS] = { 0.8f, 1.1f, 1.5f };   /* units a step: slow, normal, fast */
#define SPAWN_FIRST 20
#define LIVE_MAX    5                                              /* shots in flight at most */

static float wrap360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
float pc_df_angdiff(float a, float b) { float d = fmodf(a - b, 360.0f); if (d > 180.0f) d -= 360.0f; else if (d < -180.0f) d += 360.0f; return d; }
static float deg_of(float x, float y) { return wrap360(atan2f(y, x) * DEG); }
static float len(float x, float y) { return sqrtf(x * x + y * y); }
static uint32_t rnd(pc_df_t *g) { uint32_t x = g->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->rng = x; }

static void ev(pc_df_t *g, int kind, int value, float x, float y)
{
    if (g->n_ev >= PC_DF_EVENTS) return;
    g->ev[g->n_ev++] = (pc_df_ev_t){ .kind = (uint8_t)kind, .value = (int16_t)value, .x = x, .y = y };
}

/* ---- the guard line, by shape ---- */
float pc_df_guard_dist(int shape, float deg)
{
    if (shape != PC_DF_SQUARE) return PC_DF_GUARD_R;
    float c = fabsf(cosf(deg * RAD)), s = fabsf(sinf(deg * RAD)), m = c > s ? c : s;
    return PC_DF_GUARD_Q / m;
}
void pc_df_guard_pt(int shape, float deg, float *x, float *y)
{
    float d = pc_df_guard_dist(shape, deg);
    *x = d * cosf(deg * RAD);
    *y = d * sinf(deg * RAD);
}
void pc_df_guard_normal(int shape, float x, float y, float *nx, float *ny)
{
    if (shape != PC_DF_SQUARE) {
        float l = len(x, y);
        if (l < 1e-6f) { *nx = 1; *ny = 0; return; }
        *nx = x / l; *ny = y / l;
        return;
    }
    if (fabsf(x) >= fabsf(y)) { *nx = x >= 0 ? 1.0f : -1.0f; *ny = 0; }
    else { *nx = 0; *ny = y >= 0 ? 1.0f : -1.0f; }
}
int pc_df_shield_segs(int shape, float c_deg, float half, float seg[PC_DF_SEGS][4])
{
    if (shape != PC_DF_SQUARE) return 0;
    float a[PC_DF_SEGS + 2];
    int n = 0;
    const float a0 = c_deg - half, a1 = c_deg + half;
    a[n++] = a0;
    for (int k = -4; k <= 8 && n < PC_DF_SEGS + 1; k++) {                   /* the corners (45 + 90 k degrees) strictly inside the span */
        float ang = 45.0f + 90.0f * (float)k;
        if (ang > a0 + 0.01f && ang < a1 - 0.01f) a[n++] = ang;
    }
    a[n++] = a1;
    int m = 0;
    for (int i = 0; i + 1 < n && m < PC_DF_SEGS; i++, m++) {
        pc_df_guard_pt(shape, a[i], &seg[m][0], &seg[m][1]);
        pc_df_guard_pt(shape, a[i + 1], &seg[m][2], &seg[m][3]);
    }
    return m;
}

/* ---- the game ---- */
int pc_df_shields(const pc_df_t *g) { return g->rule == PC_DF_TWIN ? 2 : 1; }
bool pc_df_running(const pc_df_t *g) { return g->state == PC_DF_PLAY; }
float pc_df_half(const pc_df_t *g)
{
    float grow = PC_DF_GROW_DEG * (float)g->combo;
    if (grow > PC_DF_GROW_MAX) grow = PC_DF_GROW_MAX;
    return (g->rule == PC_DF_TWIN ? PC_DF_HALF_TWIN : PC_DF_HALF_DEG) + grow;
}
float pc_df_speed(const pc_df_t *g)
{
    float lv = 1.0f + 0.04f * (float)(g->level - 1);
    return SPEED[g->speed] * (lv > 2.0f ? 2.0f : lv);
}
bool pc_df_covers(const pc_df_t *g, float deg)
{
    const float h = pc_df_half(g) + PC_DF_SLACK_DEG;
    for (int k = 0; k < pc_df_shields(g); k++)
        if (fabsf(pc_df_angdiff(deg, g->deg + 180.0f * (float)k)) <= h) return true;
    return false;
}

void pc_df_init(pc_df_t *g, int rule, int speed, int shape, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = rule < 0 || rule >= PC_DF_RULES ? 0 : rule;
    g->speed = speed < 0 || speed >= PC_DF_SPEEDS ? 1 : speed;
    g->shape = shape == PC_DF_SQUARE ? PC_DF_SQUARE : PC_DF_ROUND;
    g->state = PC_DF_SERVE;
    g->level = 1;
    g->lives = PC_DF_LIVES;
    g->deg = 270.0f;
    g->rng = seed ? seed : 0x2545F491u;
    g->spawn_in = SPAWN_FIRST;
}
bool pc_df_start(pc_df_t *g)
{
    g->n_ev = 0;
    if (g->state != PC_DF_SERVE) return false;
    g->state = PC_DF_PLAY;
    ev(g, PC_DF_EV_START, 0, 0, 0);
    return true;
}
void pc_df_move(pc_df_t *g, float deg) { g->deg = wrap360(g->deg + deg); }

static int live_n(const pc_df_t *g) { int n = 0; for (int i = 0; i < PC_DF_SHOTS; i++) n += g->shot[i].live; return n; }
static void spawn(pc_df_t *g)
{
    for (int i = 0; i < PC_DF_SHOTS; i++) {
        pc_df_shot_t *s = &g->shot[i];
        if (s->live) continue;
        float a = (float)(rnd(g) % 3600) * 0.1f;                                      /* 0.1 degree steps */
        float jit = ((float)(rnd(g) % 201) - 100.0f) * 0.1f;                          /* the aim is off the centre by up to 10 degrees */
        float d = (a + 180.0f + jit) * RAD, sp = pc_df_speed(g);
        *s = (pc_df_shot_t){ .x = PC_DF_SPAWN_R * cosf(a * RAD), .y = PC_DF_SPAWN_R * sinf(a * RAD), .vx = sp * cosf(d), .vy = sp * sinf(d), .live = true,
                             .gold = rnd(g) % PC_DF_GOLD_N == 0 };
        return;
    }
}
static int interval(const pc_df_t *g) { int v = 45 - 3 * (g->level - 1); return v < 16 ? 16 : v; }

static void bounce(pc_df_t *g, pc_df_shot_t *s)
{
    float nx, ny;
    pc_df_guard_normal(g->shape, s->x, s->y, &nx, &ny);
    float d = s->vx * nx + s->vy * ny;
    s->vx -= 2.0f * d * nx;
    s->vy -= 2.0f * d * ny;
    float gd = pc_df_guard_dist(g->shape, deg_of(s->x, s->y)), a = deg_of(s->x, s->y) * RAD;
    s->x = (gd + 2.0f) * cosf(a);                                                     /* just outside the guard line: no second contact */
    s->y = (gd + 2.0f) * sinf(a);
    s->back = true;
    g->combo++;
    g->bounces++;
    if (g->combo > g->combo_best) g->combo_best = g->combo;
    int pts = 10 * (1 + g->combo / 5);
    g->score += (uint32_t)pts;
    ev(g, PC_DF_EV_BOUNCE, g->combo, s->x, s->y);
    if (s->gold) {
        g->golds++;
        g->score += 50;
        if (g->lives < PC_DF_LIVES_MAX) g->lives++;
        ev(g, PC_DF_EV_GOLD, g->lives, s->x, s->y);
        ev(g, PC_DF_EV_LIFE, g->lives, s->x, s->y);
    }
    if (g->combo % 5 == 0) ev(g, PC_DF_EV_COMBO, g->combo, s->x, s->y);
    int lv = 1 + g->bounces / 10;
    if (lv != g->level) { g->level = lv; ev(g, PC_DF_EV_LEVEL, lv, 0, 0); }
}

static void move_shot(pc_df_t *g, pc_df_shot_t *s)
{
    const float sp = len(s->vx, s->vy);
    const int n = sp <= 1.0f ? 1 : (int)ceilf(sp);
    for (int k = 0; k < n && s->live && g->state == PC_DF_PLAY; k++) {
        s->x += s->vx / (float)n;
        s->y += s->vy / (float)n;
        const float r = len(s->x, s->y);
        if (s->back) { if (r > PC_DF_SPAWN_R + 4.0f) s->live = false; continue; }
        if (!s->past) {
            const float a = deg_of(s->x, s->y);
            if (r <= pc_df_guard_dist(g->shape, a)) {
                if (pc_df_covers(g, a)) { bounce(g, s); continue; }
                s->past = true;
            }
        }
        if (s->past && r <= PC_DF_CORE_R) {
            s->live = false;
            if (s->gold) { ev(g, PC_DF_EV_MISS, 1, s->x, s->y); continue; }           /* a golden shot missed costs nothing */
            g->lives--;
            g->misses++;
            g->combo = 0;
            ev(g, PC_DF_EV_MISS, 0, s->x, s->y);
            if (g->lives <= 0) { g->state = PC_DF_OVER; ev(g, PC_DF_EV_OVER, 0, 0, 0); }
        }
    }
}

void pc_df_step(pc_df_t *g)
{
    g->n_ev = 0;
    if (g->state != PC_DF_PLAY) return;
    g->ticks++;
    if (--g->spawn_in <= 0) {
        if (live_n(g) < LIVE_MAX) spawn(g);
        g->spawn_in = interval(g);
    }
    for (int i = 0; i < PC_DF_SHOTS && g->state == PC_DF_PLAY; i++)
        if (g->shot[i].live) move_shot(g, &g->shot[i]);
    if (g->rule == PC_DF_REBOUND && g->state == PC_DF_PLAY) {                         /* a bounced shot against any other shot */
        for (int i = 0; i < PC_DF_SHOTS; i++) {
            pc_df_shot_t *b = &g->shot[i];
            if (!b->live || !b->back) continue;
            for (int j = 0; j < PC_DF_SHOTS && b->live; j++) {
                pc_df_shot_t *c = &g->shot[j];
                if (j == i || !c->live) continue;
                if (len(b->x - c->x, b->y - c->y) > 2.0f * PC_DF_SHOT_R) continue;
                g->score += PC_DF_BONUS;
                ev(g, PC_DF_EV_POP, PC_DF_BONUS, (b->x + c->x) * 0.5f, (b->y + c->y) * 0.5f);
                b->live = c->live = false;
            }
        }
    }
}
