// SPDX-License-Identifier: LGPL-2.1-or-later
/* Marble maze (ideas G6b): see knobtoy_maze.h. Statics in PSRAM (.bss: built with -msmall-data-limit=0 like the game pages). Coordinates: pc_game_maze units
 * (the field's radius = 100) x scale = the field's px radius / 100. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "imu_svc.h"
#include "games.h"
#include "knobtoy_maze.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_kit.h"

enum { ID_RING = 0, ID_WALL = ID_RING + PC_MZ_RINGS, ID_GOAL = ID_WALL + PC_MZ_WALLS_MAX, ID_HOLE, ID_MARK = ID_HOLE + PC_MZ_HOLES_MAX, ID_MARBLE, ID_N };

static lv_obj_t *s_arena;
static pc_mz_t s_g;
static const kt_maze_host_t *s_host;
static const game_style_t *s_st;
static game_field_palette_t s_pal;
static bool s_gold, s_fresh;                            /* s_fresh: a game is made (s_g is valid) */
static float s_tx, s_ty;                                /* the sensor's vector (kept over new games) */
static bool s_zpend;                                    /* a neutral pose was asked for: step() watches for its failure (the message) */
static int s_marker_a;                                  /* the last drawn marker angle (+1; 0: hidden) */

static float scale(void) { return (float)ui_arena_radius(s_arena) / (float)PC_MZ_R; }
static int px(float u) { return (int)lroundf(u * scale()); }
static int wrap(int a) { a %= PC_MZ_TURN; return a < 0 ? a + PC_MZ_TURN : a; }
const pc_mz_t *kt_maze_game(void) { return &s_g; }
lv_obj_t *kt_maze_field(void) { return s_arena; }

/* ---- drawing: the walls (set again only when they changed: ui_arena compares), the marble, the marker ---- */
static void draw_walls(void)
{
    const bool ring = s_g.rule != PC_MZ_TILT;
    for (int k = 0; k < PC_MZ_RINGS; k++) {
        if (!ring) { ui_arena_hide(s_arena, ID_RING + k); continue; }
        const float R = pc_mz_ring_r(k);
        const int half = pc_mz_gap_half(&s_g) + (int)lroundf(asinf(PC_MZ_MARBLE_R / R) * 57.29578f);   /* the gap as the marble's body sees it */
        const int a0 = wrap(s_g.gap[k] + half);
        ui_arena_arc(s_arena, ID_RING + k, px(R - PC_MZ_WALL_HALF), px(R + PC_MZ_WALL_HALF), a0, a0 + PC_MZ_TURN - 2 * half, false, s_pal.piece, LV_OPA_COVER);
    }
    for (int i = 0; i < PC_MZ_WALLS_MAX; i++) {
        if (ring || i >= s_g.n_walls) { ui_arena_hide(s_arena, ID_WALL + i); continue; }
        const pc_mz_rect_t *w = &s_g.wall[i];
        ui_arena_rect(s_arena, ID_WALL + i, px((w->x0 + w->x1) * 0.5f), px((w->y0 + w->y1) * 0.5f), LV_MAX(2, px(w->x1 - w->x0)), LV_MAX(2, px(w->y1 - w->y0)), px(1.0f), s_pal.piece, LV_OPA_COVER);
    }
    if (s_g.rule == PC_MZ_TILT) ui_arena_ball(s_arena, ID_GOAL, px(s_g.goal[0]), px(s_g.goal[1]), LV_MAX(3, px(6.0f)), s_pal.gold, LV_OPA_COVER);
    else if (s_g.rule == PC_MZ_ATTRACT) ui_arena_ball(s_arena, ID_GOAL, 0, 0, LV_MAX(3, px(PC_MZ_CENTRE_R)), s_pal.gold, LV_OPA_COVER);
    else ui_arena_hide(s_arena, ID_GOAL);
    for (int i = 0; i < PC_MZ_HOLES_MAX; i++) {
        if (i < s_g.n_holes) ui_arena_ball(s_arena, ID_HOLE + i, px(s_g.hole[i][0]), px(s_g.hole[i][1]), LV_MAX(3, px(PC_MZ_HOLE_R)), s_pal.tough, LV_OPA_COVER);
        else ui_arena_hide(s_arena, ID_HOLE + i);
    }
}
static void draw_marker(void)
{
    int a = 0;                                                   /* the rim marker: where "down" is (the knob's / the tilt's gravity) */
    if (s_g.rule == PC_MZ_GRAV) a = s_g.theta + 1;
    else if (s_g.rule == PC_MZ_TILT && hypotf(s_g.gx, s_g.gy) > 0.1f) a = wrap((int)lroundf(atan2f(s_g.gy, s_g.gx) * 57.29578f)) + 1;
    if (!a) { ui_arena_hide(s_arena, ID_MARK); s_marker_a = 0; return; }
    const int c = a - 1;
    ui_arena_arc(s_arena, ID_MARK, px(94.0f), px(99.5f), wrap(c - 7), wrap(c - 7) + 14, true, s_pal.power, LV_OPA_COVER);
    s_marker_a = a;
}
static void sync(void)
{
    ui_arena_begin(s_arena);
    ui_arena_ball(s_arena, ID_MARBLE, px(s_g.x), px(s_g.y), LV_MAX(3, px(PC_MZ_MARBLE_R)), s_gold ? s_pal.gold : s_pal.ball, LV_OPA_COVER);
    draw_marker();
    ui_arena_end(s_arena);
}
static void field(void)
{
    if (s_g.rule == PC_MZ_TILT) ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    else ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, px(PC_MZ_EXIT_R) - 1, px(PC_MZ_R), s_pal.track);   /* the exit strip, faint */
}
static void redraw_all(void)
{
    if (!s_arena || !s_fresh) return;
    ui_arena_begin(s_arena);
    field();
    draw_walls();
    ui_arena_end(s_arena);
    sync();
}

void kt_maze_text(char *b, size_t n, char *low, size_t m)   /* one line above the field (progress) and one below (the state) */
{
    b[0] = 0;
    low[0] = 0;
    if (!s_fresh) return;
    const int sec = (int)(s_g.ms / 1000u);
    if (s_g.rule == PC_MZ_TILT) snprintf(b, n, T(S_D_FALLS) /* Fell in %d holes */, s_g.falls);
    else snprintf(b, n, T(S_MZ_PROGRESS) /* Ring %d/%d, %d turns */, s_g.rule == PC_MZ_ATTRACT ? PC_MZ_RINGS - s_g.level : s_g.level, PC_MZ_RINGS, s_g.turns);
    if (s_g.state == PC_MZ_DONE) {
        snprintf(low, m, T(S_OUT_D_S) /* Out! %d s */, sec);
        if (s_g.rule == PC_MZ_ATTRACT) snprintf(low, m, T(S_CENTRE_D_S) /* Reached the centre! %d s */, sec);
        if (s_g.rule == PC_MZ_TILT) snprintf(low, m, T(S_DONE_D_FALLS_D_S) /* Arrived! %d falls %d s */, s_g.falls, sec);
    } else if (!s_g.started) snprintf(low, m, "%s", s_g.rule == PC_MZ_TILT ? T(S_TAP_TO_START_THEN_TILT) /* Tap to start, then tilt */ : T(S_TAP_TO_START_THEN_TURN) /* Tap to start, then turn */);
}

/* ---- the step's events ---- */
static void events(void)
{
    bool over = false;
    for (int i = 0; i < s_g.n_ev; i++) {
        const int x = px(s_g.x), y = px(s_g.y);
        switch (s_g.ev[i]) {
        case PC_MZ_EV_BOUNCE: if (s_g.ev_v[i] > 40) { sfx_play("tap"); ui_haptic_tick(); } break;
        case PC_MZ_EV_GAP: sfx_play("coin"); ui_haptic_tap(); ui_arena_burst(s_arena, x, y, s_pal.fx, 4); break;
        case PC_MZ_EV_FALL: sfx_play("buzz"); ui_haptic_strong(); ui_arena_flash(s_arena, s_pal.warn); break;
        case PC_MZ_EV_DONE: over = true; ui_arena_burst(s_arena, x, y, s_pal.gold, 8); ui_arena_wave(s_arena, s_pal.gold); break;
        default: break;
        }
    }
    if (over) { ui_haptic_strong(); if (s_host && s_host->finished) s_host->finished(); }
}
static void drift_redraw(void)
{
    if (s_g.drift) { ui_arena_begin(s_arena); draw_walls(); ui_arena_end(s_arena); }   /* ui_arena sets only the rings whose arc changed */
}

/* ---- the field's callbacks ---- */
static void step(void *u)
{
    (void)u;
    if (!s_fresh) return;
    if (s_zpend) {
        const int z = imu_svc_zero_state();
        if (z == 2) s_zpend = false;
        else if (z == 3) { s_zpend = false; ui_push_message(T(S_HOLD_STILL_THEN_REZERO) /* Hold it steady or lay it flat, then tap Re-zero */); }   /* absolute tilt meanwhile */
    }
    if (s_g.rule == PC_MZ_TILT && imu_svc_present()) {          /* a tilt source (the FakePod's IMU, the simulator's `simimu`): its vector is the force, every fixed step */
        float x, y;
        imu_svc_gravity(&x, &y);
        s_tx = x; s_ty = y;
        pc_mz_set_grav(&s_g, x, y);
    }
    pc_mz_step(&s_g);
    events();
    drift_redraw();
    sync();
    if (s_host && s_host->status) s_host->status();
}
static bool running(void *u) { (void)u; return s_fresh && (pc_mz_running(&s_g) || s_zpend); }
static void paused(bool on, void *u) { (void)u; if (s_host && s_host->paused) s_host->paused(on); }
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)y;
    if (!s_fresh) return;
    if (s_g.state == PC_MZ_DONE) { if (kind == UI_ARENA_TAP) { ui_haptic_tap(); if (s_host && s_host->again) s_host->again(); } return; }
    const bool was = s_g.started;
    if (kind == UI_ARENA_STEP) pc_mz_turn(&s_g, v);
    else if (!was) { pc_mz_start(&s_g); ui_haptic_tap(); }
    else if (s_g.rule != PC_MZ_TILT) { pc_mz_turn(&s_g, x < 0 ? -1 : 1); ui_haptic_tick(); }   /* the left half one detent back, the right half forward */
    if (s_g.started && !was) ui_arena_wake(s_arena);
    events();
    sync();
    if (s_host && s_host->status) s_host->status();
}
static void dump(void *u)
{
    (void)u;
    printf("APP game knobtoy maze state=%d rule=%d drift=%d x=%d y=%d r=%d a=%d theta=%d gx=%d gy=%d level=%d turns=%d falls=%d steps=%d ms=%u gold=%d started=%d gap=%d,%d,%d,%d holes=%d walls=%d\n",
           s_g.state, s_g.rule, s_g.drift, (int)lroundf(s_g.x), (int)lroundf(s_g.y), (int)lroundf(pc_mz_r(&s_g)), pc_mz_angle(&s_g), s_g.theta, (int)lroundf(s_g.gx * 100), (int)lroundf(s_g.gy * 100),
           s_g.level, s_g.turns, s_g.falls, s_g.steps, (unsigned)s_g.ms, s_gold, s_g.started, s_g.gap[0], s_g.gap[1], s_g.gap[2], s_g.gap[3], s_g.n_holes, s_g.n_walls);
}
/* console `arena game ...` (tests, pictures): "marble <x> <y>" puts the marble there (units, still); "gap <k> <deg>" ring k's gap; "theta <deg>" gravity's direction (the vector set too);
 * "grav <dx> <dy>" the vector input (percent); "start" the clock; "hole <i> <x> <y>" a hole */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int a, b, c;
    if (sscanf(arg, "marble %d %d", &a, &b) == 2) { s_g.x = (float)a; s_g.y = (float)b; s_g.vx = s_g.vy = 0; s_g.level = 0; for (int k = 0; k < PC_MZ_RINGS; k++) if (hypotf(s_g.x, s_g.y) > pc_mz_ring_r(k) + 6.5f) s_g.level = k + 1; }
    else if (sscanf(arg, "gap %d %d", &a, &b) == 2 && a >= 0 && a < PC_MZ_RINGS) { s_g.gap[a] = wrap(b); redraw_all(); }
    else if (sscanf(arg, "theta %d", &a) == 1) pc_mz_set_theta(&s_g, a);
    else if (sscanf(arg, "grav %d %d", &a, &b) == 2) kt_maze_grav((float)a / 100.0f, (float)b / 100.0f);
    else if (!strcmp(arg, "start")) { pc_mz_start(&s_g); ui_arena_wake(s_arena); }
    else if (sscanf(arg, "hole %d %d %d", &a, &b, &c) == 3 && a >= 0 && a < PC_MZ_HOLES_MAX) { s_g.hole[a][0] = (float)b; s_g.hole[a][1] = (float)c; if (a >= s_g.n_holes) s_g.n_holes = a + 1; redraw_all(); }
    else { printf("APP game knobtoy maze cmd unknown %s\n", arg); return; }
    sync();
    if (s_host && s_host->status) s_host->status();
}
static const ui_arena_def_t ARENA = { .name = "knobtoy", .centred = true, .objs = ID_N, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd };

void kt_maze_rezero(void)
{
    if (!s_arena || !imu_svc_present()) return;
    s_zpend = imu_svc_zero();
    if (s_zpend) ui_arena_wake(s_arena);                         /* the timer runs until the answer (held, or the message) */
}
void kt_maze_grav(float dx, float dy)
{
    s_tx = dx; s_ty = dy;
    if (!s_fresh) return;
    const bool was = s_g.started;
    pc_mz_set_grav(&s_g, dx, dy);
    if (s_g.started && !was && s_arena) ui_arena_wake(s_arena);
    if (s_arena) { sync(); if (s_host && s_host->status) s_host->status(); }
}
bool kt_maze_cmd(const char *arg)
{
    int a, b;
    if (sscanf(arg, "grav %d %d", &a, &b) != 2) return false;
    kt_maze_grav((float)a / 100.0f, (float)b / 100.0f);
    return true;
}

lv_obj_t *kt_maze_create(lv_obj_t *page, const kt_maze_host_t *host)
{
    s_host = host;
    s_fresh = false;
    s_arena = NULL;
    lv_obj_t *a = ui_arena_create(page, &ARENA, NULL);
    ui_obj_guard(a, &s_arena);
    return s_arena;
}
void kt_maze_style(const game_style_t *st, bool gold)
{
    s_st = st;
    s_gold = gold;
    if (st) game_style_field(st, &s_pal);
    redraw_all();
}
void kt_maze_new(int rule, int drift, uint32_t seed, bool gold, const game_style_t *st)
{
    if (!s_arena) return;
    pc_mz_init(&s_g, rule, drift, seed, gold);
    s_fresh = true;
    s_marker_a = 0;
    if (rule == PC_MZ_TILT && (s_tx != 0 || s_ty != 0)) { s_g.gx = s_tx; s_g.gy = s_ty; }   /* the sensor's tilt as it is now (the clock waits for a tap) */
    s_zpend = false;
    if (rule != PC_MZ_TILT) imu_svc_zero_clear();
    else if (imu_svc_present() && imu_svc_zero_state() != 2) kt_maze_rezero();   /* the pose held now is level (a restart keeps the one held) */
    ui_arena_unpause(s_arena);
    ui_arena_clear(s_arena);
    kt_maze_style(st, gold);
}
