// SPDX-License-Identifier: LGPL-2.1-or-later
/* Simon (ideas wave 0a G1): four coloured sectors (ui_sectors) with the hub in the middle and the milestone ring round them, the HEAD row
 * (who plays: Solo / Two players / Practice; the rule, which opens the game's ring: 規則 Classic / Reverse / Shuffle / Speed-up, 風格, 重來, 說明), the status line and Back.
 * The device plays a sequence that grows by one each round (each colour lit with its tone, on a page timer: never blocking), the player repeats it by
 * taps; a wrong tap ends the game with the reached length (the result card: the best length of this session per rule). Rules: pc_game_simon
 * (host-tested); the colours come from the game's own seed (games_seed). Every 5 rounds a fanfare and one more segment of the ring (every 10 a bigger
 * one); Shuffle moves the colours to other sectors every 5 rounds; a wrong tap shows the right sector lit while the others go dim.
 * Only composes shared pieces (ui_sectors, game_result, game_ring, game_style). */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_score.h"
#include "pc/pc_game_simon.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_sectors.h"
#include "ui_frame.h"

#define TAP_MS      220                       /* a tap lights its sector this long */
#define FIRST_MS    700                       /* the start: the first colour comes after this */
#define NEXT_MS     800                       /* after a round: the next one starts after this */
#define MOVED_MS    1300                      /* Shuffle: after the colours moved, a longer look */
#define ANSWER_MS   900                       /* a wrong tap: the right sector stays lit this long */
#define RETRY_MS    1100                      /* Practice: the round is played again after this */
#define RING_SEGS   12                        /* the milestone ring: one segment every PC_SIMON_MILESTONE rounds */

const ui_game_help_t game_help_simon = {
    .zh = "四個顏色的扇形。裝置先亮一個顏色（帶聲音），你照著點；每一輪多一個，記得越長越好。點錯就結束，結果卡寫你記到的長度和這次開機以來的最佳。"
          "點中間或任一色開始。上方左邊的格子換玩法：單人；雙人（兩人輪流接下一輪，點錯的人輸）；練習（點錯不結束，同一輪再放一次）。"
          "右邊的格子打開這個遊戲的選單：規則有四種——經典；倒序：從最後一個倒著點回第一個；洗牌：每五輪顏色會換到別的扇形，要找顏色不是找位置；"
          "加速：每一輪都比上一輪快。風格換顏色；重來馬上重新開始。每五輪有一段號角、外圈多亮一格，每十輪更大聲。偶爾會出現金色局",
    .en = "Four coloured sectors. The device lights a colour (with its tone), you tap it back; each round adds one more, remember as long as you can. "
          "A wrong tap ends the game: the result card shows the length you reached and the best since the device started. Tap the middle or any "
          "colour to start. The left cell at the top changes who plays: Solo; Two players (take turns with the rounds, the one who taps wrong loses); "
          "Practice (a wrong tap does not end it, the round is played again). The right cell opens the game's menu. Rules: Classic; Reverse: tap the "
          "sequence backwards, the last first; Shuffle: every five rounds the colours move to other sectors, follow the colour, not the place; Fast: "
          "every round is faster than the one before. Style changes the colours; Restart starts again now. Every five rounds a fanfare and one more "
          "segment of the outer ring, every ten a bigger one. Now and then a game is golden",
};

enum { P_READY = 0, P_SHOW, P_INPUT, P_OVER };
enum { M_SOLO = 0, M_TWO, M_PRACTICE, M_N };
static const char *const TONES[PC_SIMON_COLOURS] = { "sector_1", "sector_2", "sector_3", "sector_4" };
static const char *const RULE_ZH[PC_SIMON_RULES] = { "經典", "倒序", "洗牌", "加速" }, *const RULE_EN[PC_SIMON_RULES] = { "Classic", "Reverse", "Shuffle", "Fast" };

static lv_obj_t *s_sec, *s_status;
static lv_timer_t *s_timer;                  /* the sequence: the pause before it, each colour, the Practice replay */
static pc_simon_t s_g;
static game_sel_t s_sel;                     /* mode (who plays), rule, style: kept while the device is on (mode back to Solo on every open) */
static game_style_t s_gold_style;
static const game_style_t *s_style;          /* the style in use (the card takes it) */
static bool s_gold;
static bool s_over_taps;                     /* the end's delay is over: a tap on the field or the hub = again */
static uint8_t s_phase, s_k;                 /* P_*; the colour of the round the device lights next */
static uint32_t s_games, s_rounds, s_t0;     /* games started (the colours' seed) / rolls (the golden game's seed); the game's start (lv_tick ms) */

static int whose(void) { return s_g.done % 2 + 1; }   /* Two players: player 1 repeats the odd rounds, player 2 the even ones */

static void apply_style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    s_style = st;
    ui_sectors_set_style(s_sec, st);
}

static void paint(void) { for (int c = 0; c < PC_SIMON_COLOURS; c++) ui_sectors_set_paint(s_sec, s_g.place[c], c); }

static void hub_len(void)
{
    char b[8];
    snprintf(b, sizeof b, "%d", s_g.len);
    ui_sectors_set_hub(s_sec, b);
}

static void status(const char *t) { lv_label_set_text(s_status, t); ui_frame_hint(s_status, false); }
static void status_hint(const char *t) { lv_label_set_text(s_status, t); ui_frame_hint(s_status, true); } /* GS: an instruction is popup text, the score stays in the content */

static void status_turn(void)
{
    static char b[48];
    const char *how = s_g.rule == PC_SIMON_REVERSE ? T(S_TAP_IT_BACKWARDS) /* Tap in reverse */ : T(S_YOUR_TURN_2) /* Your turn */;
    if (s_sel.mode == M_TWO) snprintf(b, sizeof b, T(S_PLAYER_D_S) /* Player %d: %s */, whose(), how);
    else snprintf(b, sizeof b, "%s", s_g.rule == PC_SIMON_REVERSE ? T(S_TAP_IT_BACKWARDS_2) /* Tap it back in reverse */ : T(S_YOUR_TURN_3) /* Your turn */);
    status(b);
}

static void schedule(uint32_t ms)
{
    if (!s_timer) return;
    lv_timer_set_period(s_timer, ms);
    lv_timer_reset(s_timer);
    lv_timer_resume(s_timer);
}

static void show_begin(uint32_t delay)       /* the device plays the round from its first colour after `delay` */
{
    ui_sectors_set_enabled(s_sec, false);
    s_phase = P_SHOW;
    s_k = 0;
    char seq[3 * PC_SIMON_MAX + 1];
    int n = 0;
    for (int k = 0; k < s_g.len && n < (int)sizeof seq - 3; k++) n += snprintf(seq + n, sizeof seq - (size_t)n, k ? ",%d" : "%d", pc_simon_shown(&s_g, k));
    printf("APP game simon show len=%d seq=%s\n", s_g.len, seq);
    if (s_sel.mode == M_TWO) { static char b[40]; snprintf(b, sizeof b, T(S_PLAYER_D_WATCH) /* Player %d: watch */, whose()); status(b); }
    else status(T(S_WATCH) /* Watch... */);
    schedule(delay);
}

static void next_round(uint32_t delay)
{
    bool moved = pc_simon_grow(&s_g);
    if (moved) {                                                 /* Shuffle: the colours moved */
        paint();
        sfx_play("warp");
        printf("APP game simon moved place=%d,%d,%d,%d\n", s_g.place[0], s_g.place[1], s_g.place[2], s_g.place[3]);
        delay = MOVED_MS;
    }
    hub_len();
    show_begin(delay);
    if (moved) status(T(S_THE_COLOURS_MOVED) /* The colours changed places! */);
}

static void timer_cb(lv_timer_t *t)
{
    if (s_phase == P_OVER) { lv_timer_pause(t); s_over_taps = true; ui_sectors_set_enabled(s_sec, true); return; }   /* the card is up: a tap on the field = again, like the card */
    if (s_phase != P_SHOW) { lv_timer_pause(t); return; }
    if (s_k < s_g.len) {
        int on, gap;
        pc_simon_timing(s_g.rule, s_g.len, &on, &gap);
        if (s_k == 0) ui_sectors_set_looks(s_sec, UI_SEC_NORMAL);
        ui_sectors_flash(s_sec, pc_simon_shown(&s_g, s_k), on, true);
        ui_activity();                                           /* the device's turn keeps the screen on like a tap */
        s_k++;
        schedule((uint32_t)(on + gap));
        return;
    }
    lv_timer_pause(t);
    s_phase = P_INPUT;
    ui_sectors_set_enabled(s_sec, true);
    status_turn();
    printf("APP game simon input len=%d\n", s_g.len);
}

static void again(void);
static const char *end_title(bool all)
{
    static char b[40];
    if (all) snprintf(b, sizeof b, T(S_ALL_D) /* Remembered all! */, s_g.done);
    else if (s_sel.mode == M_TWO) snprintf(b, sizeof b, T(S_PLAYER_D_WINS) /* Player %d won! */, 3 - whose());
    else snprintf(b, sizeof b, T(S_REACHED_D) /* Remembered %d */, s_g.done);
    return b;
}

static void finish(int answer)               /* the end: the right sector lit, the others dim, the card (a moment later) */
{
    bool all = s_g.done >= PC_SIMON_MAX;
    s_phase = P_OVER;
    s_over_taps = false;
    ui_sectors_set_enabled(s_sec, false);                        /* until the card shows (a fast finger's next tap must not skip the end) */
    schedule(GAME_RESULT_DELAY_MS);
    if (answer >= 0) { ui_sectors_set_looks(s_sec, UI_SEC_DIM); ui_sectors_set_look(s_sec, answer, UI_SEC_NORMAL); ui_sectors_flash(s_sec, answer, ANSWER_MS, false); }
    ui_haptic_strong();
    const char *title = end_title(all);
    status(title);
    /* Two players keeps no best (variant -1: counted, never a best: a two-player length is not Solo's record) and its end is a win (somebody won: `win`) */
    const game_end_t e = { .id = "simon", .variant = s_sel.mode == M_TWO ? -1 : s_g.rule, .outcome = all || s_sel.mode == M_TWO ? PC_GAME_WIN : PC_GAME_LOSS, .vs_device = false,
                           .moves = s_g.done, .ms = lv_tick_elaps(s_t0), .title = title, .gold = s_gold, .mode = GAME_SCORE_HIGH, .unit = T(S_LENGTH) /* Length */,
                           .style = s_style };
    game_result_end(s_sec, &e, again);
    s_gold = false;                                              /* the golden game is over (its card keeps it); only a roll makes the next one */
}

static void round_done(void)
{
    if (pc_simon_milestone(s_g.done)) {                          /* every 5: a fanfare and a segment of the ring; every 10 a bigger one */
        const char *snd = s_g.done % (2 * PC_SIMON_MILESTONE) == 0 ? "extra_life" : "level_up";
        sfx_play(snd);
        ui_haptic_strong();
        ui_sectors_set_progress(s_sec, s_g.done / PC_SIMON_MILESTONE, RING_SEGS);
        printf("APP game simon milestone=%d sound=%s\n", s_g.done, snd);
    }
    if (s_g.over) { finish(-1); return; }                        /* the longest sequence: all of it remembered */
    next_round(NEXT_MS);
}

static void start(void)
{
    s_t0 = lv_tick_get();
    printf("APP game simon start rule=%d mode=%d\n", s_sel.rule, s_sel.mode);
    next_round(FIRST_MS);
}

static void tapped(int i, void *user)
{
    (void)user;
    if (s_phase == P_READY) { start(); return; }
    if (s_phase == P_OVER) { again(); return; }                 /* after the end a tap on the field restarts, like the card */
    if (s_phase != P_INPUT) return;
    int want = pc_simon_expect(&s_g), r = pc_simon_tap(&s_g, i);
    printf("APP game simon tap=%d result=%d pos=%d len=%d done=%d\n", i, r, s_g.pos, s_g.len, s_g.done);
    if (r == PC_SIMON_ROUND) { ui_sectors_set_enabled(s_sec, false); round_done(); }
    else if (r == PC_SIMON_WRONG && s_sel.mode == M_PRACTICE) {  /* Practice: show the right one, then the same round again */
        sfx_play("error");
        ui_haptic_tick();
        ui_sectors_set_looks(s_sec, UI_SEC_DIM);
        ui_sectors_set_look(s_sec, want, UI_SEC_NORMAL);
        ui_sectors_flash(s_sec, want, ANSWER_MS, false);
        pc_simon_retry(&s_g);
        show_begin(RETRY_MS);
        status_hint(T(S_ONCE_MORE) /* Try again */);
    }
    else if (r == PC_SIMON_WRONG) finish(want);
}

static bool hub_tapped(void *user)
{
    (void)user;
    if (s_phase == P_READY) start();
    else if (s_phase == P_OVER && s_over_taps) again();
    else return false;                                           /* the device's turn or the player's: the hub does nothing (no haptic) */
    return true;
}

/* roll: a new golden decision (重來, again after a finished game); a rule, a style, who plays and opening the page keep the one there is */
static void new_game(bool roll)
{
    if (s_timer) lv_timer_pause(s_timer);
    game_result_hide();
    ui_sectors_unlight(s_sec);
    if (roll) s_gold = game_result_gold("simon", s_rounds++);
    apply_style();
    if (roll && s_gold) sfx_play("sparkle");
    pc_simon_init(&s_g, s_sel.rule, games_seed("simon", s_games++));
    paint();
    ui_sectors_set_looks(s_sec, UI_SEC_NORMAL);
    ui_sectors_set_progress(s_sec, 0, RING_SEGS);
    ui_sectors_set_hub(s_sec, T(S_START) /* Start */);
    ui_sectors_set_enabled(s_sec, true);
    s_phase = P_READY;
    status_hint(T(S_TAP_TO_START_3) /* Tap the middle to start */);
    printf("APP game simon new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); start(); }

static const char *mode_text(int v) { return v == M_SOLO ? T(S_SOLO) /* Solo */ : v == M_TWO ? T(S_2_PLAYERS) /* Two players */ : T(S_PRACTICE) /* Practice */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else if (what == GR_AGAIN) again();                         /* 重來: a new game now (rolls the golden game) */
    else new_game(false);                                        /* who plays, a rule: ready for a new game */
}
/* the game's console words (tests, the leak action, pictures): "right" taps the sector the game wants now, "wrong" another one, as a finger would (nothing when
 * it is not the player's turn); "count <n>" sets the game's own count (its next seed) and makes a ready game: a picture independent of what ran before */
static bool game_cmd(const char *a)
{
    unsigned n;
    if (sscanf(a, "count %u", &n) == 1) { s_games = n; new_game(false); return true; }
    bool right = !strcmp(a, "right");
    if (!right && strcmp(a, "wrong")) return false;
    int want = pc_simon_expect(&s_g);
    int i = s_phase != P_INPUT || want < 0 ? -1 : right ? want : (want + 1) % PC_SIMON_COLOURS;
    printf("APP game simon %s=%d\n", a, i);
    if (i >= 0) ui_sectors_tap(s_sec, i);
    return true;
}
static const game_ring_t RING = { .id = "simon", .modes = M_N, .mode_text = mode_text, .rules = PC_SIMON_RULES, .rule_name = rule_name, .changed = changed,
                                  .cmd = game_cmd };

static bool game_knob(int d) { return ui_axis_knob(d) || ui_sectors_knob(s_sec, d); }   /* the borrowed knob first; the keys' cursor on a device without a knob */

static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; }
static bool in_flight(void *u) { (void)u; return s_phase == P_SHOW || s_phase == P_INPUT; }   /* CF2: a round is on: the FakePod keys are the answers however long the sequence took */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .resume = exit_resume };   /* G4b: taps answer (the sequence's timer is paused by the exit)  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_sel.mode = M_SOLO;
    const ui_sectors_def_t def = { .n = PC_SIMON_COLOURS, .rot = 45, .hub_pct = 36, .tap_flash_ms = TAP_MS, .tones = TONES, .progress = true, .field = true };
    ui_obj_guard(ui_sectors_create(p, &def, tapped, hub_tapped, NULL), &s_sec);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
    lv_timer_pause(s_timer);
    ui_timer_guard(p, &s_timer);
    new_game(false);
}

static void destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
}

const ui_screen_t scr_game_simon = { .name = "tools_game_simon", .create = create, .destroy = destroy, .on_knob = game_knob, .content = true };
