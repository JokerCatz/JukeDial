// SPDX-License-Identifier: LGPL-2.1-or-later
/* Stack tower (ideas G3): the shared field (ui_arena) with a tower of blocks in a vertical strip; a block slides (or swings) across above it and a tap drops it.
 * The HEAD row (the speed; the rule, which opens the game's ring: 規則 Classic / Narrow / Swing, 風格, 重來, 說明), the status line and Back. Rules and maths:
 * pc_game_tower (host-tested). One tap per block (FakePod's keys, which have no knob, count as a tap; a knob turn never drops a block); the first tap of a game only starts the slide. The overhang is cut off and falls (a piece falling out of the field, with a few particles), the
 * camera scrolls so the tower's top stays in view (in-field motion on the arena's timer, 30 fps). A perfect drop = nothing cut, a coin and "Perfect"; perfects in
 * a row grow the block back (x2, x3 ...). Surprises: the rare golden block (full width back), a milestone every 10 floors (a ring runs out), the golden game
 * (the gold style, the shared roll). The end: the result card (the height, the best of the session and of the card per rule: pc_gamesave through the shared
 * result). Only composes shared pieces (ui_arena, game_ring, game_result, game_style); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_score.h"
#include "pc/pc_game_tower.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

enum { ID_BLOCK = 0, ID_SLIDER = PC_TW_VIS, ID_DEB, ID_N = ID_DEB + PC_TW_DEBRIS };
#define HIDE_Y 80.0f                                   /* units: a piece below this screen y is out of the round field (the strip is narrow, the corners are not) */

const ui_game_help_t game_help_tower = {
    .zh = "一塊積木在塔的上方左右滑動（搖擺規則則像鐘擺一樣擺動），點一下就放下去。放下時超出下面那塊的部分會被切掉、掉出場外，剩下的寬度就是下一塊的寬度；"
          "完全沒有碰到下面那塊，遊戲就結束，結果卡片顯示塔的高度。放得剛剛好（切不到）叫完美，寬度不會變少，有叮的一聲；連續完美的話，積木會一層一層把寬度長回來。"
          "每十層有一個里程碑，塔越高滑得越快。偶爾出現金色的積木，放下去寬度會全部恢復。沒有別的操作：只要點一下場地（或按鍵）；第一下只是開始。"
          "上方右邊的格子打開選單：規則有三種——經典；窄塔：積木一開始的寬度上限每十層減半；搖擺：積木像鐘擺一樣擺，兩端比中間高，抓時機更難。"
          "左邊的格子換速度。風格換顏色。螢幕關掉或打開選單時遊戲會暫停，點一下繼續。偶爾會出現金色局",
    .en = "A block slides across above the tower (in the Swing rule it swings like a pendulum); tap to drop it. What overhangs the block below is cut off and "
          "falls out of the field, and what is left is the width of the next block. If it touches nothing the game is over and the card shows the height. A "
          "drop that cuts nothing is perfect: the width is kept and a coin rings; perfects in a row grow the block back floor by floor. Every ten floors is a "
          "milestone, and the higher the tower the faster the block. Now and then a golden block comes: dropped, it gets its full width back. There is no other "
          "control: tap the field (or press a key); the first tap only starts. The right cell at the top opens the menu. Rules: Classic; Narrow: the widest a "
          "block may start gets halved every ten floors; Swing: the block swings like a pendulum, the ends higher than the middle, so the timing is harder. "
          "The left cell changes the speed. Style changes the colours. With the screen off or the menu open the game pauses; tap to go on. Now and then a "
          "game is golden",
};

static lv_obj_t *s_arena, *s_status;
static pc_tw_t s_g;
static game_sel_t s_sel;                               /* mode: the speed (0 normal, 1 fast, 2 slow); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static uint32_t s_games, s_rounds, s_t0;              /* towers started (the game's seed) / games started (the golden game's seed) */
static char s_stat[64];
static const char *const RULE_ZH[PC_TW_RULES] = { "經典", "窄塔", "搖擺" }, *const RULE_EN[PC_TW_RULES] = { "Classic", "Narrow", "Swing" };
static const float SPEED_OF[3] = { 1.0f, 1.3f, 0.75f };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static float scale(void) { return (float)ui_arena_radius(s_arena) / PC_TW_FIELD; }
static int px(float u) { return (int)lroundf(u * scale()); }

/* ---- the objects from the game: only what changed is redrawn (ui_arena compares) ---- */
static lv_color_t floor_colour(int f)
{
    switch (s_g.kind[f % PC_TW_VIS]) {
    case PC_TW_K_GOLD: return s_pal.gold;
    case PC_TW_K_PERFECT: return s_pal.fx;
    default: { const lv_color_t c[4] = { s_pal.piece, s_pal.tough, s_pal.power, s_pal.boom }; return c[f & 3]; }
    }
}
static void block(int id, float cx, float top, float w, lv_color_t c, lv_opa_t opa)
{
    if (top > HIDE_Y) { ui_arena_hide(s_arena, id); return; }
    ui_arena_rect(s_arena, id, px(cx), px(top + PC_TW_H / 2), LV_MAX(2, px(w)), LV_MAX(3, px(PC_TW_H) - 1), px(2), c, opa);
}
static void sync(void)
{
    ui_arena_begin(s_arena);                           /* one batch: the boxes of what moved merge */
    for (int f = LV_MAX(0, s_g.n - PC_TW_VIS); f < s_g.n; f++) {
        const int i = f % PC_TW_VIS;
        block(ID_BLOCK + i, s_g.cx[i], pc_tw_floor_y(&s_g, f), s_g.w[i], floor_colour(f), LV_OPA_COVER);
    }
    if (s_g.state == PC_TW_OVER) ui_arena_hide(s_arena, ID_SLIDER);
    else block(ID_SLIDER, s_g.sx, pc_tw_slider_y(&s_g), s_g.sw, s_g.gold ? s_pal.gold : s_pal.player, LV_OPA_COVER);
    for (int i = 0; i < PC_TW_DEBRIS; i++) {
        const pc_tw_deb_t *d = &s_g.deb[i];
        if (!d->live) { ui_arena_hide(s_arena, ID_DEB + i); continue; }
        block(ID_DEB + i, d->x, PC_TW_BASE_Y - (d->wy + PC_TW_H) + s_g.cam, d->w, s_pal.player, 190);
    }
    ui_arena_end(s_arena);
}

static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_TW_WAIT) snprintf(b, sizeof b, "%s", T(S_TAP_TO_START_2) /* Tap to start */);
    else if (s_g.state == PC_TW_OVER) snprintf(b, sizeof b, T(S_TOPPLED_D) /* Tower fell · %d floors */, pc_tw_height(&s_g));
    else if (s_g.combo >= 2) snprintf(b, sizeof b, T(S_D_FLOORS_COMBO_D) /* %d floors · combo %d */, pc_tw_height(&s_g), s_g.combo);
    else snprintf(b, sizeof b, T(S_D_FLOORS) /* %d floors */, pc_tw_height(&s_g));
    ui_frame_hint(s_status, s_paused || s_g.state == PC_TW_WAIT);                    /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;        /* the label only when its text changed */
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void finish(void)
{
    const game_end_t e = { .id = "tower", .variant = s_g.rule, .outcome = PC_GAME_LOSS, .vs_device = false, .moves = pc_tw_height(&s_g), .ms = lv_tick_elaps(s_t0),
                           .title = T(S_TOPPLED) /* Tower fell */, .gold = s_gold, .mode = GAME_SCORE_POINTS, .points = (uint32_t)pc_tw_height(&s_g), .level = pc_tw_level(&s_g),
                           .style = style() };
    ui_haptic_strong();
    game_result_end(s_arena, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll (as the other games) */
}

/* the tap's events: in-field effects, one sound (the most important), graded haptics */
static void events(void)
{
    static const char *const SND[] = { [PC_TW_EV_DROP] = "tap", [PC_TW_EV_PERFECT] = "coin", [PC_TW_EV_COMBO] = "combo", [PC_TW_EV_GOLD] = "gold", [PC_TW_EV_MILE] = "level_up" };
    int top = 0, haptic = 0;
    bool over = false;
    for (int i = 0; i < s_g.n_ev; i++) {
        const pc_tw_ev_t *v = &s_g.ev[i];
        const int x = px(v->x), y = px(v->y);
        char t[12];
        switch (v->kind) {
        case PC_TW_EV_DROP:
            if (v->value > 0) {                        /* a cut: the new piece of debris has not moved yet (speed 0); a few particles at its edge */
                for (int k = 0; k < PC_TW_DEBRIS; k++)
                    if (s_g.deb[k].live && s_g.deb[k].vy == 0.0f) ui_arena_burst(s_arena, px(s_g.deb[k].x), y, s_pal.player, 4);
            }
            haptic = LV_MAX(haptic, 1);
            break;
        case PC_TW_EV_PERFECT: ui_arena_text(s_arena, x, y - px(10), T(S_PERFECT) /* Perfect */, s_pal.fx); haptic = LV_MAX(haptic, 2); break;
        case PC_TW_EV_COMBO: snprintf(t, sizeof t, "x%d", v->value); ui_arena_text(s_arena, x, y - px(22), t, s_pal.power); break;
        case PC_TW_EV_GOLD: ui_arena_burst(s_arena, x, y, s_pal.gold, 8); ui_arena_text(s_arena, x, y - px(10), T(S_GOLD) /* Gold */, s_pal.gold); haptic = 3; break;
        case PC_TW_EV_MILE: ui_arena_wave(s_arena, s_pal.fx); haptic = LV_MAX(haptic, 3); break;
        case PC_TW_EV_OVER: ui_arena_flash(s_arena, s_pal.warn); over = true; break;
        default: break;
        }
        static const uint8_t RANK[] = { [PC_TW_EV_DROP] = 1, [PC_TW_EV_PERFECT] = 2, [PC_TW_EV_COMBO] = 3, [PC_TW_EV_GOLD] = 4, [PC_TW_EV_MILE] = 5 };
        if (v->kind < (int)sizeof RANK && RANK[v->kind] > RANK[top]) top = v->kind;
    }
    if (over) { finish(); return; }                    /* the end sound is the result card's */
    if (top && top < (int)(sizeof SND / sizeof SND[0]) && SND[top]) sfx_play(SND[top]);
    if (haptic == 3) ui_haptic_strong(); else if (haptic == 2) ui_haptic_tap(); else if (haptic == 1) ui_haptic_tick();
}

/* ---- the field's callbacks ---- */
static void step(void *u) { (void)u; pc_tw_step(&s_g); sync(); }
static bool running(void *u) { (void)u; return pc_tw_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game tower paused=%d\n", on); }
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)v; (void)x; (void)y;                /* a tap or a key: one action, wherever it is */
    if (kind == UI_ARENA_STEP && ui_arena_has_knob()) return;  /* taps only: with a knob its turns are volume (the arena keeps the borrow while a game runs), never a drop; only the FakePod's keys (no knob) act */
    if (s_g.state == PC_TW_OVER) { if (kind == UI_ARENA_TAP) { ui_haptic_tap(); again(); } return; }
    const bool start = s_g.state == PC_TW_WAIT;
    if (!pc_tw_tap(&s_g)) return;
    if (start) { ui_haptic_tap(); ui_arena_wake(s_arena); }
    else events();
    sync();
    show_status();
}
static void dump(void *u)
{
    (void)u;
    int deb = 0;
    for (int i = 0; i < PC_TW_DEBRIS; i++) deb += s_g.deb[i].live;
    printf("APP game tower state=%d n=%d height=%d level=%d combo=%d combo_best=%d perfects=%d golds=%d gold=%d sx=%d sw=%d top_cx=%d top_w=%d cam=%d speed=%d rule=%d debris=%d paused=%d\n",
           s_g.state, s_g.n, pc_tw_height(&s_g), pc_tw_level(&s_g), s_g.combo, s_g.combo_best, s_g.perfects, s_g.golds, s_g.gold, (int)lroundf(s_g.sx), (int)lroundf(s_g.sw),
           (int)lroundf(s_g.cx[(s_g.n - 1) % PC_TW_VIS]), (int)lroundf(s_g.w[(s_g.n - 1) % PC_TW_VIS]), (int)lroundf(s_g.cam), (int)lroundf(s_g.speed * 100),
           s_g.rule, deb, s_paused);
}
/* console `arena game ...` (tests and pictures): "x <units>" puts the sliding block there (a drop's offset on demand), "w <units>" its width, "gold 1|0" makes
 * it golden or not, "put <n>" lands n perfect floors at once (a tall tower; no sound, no effect) */
static void cmd(const char *arg, void *u)
{
    (void)u;
    float v;
    int n;
    if (sscanf(arg, "x %f", &v) == 1) s_g.sx = v;
    else if (sscanf(arg, "w %f", &v) == 1) s_g.sw = v;
    else if (sscanf(arg, "gold %d", &n) == 1) s_g.gold = n != 0;
    else if (sscanf(arg, "put %d", &n) == 1) {
        if (s_g.state == PC_TW_WAIT) pc_tw_tap(&s_g);
        for (int i = 0; i < n && s_g.state == PC_TW_PLAY; i++) { s_g.gold = false; s_g.sx = s_g.cx[(s_g.n - 1) % PC_TW_VIS]; pc_tw_tap(&s_g); }
        s_g.n_ev = 0;
        s_g.cam = s_g.cam_to;
        ui_arena_wake(s_arena);
    }
    else { printf("APP game tower cmd unknown %s\n", arg); return; }
    sync();
    show_status();
}
static const ui_arena_def_t ARENA = { .name = "tower", .objs = ID_N, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    game_style_field(style(), &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    sync();
}

/* roll: a new golden decision (重來, again after the end); a rule, a speed, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("tower", s_rounds++);
    pc_tw_init(&s_g, s_sel.rule, games_seed("tower", s_games++));
    s_g.speed = SPEED_OF[s_sel.mode % 3];
    s_paused = false;
    ui_arena_unpause(s_arena);
    ui_arena_clear(s_arena);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    s_t0 = lv_tick_get();
    show_status();
    printf("APP game tower new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_FAST) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_game(what == GR_AGAIN);                   /* a speed, a rule, 重來: a new tower (only 重來 rolls the golden game) */
}
static const game_ring_t RING = { .id = "tower", .modes = 3, .mode_text = mode_text, .rules = PC_TW_RULES, .rule_name = rule_name, .changed = changed };

static void exit_pause(void *u) { (void)u; ui_arena_pause(s_arena); }
static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; ui_arena_resume(s_arena); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* G4b: taps are the game  (CF2: popup after a free knob turn) */
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

const ui_screen_t scr_game_tower = { .name = "tools_game_tower", .create = create, .on_knob = ui_arena_knob, .content = true };
