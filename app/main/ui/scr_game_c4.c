// SPDX-License-Identifier: LGPL-2.1-or-later
/* Connect four (games hub G0, game feel G0b): the shared board (7 x 6 discs) with the game's style, its effects and the result card, the HEAD
 * row (who plays; the rule, which opens the game's ring: 規則 Classic / Ring / Pop out, 風格, 重來, 說明), the status line and Back.
 * Rules and the computer (6 plies of minimax, 5 in pop out): pc_game_c4 (host-tested). The computer searches for 4 ms per 10 ms tick (a wall-clock budget
 * from esp_timer; the search keeps its place between ticks), so the LVGL task never waits on it; its random choices use the game's own seed (games_seed).
 * A tap on a column drops a disc there (it falls into place); pop out: a tap on the BOTTOM cell of a column whose bottom disc is the mover's pops it
 * (sparks, the column falls one cell), any other tap drops. A full column: a tick and a note. The end: the four glow, the line is drawn (broken where
 * ring wraps across the edge), then the result card. Only composes shared pieces (ui_board, game_fx, game_result, game_ring, game_style). */
#include <stdio.h>
#include "esp_timer.h"
#include "font.h"
#include "game_fx.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_c4.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_board.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

#define AI_TICK_MS 10
#define AI_BUDGET_US 4000                    /* of every tick: the rest is the UI's */
#define AI_NODE_CAP 2000                    /* the second limit of a slice */

const ui_game_help_t game_help_c4 = {
    .zh = "兩人輪流把棋子投進七行中的一行，棋子會落到最底下；先把四顆棋子連成一線（橫、直、斜都算）的贏，棋盤滿了沒人連成就是平手。點任何一格就是投進那一行。"
          "上方右邊的格子打開這個遊戲的選單：規則有三種——經典；環形：最左和最右兩行是相鄰的，橫線和斜線可以繞過邊緣連起來（直線不會）；"
          "彈出：除了投子，也可以點一行最底下那格自己的棋子，把它拿掉，上面的棋子往下掉一格；拿掉之後如果兩邊同時連成四顆，算拿子的人贏，只有對方連成就是對方贏；"
          "這個規則下棋盤滿了還能繼續，下到 200 步算平手。風格換棋盤的顏色和棋子的樣子；重來馬上開新的一局。電腦會往前看幾步，不是無敵的。偶爾會出現金色局；對電腦連贏三局有連勝獎勵",
    .en = "Take turns dropping a disc into one of the seven columns; it falls to the lowest free cell. The first to line up four discs (across, up or "
          "diagonally) wins, a full board with no line is a draw. A tap on any cell drops into its column. The right cell at the top opens the game's "
          "menu. Rules: Classic; Wrap: the leftmost and rightmost columns are neighbours, lines across and diagonals may run over the edge (lines up "
          "do not); Pop out: instead of dropping you may tap the bottom cell of a column holding your own disc to take it out, the discs above fall "
          "one cell; if that makes four for both sides at once the one who popped wins, four for the other side only and the other side wins; the "
          "board may fill and play goes on, 200 moves is a draw. Style changes the board's colours and discs; Restart starts a new game now. The device "
          "looks a few moves ahead and can be beaten. Now and then a game is golden; three wins in a row against the device earn a streak",
};

static int64_t now_us(void *u) { (void)u; return esp_timer_get_time(); }
static const pc_ai_budget_t BUDGET = { .now_us = now_us, .budget_us = AI_BUDGET_US, .node_cap = AI_NODE_CAP };
static lv_obj_t *s_board, *s_fx, *s_status;
static uint32_t s_games, s_rounds;                  /* searches started (the computer's seed) / games started (the golden game's seed) */
static pc_c4_t s_g;
static pc_c4_ai_t s_ai;
static lv_timer_t *s_timer;
static game_sel_t s_sel;                            /* mode: 0 you move first, 1 the device starts, 2 two players; rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static bool s_gold;
static uint32_t s_t0;
static const char *const RULE_ZH[PC_C4_RULES] = { "經典", "環形", "彈出" }, *const RULE_EN[PC_C4_RULES] = { "Classic", "Wrap", "Pop out" };

static int device_side(void) { return s_sel.mode == 0 ? 2 : s_sel.mode == 1 ? 1 : 0; }
static ui_cell_look_t side_look(int who) { return who == 1 ? UI_CELL_A : who == 2 ? UI_CELL_B : UI_CELL_NORMAL; }

static void apply_style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    ui_board_set_style(s_board, st);
}

static const char *end_text(int w)
{
    if (!w) return T(S_DRAW) /* Draw! */;
    if (s_sel.mode == 2) return w == 1 ? T(S_1ST_WINS) /* First player won! */ : T(S_2ND_WINS) /* Second player won! */;
    return w == device_side() ? T(S_DEVICE_WINS) /* The computer won */ : T(S_YOU_WIN) /* You won! */;
}

static void show_status(void)
{
    static char b[48];
    int w = pc_c4_winner(&s_g, NULL);
    if (pc_c4_over(&s_g)) snprintf(b, sizeof b, "%s", end_text(w));            /* the end: who won (the result card says the rest) */
    else if (s_g.turn == device_side()) snprintf(b, sizeof b, "%s", T(S_THINKING) /* The computer is thinking... */);
    else if (s_sel.mode == 2) snprintf(b, sizeof b, "%s", s_g.turn == 1 ? T(S_1ST_TO_MOVE) /* First player's turn */ : T(S_2ND_TO_MOVE) /* Second player's turn */);
    else snprintf(b, sizeof b, "%s", T(S_YOUR_MOVE) /* Your turn */);
    ui_frame_hint(s_status, !pc_c4_over(&s_g) && s_g.turn != device_side() && s_sel.mode != 2); /* GS: an instruction is popup text, the score stays in the content */
    lv_label_set_text(s_status, b);
}

static void again(void);
static void finish(int w)
{
    const game_end_t e = { .id = "c4", .variant = s_g.rule, .outcome = !w ? PC_GAME_DRAW : (s_sel.mode == 2 || w != device_side()) ? PC_GAME_WIN : PC_GAME_LOSS,
                           .vs_device = s_sel.mode != 2, .moves = s_g.moves, .ms = lv_tick_elaps(s_t0), .title = end_text(w), .gold = s_gold };
    game_result_end(s_board, &e, again);
    s_gold = false;                                 /* the golden game is over (its card keeps it): the next game is golden only by a new roll (fix round 2 N1) */
}

static void move(int m);
static void ai_tick(lv_timer_t *t);
static void ai_start(void)
{
    pc_c4_ai_begin(&s_ai, &s_g, games_seed("c4", s_games++), 0);
    if (!s_timer) s_timer = lv_timer_create(ai_tick, AI_TICK_MS, NULL);
    game_fx_think(s_fx, true);
}

static void ai_tick(lv_timer_t *t)
{
    if (!pc_c4_ai_step(&s_ai, &BUDGET)) return;
    s_timer = NULL;                                 /* a one-shot: it ends itself (ui_timer_guard clears the slot only for a live timer) */
    lv_timer_delete(t);
    game_fx_think(s_fx, false);
    printf("APP game c4 ai slices=%d max_us=%d nodes=%d nodes_max=%d\n", s_ai.slices, s_ai.max_slice_us, s_ai.nodes_total, s_ai.nodes_max);
    move(pc_c4_ai_move(&s_ai));
}

/* m: a column (a drop) or PC_C4_POP_MOVE(col) (a pop) */
static void move(int m)
{
    const bool pop = m >= PC_C4_W;
    const int col = m % PC_C4_W;
    int at = pc_c4_play(&s_g, m);
    if (at < 0) return;
    if (pop) {                                      /* the column is new from the bottom up: every disc falls one cell, sparks where the popped one was */
        for (int r = 0; r < PC_C4_H; r++) { int i = r * PC_C4_W + col; ui_board_set(s_board, i, NULL, side_look(s_g.cell[i])); game_fx_stop_cell(s_fx, i); }   /* the cell's new state first, then its drop in flight ends (its unhide shows the new state) */
        game_fx_burst(s_fx, at);
        for (int r = PC_C4_H - s_g.height[col]; r < PC_C4_H; r++) game_fx_drop(s_fx, r * PC_C4_W + col, 1);
        sfx_play("pop_out");
    } else {
        ui_board_set(s_board, at, NULL, side_look(s_g.cell[at]));
        game_fx_drop(s_fx, at, at / PC_C4_W + 1);     /* from just above the top row */
    }
    int line[4];
    int w = pc_c4_winner(&s_g, line);
    if (w) {
        for (int k = 0; k < 4; k++) ui_board_set(s_board, line[k], NULL, UI_CELL_HI);
        game_fx_glow(s_fx, line, 4, false);
        game_fx_line(s_fx, line, 4);
        ui_haptic_strong();
    }
    else if (pc_c4_over(&s_g)) ui_haptic_tick();
    else { ui_haptic_tap(); if (!pop) sfx_play("drop"); }
    if (pc_c4_over(&s_g)) finish(w);
    else if (s_g.turn == device_side()) ai_start();
    show_status();
    printf("APP game c4 move=%d col=%d turn=%d moves=%d winner=%d%s\n", at, col, s_g.turn, s_g.moves, w, pop ? " pop=1" : "");
}

/* roll: a new golden decision (重來, again after a finished game); a rule, a style, who plays and opening the page keep the one there is (fix round 1 M4) */
static void new_game(bool roll)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    game_fx_clear(s_fx);
    game_result_hide();
    if (roll) s_gold = game_result_gold("c4", s_rounds++);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    pc_c4_init_rule(&s_g, s_sel.rule);
    ui_board_clear(s_board);
    s_t0 = lv_tick_get();
    if (s_g.turn == device_side()) ai_start();
    show_status();
    printf("APP game c4 new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static void tapped(int idx, void *user)
{
    (void)user;
    if (pc_c4_over(&s_g)) { ui_haptic_tap(); new_game(true); return; }
    if (s_g.turn == device_side()) return;          /* the device is thinking */
    int col = idx % PC_C4_W;
    if (idx / PC_C4_W == PC_C4_H - 1 && pc_c4_can_pop(&s_g, col)) { move(PC_C4_POP_MOVE(col)); return; }   /* pop out: the bottom cell with the mover's own disc */
    if (s_g.height[col] >= PC_C4_H) {                /* a full column: not a move, but not a dead tap either */
        ui_haptic_tick();
        game_fx_glow(s_fx, &idx, 1, true);
        ui_frame_hint(s_status, true);
        lv_label_set_text(s_status, pc_c4_can_pop(&s_g, col) ? T(S_FULL_TAP_ITS_BOTTOM_TO_POP) /* Full: tap the bottom to remove your own */ : T(S_COLUMN_FULL) /* This column is full */);
        return;
    }
    move(col);
}

static const char *mode_text(int v) { return v == 0 ? T(S_YOU_1ST) /* You go first */ : v == 1 ? T(S_YOU_2ND) /* You go second */ : T(S_2_PLAYERS) /* Two players */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_game(what == GR_AGAIN);                /* who plays, a rule, 重來: a new game (only 重來 rolls the golden game) */
}
static const game_ring_t RING = { .id = "c4", .modes = 3, .mode_text = mode_text, .rules = PC_C4_RULES, .rule_name = rule_name, .changed = changed };

/* the borrowed knob first (volume otherwise); then, on a build without a knob only, the keys: +1 the key cursor to the next column, -1 drops a disc in the cursor's column (after the end: a new game) */
static bool game_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    if (!s_board || !ui_board_keys(s_board)) return false;
    const int cur = ui_board_cursor(s_board);
    if (d > 0) ui_board_set_cursor(s_board, cur < 0 ? 0 : (cur % PC_C4_W + 1) % PC_C4_W);
    else tapped(cur < 0 ? 0 : cur % PC_C4_W, NULL);
    return true;
}

static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; }
static bool in_flight(void *u) { (void)u; return s_g.moves > 0 && !game_result_up() && !pc_c4_winner(&s_g, NULL); }   /* CF2: see simon */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .resume = exit_resume };   /* G4b: taps drop, the knob moves the cursor  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_sel.mode = 0;                                 /* who plays starts as you-first on every open (G0); the rule and the style are kept */
    s_timer = NULL;
    ui_timer_guard(p, &s_timer);
    const ui_board_def_t def = { .layout = UI_BOARD_SQUARE, .cols = PC_C4_W, .rows = PC_C4_H, .round = true };
    ui_obj_guard(ui_board_create(p, &def, tapped, NULL), &s_board);
    ui_obj_guard(game_fx_create(s_board), &s_fx);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

static void destroy(void) { if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; } }

const ui_screen_t scr_game_c4 = { .name = "tools_game_c4", .create = create, .destroy = destroy, .on_knob = game_knob, .content = true };
