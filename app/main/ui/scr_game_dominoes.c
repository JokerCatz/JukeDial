// SPDX-License-Identifier: LGPL-2.1-or-later
/* Dominoes (ideas G4): the shared piles (ui_cards: the device's hand face down at the top, the chain in snaking rows in the middle, your hand fanned at the
 * bottom), the game's style, the result card, the HEAD row (the device's thinking time; the rule, which opens the game's ring: 規則 Classic / Block / Fives, 風格, 重來,
 * 說明), the status line and Back. Rules and the device's choice: pc_game_dominoes (host-tested). Tap a tile in your hand (it lifts, the ends it fits are ringed),
 * then tap the chain: the end nearest your tap (a tile that fits one end goes there wherever you tap). A turn with nothing to play draws by itself (a tile at a
 * time) or passes; the device 'thinks' with three pulsing dots, then plays, draws or passes. A taps-only game: a knob turn never acts on a board with a knob; on the
 * FakePod the keys do (+ the next playable tile, - plays it). The golden double (1 game in 10): one tile is gold, playing it rings and gives +10 in Fives; the golden
 * game (the shared roll) is the gold style. Only composes shared pieces (ui_cards, game_result, game_ring, game_style). State: PSRAM statics (this file is built
 * with -msmall-data-limit=0). */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_dominoes.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_axis.h"
#include "ui_cards.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

enum { P_OPP = 0, P_CHAIN, P_HAND };
#define DRAW_MS 450                                    /* a tile drawn or a pass: the next step */

const ui_game_help_t game_help_dominoes = {
    .zh = "雙六骨牌，共 28 塊，你和電腦各 7 塊。誰有最大的對子（沒有就是點數最多的一塊）誰先下，那塊自動放在中間。輪到你時，先點下方手上的一塊（它會抬起來，"
          "接得上的那一頭會亮框），再點中間那一串：有兩頭都接得上時，點靠近哪一頭就接哪一頭，只有一頭接得上就接那一頭。接上的點數要和那一頭一樣。"
          "沒有牌可以出的時候會自動抽牌（經典和五點）直到抽到能出的，牌堆空了或阻塞賽就直接過牌。手上的牌先出完的贏；兩邊都過牌（卡住了）就比誰手上的點數少。"
          "上方右邊的格子打開選單：規則有三種——經典；阻塞賽：沒有牌堆，不能抽；五點：每下一塊，如果兩個開口的點數加起來是 5 的倍數就得那麼多分（對子在開口算兩邊），"
          "贏的人再得對手剩下點數（四捨五入到 5），最後比分數。左邊的格子換電腦思考的快慢。風格換骨牌的顏色。偶爾有一塊金色的對子，下它有加成（五點加 10 分）。"
          "偶爾會出現金色局；對電腦連贏三局有連勝獎勵",
    .en = "A double-six set, 28 tiles, seven each. Whoever holds the highest double (else the tile with the most pips) opens, and that tile goes down by itself. "
          "On your turn tap a tile in your hand (it lifts, and the ends it fits are ringed), then tap the chain: when both ends fit, the end nearest your tap is "
          "used; when only one fits, that one. The touching numbers must match. With nothing to play you draw by yourself (Classic and Five) until you can, and pass "
          "when the boneyard is empty or in Block. The first empty hand wins; if both sides pass (blocked) the fewer pips win. The right cell at the top opens the menu. "
          "Rules: Classic; Block: no boneyard, no drawing; Five: after each tile, when the sum of the two open ends is a multiple of 5 you score it (a double at an end "
          "counts both halves), the winner also scores the other side's pips rounded to 5, and the higher score wins. The left cell changes how long the device thinks. "
          "Style changes the tiles' colours. Now and then one double is golden: playing it is a bonus (+10 in Five). Now and then a game is golden; three wins in a row "
          "against the device earn a streak",
};

static lv_obj_t *s_cards, *s_status;
static lv_timer_t *s_tm;
static pc_dom_t s_g;
static game_sel_t s_sel;                               /* mode: the device's thinking time (0 normal, 1 quick, 2 slow); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static bool s_gold;
static uint32_t s_games, s_rounds, s_t0;               /* games dealt (the deal's seed) / games started (the golden game's seed) */
static uint8_t s_hid[PC_DOM_TILES];                    /* the tile of each card of your hand, in the pile's order */
static int s_nh;
static char s_stat[72], s_note[40];
static const char *const RULE_ZH[PC_DOM_RULES] = { "經典", "阻塞賽", "五點" }, *const RULE_EN[PC_DOM_RULES] = { "Classic", "Block", "Five" };
static const int THINK_MS[3] = { 700, 250, 1300 };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static bool your_turn(void) { return s_g.state == PC_DOM_PLAY && s_g.turn == PC_DOM_YOU && pc_dom_can_move(&s_g, PC_DOM_YOU); }

static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_g.state == PC_DOM_OVER) snprintf(b, sizeof b, "%s", s_g.winner == PC_DOM_YOU ? T(S_YOU_WIN_2) /* You won */ : s_g.winner == PC_DOM_DEV ? T(S_DEVICE_WINS) /* The computer won */ : T(S_DRAW_2) /* Draw */);
    else if (s_note[0]) snprintf(b, sizeof b, "%s", s_note);
    else if (s_g.rule == PC_DOM_FIVE) snprintf(b, sizeof b, T(S_YOU_D_D) /* You %d - %d */, s_g.score[0], s_g.score[1]);
    else snprintf(b, sizeof b, "%s", your_turn() ? T(S_YOUR_TURN) /* Your turn */ : T(S_THINKING_2) /* Computer thinking */);
    ui_frame_hint(s_status, s_g.state != PC_DOM_OVER && !s_note[0] && s_g.rule != PC_DOM_FIVE && your_turn()); /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}
static void note(const char *t) { snprintf(s_note, sizeof s_note, "%s", t); }

static void ends_for(int idx)
{
    int m = 0;
    if (idx >= 0 && idx < s_nh && s_g.state == PC_DOM_PLAY) m = pc_dom_legal(&s_g, s_hid[idx]);
    ui_cards_ends(s_cards, P_CHAIN, m);
}
static void refresh(void)
{
    uint16_t f[PC_DOM_TILES];
    uint8_t fl[PC_DOM_TILES];
    int n = pc_dom_count(s_g.hand[PC_DOM_DEV]);
    for (int i = 0; i < n; i++) { f[i] = 0; fl[i] = UI_CARD_DOWN; }
    ui_cards_set(s_cards, P_OPP, f, fl, n);
    for (int i = 0; i < s_g.chain_n; i++) { f[i] = (uint16_t)(s_g.cl[i] * 7 + s_g.cr[i]); fl[i] = s_g.chain[i] == s_g.gold_tile ? UI_CARD_GOLD : 0; }
    ui_cards_set(s_cards, P_CHAIN, f, fl, s_g.chain_n);
    const bool mine = s_g.state == PC_DOM_PLAY && s_g.turn == PC_DOM_YOU;
    const uint32_t ok = mine ? pc_dom_playable(&s_g, PC_DOM_YOU) : 0;
    s_nh = 0;
    for (int id = 0; id < PC_DOM_TILES; id++) {
        if (!(s_g.hand[PC_DOM_YOU] & (1u << id))) continue;
        s_hid[s_nh] = (uint8_t)id;
        f[s_nh] = (uint16_t)(pc_dom_a(id) * 7 + pc_dom_b(id));
        fl[s_nh] = (uint8_t)((id == s_g.gold_tile ? UI_CARD_GOLD : 0) | ((ok >> id & 1) ? UI_CARD_HI : 0));
        s_nh++;
    }
    ui_cards_set(s_cards, P_HAND, f, fl, s_nh);
    ui_cards_ends(s_cards, P_CHAIN, 0);
    ui_cards_enabled(s_cards, your_turn());
    ui_cards_think(s_cards, P_OPP, s_g.state == PC_DOM_PLAY && s_g.turn == PC_DOM_DEV);
    show_status();
}

static void again(void);
static void finish(void)
{
    static char title[24];
    const int out = s_g.winner == PC_DOM_YOU ? PC_GAME_WIN : s_g.winner == PC_DOM_DEV ? PC_GAME_LOSS : PC_GAME_DRAW;
    snprintf(title, sizeof title, "%s", out == PC_GAME_WIN ? T(S_YOU_WIN) /* You won! */ : out == PC_GAME_LOSS ? T(S_DEVICE_WINS) /* The computer won */ : T(S_DRAW_2) /* Draw */);
    const game_end_t e = { .id = "dominoes", .variant = s_g.rule, .outcome = out, .vs_device = true, .moves = s_g.plays[PC_DOM_YOU], .ms = lv_tick_elaps(s_t0),
                           .title = title, .gold = s_gold, .style = style() };
    ui_haptic_strong();
    game_result_end(s_cards, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll */
    if (s_tm) lv_timer_pause(s_tm);
}

/* what a placed tile makes: the sound (the most important), the in-field note, haptics */
static void played(int who, int gain)
{
    char b[sizeof s_note];
    if (s_g.last_gold) { sfx_play("gold"); ui_haptic_strong(); note(T(S_GOLDEN_TILE) /* Golden domino! */); return; }
    if (gain > 0) {
        sfx_play("coin");
        snprintf(b, sizeof b, who == PC_DOM_YOU ? T(S_D) /* Scored %d */ : T(S_DEVICE_D) /* Computer scored %d */, gain);
        note(b);
        ui_haptic_tap();
        return;
    }
    sfx_play("place");
    if (who == PC_DOM_YOU) { s_note[0] = 0; ui_haptic_tick(); } else note(T(S_DEVICE_PLAYS) /* Computer plays */);
}
static void schedule(int ms)
{
    if (!s_tm) return;
    lv_timer_set_period(s_tm, (uint32_t)ms);
    lv_timer_reset(s_tm);
    lv_timer_resume(s_tm);
}
/* one step of whoever is to move when it is not a tap of yours: the device's move, a draw or a pass of either side; stops when it is your turn to play */
static void advance(void)
{
    if (s_g.state != PC_DOM_PLAY) { if (s_tm) lv_timer_pause(s_tm); return; }
    const int who = s_g.turn;
    if (pc_dom_can_move(&s_g, who)) {
        if (who == PC_DOM_YOU) { if (s_tm) lv_timer_pause(s_tm); refresh(); return; }
        int id, side;
        if (pc_dom_ai(&s_g, &id, &side)) played(who, pc_dom_place(&s_g, who, id, side));
    } else if (pc_dom_draw(&s_g, who) >= 0) {
        sfx_play("tick");
        note(who == PC_DOM_YOU ? T(S_YOU_DRAW) /* You drew a tile */ : T(S_DEVICE_DRAWS) /* Computer draws */);
    } else if (pc_dom_pass(&s_g, who)) {
        note(who == PC_DOM_YOU ? T(S_YOU_PASS) /* You pass */ : T(S_DEVICE_PASSES) /* Computer passes */);
    }
    refresh();
    if (s_g.state == PC_DOM_OVER) { finish(); return; }
    if (your_turn()) { if (s_tm) lv_timer_pause(s_tm); return; }
    schedule(s_g.turn == PC_DOM_DEV && pc_dom_can_move(&s_g, PC_DOM_DEV) ? THINK_MS[s_sel.mode % 3] : DRAW_MS);
}
static void tm_cb(lv_timer_t *t) { (void)t; advance(); }

/* a tile of your hand onto an end: false = refused (a tick) */
static bool you_place(int tile, int side)
{
    const int gain = pc_dom_place(&s_g, PC_DOM_YOU, tile, side);
    if (gain < 0) { sfx_play("refuse"); ui_haptic_tick(); return false; }
    played(PC_DOM_YOU, gain);
    refresh();
    if (s_g.state == PC_DOM_OVER) finish(); else schedule(THINK_MS[s_sel.mode % 3]);
    return true;
}
static bool on_move(int sp, int si, int dp, int di, void *u)
{
    (void)u;
    if (sp != P_HAND || dp != P_CHAIN || si < 0 || si >= s_nh || !your_turn()) return false;
    const int tile = s_hid[si], lg = pc_dom_legal(&s_g, tile);
    int side = (lg == 3) ? ((di >= 0 && di * 2 < s_g.chain_n) ? PC_DOM_LEFT : PC_DOM_RIGHT) : (lg & 1) ? PC_DOM_LEFT : PC_DOM_RIGHT;
    if (!lg) { sfx_play("refuse"); ui_haptic_tick(); note(T(S_DOES_NOT_FIT) /* Does not fit */); show_status(); return false; }
    return you_place(tile, side);
}
static void on_select(int pile, int idx, void *u) { (void)u; ends_for(pile == P_HAND ? idx : -1); }

static const ui_cards_def_t CARDS = {
    .set = UI_CARDS_SET_DOMINO, .piles = 3,
    .pile = {
        [P_OPP] = { .layout = UI_PILE_FAN, .x0 = 0, .y0 = 0, .x1 = 1000, .y1 = 130, .scale_pct = 100 },
        [P_CHAIN] = { .layout = UI_PILE_SNAKE, .x0 = 0, .y0 = 160, .x1 = 1000, .y1 = 640, .cap = PC_DOM_TILES, .dst = true },
        [P_HAND] = { .layout = UI_PILE_FAN, .x0 = 0, .y0 = 660, .x1 = 1000, .y1 = 1000, .scale_pct = 100, .src = true },
    },
    .select = on_select, .move = on_move,
};

/* FakePod's keys (no knob): + the next playable tile, - plays the selected tile (the left end when both fit) */
static bool keys(int d)
{
    if (ui_arena_has_knob() || !d || !your_turn()) return false;
    int p, cur = -1;
    if (ui_cards_selected(s_cards, &p, &cur) && p != P_HAND) cur = -1;
    if (d > 0) {
        for (int k = 1; k <= s_nh; k++) {
            const int i = (cur + k) % s_nh;
            if (pc_dom_legal(&s_g, s_hid[i])) { ui_haptic_tick(); ui_cards_select(s_cards, P_HAND, i); break; }
        }
    } else if (cur >= 0) {
        const int lg = pc_dom_legal(&s_g, s_hid[cur]);
        you_place(s_hid[cur], (lg & 1) ? PC_DOM_LEFT : PC_DOM_RIGHT);
    }
    return true;
}
static bool game_knob(int d) { return ui_axis_knob(d) || keys(d); }

static void apply_style(void)
{
    ui_cards_set_style(s_cards, style());
}
/* roll: a new golden decision (重來, again after the end); a rule, a thinking time, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("dominoes", s_rounds++);
    pc_dom_init(&s_g, s_sel.rule, games_seed("dominoes", s_games++));
    s_note[0] = 0;
    s_stat[0] = 0;
    s_t0 = lv_tick_get();
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    refresh();
    printf("APP game dominoes new rule=%d style=%d mode=%d gold=%d gold_tile=%d start=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold, s_g.gold_tile, s_g.start_tile);
    if (s_g.turn == PC_DOM_DEV || !pc_dom_can_move(&s_g, PC_DOM_YOU)) schedule(THINK_MS[s_sel.mode % 3]);   /* the device opened (or you must draw): its first move */
    else if (s_tm) lv_timer_pause(s_tm);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_QUICK) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) { apply_style(); return; }
    new_game(what == GR_AGAIN);
}

static void dump(void)
{
    printf("APP game dominoes state=%d turn=%d rule=%d chain=%d yard=%d hand=%d,%d left=%d right=%d score=%d,%d winner=%d gold_tile=%d plays=%d,%d passes=%d ends=%d think=%d\n",
           s_g.state, s_g.turn, s_g.rule, s_g.chain_n, s_g.yard_n, pc_dom_count(s_g.hand[0]), pc_dom_count(s_g.hand[1]), pc_dom_left(&s_g), pc_dom_right(&s_g),
           s_g.score[0], s_g.score[1], s_g.winner, s_g.gold_tile, s_g.plays[0], s_g.plays[1], s_g.passes, pc_dom_legal(&s_g, s_nh ? s_hid[0] : 0), s_tm && !lv_timer_get_paused(s_tm));
}
/* console `games dom ...` (tests and pictures): "" dumps; "dev" the device (or an automatic draw / pass) steps now; "auto <n>" both sides play by the device's rules
 * until the chain holds n tiles and it is your turn with a tile to play (no sound, no effects); "gold <tile|-1>" the golden double; "hand <i>" taps your i-th tile;
 * "end <l|r>" taps the chain's left / right end; "key <d>" a key press; "deal <n>" a new game with deal seed n; "hold" stops the device's timer and its dots */
static bool cmd(const char *arg)
{
    if (strncmp(arg, "dom", 3) || (arg[3] && arg[3] != ' ')) return false;
    arg += arg[3] ? 4 : 3;
    int n;
    char c;
    if (!strcmp(arg, "dev")) { if (s_tm) lv_timer_pause(s_tm); advance(); }
    else if (sscanf(arg, "auto %d", &n) == 1) {
        for (int guard = 0; s_g.state == PC_DOM_PLAY && guard < 200 && !(s_g.chain_n >= n && your_turn()); guard++) {
            const int who = s_g.turn;
            int id, side;
            if (pc_dom_ai(&s_g, &id, &side)) pc_dom_place(&s_g, who, id, side);
            else if (pc_dom_draw(&s_g, who) < 0) pc_dom_pass(&s_g, who);
        }
        note("");
        refresh();
        if (s_g.state == PC_DOM_OVER) finish(); else if (s_tm) lv_timer_pause(s_tm);
    }
    else if (sscanf(arg, "gold %d", &n) == 1) { s_g.gold_tile = (int8_t)n; refresh(); }
    else if (sscanf(arg, "hand %d", &n) == 1) ui_cards_tap(s_cards, P_HAND, n);
    else if (sscanf(arg, "end %c", &c) == 1) ui_cards_tap(s_cards, P_CHAIN, c == 'l' ? 0 : s_g.chain_n - 1);
    else if (sscanf(arg, "key %d", &n) == 1) printf("APP game dominoes key %d took=%d\n", n, keys(n));
    else if (sscanf(arg, "deal %d", &n) == 1) { s_games = (uint32_t)n; new_game(false); }
    else if (!strcmp(arg, "hold")) { if (s_tm) lv_timer_pause(s_tm); ui_cards_think(s_cards, P_OPP, false); }
    else if (arg[0]) { printf("APP game dominoes cmd unknown %s\n", arg); return true; }
    dump();
    return true;
}
static const game_ring_t RING = { .id = "dominoes", .modes = 3, .mode_text = mode_text, .rules = PC_DOM_RULES, .rule_name = rule_name, .changed = changed, .cmd = cmd };

static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; }
static bool in_flight(void *u) { (void)u; return s_g.state == PC_DOM_PLAY; }   /* CF2: see simon */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .resume = exit_resume };   /* G4b: taps play, the knob moves the cursor (the computer's timer is paused by the exit)  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    s_tm = NULL;
    ui_obj_guard(ui_cards_create(p, &CARDS, NULL), &s_cards);
    if (s_cards) {
        s_tm = lv_timer_create(tm_cb, DRAW_MS, NULL);
        if (s_tm) { lv_timer_pause(s_tm); ui_timer_guard(s_cards, &s_tm); }     /* deleted and nulled with the page */
    }
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

const ui_screen_t scr_game_dominoes = { .name = "tools_game_dominoes", .create = create, .on_knob = game_knob, .content = true };
