// SPDX-License-Identifier: LGPL-2.1-or-later
/* Tic-tac-toe (games hub G0, game feel G0b): the shared board (3 x 3) with the game's style, its effects and the result card, the HEAD row
 * (who plays; the rule, which opens the game's ring: 規則 Classic / Vanishing / Blind, 風格, 重來, 說明), the status line and Back.
 * Rules and the computer: pc_game_ttt (host-tested); the computer searches for 4 ms per 10 ms tick (a wall-clock budget from esp_timer, the search
 * resumes where it stopped), so the LVGL task never waits on it; its random choices use the game's own seed (games_seed).
 * Vanishing: the mark that goes at its side's next move is dim; when it goes it fades out with a few particles. Blind: a mark hides 2 s after it was placed
 * (it fades); a tap on a hidden mark is refused (a tick, a red pulse). The end: the win line is drawn, the three glow, then the result card.
 * Only composes shared pieces (ui_board, game_fx, game_result, game_ring, game_style). */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "font.h"
#include "game_fx.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_score.h"
#include "pc/pc_game_ttt.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_board.h"
#include "ui_exit.h"
#include "ui_kit.h"

#define AI_TICK_MS 10
#define AI_BUDGET_US 4000                    /* of every tick: the rest is the UI's */
#define AI_NODE_CAP 2000                    /* the second limit of a slice */
#define BLIND_MS 2000                       /* Blind: a mark shows this long */
#define BLIND_TICK_MS 100

const ui_game_help_t game_help_ttt = {
    .zh = "兩人輪流在九宮格裡下 X 和 O，先連成一直線（橫、直、斜都算）的贏；格子滿了沒人連成就是平手。"
          "上方右邊的格子打開這個遊戲的選單：規則有三種——經典；消失版：每邊最多留三個記號，下第四個時自己最舊的那個先消失（快消失的那個會變暗），所以不會平手；"
          "盲棋：記號下完兩秒後就看不見了，要自己記住，點到看不見的記號不算一步。風格換棋盤的顏色和記號的樣子；重來馬上開新的一局。"
          "經典和盲棋裡電腦的下法是完美的，不會輸；消失版裡它往前看七步，贏得了。偶爾會出現金色局；對電腦連贏三局有連勝獎勵",
    .en = "Take turns putting X and O on the 3 by 3 grid; the first to make a line (row, column or diagonal) wins, a full grid with no line is a draw. "
          "The right cell at the top opens the game's menu. Rules: Classic; Vanish: each side keeps three marks, placing a fourth removes your oldest "
          "first (the one about to go is dim), so there are no draws; Blind: a mark disappears two seconds after it is placed, remember where it was; "
          "a tap on a hidden mark is not a move. Style changes the board's colours and marks; Restart starts a new game now. In Classic and Blind the "
          "device plays perfectly and never loses; in Vanish it looks seven moves ahead and can be beaten. Now and then a game is golden; three wins "
          "in a row against the device earn a streak",
};

static int64_t now_us(void *u) { (void)u; return esp_timer_get_time(); }
static const pc_ai_budget_t BUDGET = { .now_us = now_us, .budget_us = AI_BUDGET_US, .node_cap = AI_NODE_CAP };
static lv_obj_t *s_board, *s_fx, *s_status;
static uint32_t s_games, s_rounds;                  /* searches started (the computer's seed) / games started (the golden game's seed) */
static pc_ttt_t s_g;
static pc_ttt_ai_t s_ai;
static lv_timer_t *s_timer, *s_blind;              /* the computer's slices; Blind's check of the marks' 2 s (only in Blind, fix round 1 M5) */
static game_sel_t s_sel;                            /* mode: 0 you are X (move first), 1 you are O (the device starts), 2 two players; rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static bool s_gold;
static uint32_t s_t0, s_placed[PC_TTT_N];           /* lv_tick ms: the game's start, each mark's placing */
static const char *const RULE_ZH[PC_TTT_RULES] = { "經典", "消失版", "盲棋" }, *const RULE_EN[PC_TTT_RULES] = { "Classic", "Vanish", "Blind" };

static int device_side(void) { return s_sel.mode == 0 ? 2 : s_sel.mode == 1 ? 1 : 0; }
static const char *mark_text(int who) { return who == 1 ? "X" : "O"; }
static bool hidden(int i) { return s_g.rule == PC_TTT_BLIND && s_g.cell[i] && !pc_ttt_over(&s_g) && lv_tick_elaps(s_placed[i]) >= BLIND_MS; }

static void apply_style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    ui_board_set_style(s_board, st);
}

static const char *end_text(int w)
{
    if (!w) return T(S_DRAW) /* Draw! */;
    if (s_sel.mode == 2) return w == 1 ? T(S_X_WINS) /* X won! */ : T(S_O_WINS) /* O won! */;
    return w == device_side() ? T(S_DEVICE_WINS) /* The computer won */ : T(S_YOU_WIN) /* You won! */;
}

static void show_status(void)
{
    static char b[48];
    int w = pc_ttt_winner(&s_g, NULL);
    if (pc_ttt_over(&s_g)) snprintf(b, sizeof b, "%s", end_text(w));            /* the end: who won (the result card says the rest) */
    else if (s_g.turn == device_side()) snprintf(b, sizeof b, "%s", T(S_THINKING) /* The computer is thinking... */);
    else if (s_sel.mode == 2) snprintf(b, sizeof b, T(S_S_TO_MOVE) /* %s's turn */, mark_text(s_g.turn));
    else snprintf(b, sizeof b, T(S_YOUR_MOVE_S) /* Your turn (%s) */, mark_text(s_g.turn));
    lv_label_set_text(s_status, b);
}

/* a cell as the rules and the variant show it: hidden (Blind), dim (Vanishing: it goes next), the side's mark, the winning three */
static void show_cell(int i)
{
    int v = s_g.cell[i];
    if (!v || hidden(i)) { ui_board_set(s_board, i, NULL, UI_CELL_NORMAL); return; }
    ui_board_set(s_board, i, mark_text(v), v == 1 ? UI_CELL_A : UI_CELL_B);
    if (i == pc_ttt_fading(&s_g, 1) || i == pc_ttt_fading(&s_g, 2)) ui_board_set_look(s_board, i, UI_CELL_DIM);
}
static void show_all(void)
{
    for (int i = 0; i < PC_TTT_N; i++) show_cell(i);
    int line[3];
    if (pc_ttt_winner(&s_g, line)) for (int k = 0; k < 3; k++) ui_board_set_look(s_board, line[k], UI_CELL_HI);
}

static void again(void);
static void finish(int w)
{
    const game_end_t e = { .id = "ttt", .variant = s_g.rule, .outcome = !w ? PC_GAME_DRAW : (s_sel.mode == 2 || w != device_side()) ? PC_GAME_WIN : PC_GAME_LOSS,
                           .vs_device = s_sel.mode != 2, .moves = s_g.moves, .ms = lv_tick_elaps(s_t0), .title = end_text(w), .gold = s_gold };
    game_result_end(s_board, &e, again);
    s_gold = false;                                 /* the golden game is over (its card keeps it): the next game is golden only by a new roll (fix round 2 N1) */
}

static void blind_tick(lv_timer_t *t);
static void move(int idx);
static void ai_tick(lv_timer_t *t);
static void ai_start(void)
{
    pc_ttt_ai_begin(&s_ai, &s_g, games_seed("ttt", s_games++));
    if (!s_timer) s_timer = lv_timer_create(ai_tick, AI_TICK_MS, NULL);
    game_fx_think(s_fx, true);
}

static void ai_tick(lv_timer_t *t)
{
    if (!pc_ttt_ai_step(&s_ai, &BUDGET)) return;
    s_timer = NULL;                                 /* a one-shot: it ends itself (ui_timer_guard clears the slot only for a live timer) */
    lv_timer_delete(t);
    game_fx_think(s_fx, false);
    printf("APP game ttt ai slices=%d max_us=%d nodes=%d\n", s_ai.slices, s_ai.max_slice_us, s_ai.nodes_total);
    move(pc_ttt_ai_move(&s_ai));
}

static void move(int idx)
{
    int prev[PC_TTT_N];
    for (int i = 0; i < PC_TTT_N; i++) prev[i] = s_g.cell[i];
    if (!pc_ttt_play(&s_g, idx)) return;
    s_placed[idx] = lv_tick_get();
    if (s_g.gone >= 0) {                            /* Vanishing: the oldest mark went: it fades with a few sparks */
        game_fx_fade(s_fx, s_g.gone, prev[s_g.gone], mark_text(prev[s_g.gone]));
        game_fx_burst(s_fx, s_g.gone);
        sfx_play("vanish");
    }
    show_all();
    int line[3];
    int w = pc_ttt_winner(&s_g, line);
    if (w) { game_fx_line(s_fx, line, 3); game_fx_glow(s_fx, line, 3, false); ui_haptic_strong(); }
    else if (pc_ttt_over(&s_g)) ui_haptic_tick();
    else { ui_haptic_tap(); sfx_play("place"); }
    if (pc_ttt_over(&s_g)) finish(w);
    else if (s_g.turn == device_side()) ai_start();
    show_status();
    if (s_g.gone >= 0) printf("APP game ttt move=%d turn=%d moves=%d winner=%d gone=%d\n", idx, s_g.turn, s_g.moves, w, s_g.gone);
    else printf("APP game ttt move=%d turn=%d moves=%d winner=%d\n", idx, s_g.turn, s_g.moves, w);
}

/* roll: a new golden decision (重來, again after a finished game); a rule, a style, who plays and opening the page keep the one there is (fix round 1 M4) */
static void new_game(bool roll)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    game_fx_clear(s_fx);
    game_result_hide();
    if (roll) s_gold = game_result_gold("ttt", s_rounds++);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    pc_ttt_init_rule(&s_g, s_sel.rule);
    if (s_g.rule == PC_TTT_BLIND && !s_blind) s_blind = lv_timer_create(blind_tick, BLIND_TICK_MS, NULL);
    else if (s_g.rule != PC_TTT_BLIND && s_blind) { lv_timer_delete(s_blind); s_blind = NULL; }
    ui_board_clear(s_board);
    s_t0 = lv_tick_get();
    if (s_g.turn == device_side()) ai_start();
    show_status();
    printf("APP game ttt new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static void tapped(int idx, void *user)
{
    (void)user;
    if (pc_ttt_over(&s_g)) { ui_haptic_tap(); new_game(true); return; }
    if (s_g.turn == device_side()) return;          /* the device is thinking */
    if (hidden(idx)) {                              /* Blind: a hidden mark is still there: refused, not a move */
        ui_haptic_tick();
        sfx_play("refuse");
        game_fx_glow(s_fx, &idx, 1, true);
        lv_label_set_text(s_status, T(S_TAKEN) /* That cell is taken */);
        printf("APP game ttt refused=%d\n", idx);
        return;
    }
    move(idx);
}

static void blind_tick(lv_timer_t *t)               /* Blind: a mark whose 2 s are over fades away */
{
    (void)t;
    if (!s_board || s_g.rule != PC_TTT_BLIND) return;
    for (int i = 0; i < PC_TTT_N; i++)
        if (hidden(i) && ui_board_mark(s_board, i)) { game_fx_fade(s_fx, i, s_g.cell[i], mark_text(s_g.cell[i])); show_cell(i); }
}

static const char *mode_text(int v) { return v == 0 ? T(S_YOU_X) /* You are X */ : v == 1 ? T(S_YOU_O) /* You are O */ : T(S_2_PLAYERS) /* Two players */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_game(what == GR_AGAIN);                /* who plays, a rule, 重來: a new game (only 重來 rolls the golden game) */
}
static const game_ring_t RING = { .id = "ttt", .modes = 3, .mode_text = mode_text, .rules = PC_TTT_RULES, .rule_name = rule_name, .changed = changed, .style_fix = 2 };   /* CF2: only the neon style */

/* the borrowed knob first (volume otherwise); then, on a build without a knob only, the keys: +1 the key cursor to the next empty cell, -1 places a mark on the cursor's cell
 * (after the end: a new game) */
static bool game_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    if (!s_board || !ui_board_keys(s_board)) return false;
    const int n = ui_board_cols(s_board) * ui_board_rows(s_board), cur = ui_board_cursor(s_board);
    if (d > 0) {
        for (int k = 1; k <= n; k++) {
            int i = ((cur < 0 ? -1 : cur) + k) % n;
            if (!ui_board_mark(s_board, i)) { ui_board_set_cursor(s_board, i); break; }
        }
    } else tapped(cur >= 0 ? cur : 0, NULL);
    return true;
}

static void exit_resume(void *u, uint32_t ms)
{
    (void)u;
    s_t0 += ms;
    for (int i = 0; i < PC_TTT_N; i++) s_placed[i] += ms;   /* Blind: a mark's 2 s are playing time */
}
static bool in_flight(void *u) { (void)u; return s_g.moves > 0 && !game_result_up() && !pc_ttt_winner(&s_g, NULL); }   /* CF2: see simon */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .resume = exit_resume };   /* G4b: taps place, the knob moves the cursor (the AI's timers are paused by the exit)  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_sel.mode = 0;                                 /* who plays starts as you-first on every open (G0); the rule and the style are kept */
    s_timer = NULL;
    ui_timer_guard(p, &s_timer);
    const ui_board_def_t def = { .layout = UI_BOARD_SQUARE, .cols = 3, .rows = 3, .round = false };
    ui_obj_guard(ui_board_create(p, &def, tapped, NULL), &s_board);
    ui_obj_guard(game_fx_create(s_board), &s_fx);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    s_blind = NULL;
    ui_timer_guard(p, &s_blind);
    new_game(false);
}

static void destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_blind) { lv_timer_delete(s_blind); s_blind = NULL; }
}

const ui_screen_t scr_game_ttt = { .name = "tools_game_ttt", .create = create, .destroy = destroy, .on_knob = game_knob, .content = true };
