// SPDX-License-Identifier: LGPL-2.1-or-later
/* 氛圍頁 / Ambient scenes (ideas G5): pure motion, see pc/pc_ambient.h. */
#include "pc/pc_ambient.h"
#include <math.h>
#include <string.h>

#define R PC_AM_FIELD
#define BLOBS_MAX 10
#define BLOB_RMAX 24.0f
#define FISH_N 5
#define BUBBLES_MAX 8
#define FOOD_MAX 4
#define EMBERS_MAX 14
#define RARE_ONE_IN 1200
#define RARE_GAP 900
#define PI_F 3.14159265f

static float rnd(pc_am_t *g)                                   /* 0..1 */
{
    uint32_t x = g->rng ? g->rng : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}
static float rr(pc_am_t *g, float a, float b) { return a + (b - a) * rnd(g); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static pc_am_item_t *alloc_item(pc_am_t *g, int kind)
{
    for (int i = 0; i < PC_AM_ITEMS; i++)
        if (g->it[i].kind == PC_AM_K_NONE) { memset(&g->it[i], 0, sizeof g->it[i]); g->it[i].kind = (uint8_t)kind; return &g->it[i]; }
    return NULL;
}
int pc_am_count(const pc_am_t *g, int kind)
{
    int n = 0;
    for (int i = 0; i < PC_AM_ITEMS; i++) n += g->it[i].kind == kind;
    return n;
}
static void ev(pc_am_t *g, int kind, float x, float y)
{
    if (g->n_ev < PC_AM_EVENTS) g->ev[g->n_ev++] = (pc_am_ev_t){ (uint8_t)kind, x, y };
}
/* keep a round thing of radius r inside the field (pull in, kill the outward speed) */
static void bounce(pc_am_item_t *o, float lim)
{
    const float d = hypotf(o->x, o->y);
    if (d <= lim || d < 0.001f) return;
    const float nx = o->x / d, ny = o->y / d;
    o->x = nx * lim;
    o->y = ny * lim;
    const float vn = o->vx * nx + o->vy * ny;
    if (vn > 0) { o->vx -= 1.6f * vn * nx; o->vy -= 1.6f * vn * ny; }
}

/* ---------------- init ---------------- */
void pc_am_init(pc_am_t *g, int scene, uint32_t seed, float speed)
{
    memset(g, 0, sizeof *g);
    g->scene = (uint8_t)(scene >= 0 && scene < PC_AM_SCENES ? scene : PC_AM_LAVA);
    g->speed = speed > 0 ? speed : 1.0f;
    g->rng = seed * 2654435761u + 12345u;
    g->rare_gap = 300;
    for (int i = 0; i < 4; i++) rnd(g);
    switch (g->scene) {
    case PC_AM_LAVA:
        for (int i = 0; i < 6; i++) {
            pc_am_item_t *b = alloc_item(g, PC_AM_K_BLOB);
            b->x = rr(g, -28, 28); b->y = rr(g, -55, 55); b->r = rr(g, 11, 17); b->aux = rnd(g); b->tone = (uint8_t)(1 + (i & 1));
        }
        break;
    case PC_AM_FISH:
        for (int i = 0; i < FISH_N; i++) {
            pc_am_item_t *f = alloc_item(g, PC_AM_K_FISH);
            f->x = rr(g, -45, 45); f->y = rr(g, -40, 40); f->r = rr(g, 7.0f, 10.0f); f->tone = (uint8_t)(1 + (i & 1));
            f->tx = rr(g, -60, 60); f->ty = rr(g, -40, 40); f->aux = rr(g, 0.5f, 3);
            f->vx = f->tx > f->x ? 6 : -6;
        }
        break;
    case PC_AM_NIGHT:
        for (int i = 0; i < 5; i++) {
            pc_am_item_t *s = alloc_item(g, PC_AM_K_STAR);
            const float a = (float)i * 2.399f + 0.7f, d = 55.0f + 28.0f * (float)((i * 3) % 5) / 4.0f;
            s->x = cosf(a) * d; s->y = sinf(a) * d; s->aux = (float)i * 1.3f; s->tone = 4;
        }
        break;
    default: break;
    }
}

/* ---------------- lava ---------------- */
static int blob_count(const pc_am_t *g) { return pc_am_count(g, PC_AM_K_BLOB); }
static void lava_step(pc_am_t *g, float dt)
{
    const float k = 1.0f + 0.8f * g->energy;
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *b = &g->it[i];
        if (b->kind != PC_AM_K_BLOB) continue;
        if (b->y > 40) b->aux = fminf(1, b->aux + 1.2f * dt);
        else if (b->y < -40) b->aux = fmaxf(0, b->aux - 1.2f * dt);
        else b->aux = fmaxf(0, b->aux - 0.08f * dt);
        b->vy += (0.5f - b->aux) * 70.0f * k * dt;
        b->vy -= b->vy * 1.5f * dt;
        b->vx += sinf((float)g->tick * 0.05f + (float)i) * 8.0f * dt;
        b->vx -= b->vx * 1.2f * dt;
        b->x += b->vx * dt; b->y += b->vy * dt;
        bounce(b, R - b->r - 2);
        if (b->life > 0) { b->life -= dt; if (b->life <= 0) { b->life = 0; b->tone = (uint8_t)(1 + (i & 1)); } }   /* a golden blob goes back to its colour */
    }
    for (int i = 0; i < PC_AM_ITEMS; i++) {                      /* touching blobs merge (the area is kept) */
        pc_am_item_t *a = &g->it[i];
        if (a->kind != PC_AM_K_BLOB) continue;
        for (int j = i + 1; j < PC_AM_ITEMS; j++) {
            pc_am_item_t *b = &g->it[j];
            if (b->kind != PC_AM_K_BLOB) continue;
            if (hypotf(a->x - b->x, a->y - b->y) >= 0.55f * (a->r + b->r)) continue;
            pc_am_item_t *big = a->r >= b->r ? a : b, *small = big == a ? b : a;
            const float wa = big->r * big->r, wb = small->r * small->r, w = wa + wb;
            big->x = (big->x * wa + small->x * wb) / w; big->y = (big->y * wa + small->y * wb) / w;
            big->vx = (big->vx * wa + small->vx * wb) / w; big->vy = (big->vy * wa + small->vy * wb) / w;
            big->aux = (big->aux * wa + small->aux * wb) / w;
            big->r = fminf(BLOB_RMAX, sqrtf(w));
            if (small->tone == 3) { big->tone = 3; big->life = fmaxf(big->life, small->life); }
            small->kind = PC_AM_K_NONE;
        }
    }
    if (blob_count(g) < 5 && rnd(g) < 0.03f) {                   /* the lamp's wax is never gone: a small blob forms at the bottom */
        pc_am_item_t *b = alloc_item(g, PC_AM_K_BLOB);
        if (b) { b->x = rr(g, -20, 20); b->y = 62; b->r = rr(g, 8, 11); b->aux = 1; b->tone = (uint8_t)(1 + (g->tick & 1)); }
    }
}
static void lava_poke(pc_am_t *g, float x, float y)
{
    pc_am_item_t *n = NULL;
    float best = 1e9f;
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *b = &g->it[i];
        if (b->kind != PC_AM_K_BLOB) continue;
        const float d = hypotf(b->x - x, b->y - y) - b->r;
        if (d < best) { best = d; n = b; }
    }
    if (!n) return;
    if (n->tone == 3) {                                          /* a golden blob: it bursts into three small golden ones */
        const float ox = n->x, oy = n->y;
        n->kind = PC_AM_K_NONE;
        for (int k = 0; k < 3; k++) {
            pc_am_item_t *s = alloc_item(g, PC_AM_K_BLOB);
            if (!s) break;
            const float a = (float)k * 2.094f + 0.5f;
            s->x = ox + cosf(a) * 6; s->y = oy + sinf(a) * 6; s->vx = cosf(a) * 28; s->vy = sinf(a) * 28 - 8; s->r = 5; s->tone = 3; s->life = 10; s->aux = 0.5f;
        }
        ev(g, PC_AM_EV_GOLD_CAUGHT, ox, oy);
        return;
    }
    pc_am_item_t *s = (n->r >= 9 && blob_count(g) < BLOBS_MAX) ? alloc_item(g, PC_AM_K_BLOB) : NULL;
    if (!s) { n->vy -= 25; return; }                             /* too small or the slots are full: a kick */
    n->r *= 0.72f; s->r = n->r;
    s->x = n->x + n->r * 0.6f; n->x -= n->r * 0.6f; s->y = n->y;
    s->vx = 12; n->vx = -12; s->vy = n->vy - 10; n->vy -= 10;
    s->aux = n->aux; s->tone = (uint8_t)(n->tone == 1 ? 2 : 1);
    ev(g, PC_AM_EV_SPLIT, n->x, n->y);
}

/* ---------------- aquarium ---------------- */
static void add_bubble(pc_am_t *g, float x, float y)
{
    if (pc_am_count(g, PC_AM_K_BUBBLE) >= BUBBLES_MAX) return;
    pc_am_item_t *b = alloc_item(g, PC_AM_K_BUBBLE);
    if (!b) return;
    b->x = x; b->y = y; b->vy = -rr(g, 10, 20); b->r = rr(g, 1.5f, 3.2f); b->aux = rr(g, 0, 6);
}
static pc_am_item_t *nearest_food(pc_am_t *g, const pc_am_item_t *f, float within)
{
    pc_am_item_t *n = NULL;
    float best = within;
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *o = &g->it[i];
        if (o->kind != PC_AM_K_FOOD) continue;
        const float d = hypotf(o->x - f->x, o->y - f->y);
        if (d < best) { best = d; n = o; }
    }
    return n;
}
static void fish_step(pc_am_t *g, float dt)
{
    const float base = 13.0f * (1.0f + 0.9f * g->energy);
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind == PC_AM_K_FISH) {
            f->aux -= dt;
            if (f->life > 0) f->life -= dt;                      /* darting */
            pc_am_item_t *food = nearest_food(g, f, 70);
            if (food) { f->tx = food->x; f->ty = food->y; }
            else if (f->aux <= 0 || hypotf(f->tx - f->x, f->ty - f->y) < 5) {
                const float a = rr(g, 0, 2 * PI_F), d = rr(g, 10, 0.72f * R);
                f->tx = cosf(a) * d; f->ty = sinf(a) * d * 0.8f; f->aux = rr(g, 1.5f, 4);
            }
            float dx = f->tx - f->x, dy = f->ty - f->y;
            const float d = hypotf(dx, dy);
            if (d > 0.001f) { dx /= d; dy /= d; }
            const float sp = base * (f->life > 0 ? 3.2f : 1.0f) * (food ? 1.4f : 1.0f);
            const float kk = fminf(1, 4 * dt);
            f->vx += (dx * sp - f->vx) * kk; f->vy += (dy * sp - f->vy) * kk;
            f->x += f->vx * dt; f->y += f->vy * dt;
            bounce(f, 0.86f * R - f->r);
            if (food && hypotf(food->x - f->x, food->y - f->y) < f->r + 2.5f) {
                food->kind = PC_AM_K_NONE;
                f->r = fminf(12, f->r + 0.6f);
                ev(g, PC_AM_EV_EAT, f->x, f->y);
                add_bubble(g, f->x, f->y - f->r); add_bubble(g, f->x + 2, f->y - f->r);
            }
        } else if (f->kind == PC_AM_K_GOLDFISH) {
            f->age += dt;
            f->x += f->vx * dt;
            f->y = f->ty + sinf(f->age * 2.0f) * 12.0f;
            if (fabsf(f->x) > 92) f->kind = PC_AM_K_NONE;
        } else if (f->kind == PC_AM_K_FOOD) {
            f->age += dt;
            f->y += 9.0f * dt;
            if (f->age > 12 || hypotf(f->x, f->y) > 0.92f * R) f->kind = PC_AM_K_NONE;
        } else if (f->kind == PC_AM_K_BUBBLE) {
            f->age += dt;
            f->y += f->vy * dt;
            f->x += sinf(f->age * 3 + f->aux) * 6 * dt;
            if (hypotf(f->x, f->y) > 0.93f * R) f->kind = PC_AM_K_NONE;
        }
    }
    if (rnd(g) < 0.06f * g->speed) add_bubble(g, rr(g, -35, 35), 70);
}
static void fish_poke(pc_am_t *g, float x, float y)
{
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind != PC_AM_K_GOLDFISH || hypotf(f->x - x, f->y - y) > f->r + 6) continue;
        ev(g, PC_AM_EV_GOLD_CAUGHT, f->x, f->y);
        for (int k = 0; k < 4; k++) add_bubble(g, f->x + (float)k, f->y);
        f->kind = PC_AM_K_NONE;
        return;
    }
    pc_am_item_t *n = NULL;
    float best = 1e9f;
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind != PC_AM_K_FISH) continue;
        const float d = hypotf(f->x - x, f->y - y);
        if (d < best) { best = d; n = f; }
    }
    if (n && best < n->r + 6) {                                  /* a tap on a fish: it darts away */
        n->life = 0.7f;
        n->tx = clampf(n->x + (n->x - x) * 4, -70, 70); n->ty = clampf(n->y + (n->y - y) * 4, -55, 55);
        n->aux = 2;
        ev(g, PC_AM_EV_DART, n->x, n->y);
        return;
    }
    const float d = hypotf(x, y), lim = 0.85f * R;               /* else: food */
    if (d > lim) { x *= lim / d; y *= lim / d; }
    pc_am_item_t *p = alloc_item(g, PC_AM_K_FOOD);
    if (pc_am_count(g, PC_AM_K_FOOD) > FOOD_MAX) {               /* the oldest is replaced */
        pc_am_item_t *old = NULL;
        for (int i = 0; i < PC_AM_ITEMS; i++) if (g->it[i].kind == PC_AM_K_FOOD && &g->it[i] != p && (!old || g->it[i].age > old->age)) old = &g->it[i];
        if (old) old->kind = PC_AM_K_NONE;
    }
    if (p) { p->x = x; p->y = y; p->r = 1.8f; }
    ev(g, PC_AM_EV_FEED, x, y);
}

/* ---------------- fire ---------------- */
static void add_flame(pc_am_t *g, float boost)
{
    pc_am_item_t *f = alloc_item(g, PC_AM_K_FLAME);
    if (!f) return;
    f->x = rr(g, -12, 12) * (1 + 0.5f * boost); f->y = 54;
    f->vy = -rr(g, 70, 105) * (1 + 0.5f * g->energy + 0.5f * boost);
    f->r = rr(g, 11, 16) * (1 + 0.2f * boost);
    f->life = rr(g, 0.9f, 1.4f); f->aux = rr(g, 0, 6);
}
static void add_ember(pc_am_t *g, float x, float y, float vx, float vy)
{
    if (pc_am_count(g, PC_AM_K_EMBER) >= EMBERS_MAX) return;
    pc_am_item_t *e = alloc_item(g, PC_AM_K_EMBER);
    if (!e) return;
    e->x = x; e->y = y; e->vx = vx; e->vy = vy; e->r = 1.3f; e->life = rr(g, 1.6f, 2.4f); e->tone = 4;
}
static void fire_step(pc_am_t *g, float dt)
{
    g->acc += (0.9f + 1.4f * g->energy + 1.5f * g->flare) * g->speed;
    while (g->acc >= 1.0f) { g->acc -= 1.0f; add_flame(g, g->flare); }
    g->flare *= powf(0.94f, g->speed);
    if (rnd(g) < 0.04f * g->speed) add_ember(g, rr(g, -14, 14), 50, rr(g, -10, 10), -rr(g, 30, 55));
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind == PC_AM_K_FLAME) {
            f->age += dt;
            f->y += f->vy * dt;
            f->x += (-f->x * 1.5f + sinf(f->age * 9 + f->aux) * 7) * dt;
            if (f->age >= f->life) f->kind = PC_AM_K_NONE;
        } else if (f->kind == PC_AM_K_EMBER) {
            f->age += dt;
            f->vy += 18 * dt;
            f->x += f->vx * dt; f->y += f->vy * dt;
            if (f->age >= f->life || hypotf(f->x, f->y) > 0.95f * R) f->kind = PC_AM_K_NONE;
        }
    }
}
static void fire_poke(pc_am_t *g)
{
    g->flare = 1;
    for (int i = 0; i < 8; i++) add_flame(g, 1);
    for (int i = 0; i < 5; i++) add_ember(g, rr(g, -10, 10), 45, rr(g, -25, 25), -rr(g, 35, 60));
    ev(g, PC_AM_EV_FLARE, 0, 40);
}

/* ---------------- night light ---------------- */
static void night_step(pc_am_t *g, float dt)
{
    g->phase += dt * 0.8f;
    g->drift += dt * 0.12f;
    g->pulse *= powf(0.95f, g->speed);
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind == PC_AM_K_STAR) f->age += dt;
        else if (f->kind == PC_AM_K_FIREFLY) {
            f->age += dt;
            f->vx = clampf(f->vx + rr(g, -1, 1) * 20 * dt, -10, 10); f->vy = clampf(f->vy + rr(g, -1, 1) * 20 * dt, -10, 10);
            f->x += f->vx * dt; f->y += f->vy * dt;
            f->tx *= powf(0.9f, g->speed);                       /* the blink boost fades */
            if (f->age > f->life || hypotf(f->x, f->y) > 0.95f * R) f->kind = PC_AM_K_NONE;
        }
    }
}
static void night_poke(pc_am_t *g, float x, float y)
{
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        pc_am_item_t *f = &g->it[i];
        if (f->kind == PC_AM_K_FIREFLY && hypotf(f->x - x, f->y - y) < 18) { f->tx = 1; ev(g, PC_AM_EV_BLINK, f->x, f->y); return; }
    }
    g->pulse = 1;
    ev(g, PC_AM_EV_PULSE, x, y);
}

/* ---------------- common ---------------- */
bool pc_am_force_rare(pc_am_t *g)
{
    g->rare_gap = RARE_GAP;
    switch (g->scene) {
    case PC_AM_LAVA: {
        pc_am_item_t *pick = NULL;
        for (int i = 0; i < PC_AM_ITEMS; i++) {
            pc_am_item_t *b = &g->it[i];
            if (b->kind != PC_AM_K_BLOB) continue;
            if (b->tone == 3) return false;
            if (!pick || b->r > pick->r) pick = b;
        }
        if (!pick) return false;
        pick->tone = 3; pick->life = 20;
        ev(g, PC_AM_EV_GOLD_SEEN, pick->x, pick->y);
        return true;
    }
    case PC_AM_FISH: {
        if (pc_am_count(g, PC_AM_K_GOLDFISH)) return false;
        pc_am_item_t *f = alloc_item(g, PC_AM_K_GOLDFISH);
        if (!f) return false;
        f->x = -88; f->ty = rr(g, -18, 18); f->y = f->ty; f->vx = 16; f->r = 8; f->tone = 4;
        ev(g, PC_AM_EV_GOLD_SEEN, f->x, f->y);
        return true;
    }
    case PC_AM_FIRE:
        for (int i = 0; i < 12; i++) {
            const float a = -PI_F * (0.1f + 0.8f * (float)i / 11.0f), sp = rr(g, 40, 70);
            add_ember(g, 0, 50, cosf(a) * sp, sinf(a) * sp);
        }
        g->flare = fmaxf(g->flare, 0.4f);
        ev(g, PC_AM_EV_SPARKS, 0, 50);
        return true;
    default: {
        if (pc_am_count(g, PC_AM_K_FIREFLY)) return false;
        pc_am_item_t *f = alloc_item(g, PC_AM_K_FIREFLY);
        if (!f) return false;
        const float a = rr(g, 0, 2 * PI_F);
        f->x = cosf(a) * 70; f->y = sinf(a) * 70; f->life = 14; f->tone = 3;
        ev(g, PC_AM_EV_FIREFLY, f->x, f->y);
        return true;
    }
    }
}

void pc_am_step(pc_am_t *g, float energy)
{
    const float dt = g->speed / 30.0f;
    g->n_ev = 0;
    g->energy = clampf(energy, 0, 1);
    g->tick++;
    switch (g->scene) {
    case PC_AM_LAVA: lava_step(g, dt); break;
    case PC_AM_FISH: fish_step(g, dt); break;
    case PC_AM_FIRE: fire_step(g, dt); break;
    default: night_step(g, dt); break;
    }
    if (g->rare_gap > 0) g->rare_gap--;
    else if (rnd(g) < 1.0f / RARE_ONE_IN) pc_am_force_rare(g);
}

void pc_am_poke(pc_am_t *g, float x, float y)
{
    switch (g->scene) {
    case PC_AM_LAVA: lava_poke(g, x, y); break;
    case PC_AM_FISH: fish_poke(g, x, y); break;
    case PC_AM_FIRE: fire_poke(g); break;
    default: night_poke(g, x, y); break;
    }
}

/* ---------------- draw ---------------- */
typedef struct { pc_am_prim_t *o; int n, max; } sink_t;
static void put(sink_t *s, int shape, int tone, int mix, int opa, float x, float y, float a, float b)
{
    if (s->n >= s->max) return;
    s->o[s->n++] = (pc_am_prim_t){ (uint8_t)shape, (uint8_t)tone, (uint8_t)clampf((float)mix, 0, 255), (uint8_t)clampf((float)opa, 0, 255), x, y, a, b };
}
static void draw_lava(const pc_am_t *g, sink_t *s)
{
    const float pulse = 1.0f + 0.25f * g->energy;
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < PC_AM_ITEMS; i++) {
            const pc_am_item_t *b = &g->it[i];
            if (b->kind != PC_AM_K_BLOB) continue;
            const float r = b->r * pulse;
            if (pass == 0) put(s, PC_AM_BALL, 0, 0, 90, b->x, b->y, r * 1.35f, 0);
            else if (pass == 1) put(s, PC_AM_BALL, b->tone, 0, 235, b->x, b->y, r, 0);
            else put(s, PC_AM_BALL, 3, 0, b->tone == 3 ? 200 : 110, b->x - r * 0.3f, b->y - r * 0.3f, r * 0.33f, 0);
        }
}
static void draw_fish(const pc_am_t *g, sink_t *s)
{
    for (int i = 0; i < PC_AM_ITEMS; i++) {                      /* bubbles, then food, behind the fish */
        const pc_am_item_t *o = &g->it[i];
        if (o->kind == PC_AM_K_BUBBLE) put(s, PC_AM_BALL, 3, 0, 100, o->x, o->y, o->r, 0);
        else if (o->kind == PC_AM_K_FOOD) put(s, PC_AM_BALL, 0, 0, 235, o->x, o->y, o->r, 0);
    }
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        const pc_am_item_t *f = &g->it[i];
        if (f->kind != PC_AM_K_FISH && f->kind != PC_AM_K_GOLDFISH) continue;
        const float dir = f->vx >= 0 ? 1.0f : -1.0f, r = f->r;
        put(s, PC_AM_BALL, f->tone, 0, 235, f->x - dir * r * 1.5f, f->y, r * 0.8f, 0);                  /* the tail */
        put(s, PC_AM_RECT, f->tone, 0, 245, f->x, f->y, r * 3.0f, r * 1.7f);                            /* the body */
        put(s, PC_AM_BALL, 5, 0, 255, f->x + dir * r * 0.8f, f->y - r * 0.2f, r * 0.28f, 0);            /* the eye */
    }
}
static void draw_fire(const pc_am_t *g, sink_t *s)
{
    put(s, PC_AM_RECT, 5, 0, 255, 0, 64, 52, 9);
    put(s, PC_AM_RECT, 5, 0, 255, 0, 72, 38, 7);
    for (int tone = 0; tone <= 4; tone++)                        /* the oldest (smoke, red) behind, the hot roots in front */
        for (int i = 0; i < PC_AM_ITEMS; i++) {
            const pc_am_item_t *f = &g->it[i];
            if (f->kind != PC_AM_K_FLAME) continue;
            const float fr = f->age / f->life;
            const int t = fr < 0.25f ? 3 : fr < 0.5f ? 2 : fr < 0.75f ? 1 : 0;
            if (t != tone) continue;
            put(s, PC_AM_BALL, t, 0, (int)(220.0f * (1 - fr) + 30.0f), f->x, f->y, f->r * (1.0f - 0.75f * fr), 0);
        }
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        const pc_am_item_t *e = &g->it[i];
        if (e->kind == PC_AM_K_EMBER) put(s, PC_AM_BALL, 4, 0, (int)(255.0f * (1 - e->age / e->life)), e->x, e->y, e->r, 0);
    }
}
static void draw_night(const pc_am_t *g, sink_t *s)
{
    const float breath = 0.5f + 0.5f * sinf(g->phase), m = 0.5f + 0.5f * sinf(g->drift);
    const float sc = 0.84f + 0.12f * breath + 0.10f * g->pulse + 0.10f * g->energy;
    const int mix = (int)(m * 255);
    put(s, PC_AM_BALL, 0, mix, 90, 0, 0, 96 * sc, 0);
    put(s, PC_AM_BALL, 1, mix, 140, 0, 0, 66 * sc, 0);
    put(s, PC_AM_BALL, 2, mix, (int)(215 + 40 * breath), 0, 0, 34 * sc, 0);
    for (int i = 0; i < PC_AM_ITEMS; i++) {
        const pc_am_item_t *o = &g->it[i];
        if (o->kind == PC_AM_K_STAR) put(s, PC_AM_BALL, 4, 0, (int)(80 + 150 * (0.5f + 0.5f * sinf(o->age * 1.7f + o->aux))), o->x, o->y, 1.3f, 0);
        else if (o->kind == PC_AM_K_FIREFLY) {
            const float bl = clampf(0.4f + 0.6f * (0.5f + 0.5f * sinf(o->age * 4)) + o->tx, 0, 1);
            put(s, PC_AM_BALL, 3, 0, (int)(70 * bl), o->x, o->y, 5.5f, 0);
            put(s, PC_AM_BALL, 3, 0, (int)(255 * bl), o->x, o->y, 2.0f, 0);
        }
    }
}
int pc_am_draw(const pc_am_t *g, pc_am_prim_t *out, int max)
{
    sink_t s = { out, 0, max < PC_AM_MAXPRIM ? max : PC_AM_MAXPRIM };
    switch (g->scene) {
    case PC_AM_LAVA: draw_lava(g, &s); break;
    case PC_AM_FISH: draw_fish(g, &s); break;
    case PC_AM_FIRE: draw_fire(g, &s); break;
    default: draw_night(g, &s); break;
    }
    return s.n;
}
