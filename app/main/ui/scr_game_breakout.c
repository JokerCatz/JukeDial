// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ring breakout (ideas G1): the shared field (ui_arena) with the bricks on inner rings, the paddle an arc on the outer edge, the ball
 * bouncing inside; the HEAD row (the speed; the rule, which opens the game's ring: 規則 Classic / Double paddle / Sticky ball, 風格, 重來, 說明), the status line and Back.
 * Rules and physics: pc_game_breakout (host-tested). The paddle: the knob (a tap on the field lends it to the paddle, the borrowed-knob way; FakePod:
 * the keys) one detent = 6 degrees; a tap on one side of the paddle (its left or right as seen from the centre) moves it 18 degrees that way; the
 * first tap of a ball serves it (sticky ball: lets a caught ball go). In-field feedback (ui_arena effects): a break = a burst of particles + a chiptune blip
 * (+ the combo's points rising), a boom = a bigger burst, a power-up = its name, a lost ball = the rim flashes, a cleared level = a ring runs out.
 * Surprises: the combo (bricks without touching the paddle: x points, a fanfare every 5), the rare golden brick (+1 life), the golden game (the
 * gold style), the boss ring every 5th level. The end: the result card (points, level, the best of the session). Only composes shared pieces
 * (ui_arena, game_ring, game_result, game_style); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_breakout.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

enum { ID_BRICK = 0, ID_PAD = PC_BO_BRICKS, ID_BALL = ID_PAD + 2, ID_DROP = ID_BALL + PC_BO_BALLS, ID_N = ID_DROP + PC_BO_DROPS };
#define KNOB_DEG 6.0f                                  /* one detent */
#define TAP_DEG  18.0f                                 /* a tap beside the paddle */

const ui_game_help_t game_help_breakout = {
    .zh = "球拍是外圈的一段弧，磚塊排在裡面的圓環上；球碰到磚塊會打碎它並彈回來，球飛出圓圈就少一條命，三條命用完遊戲結束，打完一關的磚塊就進下一關。"
          "點一下發球；點球拍的左邊或右邊，球拍就往那邊移一段；點一下場地之後，轉動可以一格一格地移動球拍（停下四秒之後回到音量）。"
          "磚塊有幾種：兩下才碎的、會連鎖爆開旁邊的、掉出加寬球拍、多一顆球或讓球變慢的道具（用球拍接住）、偶爾一個金色的多一條命。"
          "從第三關起磚塊圈會轉，每五關有一個轉得很快的頭目圈。連續打碎磚塊不碰球拍就是連擊，分數加倍，每五下有獎勵音。"
          "上方右邊的格子打開選單：規則有三種——經典；雙拍：對面多一支球拍，一起移動，但比較短；黏球：球落在球拍上會黏住，跟著球拍走，點一下才放出去，三秒後自己放。"
          "左邊的格子換速度。風格換顏色，霓虹風格的磚塊會跟著正在播的音樂閃。螢幕關掉或打開選單時遊戲會暫停，點一下繼續。偶爾會出現金色局",
    .en = "The paddle is an arc on the outer edge, the bricks sit on rings inside; the ball breaks a brick it hits and bounces back, a ball that leaves "
          "the circle costs a life, three lives and the game is over, every brick gone and the next level starts. Tap to serve; a tap on the left or "
          "right of the paddle moves it that way; after a tap on the field, turning moves the paddle step by step (four seconds after you stop it is "
          "the volume again). Bricks: some take two hits, some burst and take their neighbours along, some drop a power-up to catch with the paddle "
          "(a wider paddle, a second ball, a slower ball), now and then a golden one gives a life. From level 3 the rings turn, every fifth level "
          "has a fast boss ring. Bricks in a row without touching the paddle are a combo: more points, a fanfare every five. The right cell at the top "
          "opens the menu. Rules: Classic; Twin: a second paddle opposite, moving with the first, both shorter; Catch: the ball sticks where it lands "
          "on the paddle and moves with it, a tap lets it go (it goes by itself after three seconds). The left cell changes the speed. Style changes "
          "the colours; in Neon the bricks pulse with the music playing. With the screen off or the menu open the game pauses; tap to go on. Now "
          "and then a game is golden",
};

static lv_obj_t *s_arena, *s_status;
static pc_bo_t s_g;
static game_sel_t s_sel;                               /* mode: the speed (0 normal, 1 fast, 2 slow); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static uint32_t s_games, s_rounds, s_t0;              /* layouts dealt (the game's seed) / games started (the golden game's seed) */
static char s_stat[64];
static const char *const RULE_ZH[PC_BO_RULES] = { "經典", "雙拍", "黏球" }, *const RULE_EN[PC_BO_RULES] = { "Classic", "Twin", "Catch" };
static const int SPEED_OF[PC_BO_SPEEDS] = { 1, 2, 0 };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static float scale(void) { return (float)ui_arena_radius(s_arena) / PC_BO_FIELD; }
static int px(float u) { return (int)lroundf(u * scale()); }

/* ---- the objects from the game: only what changed is redrawn (ui_arena compares) ---- */
static lv_color_t brick_colour(const pc_bo_brick_t *b)
{
    switch (b->kind) {
    case PC_BK_TOUGH: case PC_BK_BOSS: return s_pal.tough;
    case PC_BK_BOOM: return s_pal.boom;
    case PC_BK_POWER: return s_pal.power;
    case PC_BK_GOLD: return s_pal.gold;
    default: return s_pal.piece;
    }
}
static void sync(void)
{
    const int e = ui_arena_energy(s_arena) >> 5 << 5;                          /* 8 steps: a small wobble of the music redraws nothing */
    const int span = (int)lroundf(360.0f / PC_BO_PER_RING - PC_BO_BRICK_GAP);  /* fix round 1 m4: a0 rounded once, the width fixed (no shimmer while turning) */
    ui_arena_begin(s_arena);                                                    /* one batch: the boxes merge (a turning ring = one band) */
    for (int i = 0; i < PC_BO_BRICKS; i++) {
        float r0, r1, a0, a1;
        if (!pc_bo_brick_sector(&s_g, i, &r0, &r1, &a0, &a1)) { ui_arena_hide(s_arena, ID_BRICK + i); continue; }
        const pc_bo_brick_t *b = &s_g.brick[i];
        int opa = b->hp < b->hp0 ? 120 + 135 * b->hp / b->hp0 : 255;                /* a hit brick is fainter */
        if (s_g.state == PC_BO_PLAY && style()->pulse && (b->kind == PC_BK_NORMAL || b->kind == PC_BK_TOUGH || b->kind == PC_BK_BOSS)) opa = opa * (110 + e * 145 / 255) / 255;   /* with the music */
        ui_arena_arc(s_arena, ID_BRICK + i, px(r0), px(r1), (int)lroundf(a0), (int)lroundf(a0) + span, false, brick_colour(b), (lv_opa_t)opa);
    }
    const float half = pc_bo_half(&s_g);
    for (int k = 0; k < 2; k++) {
        if (k >= pc_bo_paddles(&s_g)) { ui_arena_hide(s_arena, ID_PAD + k); continue; }
        float c = s_g.paddle_deg + 180.0f * (float)k;
        ui_arena_arc(s_arena, ID_PAD + k, px(PC_BO_PAD_IN), px(PC_BO_PAD_OUT), (int)lroundf(c - half), (int)lroundf(c - half) + (int)lroundf(2 * half), true, s_pal.player, LV_OPA_COVER);
    }
    for (int k = 0; k < PC_BO_BALLS; k++) {
        const pc_bo_ball_t *b = &s_g.ball[k];
        if (!b->live) { ui_arena_hide(s_arena, ID_BALL + k); continue; }
        ui_arena_ball(s_arena, ID_BALL + k, px(b->x), px(b->y), LV_MAX(3, px(PC_BO_BALL_R)), s_pal.ball, LV_OPA_COVER);
    }
    for (int d = 0; d < PC_BO_DROPS; d++) {
        const pc_bo_drop_t *p = &s_g.drop[d];
        if (!p->live) { ui_arena_hide(s_arena, ID_DROP + d); continue; }
        float a = p->deg * 0.0174532925f;
        ui_arena_rect(s_arena, ID_DROP + d, px(p->r * cosf(a)), px(p->r * sinf(a)), px(8), px(8), px(3), s_pal.power, LV_OPA_COVER);
    }
    ui_arena_end(s_arena);
}
/* Neon's feed only while the ball flies (fix round 1 I1: no 10 Hz redraw of the bricks while the ball waits) */
static void feed(void) { ui_arena_energy_on(s_arena, style()->pulse && s_g.state == PC_BO_PLAY); }

static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_BO_SERVE) snprintf(b, sizeof b, "%s", T(S_TAP_TO_SERVE) /* Tap to serve */);
    else if (s_g.state == PC_BO_CLEAR) snprintf(b, sizeof b, "%s", T(S_LEVEL_CLEAR) /* Level cleared! */);
    else if (s_g.state == PC_BO_OVER) snprintf(b, sizeof b, "%s", T(S_GAME_OVER) /* Game over */);
    else snprintf(b, sizeof b, T(S_L_D_D_LIVES_U) /* L%d %d lives %u pts */, s_g.level, s_g.lives, (unsigned)s_g.score);
    ui_frame_hint(s_status, s_paused || s_g.state == PC_BO_SERVE);                    /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;        /* the label only when its text changed */
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void finish(void)
{
    const game_end_t e = { .id = "breakout", .variant = s_g.rule, .outcome = PC_GAME_LOSS, .vs_device = false, .moves = s_g.broken, .ms = lv_tick_elaps(s_t0),
                           .title = T(S_GAME_OVER) /* Game over */, .gold = s_gold, .mode = GAME_SCORE_POINTS, .points = s_g.score, .level = s_g.level, .style = style() };
    ui_haptic_strong();
    game_result_end(s_arena, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll (as the board games) */
}

/* the step's events: every effect, one sound (the most important), graded haptics */
static const char *power_name(int p) { return p == PC_PW_WIDE ? T(S_WIDE_2) /* Wider */ : p == PC_PW_MULTI ? T(S_2_BALLS) /* Two balls */ : T(S_SLOW) /* Slow */; }
static void events(void)
{
    static const char *const SND[] = { [PC_EV_PADDLE] = "tap", [PC_EV_HIT] = "tick", [PC_EV_BREAK] = "pop", [PC_EV_BOOM] = "boom", [PC_EV_POWER] = "power_up",
                                       [PC_EV_GOLD] = "coin", [PC_EV_LOST] = "hit", [PC_EV_LIFE] = "extra_life", [PC_EV_CLEAR] = "level_up", [PC_EV_COMBO] = "combo",
                                       [PC_EV_RELEASE] = "move" };
    static const uint8_t RANK[] = { [PC_EV_RELEASE] = 1, [PC_EV_PADDLE] = 2, [PC_EV_HIT] = 3, [PC_EV_BREAK] = 4, [PC_EV_POWER] = 5, [PC_EV_GOLD] = 6, [PC_EV_LIFE] = 7,
                                    [PC_EV_COMBO] = 8, [PC_EV_BOOM] = 9, [PC_EV_LOST] = 10, [PC_EV_CLEAR] = 11, [PC_EV_OVER] = 12 };
    int top = 0, haptic = 0;
    bool over = false;
    for (int i = 0; i < s_g.n_ev; i++) {
        const pc_bo_ev_t *v = &s_g.ev[i];
        const int x = px(v->x), y = px(v->y);
        char t[12];
        switch (v->kind) {
        case PC_EV_PADDLE: haptic = LV_MAX(haptic, 1); break;
        case PC_EV_HIT: ui_arena_burst(s_arena, x, y, s_pal.tough, 3); haptic = LV_MAX(haptic, 1); break;
        case PC_EV_BREAK:
            ui_arena_burst(s_arena, x, y, s_pal.fx, 5);
            if (v->value >= 2) { snprintf(t, sizeof t, "+%d", 10 * v->value); ui_arena_text(s_arena, x, y, t, s_pal.fx); }   /* a combo: its points rise */
            haptic = LV_MAX(haptic, 2);
            break;
        case PC_EV_COMBO: snprintf(t, sizeof t, "x%d", v->value); ui_arena_text(s_arena, x, y - 14, t, s_pal.power); break;
        case PC_EV_BOOM: ui_arena_burst(s_arena, x, y, s_pal.boom, 8); haptic = 3; break;
        case PC_EV_POWER: ui_arena_text(s_arena, x * 3 / 4, y * 3 / 4, power_name(v->value), s_pal.power); haptic = LV_MAX(haptic, 2); break;
        case PC_EV_GOLD: ui_arena_burst(s_arena, x, y, s_pal.gold, 8); ui_arena_text(s_arena, x, y, "+1", s_pal.gold); haptic = 3; break;
        case PC_EV_LOST: ui_arena_flash(s_arena, s_pal.warn); haptic = 3; break;
        case PC_EV_CLEAR: ui_arena_wave(s_arena, s_pal.fx); haptic = 3; break;
        case PC_EV_OVER: over = true; break;
        default: break;
        }
        if (v->kind < (int)(sizeof RANK) && RANK[v->kind] > RANK[top]) top = v->kind;
    }
    if (over) { finish(); return; }                    /* the end sound is the result card's */
    if (top && top < (int)(sizeof SND / sizeof SND[0]) && SND[top]) sfx_play(SND[top]);
    if (haptic == 3) ui_haptic_strong(); else if (haptic == 2) ui_haptic_tap(); else if (haptic == 1) ui_haptic_tick();
}

/* ---- the field's callbacks ---- */
static void step(void *u) { (void)u; int st = s_g.state; pc_bo_step(&s_g); events(); if (st != s_g.state) feed(); sync(); show_status(); }
static bool running(void *u) { (void)u; return pc_bo_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game breakout paused=%d\n", on); }
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u;
    if (s_g.state == PC_BO_OVER) { if (kind == UI_ARENA_TAP) { ui_haptic_tap(); again(); } return; }
    if (kind == UI_ARENA_STEP) { pc_bo_move(&s_g, (float)v * KNOB_DEG); sync(); return; }
    if (s_g.state == PC_BO_CLEAR) return;
    if (pc_bo_serve(&s_g)) { ui_haptic_tap(); events(); ui_arena_wake(s_arena); feed(); sync(); show_status(); return; }   /* a serve, or a caught ball let go */
    float c = s_g.paddle_deg * 0.0174532925f, side = cosf(c) * (float)y - sinf(c) * (float)x;   /* > 0: the tap is clockwise of the paddle */
    pc_bo_move(&s_g, side >= 0 ? TAP_DEG : -TAP_DEG);
    ui_haptic_tick();
    sync();
}
static void dump(void *u)
{
    (void)u;
    int balls = 0, drops = 0, stuck = 0;
    for (int k = 0; k < PC_BO_BALLS; k++) { balls += s_g.ball[k].live; stuck += s_g.ball[k].live && s_g.ball[k].stuck; }
    for (int d = 0; d < PC_BO_DROPS; d++) drops += s_g.drop[d].live;
    printf("APP game breakout state=%d level=%d lives=%d score=%u combo=%d combo_best=%d bricks=%d balls=%d stuck=%d drops=%d paddle=%d half=%d rule=%d speed=%d gold=%d paused=%d catches=%d\n",
           s_g.state, s_g.level, s_g.lives, (unsigned)s_g.score, s_g.combo, s_g.combo_best, s_g.bricks_left, balls, stuck, drops, (int)lroundf(s_g.paddle_deg),
           (int)lroundf(pc_bo_half(&s_g)), s_g.rule, s_g.speed, s_gold, s_paused, s_g.catches);
}
/* console `arena game ...` (tests and pictures): "level <n>" lays out level n (the ball on the paddle); "put <brick|all> <kind 1..6> [power 0..2]" makes a
 * brick (or every brick there is) that kind: a power-up, a boom chain or the golden brick on demand */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int n, k, pw = 0;
    char which[8];
    if (sscanf(arg, "level %d", &n) == 1) pc_bo_level(&s_g, n);
    else if (sscanf(arg, "put %7s %d %d", which, &k, &pw) >= 2 && k >= PC_BK_NORMAL && k <= PC_BK_BOSS) {
        int from = strcmp(which, "all") ? atoi(which) : 0, to = strcmp(which, "all") ? from : PC_BO_BRICKS - 1;
        for (int i = from; i <= to && i < PC_BO_BRICKS; i++) {
            pc_bo_brick_t *b = &s_g.brick[i];
            if (!b->kind && strcmp(which, "all")) s_g.bricks_left++;
            if (!b->kind && !strcmp(which, "all")) continue;
            int hp = k == PC_BK_TOUGH ? 2 : k == PC_BK_BOSS ? 3 : 1;
            *b = (pc_bo_brick_t){ (uint8_t)k, (uint8_t)hp, (uint8_t)hp, (uint8_t)(pw % PC_PW_N) };
        }
    }
    else { printf("APP game breakout cmd unknown %s\n", arg); return; }
    feed();
    sync();
    show_status();
}
static const ui_arena_def_t ARENA = { .name = "breakout", .objs = ID_N, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    const game_style_t *st = style();
    game_style_field(st, &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, px(PC_BO_PAD_IN) + 1, px(PC_BO_PAD_OUT) - 1, s_pal.track);
    feed();
    sync();
}

/* roll: a new golden decision (重來, again after the end); a rule, a speed, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("breakout", s_rounds++);
    pc_bo_init(&s_g, s_sel.rule, SPEED_OF[s_sel.mode], games_seed("breakout", s_games++));
    s_paused = false;
    ui_arena_unpause(s_arena);                                                  /* fix round 1 m1: a pause of the old game is gone too */
    ui_arena_clear(s_arena);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    s_t0 = lv_tick_get();
    show_status();
    printf("APP game breakout new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_FAST) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_game(what == GR_AGAIN);                   /* a speed, a rule, 重來: a new game (only 重來 rolls the golden game) */
}
static const game_ring_t RING = { .id = "breakout", .modes = 3, .mode_text = mode_text, .rules = PC_BO_RULES, .rule_name = rule_name, .changed = changed };

static void exit_pause(void *u) { (void)u; ui_arena_pause(s_arena); }
static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; ui_arena_resume(s_arena); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* G4b: taps nudge the paddle and the knob steers  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

const ui_screen_t scr_game_breakout = { .name = "tools_game_breakout", .create = create, .on_knob = ui_arena_knob, .content = true };
