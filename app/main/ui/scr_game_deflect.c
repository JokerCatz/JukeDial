// SPDX-License-Identifier: LGPL-2.1-or-later
/* Rotating deflector (ideas G6): the shared field (ui_arena) with a core in the middle, a guard line round it and a shield (an arc; the
 * square panel: the pieces of a square's edges) that turns along the line; shots come in from the rim at random angles, aimed at the core: the shield
 * bounces one back (points, the combo widens the shield), one that gets past hits the core (a life). The HEAD row (the speed; the rule, which opens
 * the game's ring: 規則 Classic / Double shield / Bounce, 風格, 重來, 說明), the status line, and the shared exit (mode C: taps AND the knob steer).
 * Rules and geometry: pc_game_deflect (host-tested, both shapes). Steering: the knob (a tap on the field lends it, the borrowed-knob way; FakePod: the
 * keys) one detent = 15 degrees; a tap on the left half of the field turns the shield 15 degrees counter-clockwise, on the right half clockwise; the
 * first tap starts the game. In-field feedback: a bounce = a burst (+ the points when the combo is up), a miss = the rim flashes, two shots broken
 * against each other (bounce) = a bigger burst, a golden shot bounced = +1 life, a level = a ring runs out. The end: the result card (points, level, the
 * best of the session, saved by pc_gamesave). Only composes shared pieces (ui_arena, game_ring, game_result, game_style); state in PSRAM (.bss: this
 * file is built with -msmall-data-limit=0). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/bsp_geom.h"
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_deflect.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

enum { ID_SHOT = 0, ID_SHIELD = PC_DF_SHOTS, ID_CORE = ID_SHIELD + 2 * PC_DF_SEGS, ID_EDGE, ID_N = ID_EDGE + 4 };
#define THICK 8.0f                                     /* the shield's thickness, units */

const ui_game_help_t game_help_deflect = {
    .zh = "場地中間是核心，核心外面有一圈防線，防線上有一片擋板；子彈從場地邊緣的隨機方向飛向核心。"
          "擋板擋到子彈就把它彈回去並得分，子彈從擋板外面溜過去撞到核心就少一條命，三條命用完遊戲結束。"
          "點一下開始；點場地的左半邊，擋板逆時針轉十五度，點右半邊順時針轉十五度；點過場地之後轉動旋鈕也是一格十五度（停下四秒之後回到音量）。"
          "連續擋下子彈不漏接就是連擊：分數加倍，擋板也會跟著變寬，漏接一次就歸零；每擋十發升一級，子彈變快、出得更密。偶爾有金色子彈，擋下來多一條命，漏掉不扣命。"
          "上方右邊的格子打開選單：規則有三種——經典：一片擋板；雙擋：兩片擋板在對面，一起轉，但比較窄；反彈：被彈回去的子彈撞到別的子彈兩顆一起爆開，有額外加分。"
          "方形螢幕上防線是正方形的四條邊，擋板沿著邊走、會轉過角落。左邊的格子換速度。風格換顏色。螢幕關掉或打開選單時遊戲會暫停，點一下繼續。偶爾會出現金色局",
    .en = "A core sits in the middle of the field with a guard line round it and a shield on the line; shots fly at the core from random angles at the "
          "rim. The shield bounces a shot back and scores; a shot that slips past the shield hits the core and costs a life; three lives and the game is "
          "over. Tap to start; a tap on the left half of the field turns the shield 15 degrees counter-clockwise, on the right half clockwise; after "
          "a tap on the field the knob turns it 15 degrees a detent too (four seconds after you stop it is the volume again). Bounces in a row without a "
          "miss are a combo: more points and a wider shield, a miss resets it; every ten bounces is a level, shots get faster and denser. Now and then "
          "a shot is golden: bounce it for a life, miss it for free. The right cell at the top opens the menu. Rules: Classic: one shield; Twin: two "
          "shields opposite, turning together, narrower; Rebound: a bounced shot that meets another shot breaks both for a bonus. On a square screen the "
          "guard line is the four edges of a square and the shield runs along them and round the corners. The left cell changes the speed. Style changes "
          "the colours. With the screen off or the menu open the game pauses; tap to go on. Now and then a game is golden",
};

static lv_obj_t *s_arena, *s_status;
static pc_df_t s_g;
static game_sel_t s_sel;                               /* mode: the speed (0 normal, 1 fast, 2 slow); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static int s_shape;                                    /* the guard line's shape: the display's, set at create (console `arena game shape 0|1` tries the other); no initialiser: .bss, not internal .data */
static bool s_nospawn;                                 /* console `arena game spawn 0|1`: no new shots (pictures, tests) */
static uint32_t s_games, s_rounds, s_t0;              /* seeds: games dealt / games started (the golden game's) */
static char s_stat[64];
static const char *const RULE_ZH[PC_DF_RULES] = { "經典", "雙擋", "反彈" }, *const RULE_EN[PC_DF_RULES] = { "Classic", "Twin", "Rebound" };
static const int SPEED_OF[PC_DF_SPEEDS] = { 1, 2, 0 };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static float scale(void) { return (float)ui_arena_radius(s_arena) / PC_DF_FIELD; }
static int px(float u) { return (int)lroundf(u * scale()); }

/* ---- the objects from the game: only what changed is redrawn (ui_arena compares) ---- */
static void rect_seg(int id, const float *s, lv_color_t c)
{
    const float dx = fabsf(s[2] - s[0]), dy = fabsf(s[3] - s[1]);
    const float cx = (s[0] + s[2]) * 0.5f, cy = (s[1] + s[3]) * 0.5f;
    if (dy < 0.01f) ui_arena_rect(s_arena, id, px(cx), px(cy), LV_MAX(2, px(dx + THICK)), px(THICK), px(THICK * 0.5f), c, LV_OPA_COVER);
    else ui_arena_rect(s_arena, id, px(cx), px(cy), px(THICK), LV_MAX(2, px(dy + THICK)), px(THICK * 0.5f), c, LV_OPA_COVER);
}
static void sync(void)
{
    const float half = pc_df_half(&s_g);
    ui_arena_begin(s_arena);
    for (int k = 0; k < PC_DF_SHOTS; k++) {
        const pc_df_shot_t *b = &s_g.shot[k];
        if (!b->live) { ui_arena_hide(s_arena, ID_SHOT + k); continue; }
        ui_arena_ball(s_arena, ID_SHOT + k, px(b->x), px(b->y), LV_MAX(3, px(PC_DF_SHOT_R)), b->gold ? s_pal.gold : b->back ? s_pal.fx : s_pal.ball, LV_OPA_COVER);
    }
    for (int k = 0; k < 2; k++) {
        const float c = s_g.deg + (float)k * (PC_DF_STEP_DEG * 12.0f);          /* the second shield: opposite (12 steps) */
        float seg[PC_DF_SEGS][4];
        const int n = k < pc_df_shields(&s_g) ? pc_df_shield_segs(s_shape, c, half, seg) : 0;
        for (int i = 0; i < PC_DF_SEGS; i++) {
            const int id = ID_SHIELD + k * PC_DF_SEGS + i;
            if (i < n) rect_seg(id, seg[i], s_pal.player);
            else if (i == 0 && s_shape == PC_DF_ROUND && k < pc_df_shields(&s_g))
                ui_arena_arc(s_arena, id, px(PC_DF_GUARD_R - THICK * 0.5f), px(PC_DF_GUARD_R + THICK * 0.5f), (int)lroundf(c - half), (int)lroundf(c - half) + (int)lroundf(2 * half), true, s_pal.player, LV_OPA_COVER);
            else ui_arena_hide(s_arena, id);
        }
    }
    ui_arena_ball(s_arena, ID_CORE, 0, 0, LV_MAX(4, px(PC_DF_CORE_R)), s_g.lives <= 1 ? s_pal.warn : s_pal.piece, LV_OPA_COVER);
    for (int e = 0; e < 4; e++) {                                               /* the square's guard line (faint) */
        if (s_shape != PC_DF_SQUARE) { ui_arena_hide(s_arena, ID_EDGE + e); continue; }
        const float q = PC_DF_GUARD_Q, l = 2.0f * q + 2.0f;
        const float seg[4][4] = { { -q, -q, q, -q }, { q, -q, q, q }, { -q, q, q, q }, { -q, -q, -q, q } };
        const float cx = (seg[e][0] + seg[e][2]) * 0.5f, cy = (seg[e][1] + seg[e][3]) * 0.5f;
        if (e % 2 == 0) ui_arena_rect(s_arena, ID_EDGE + e, px(cx), px(cy), px(l), LV_MAX(2, px(2)), 0, s_pal.track, LV_OPA_COVER);
        else ui_arena_rect(s_arena, ID_EDGE + e, px(cx), px(cy), LV_MAX(2, px(2)), px(l), 0, s_pal.track, LV_OPA_COVER);
    }
    ui_arena_end(s_arena);
}

static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_DF_SERVE) snprintf(b, sizeof b, "%s", T(S_TAP_TO_START_2) /* Tap to start */);
    else if (s_g.state == PC_DF_OVER) snprintf(b, sizeof b, "%s", T(S_GAME_OVER) /* Game over */);
    else if (s_g.combo >= 2) snprintf(b, sizeof b, T(S_L_D_D_LIVES_U_X_D) /* L%d %d lives %u pts x%d */, s_g.level, s_g.lives, (unsigned)s_g.score, s_g.combo);
    else snprintf(b, sizeof b, T(S_L_D_D_LIVES_U) /* L%d %d lives %u pts */, s_g.level, s_g.lives, (unsigned)s_g.score);
    ui_frame_hint(s_status, s_paused || s_g.state == PC_DF_SERVE);                    /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;        /* the label only when its text changed */
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void finish(void)
{
    const game_end_t e = { .id = "deflect", .variant = s_g.rule, .outcome = PC_GAME_LOSS, .vs_device = false, .moves = s_g.bounces, .ms = lv_tick_elaps(s_t0),
                           .title = T(S_GAME_OVER) /* Game over */, .gold = s_gold, .mode = GAME_SCORE_POINTS, .points = s_g.score, .level = s_g.level, .style = style() };
    ui_haptic_strong();
    game_result_end(s_arena, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll (as the other games) */
}

/* the step's events: every effect, one sound (the most important), graded haptics */
static void events(void)
{
    static const char *const SND[] = { [PC_DF_EV_BOUNCE] = "tap", [PC_DF_EV_MISS] = "hit", [PC_DF_EV_GOLD] = "coin", [PC_DF_EV_LIFE] = "extra_life", [PC_DF_EV_POP] = "boom",
                                       [PC_DF_EV_COMBO] = "combo", [PC_DF_EV_LEVEL] = "level_up", [PC_DF_EV_START] = "move" };
    static const uint8_t RANK[] = { [PC_DF_EV_START] = 1, [PC_DF_EV_BOUNCE] = 2, [PC_DF_EV_POP] = 3, [PC_DF_EV_COMBO] = 4, [PC_DF_EV_GOLD] = 5, [PC_DF_EV_LIFE] = 6,
                                    [PC_DF_EV_LEVEL] = 7, [PC_DF_EV_MISS] = 8, [PC_DF_EV_OVER] = 9 };
    int top = 0, haptic = 0;
    bool over = false;
    for (int i = 0; i < s_g.n_ev; i++) {
        const pc_df_ev_t *v = &s_g.ev[i];
        const int x = px(v->x), y = px(v->y);
        char t[12];
        switch (v->kind) {
        case PC_DF_EV_BOUNCE:
            ui_arena_burst(s_arena, x, y, s_pal.fx, 4);
            if (v->value >= 3) { snprintf(t, sizeof t, "+%d", 10 * (1 + v->value / 5)); ui_arena_text(s_arena, x, y, t, s_pal.fx); }   /* a combo: its points rise */
            haptic = LV_MAX(haptic, 1);
            break;
        case PC_DF_EV_POP: ui_arena_burst(s_arena, x, y, s_pal.boom, 8); snprintf(t, sizeof t, "+%d", v->value); ui_arena_text(s_arena, x, y, t, s_pal.boom); haptic = LV_MAX(haptic, 2); break;
        case PC_DF_EV_COMBO: snprintf(t, sizeof t, "x%d", v->value); ui_arena_text(s_arena, x * 3 / 4, y * 3 / 4, t, s_pal.power); haptic = LV_MAX(haptic, 2); break;
        case PC_DF_EV_GOLD: ui_arena_burst(s_arena, x, y, s_pal.gold, 8); ui_arena_text(s_arena, x, y, "+1", s_pal.gold); haptic = 3; break;
        case PC_DF_EV_MISS: ui_arena_flash(s_arena, v->value ? s_pal.gold : s_pal.warn); haptic = v->value ? haptic : 3; break;
        case PC_DF_EV_LEVEL: ui_arena_wave(s_arena, s_pal.fx); haptic = LV_MAX(haptic, 2); break;
        case PC_DF_EV_OVER: over = true; break;
        default: break;
        }
        if (v->kind < (int)(sizeof RANK) && RANK[v->kind] > RANK[top]) top = v->kind;
    }
    if (over) { finish(); return; }                    /* the end sound is the result card's */
    if (top && top < (int)(sizeof SND / sizeof SND[0]) && SND[top]) sfx_play(SND[top]);
    if (haptic == 3) ui_haptic_strong(); else if (haptic == 2) ui_haptic_tap(); else if (haptic == 1) ui_haptic_tick();
}

/* ---- the field's callbacks ---- */
static void step(void *u)
{
    (void)u;
    if (s_nospawn && s_g.spawn_in < 1000) s_g.spawn_in = 1000000;                 /* held: no new shot */
    pc_df_step(&s_g);
    events();
    sync();
    show_status();
}
static bool running(void *u) { (void)u; return pc_df_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game deflect paused=%d\n", on); }
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)y;
    if (s_g.state == PC_DF_OVER) { if (kind == UI_ARENA_TAP) { ui_haptic_tap(); again(); } return; }
    if (kind == UI_ARENA_STEP) { pc_df_move(&s_g, (float)v * PC_DF_STEP_DEG); sync(); return; }
    if (pc_df_start(&s_g)) { ui_haptic_tap(); events(); ui_arena_wake(s_arena); sync(); show_status(); return; }   /* the first tap starts */
    pc_df_move(&s_g, x < 0 ? -PC_DF_STEP_DEG : PC_DF_STEP_DEG);                 /* the left half turns it counter-clockwise, the right half clockwise */
    ui_haptic_tick();
    sync();
}
static void dump(void *u)
{
    (void)u;
    int shots = 0, backs = 0, golds = 0;
    for (int k = 0; k < PC_DF_SHOTS; k++) { shots += s_g.shot[k].live; backs += s_g.shot[k].live && s_g.shot[k].back; golds += s_g.shot[k].live && s_g.shot[k].gold; }
    printf("APP game deflect state=%d level=%d lives=%d score=%u combo=%d combo_best=%d bounces=%d misses=%d golds=%d shots=%d backs=%d goldshots=%d deg=%d half=%d shields=%d rule=%d speed=%d shape=%d gold=%d paused=%d\n",
           s_g.state, s_g.level, s_g.lives, (unsigned)s_g.score, s_g.combo, s_g.combo_best, s_g.bounces, s_g.misses, s_g.golds, shots, backs, golds, (int)lroundf(s_g.deg),
           (int)lroundf(pc_df_half(&s_g)), pc_df_shields(&s_g), s_g.rule, s_g.speed, s_shape, s_gold, s_paused);
}
/* console `arena game ...` (tests and pictures): "shot <i> <x> <y> <vx> <vy> [gold|back]" puts a shot (units, units a step); "deg <d>" turns the shield there;
 * "kill" no shot at all; "spawn 0|1" the game's own shots off / on; "lives <n>"; "combo <n>"; "shape 0|1" the guard line's shape (0 round, 1 square), the game starts over */
static void new_game(bool roll);
static void cmd(const char *arg, void *u)
{
    (void)u;
    int i, n;
    float x, y, vx, vy;
    char w[8] = "";
    if (sscanf(arg, "shot %d %f %f %f %f %7s", &i, &x, &y, &vx, &vy, w) >= 5 && i >= 0 && i < PC_DF_SHOTS)
        s_g.shot[i] = (pc_df_shot_t){ .x = x, .y = y, .vx = vx, .vy = vy, .live = true, .back = !strcmp(w, "back"), .gold = !strcmp(w, "gold") };
    else if (sscanf(arg, "deg %f", &x) == 1) pc_df_move(&s_g, x - s_g.deg);
    else if (!strcmp(arg, "kill")) memset(s_g.shot, 0, sizeof s_g.shot);
    else if (sscanf(arg, "spawn %d", &n) == 1) s_nospawn = n == 0;
    else if (sscanf(arg, "lives %d", &n) == 1) s_g.lives = LV_CLAMP(1, n, PC_DF_LIVES_MAX);
    else if (sscanf(arg, "combo %d", &n) == 1) s_g.combo = LV_MAX(0, n);
    else if (sscanf(arg, "shape %d", &n) == 1) { s_shape = n ? PC_DF_SQUARE : PC_DF_ROUND; new_game(false); return; }
    else { printf("APP game deflect cmd unknown %s\n", arg); return; }
    sync();
    show_status();
}
static const ui_arena_def_t ARENA = { .name = "deflect", .objs = ID_N, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    const game_style_t *st = style();
    game_style_field(st, &s_pal);
    if (s_shape == PC_DF_ROUND) ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, px(PC_DF_GUARD_R) - 1, px(PC_DF_GUARD_R) + 1, s_pal.track);
    else ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    sync();
}

/* roll: a new golden decision (重來, again after the end); a rule, a speed, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("deflect", s_rounds++);
    pc_df_init(&s_g, s_sel.rule, SPEED_OF[s_sel.mode], s_shape, games_seed("deflect", s_games++));
    s_paused = false;
    ui_arena_unpause(s_arena);                                                  /* a pause of the old game is gone too */
    ui_arena_clear(s_arena);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    s_t0 = lv_tick_get();
    show_status();
    printf("APP game deflect new rule=%d style=%d mode=%d gold=%d shape=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold, s_shape);
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
static const game_ring_t RING = { .id = "deflect", .modes = 3, .mode_text = mode_text, .rules = PC_DF_RULES, .rule_name = rule_name, .changed = changed };

static void exit_pause(void *u) { (void)u; ui_arena_pause(s_arena); }
static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; ui_arena_resume(s_arena); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* taps turn the shield and the knob steers  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    s_nospawn = false;
    s_shape = BSP_LCD_ROUND ? PC_DF_ROUND : PC_DF_SQUARE;              /* a page opened again is the display's shape (the console may have tried the other) */
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

const ui_screen_t scr_game_deflect = { .name = "tools_game_deflect", .create = create, .on_knob = ui_arena_knob, .content = true };
