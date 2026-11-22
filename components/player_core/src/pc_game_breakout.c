// SPDX-License-Identifier: LGPL-2.1-or-later
/* 環形打磚塊 / Ring breakout: rules and physics (pc_game_breakout.h). */
#include "pc/pc_game_breakout.h"
#include <math.h>
#include <string.h>

#define DEG   57.2957795f
#define RAD   0.0174532925f
#define SPAN  (360.0f / PC_BO_PER_RING)

static const float SPEED[PC_BO_SPEEDS] = { 1.2f, 1.55f, 1.95f };

static float wrap360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
float pc_bo_angdiff(float a, float b) { float d = fmodf(a - b, 360.0f); if (d > 180.0f) d -= 360.0f; else if (d < -180.0f) d += 360.0f; return d; }
static float deg_of(float x, float y) { return wrap360(atan2f(y, x) * DEG); }
static float len(float x, float y) { return sqrtf(x * x + y * y); }
static uint32_t rnd(pc_bo_t *g) { uint32_t x = g->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g->rng = x; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ev(pc_bo_t *g, int kind, int brick, int value, float x, float y)
{
    if (g->n_ev >= PC_BO_EVENTS) return;
    g->ev[g->n_ev++] = (pc_bo_ev_t){ .kind = (uint8_t)kind, .brick = (int16_t)brick, .value = (int16_t)value, .x = x, .y = y };
}

float pc_bo_half(const pc_bo_t *g) { return (g->rule == PC_BO_TWIN ? PC_BO_HALF_TWIN : PC_BO_HALF_DEG) * (g->wide_ticks > 0 ? PC_BO_WIDE : 1.0f); }
int pc_bo_paddles(const pc_bo_t *g) { return g->rule == PC_BO_TWIN ? 2 : 1; }
bool pc_bo_running(const pc_bo_t *g) { return g->state == PC_BO_PLAY || g->state == PC_BO_CLEAR; }
float pc_bo_speed(const pc_bo_t *g)
{
    float lv = 1.0f + 0.05f * (float)(g->level - 1);
    return SPEED[g->speed] * (lv > 1.5f ? 1.5f : lv) * (g->slow_ticks > 0 ? 0.7f : 1.0f);
}

void pc_bo_reflect(float vx, float vy, float nx, float ny, float *ox, float *oy)
{
    float d = vx * nx + vy * ny;
    *ox = vx - 2.0f * d * nx;
    *oy = vy - 2.0f * d * ny;
}

void pc_bo_paddle_dir(float x, float y, float vx, float vy, float pad_deg, float half_deg, float *ox, float *oy)
{
    float r = len(x, y);
    if (r < 1e-3f) r = 1e-3f;
    float nx = -x / r, ny = -y / r, rx, ry, sp = len(vx, vy);
    pc_bo_reflect(vx, vy, nx, ny, &rx, &ry);
    float o = clampf(pc_bo_angdiff(deg_of(x, y), pad_deg) / (half_deg > 0 ? half_deg : 1.0f), -1.0f, 1.0f);
    float phin = atan2f(ny, nx) * DEG;
    float phi = atan2f(ry, rx) * DEG - o * PC_BO_ENGLISH_DEG;   /* an end sends the ball to its side: the counter-clockwise end turns it clockwise-wards of the normal */
    float d = clampf(pc_bo_angdiff(phi, phin), -PC_BO_MAX_OUT_DEG, PC_BO_MAX_OUT_DEG);
    phi = (phin + d) * RAD;
    *ox = sp * cosf(phi);
    *oy = sp * sinf(phi);
}

bool pc_bo_brick_sector(const pc_bo_t *g, int i, float *r_in, float *r_out, float *a0, float *a1)
{
    if (i < 0 || i >= PC_BO_BRICKS || g->brick[i].kind == PC_BK_NONE) return false;
    int k = i / PC_BO_PER_RING, j = i % PC_BO_PER_RING;
    *r_in = PC_BO_RING_IN + (float)k * (PC_BO_RING_T + PC_BO_RING_GAP);
    *r_out = *r_in + PC_BO_RING_T;
    *a0 = wrap360(g->rot[k] + (float)j * SPAN + PC_BO_BRICK_GAP / 2);
    *a1 = *a0 + SPAN - PC_BO_BRICK_GAP;
    return true;
}

/* ---- the ball on the paddle ---- */
static void place(pc_bo_ball_t *b, float deg, float r) { b->x = r * cosf(deg * RAD); b->y = r * sinf(deg * RAD); }
static void rest(pc_bo_t *g)                                 /* the ball waits on the paddle; nothing else in flight */
{
    memset(g->ball, 0, sizeof g->ball);
    memset(g->drop, 0, sizeof g->drop);
    g->ball[0].live = true;
    place(&g->ball[0], g->paddle_deg, PC_BO_PAD_IN - PC_BO_BALL_R - 0.5f);
    g->wide_ticks = g->slow_ticks = 0;
    g->combo = 0;
    g->catches = 0;                                                             /* 黏球: PC_BO_CATCHES again for this ball */
    g->state = PC_BO_SERVE;
}

void pc_bo_level(pc_bo_t *g, int level)
{
    g->level = level < 1 ? 1 : level;
    memset(g->brick, 0, sizeof g->brick);
    const bool boss = g->level % 5 == 0;
    const int rings = g->level == 1 ? 2 : PC_BO_RINGS;
    for (int k = 0; k < rings; k++)
        for (int j = 0; j < PC_BO_PER_RING; j++) {
            pc_bo_brick_t *b = &g->brick[k * PC_BO_PER_RING + j];
            if (boss && k == 1) continue;                    /* the boss: the middle ring is open, the outer a wall */
            if (boss && k == 2) { *b = (pc_bo_brick_t){ PC_BK_BOSS, 3, 3, 0 }; continue; }
            *b = (pc_bo_brick_t){ PC_BK_NORMAL, 1, 1, 0 };
            if (g->level >= 2 && k == rings - 1 && j % 3 == 0) *b = (pc_bo_brick_t){ PC_BK_TOUGH, 2, 2, 0 };
        }
    /* boom, power and golden bricks on plain ones, by the seed */
    int booms = g->level >= 2 ? 2 : 0, powers = g->level == 1 ? 2 : 3, tries = 0;
    while ((booms || powers) && tries++ < 200) {
        int i = (int)(rnd(g) % (uint32_t)(rings * PC_BO_PER_RING));
        pc_bo_brick_t *b = &g->brick[i];
        if (b->kind != PC_BK_NORMAL) continue;
        if (booms) { b->kind = PC_BK_BOOM; booms--; }
        else { b->kind = PC_BK_POWER; b->power = (uint8_t)(rnd(g) % PC_PW_N); powers--; }
    }
    if (rnd(g) % PC_BO_GOLD_N == 0)
        for (int t = 0; t < 50; t++) {
            int i = (int)(rnd(g) % PC_BO_PER_RING);          /* ring 0: the hardest to reach */
            if (g->brick[i].kind == PC_BK_NORMAL) { g->brick[i].kind = PC_BK_GOLD; break; }
        }
    g->bricks_left = 0;
    for (int i = 0; i < PC_BO_BRICKS; i++) g->bricks_left += g->brick[i].kind != PC_BK_NONE;
    float s = g->level >= 3 ? 0.25f + 0.04f * (float)(g->level - 3) : 0.0f;
    if (s > 0.8f) s = 0.8f;
    for (int k = 0; k < PC_BO_RINGS; k++) { g->rot[k] = 0; g->spin[k] = (k & 1 ? -s : s) * (1.0f + 0.2f * (float)k); }
    if (boss) g->spin[2] = (g->level / 5) & 1 ? 1.2f : -1.2f;
    rest(g);
}

void pc_bo_init(pc_bo_t *g, int rule, int speed, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = rule < 0 || rule >= PC_BO_RULES ? 0 : rule;
    g->speed = speed < 0 || speed >= PC_BO_SPEEDS ? 1 : speed;
    g->rng = seed ? seed : 0x9E3779B9u;
    g->lives = PC_BO_LIVES;
    g->paddle_deg = 90.0f;
    pc_bo_level(g, 1);
}

static int paddle_of(const pc_bo_t *g, float deg, float margin)   /* the paddle (0, 1) whose span (+ margin) holds deg, -1 none */
{
    for (int k = 0; k < pc_bo_paddles(g); k++)
        if (fabsf(pc_bo_angdiff(deg, g->paddle_deg + 180.0f * (float)k)) <= pc_bo_half(g) + margin) return k;
    return -1;
}
static float pad_deg(const pc_bo_t *g, int k) { return wrap360(g->paddle_deg + 180.0f * (float)k); }

static void set_speed(pc_bo_ball_t *b, float sp) { float l = len(b->vx, b->vy); if (l > 1e-6f) { b->vx *= sp / l; b->vy *= sp / l; } }

static void release(pc_bo_t *g, pc_bo_ball_t *b)
{
    float r = len(b->x, b->y), sp = pc_bo_speed(g);
    pc_bo_paddle_dir(b->x, b->y, b->x / r * sp, b->y / r * sp, g->paddle_deg, pc_bo_half(g), &b->vx, &b->vy);   /* as if it came straight out: aimed by where it sits */
    b->stuck = false;
    b->stuck_ticks = 0;
    ev(g, PC_EV_RELEASE, -1, 0, b->x, b->y);
}

bool pc_bo_serve(pc_bo_t *g)
{
    if (g->state == PC_BO_SERVE) {
        pc_bo_ball_t *b = &g->ball[0];
        float a = deg_of(-b->x, -b->y) + (float)((int)(rnd(g) % 41) - 20);   /* inward, up to 20 degrees off */
        if (fabsf(a - deg_of(-b->x, -b->y)) < 4.0f) a += 6.0f;               /* never straight through the centre */
        float sp = pc_bo_speed(g);
        b->vx = sp * cosf(a * RAD);
        b->vy = sp * sinf(a * RAD);
        g->state = PC_BO_PLAY;
        g->n_ev = 0;
        ev(g, PC_EV_RELEASE, -1, 0, b->x, b->y);
        return true;
    }
    if (g->state != PC_BO_PLAY) return false;
    bool any = false;
    for (int k = 0; k < PC_BO_BALLS; k++)
        if (g->ball[k].live && g->ball[k].stuck) { if (!any) g->n_ev = 0; release(g, &g->ball[k]); any = true; }
    return any;
}

void pc_bo_move(pc_bo_t *g, float deg)
{
    g->paddle_deg = wrap360(g->paddle_deg + deg);
    for (int k = 0; k < PC_BO_BALLS; k++) {
        pc_bo_ball_t *b = &g->ball[k];
        if (!b->live) continue;
        if (g->state == PC_BO_SERVE && k == 0) place(b, g->paddle_deg, PC_BO_PAD_IN - PC_BO_BALL_R - 0.5f);
        else if (b->stuck) place(b, g->paddle_deg + b->stuck_deg, PC_BO_PAD_IN - PC_BO_BALL_R);
    }
}

/* ---- bricks ---- */
static void brick_mid(const pc_bo_t *g, int i, float *x, float *y, float *deg, float *r)
{
    float r0, r1, a0, a1;
    if (!pc_bo_brick_sector(g, i, &r0, &r1, &a0, &a1)) { *x = *y = *deg = *r = 0; return; }
    *deg = wrap360((a0 + a1) / 2);
    *r = (r0 + r1) / 2;
    *x = *r * cosf(*deg * RAD);
    *y = *r * sinf(*deg * RAD);
}
static int brick_at(const pc_bo_t *g, int ring, float deg)    /* the brick of `ring` whose slot holds deg (gap or not) */
{
    if (ring < 0 || ring >= PC_BO_RINGS) return -1;
    int j = (int)floorf(wrap360(deg - g->rot[ring]) / SPAN) % PC_BO_PER_RING;
    return ring * PC_BO_PER_RING + j;
}

/* breaks brick i (a hit's last, or a boom's neighbour) and what its booms reach: the combo grows with every brick */
static void smash(pc_bo_t *g, int first)
{
    int stack[PC_BO_BRICKS], n = 0;
    uint64_t pushed = 1ull << first;
    stack[n++] = first;
    while (n) {
        int i = stack[--n];
        pc_bo_brick_t b = g->brick[i];
        if (b.kind == PC_BK_NONE) continue;
        float x, y, deg, r;
        brick_mid(g, i, &x, &y, &deg, &r);
        g->brick[i] = (pc_bo_brick_t){ 0 };
        g->bricks_left--;
        g->broken++;
        g->combo++;
        if (g->combo > g->combo_best) g->combo_best = g->combo;
        g->score += 10u * (uint32_t)g->combo;
        ev(g, PC_EV_BREAK, i, g->combo, x, y);
        if (g->combo % 5 == 0) ev(g, PC_EV_COMBO, i, g->combo, x, y);
        if (b.kind == PC_BK_GOLD) {
            g->score += 100;
            ev(g, PC_EV_GOLD, i, 100, x, y);
            if (g->lives < PC_BO_LIVES_MAX) { g->lives++; ev(g, PC_EV_LIFE, i, g->lives, x, y); }
        }
        if (b.kind == PC_BK_POWER)
            for (int d = 0; d < PC_BO_DROPS; d++)
                if (!g->drop[d].live) { g->drop[d] = (pc_bo_drop_t){ .r = r, .deg = deg, .power = b.power, .live = true }; break; }
        if (b.kind == PC_BK_BOOM) {
            ev(g, PC_EV_BOOM, i, 0, x, y);
            int k = i / PC_BO_PER_RING, j = i % PC_BO_PER_RING;
            int nb[4] = { k * PC_BO_PER_RING + (j + 1) % PC_BO_PER_RING, k * PC_BO_PER_RING + (j + PC_BO_PER_RING - 1) % PC_BO_PER_RING,
                          brick_at(g, k - 1, deg), brick_at(g, k + 1, deg) };
            for (int q = 0; q < 4; q++)
                if (nb[q] >= 0 && g->brick[nb[q]].kind && !(pushed >> nb[q] & 1)) { pushed |= 1ull << nb[q]; stack[n++] = nb[q]; }
        }
    }
}
static void hit(pc_bo_t *g, int i)
{
    pc_bo_brick_t *b = &g->brick[i];
    if (b->hp > 1) {
        float x, y, deg, r;
        brick_mid(g, i, &x, &y, &deg, &r);
        b->hp--;
        g->score += 5;
        ev(g, PC_EV_HIT, i, b->hp, x, y);
        return;
    }
    smash(g, i);
}

/* the ball against every brick. The face of least overlap is the one touched (inner arc, outer arc, a side; off both ranges: the nearest corner, a true
 * distance check). The collision response is ALWAYS there (fix round 2 ruling): a touching ball is pushed out, and a ball moving INTO the face is
 * mirrored; a face that comes into a ball already leaving it (a turning ring sweeping a ball) only pushes it. Only the DAMAGE is limited, one per
 * contact (fix round 1 I2): the brick just hit (b->touch) loses no more hit points until the ball is clear of it by TOUCH_CLEAR units, or out of the
 * brick band, or at the centre. true: the ball bounced off a brick (one a sub-step) */
#define TOUCH_CLEAR 1.5f
static bool near_brick(const pc_bo_t *g, int i, const pc_bo_ball_t *b, float rad)
{
    float r0, r1, a0, a1, r = len(b->x, b->y);
    if (r < 1.0f || !pc_bo_brick_sector(g, i, &r0, &r1, &a0, &a1)) return false;
    float half = (a1 - a0) / 2;
    return r + rad > r0 && r - rad < r1 && fabsf(pc_bo_angdiff(deg_of(b->x, b->y), a0 + half)) < half + rad / r * DEG;
}
static bool bricks(pc_bo_t *g, pc_bo_ball_t *b)
{
    float r = len(b->x, b->y);
    const float outer = PC_BO_RING_IN + PC_BO_RINGS * (PC_BO_RING_T + PC_BO_RING_GAP);
    if (r < 1.0f || r - PC_BO_BALL_R > outer) { b->touch = 0; return false; }   /* out of the band (or at the centre): every brick may be hit again (fix round 2 N1) */
    const float bd = deg_of(b->x, b->y), md = PC_BO_BALL_R / r * DEG;
    const int t = b->touch - 1;
    if (t >= 0 && !near_brick(g, t, b, PC_BO_BALL_R + TOUCH_CLEAR)) b->touch = 0;   /* clear of the last one: it may be hit again */
    for (int i = 0; i < PC_BO_BRICKS; i++) {
        float r0, r1, a0, a1;
        if (!pc_bo_brick_sector(g, i, &r0, &r1, &a0, &a1)) continue;
        if (r + PC_BO_BALL_R <= r0 || r - PC_BO_BALL_R >= r1) continue;
        float half = (a1 - a0) / 2, d = pc_bo_angdiff(bd, a0 + half);
        if (fabsf(d) >= half + md) continue;
        float nx, ny, push, ux = b->x / r, uy = b->y / r;
        const bool in_r = r >= r0 && r <= r1, in_a = fabsf(d) <= half;
        if (!in_r && !in_a) {                                                      /* past a corner: the distance to the corner itself */
            float cr = r < r0 ? r0 : r1, ca = (d < 0 ? a0 : a1) * RAD, cx = cr * cosf(ca), cy = cr * sinf(ca), dx = b->x - cx, dy = b->y - cy, dist = len(dx, dy);
            if (dist >= PC_BO_BALL_R || dist < 1e-4f) continue;
            nx = dx / dist; ny = dy / dist; push = PC_BO_BALL_R - dist;
        } else {
            float pen[4] = { r + PC_BO_BALL_R - r0, r1 - (r - PC_BO_BALL_R), (d + half + md) * r * RAD, (half + md - d) * r * RAD };
            int f = 0;
            for (int q = 1; q < 4; q++) if (pen[q] < pen[f]) f = q;
            if (f == 0) { nx = -ux; ny = -uy; }                                   /* the inner arc: outward of the brick = towards the centre */
            else if (f == 1) { nx = ux; ny = uy; }                                /* the outer arc */
            else if (f == 2) { nx = sinf(a0 * RAD); ny = -cosf(a0 * RAD); }      /* the side at a0: towards smaller angles */
            else { nx = -sinf(a1 * RAD); ny = cosf(a1 * RAD); }                   /* the side at a1 */
            push = pen[f];
        }
        b->x += nx * push;
        b->y += ny * push;
        if (b->vx * nx + b->vy * ny >= 0) continue;                                /* leaving it: pushed out only */
        pc_bo_reflect(b->vx, b->vy, nx, ny, &b->vx, &b->vy);                       /* the bounce, always */
        if (b->touch != i + 1) { b->touch = i + 1; hit(g, i); }                    /* the damage, once a contact */
        return true;
    }
    return false;
}

static void level_clear(pc_bo_t *g)
{
    g->state = PC_BO_CLEAR;
    g->clear_ticks = PC_BO_CLEAR_TICKS;
    g->score += 50u * (uint32_t)g->level;
    for (int k = 0; k < PC_BO_BALLS; k++) g->ball[k].live = false;
    memset(g->drop, 0, sizeof g->drop);
    ev(g, PC_EV_CLEAR, -1, g->level, 0, 0);
}

/* one ball for one step: sub-steps of at most 1 unit; the paddle when its outer edge crosses the inner face; a brick; out of the field */
static void ball_step(pc_bo_t *g, pc_bo_ball_t *b)
{
    if (b->stuck) {
        if (++b->stuck_ticks >= PC_BO_CATCH_TICKS) release(g, b);
        return;
    }
    float sp = len(b->vx, b->vy);
    int n = (int)ceilf(sp);
    if (n < 1) n = 1;
    for (int s = 0; s < n && b->live && g->state == PC_BO_PLAY; s++) {
        float r_prev = len(b->x, b->y);
        b->x += b->vx / (float)n;
        b->y += b->vy / (float)n;
        float r = len(b->x, b->y);
        if (r_prev + PC_BO_BALL_R < PC_BO_PAD_IN && r + PC_BO_BALL_R >= PC_BO_PAD_IN) {
            float bd = deg_of(b->x, b->y);
            int k = paddle_of(g, bd, PC_BO_BALL_R / r * DEG);
            if (k >= 0) {
                b->x *= (PC_BO_PAD_IN - PC_BO_BALL_R) / r;
                b->y *= (PC_BO_PAD_IN - PC_BO_BALL_R) / r;
                g->combo = 0;
                if (g->rule == PC_BO_CATCH && g->catches < PC_BO_CATCHES) { g->catches++; b->stuck = true; b->stuck_ticks = 0; b->stuck_deg = pc_bo_angdiff(bd, g->paddle_deg); }
                else { pc_bo_paddle_dir(b->x, b->y, b->vx, b->vy, pad_deg(g, k), pc_bo_half(g), &b->vx, &b->vy); set_speed(b, pc_bo_speed(g)); }
                ev(g, PC_EV_PADDLE, -1, k, b->x, b->y);
                if (b->stuck) return;
                continue;
            }
        }
        if (bricks(g, b) && g->bricks_left <= 0) { level_clear(g); return; }
        if (r - PC_BO_BALL_R > PC_BO_FIELD) b->live = false;
    }
}

void pc_bo_step(pc_bo_t *g)
{
    g->n_ev = 0;
    g->ticks++;
    if (g->state == PC_BO_CLEAR) { if (--g->clear_ticks <= 0) pc_bo_level(g, g->level + 1); return; }
    if (g->state != PC_BO_PLAY) return;
    for (int k = 0; k < PC_BO_RINGS; k++) g->rot[k] = wrap360(g->rot[k] + g->spin[k]);
    if (g->wide_ticks > 0) g->wide_ticks--;
    if (g->slow_ticks > 0 && --g->slow_ticks == 0)                             /* the slow ball is over: every moving ball back to the speed (fix round 1 m5) */
        for (int k = 0; k < PC_BO_BALLS; k++) if (g->ball[k].live && !g->ball[k].stuck) set_speed(&g->ball[k], pc_bo_speed(g));
    for (int k = 0; k < PC_BO_BALLS && g->state == PC_BO_PLAY; k++) if (g->ball[k].live) ball_step(g, &g->ball[k]);
    if (g->state != PC_BO_PLAY) return;
    for (int d = 0; d < PC_BO_DROPS; d++) {                                    /* capsules fly outward; the paddle under one takes it */
        pc_bo_drop_t *p = &g->drop[d];
        if (!p->live) continue;
        float r0 = p->r;
        p->r += PC_BO_DROP_SPEED;
        if (r0 < PC_BO_PAD_IN && p->r >= PC_BO_PAD_IN && paddle_of(g, p->deg, 3.0f) >= 0) {
            p->live = false;
            if (p->power == PC_PW_WIDE) g->wide_ticks = PC_BO_POWER_TICKS;
            else if (p->power == PC_PW_SLOW) {
                if (!g->slow_ticks) for (int k = 0; k < PC_BO_BALLS; k++) { g->ball[k].vx *= 0.7f; g->ball[k].vy *= 0.7f; }
                g->slow_ticks = PC_BO_POWER_TICKS;
            } else {
                int from = -1, to = -1;
                for (int k = 0; k < PC_BO_BALLS; k++) { if (g->ball[k].live && !g->ball[k].stuck && from < 0) from = k; if (!g->ball[k].live && to < 0) to = k; }
                if (from >= 0 && to >= 0) {
                    pc_bo_ball_t *a = &g->ball[from], *c = &g->ball[to];
                    float t = 30.0f * RAD, co = cosf(t), si = sinf(t);
                    *c = (pc_bo_ball_t){ .x = a->x, .y = a->y, .vx = a->vx * co - a->vy * si, .vy = a->vx * si + a->vy * co, .live = true };
                }
            }
            ev(g, PC_EV_POWER, -1, p->power, p->r * cosf(p->deg * RAD), p->r * sinf(p->deg * RAD));
        }
        if (p->r > PC_BO_FIELD) p->live = false;
    }
    bool any = false;
    for (int k = 0; k < PC_BO_BALLS; k++) any |= g->ball[k].live;
    if (any) return;
    g->lives--;
    ev(g, PC_EV_LOST, -1, g->lives, 0, 0);
    if (g->lives <= 0) {
        g->lives = 0;
        g->state = PC_BO_OVER;
        memset(g->drop, 0, sizeof g->drop);
        ev(g, PC_EV_OVER, -1, 0, 0, 0);
        return;
    }
    rest(g);                                                                    /* the next ball waits on the paddle (the events stay) */
}
