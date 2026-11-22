// SPDX-License-Identifier: LGPL-2.1-or-later
/* 15 puzzle (ideas G4): a sliding-tile puzzle on the shared board (ui_board SQUARE, tiles with a slide), the HEAD row (the hint mode; the rule, which opens
 * the game's ring: 規則 Classic 4x4 / Quick 3x3 / Big 5x5, 風格, 重來, 說明), the status line and Back. Rules: pc_game_puzzle15 (host-tested, solvable shuffles by inversion parity).
 * A tap on a tile next to the blank slides it (ui_board_slide, 200 ms inside the board); a tap anywhere else is refused (a tick, a red pulse). No undo.
 * FakePod (no knob, two keys): the key cursor ui_board draws stands on one tile next to the blank; +1 moves it to the next one, -1 slides it; on a build with a knob a
 * turn is never a move (the borrowed knob first, then the volume).
 * 風格 (one combined value with the board's palette): 0 Classic numbers, 1 Neon letters, 2 Candy numbers on a diagonal gradient (the solved picture is a smooth ramp).
 * The end: every tile glows, a few sparks, then the result card (moves, time, the fewest moves per rule via game_result -> pc_game_score / pc_gamesave); a solve within
 * pc_p15_par moves is a golden solve (golden card), and one game in PC_GAME_GOLD_N is a golden game (gold tiles).
 * Only composes shared pieces (ui_board, game_fx, game_result, game_ring, game_style). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "font.h"
#include "game_fx.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_puzzle15.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_board.h"
#include "ui_exit.h"
#include "ui_kit.h"

#define SLIDE_MS 200

const ui_game_help_t game_help_puzzle15 = {
    .zh = "格子裡有一個空格和一堆打亂的方塊，點空格旁邊的方塊，它就滑進空格。把方塊排回順序（從左上角開始橫排，空格留在右下角）就過關。"
          "上方右邊的格子打開這個遊戲的選單：規則有三種——經典是 4 x 4 的十五拼圖；快是 3 x 3；大是 5 x 5。風格換顏色和方塊上的字（數字、字母，或數字加漸層，"
          "漸層的方塊排好時是一片平滑的顏色）；左邊的格子換成提示，放對位置的方塊會亮起邊框；重來馬上打亂新的一局。每一局都能解開，不能悔棋。"
          "完成後會顯示步數、時間和這個規則最少的步數；用很少的步數解開會得到金色成績，偶爾也會出現金色局",
    .en = "A grid holds a blank and scrambled tiles: tap a tile next to the blank and it slides in. Put the tiles back in order (rows from the top left, the blank "
          "ends at the bottom right) to win. The right cell at the top opens the game's menu. Rules: Classic is the 4 by 4 fifteen puzzle, Quick 3 by 3, Big 5 by 5. "
          "Style changes the colours and what is on the tiles (numbers, letters, or numbers on a gradient that is one smooth ramp when solved); the left cell turns hints "
          "on: a tile in its right place gets a border; Restart scrambles a new game now. Every game can be solved; there is no undo. At the end you see the moves, the "
          "time and the fewest moves for that rule; a solve in very few moves is golden, and now and then a game is golden",
};

static lv_obj_t *s_page, *s_board, *s_fx, *s_status;
static pc_p15_t s_g;
static game_sel_t s_sel;                            /* mode: 0 standard, 1 hints; rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static bool s_gold, s_started;
static uint32_t s_games, s_rounds, s_t0;            /* games started (the shuffle's seed) / rounds (the golden game's seed); lv_tick of the first move */
static int s_built;                                 /* the side of the board that is on the page */
static const char *const RULE_ZH[PC_P15_RULES] = { "經典 4x4", "快 3x3", "大 5x5" }, *const RULE_EN[PC_P15_RULES] = { "Classic 4x4", "Quick 3x3", "Big 5x5" };

static void again(void);
static void tapped(int idx, void *user);

static void apply_style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    ui_board_set_style(s_board, st);
}

/* the tile's text and tint by the style: numbers (0), letters (1), numbers on the diagonal gradient of the tile's home cell (2) */
static void tile_look(int v, char *text, int *tint)
{
    const int n = s_g.n;
    if (s_sel.style == 1) { text[0] = (char)('A' + (v - 1) % 26); text[1] = 0; *tint = 100; return; }
    snprintf(text, 6, "%d", v);
    if (s_sel.style == 2) { int h = v - 1; *tint = (h / n + h % n) * 100 / (2 * (n - 1)); }
    else *tint = 0;
}

static void show_cell(int i, bool all_hi)
{
    int v = s_g.t[i];
    if (!v) { ui_board_set(s_board, i, NULL, UI_CELL_DIM); return; }     /* the blank: a hole */
    char tx[6];
    int tint;
    tile_look(v, tx, &tint);
    ui_board_set_tile(s_board, i, tx, tint, all_hi || (s_sel.mode == 1 && pc_p15_home(&s_g, i)) ? UI_CELL_HI : UI_CELL_NORMAL);
}
static void show_all(void)
{
    const bool done = pc_p15_solved(&s_g);
    for (int i = 0; i < s_g.n * s_g.n; i++) show_cell(i, done);
}

static void show_status(void)
{
    static char b[48];
    if (pc_p15_solved(&s_g)) snprintf(b, sizeof b, T(S_SOLVED_D_MOVES) /* Solved! %d moves */, s_g.moves);
    else if (!s_g.moves) snprintf(b, sizeof b, "%s", T(S_TAP_A_TILE) /* Tap a tile next to the gap */);
    else snprintf(b, sizeof b, T(S_MOVES_D) /* Moves %d */, s_g.moves);
    lv_label_set_text(s_status, b);
}

/* FakePod's key cursor: on the first tile next to the blank (none after the end, and none on a build with a knob) */
static void cursor_reset(void)
{
    int nb[4];
    if (!ui_board_keys(s_board)) return;
    ui_board_set_cursor(s_board, !pc_p15_solved(&s_g) && pc_p15_neighbours(&s_g, nb) > 0 ? nb[0] : -1);
}

static void build_board(void)
{
    game_result_hide();
    if (s_board) { lv_obj_delete(s_board); s_board = NULL; s_fx = NULL; }
    const ui_board_def_t def = { .layout = UI_BOARD_SQUARE, .cols = s_g.n, .rows = s_g.n, .round = false };
    ui_obj_guard(ui_board_create(s_page, &def, tapped, NULL), &s_board);
    if (s_board) lv_obj_move_to_index(s_board, 0);   /* behind the page's other objects: the game's ring (opened while the rule changes) stays on top */
    ui_obj_guard(game_fx_create(s_board), &s_fx);
    s_built = s_g.n;
}

static void new_game(bool roll)
{
    game_result_hide();
    if (s_fx) game_fx_clear(s_fx);
    if (roll) s_gold = game_result_gold("puzzle15", s_rounds++);
    pc_p15_init(&s_g, s_sel.rule, games_seed("puzzle15", s_games++));
    if (s_g.n != s_built || !s_board) build_board();
    ui_board_clear(s_board);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    s_started = false;
    show_all();
    cursor_reset();
    show_status();
    printf("APP game puzzle15 new rule=%d side=%d cell=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_g.n, ui_board_pitch(s_board), s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static void finish(void)
{
    const int N = s_g.n * s_g.n;
    const bool golden_solve = s_g.moves <= pc_p15_par(s_g.rule);
    const game_end_t e = { .id = "puzzle15", .variant = s_g.rule, .outcome = PC_GAME_WIN, .vs_device = false, .moves = s_g.moves, .ms = lv_tick_elaps(s_t0),
                           .title = T(S_SOLVED) /* Solved! */, .gold = s_gold || golden_solve };
    const int corners[4] = { 0, s_g.n - 1, N - s_g.n, N - 2 };
    int all[PC_P15_MAX * PC_P15_MAX], k = 0;
    for (int i = 0; i < N; i++) if (s_g.t[i]) all[k++] = i;
    game_fx_glow(s_fx, all, k, false);
    for (int c = 0; c < 4; c++) game_fx_burst(s_fx, corners[c]);
    ui_haptic_strong();
    game_result_end(s_board, &e, again);
    s_gold = false;                                 /* the golden game is over (its card keeps it): the next one is golden only by a new roll */
}

static void move(int idx)
{
    const int from = s_g.blank;                     /* the tile goes from idx into the blank */
    if (!pc_p15_move(&s_g, idx)) return;
    if (!s_started) { s_started = true; s_t0 = lv_tick_get(); }
    show_all();
    ui_board_slide(s_board, from, idx, SLIDE_MS);   /* the tile now in `from` comes in from `idx` */
    const bool done = pc_p15_solved(&s_g);
    if (!done) { ui_haptic_tap(); sfx_play("place"); }
    cursor_reset();
    if (done) finish();
    show_status();
    printf("APP game puzzle15 move=%d moves=%d blank=%d solved=%d\n", idx, s_g.moves, s_g.blank, done);
}

static void tapped(int idx, void *user)
{
    (void)user;
    if (pc_p15_solved(&s_g)) { ui_haptic_tap(); new_game(true); return; }
    if (idx == s_g.blank) return;                   /* the hole: nothing */
    if (!pc_p15_movable(&s_g, idx)) {               /* a tile that is not next to the blank: refused, not a dead tap */
        ui_haptic_tick();
        sfx_play("refuse");
        game_fx_glow(s_fx, &idx, 1, true);
        printf("APP game puzzle15 refused=%d\n", idx);
        return;
    }
    move(idx);
}

static const char *mode_text(int v) { return v == 0 ? T(S_STANDARD) /* Standard */ : T(S_HINTS) /* Hint */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) { apply_style(); show_all(); }
    else if (what == GR_MODE) { show_all(); }
    else new_game(what == GR_AGAIN);                /* a rule, 重來: a new game (only 重來 rolls the golden game) */
}

/* the game's console words (tests, pictures, the leak action): "count <n>" the shuffle's seed counter (a new game); "set <t0> <t1> ..." the tiles of the cells
 * (a permutation of 0..n*n-1, 0 the blank; moves 0); "almost [moves]" solved but for one slide: tap the last cell; "state" prints
 * "APP game puzzle15 state n= moves= blank= solved= movable= tiles=<a,b,...>" */
static void print_state(void)
{
    char b[PC_P15_MAX * PC_P15_MAX * 3 + 1];
    int k = 0, nb[4], N = s_g.n * s_g.n;
    for (int i = 0; i < N; i++) k += snprintf(b + k, sizeof b - (size_t)k, i ? ",%d" : "%d", s_g.t[i]);
    int m = pc_p15_neighbours(&s_g, nb);
    printf("APP game puzzle15 state n=%d moves=%d blank=%d solved=%d movable=%d cursor=%d tiles=%s\n", s_g.n, s_g.moves, s_g.blank, pc_p15_solved(&s_g), m,
           ui_board_cursor(s_board), b);
}
static void restate(void)
{
    game_result_hide();
    if (s_fx) game_fx_clear(s_fx);
    s_started = false;
    show_all();
    cursor_reset();
    show_status();
}
static bool game_cmd(const char *a)
{
    unsigned n;
    unsigned m = 1;
    const int N = s_g.n * s_g.n;
    if (sscanf(a, "count %u", &n) == 1) { s_games = n; new_game(false); return true; }
    if (!strcmp(a, "state")) { print_state(); return true; }
    if (!strncmp(a, "almost", 6)) {
        if (sscanf(a, "almost %u", &m) != 1) m = 40;
        pc_p15_solved_state(&s_g, s_sel.rule);
        s_g.t[N - 1] = (uint8_t)(N - 1); s_g.t[N - 2] = 0; s_g.blank = (uint8_t)(N - 2);
        s_g.moves = (uint16_t)m;
        restate();
        return true;
    }
    if (!strncmp(a, "set ", 4)) {
        uint8_t t[PC_P15_MAX * PC_P15_MAX];
        int seen[PC_P15_MAX * PC_P15_MAX] = { 0 }, k = 0;
        const char *p = a + 3;
        while (*p && k < N) {
            char *e;
            long v = strtol(p, &e, 10);
            if (e == p || v < 0 || v >= N || seen[v]) break;
            seen[v] = 1; t[k++] = (uint8_t)v; p = e;
        }
        if (k != N) { printf("APP game puzzle15 set refused\n"); return true; }
        memcpy(s_g.t, t, (size_t)N);
        for (int i = 0; i < N; i++) if (!t[i]) s_g.blank = (uint8_t)i;
        s_g.moves = 0;
        restate();
        return true;
    }
    return false;
}
static const game_ring_t RING = { .id = "puzzle15", .modes = 2, .mode_text = mode_text, .rules = PC_P15_RULES, .rule_name = rule_name, .changed = changed, .cmd = game_cmd };

/* the borrowed knob first (volume otherwise); then, on a build without a knob only, the keys: +1 the cursor to the next tile that can slide, -1 slides the cursor's tile
 * (after the end -1 starts again). With a knob a turn never reaches the puzzle: taps only. */
static bool game_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    if (!s_board || !ui_board_keys(s_board)) return false;
    if (pc_p15_solved(&s_g)) { if (d < 0) again(); return true; }
    int nb[4], m = pc_p15_neighbours(&s_g, nb), cur = ui_board_cursor(s_board);
    if (m <= 0) return true;
    if (d > 0) {
        int k = 0;
        for (int i = 0; i < m; i++) if (nb[i] == cur) k = (i + 1) % m;
        ui_board_set_cursor(s_board, nb[k]);
    } else tapped(cur >= 0 ? cur : nb[0], NULL);
    return true;
}

static void exit_resume(void *u, uint32_t ms) { (void)u; if (s_started) s_t0 += ms; }
static bool in_flight(void *u) { (void)u; return s_started && !pc_p15_solved(&s_g); }   /* CF2: see simon */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .resume = exit_resume };   /* G4b: taps slide, the knob moves the cursor  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_page = p;
    ui_obj_guard(p, &s_page);
    s_board = s_fx = NULL;
    s_built = 0;
    s_sel.mode = 0;
    pc_p15_solved_state(&s_g, s_sel.rule);          /* the side of the board to build; new_game shuffles */
    build_board();
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

const ui_screen_t scr_game_puzzle15 = { .name = "tools_game_puzzle15", .create = create, .on_knob = game_knob, .content = true };
