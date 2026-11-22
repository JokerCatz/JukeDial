// SPDX-License-Identifier: LGPL-2.1-or-later
/* The result card (game_result.h). */
#include "game_result.h"
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "gamesave_svc.h"
#include "games.h"
#include "game_style.h"
#include "pc/pc_game_score.h"
#include "psram_alloc.h"
#include "sfx.h"
#include "ui.h"
#include "ui_board.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define CARD_W ui_by_panel(224, 296)    /* the square panel: the 24 / 32 px text needs a larger card */
#define CARD_H ui_by_panel(136, 184)
typedef struct {
    game_result_t r;
    char title[40];
    void (*again)(void);
    bool shown;
} card_t;
static lv_obj_t *s_card;
static int8_t s_gold_force;                          /* 0 by the seed, 1 every game golden, 2 none (zero-initialised: .bss in PSRAM, an initialised static would be internal .data) */
#define GAME(id, zh, en, scr) + 1
enum { N_GAMES = 0
#include "games.def"
};
#undef GAME
static bool s_loaded[N_GAMES];                       /* the stored bests of the game are in s_score (games.txt, read at the first result after boot; ideas G2) */
static pc_game_score_t s_score[N_GAMES];             /* the session of each game (PSRAM: this file is built with -msmall-data-limit=0) */

static void show_now(lv_obj_t *o, card_t *c) { lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); c->shown = true; }
static void delay_cb(lv_timer_t *t)
{
    lv_obj_t *o = lv_timer_get_user_data(t);
    lv_timer_pause(t);                        /* a bound timer: it goes with the card */
    show_now(o, lv_obj_get_user_data(o));
}
static void click_cb(lv_event_t *e)
{
    card_t *c = lv_event_get_user_data(e);
    void (*again)(void) = c->again;
    ui_haptic_tap();
    if (again) again();                       /* the page starts again: it hides the card (game_result_hide) */
}
static void delete_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    if (s_card == o) s_card = NULL;
    gamesave_flush();                                  /* the card goes (again, Back, another game): a new best reaches the card now, not at the debounce (writes only when something changed) */
    psram_free(lv_event_get_user_data(e));
}

static void fmt_time(char *b, size_t n, uint32_t ms) { snprintf(b, n, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60)); }

static lv_obj_t *line(lv_obj_t *card, const lv_font_t *f, int y, lv_color_t c, const char *text)
{
    lv_obj_t *l = ui_label(card, f, y, CARD_W - 16);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, lv_font_get_line_height(f));
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *game_result_show(lv_obj_t *board, const game_result_t *r, void (*again)(void))
{
    game_result_hide();
    if (!board || !r) return NULL;
    card_t *c = psram_calloc(1, sizeof *c);
    if (!c) return NULL;
    c->r = *r;
    snprintf(c->title, sizeof c->title, "%s", r->title ? r->title : "");
    c->r.title = c->title;
    c->again = again;
    game_palette_t p;
    game_style_palette(r->style ? r->style : ui_board_style(board), &p);
    lv_obj_update_layout(board);
    lv_obj_t *o = lv_obj_create(board);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_MIN(CARD_W, lv_obj_get_width(board) - 8), CARD_H);
    lv_obj_center(o);
    const lv_color_t card = lv_color_mix(p.cell, ui_theme_is_light() ? UI_PANEL : UI_BG, ui_theme_is_light() ? 120 : 160), ink = ui_text_on(card);   /* the card's text follows the card's own colour, not the theme's */
    lv_obj_set_style_bg_color(o, card, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_border_width(o, r->gold ? 3 : 2, 0);
    lv_obj_set_style_border_color(o, p.hi, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, c);
    lv_obj_add_event_cb(o, click_cb, LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, c);
    char b[64], t1[16], t2[16];
    const int lh = lv_font_get_line_height(font_main());     /* four lines: the title (large), the game, the best, the badges or the hint */
    line(o, font_large(), -lh - 18, ui_ink_on(p.hi, card), c->title);
    fmt_time(t1, sizeof t1, r->ms);
    if (r->mode == GAME_SCORE_POINTS && r->level <= 0) snprintf(b, sizeof b, T(S_U_PTS) /* %u pts */, (unsigned)r->points);   /* G3: a score game without levels (beat tap) */
    else if (r->mode == GAME_SCORE_POINTS) snprintf(b, sizeof b, T(S_U_PTS_LEVEL_D) /* %u pts · level %d */, (unsigned)r->points, r->level);
    else if (r->mode == GAME_SCORE_HIGH) snprintf(b, sizeof b, "%s %d · %s", r->unit ? r->unit : T(S_MOVES) /* moves */, r->moves, t1);
    else snprintf(b, sizeof b, T(S_D_MOVES_S) /* %d moves · %s */, r->moves, t1);
    line(o, font_main(), -10, ink, b);
    if (r->new_best) snprintf(b, sizeof b, "%s", T(S_BEST_SO_FAR) /* Best this time! */);
    else if (r->mode == GAME_SCORE_POINTS && r->best_points) snprintf(b, sizeof b, T(S_BEST_U_PTS) /* Best %u pts */, (unsigned)r->best_points);
    else if (r->mode == GAME_SCORE_HIGH && r->best_moves) snprintf(b, sizeof b, T(S_BEST_S_D) /* Best %s %d */, r->unit ? r->unit : T(S_MOVES_2) /* moves */, r->best_moves);
    else if (r->mode != GAME_SCORE_POINTS && r->best_moves) { fmt_time(t2, sizeof t2, r->best_ms); snprintf(b, sizeof b, T(S_BEST_D_MOVES_S) /* Best %d moves · %s */, r->best_moves, t2); }
    else b[0] = 0;
    line(o, font_main(), lh - 8, ui_ink_on(r->new_best ? p.hi : UI_DIM, card), b);
    if (r->streak >= PC_GAME_STREAK && r->gold) snprintf(b, sizeof b, T(S_D_IN_A_ROW_GOLDEN) /* Streak %d · golden game */, r->streak);
    else if (r->streak >= PC_GAME_STREAK) snprintf(b, sizeof b, T(S_D_IN_A_ROW) /* Streak %d */, r->streak);
    else if (r->gold) snprintf(b, sizeof b, "%s", T(S_GOLDEN_GAME) /* Golden game */);
    else snprintf(b, sizeof b, "%s", T(S_TAP_TO_PLAY_AGAIN) /* Tap to play again */);
    line(o, font_main(), 2 * lh - 6, ui_ink_on(r->streak >= PC_GAME_STREAK || r->gold ? p.a : UI_DIM, card), b);
    s_card = o;
    if (ui_screen_on()) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (!ui_timer_bind(o, delay_cb, GAME_RESULT_DELAY_MS, o)) show_now(o, c);
    } else show_now(o, c);
    return o;
}

void game_result_hide(void) { if (s_card) lv_obj_delete(s_card); }   /* the delete event clears s_card */
bool game_result_up(void) { return s_card != NULL; }

void game_result_cmd(const char *arg)
{
    if (!s_card) { printf("APP game_result up=0\n"); return; }
    card_t *c = lv_obj_get_user_data(s_card);
    if (!strcmp(arg, "tap")) {
        lv_obj_send_event(s_card, LV_EVENT_CLICKED, NULL);
        if (!s_card) { printf("APP game_result up=0\n"); return; }
        c = lv_obj_get_user_data(s_card);
    }
    else if (arg[0]) { printf("APP game_result unknown %s\n", arg); return; }
    if (c->r.mode == GAME_SCORE_POINTS) printf("APP game_result up=1 shown=%d title=%s points=%u level=%d best=%u new_best=%d gold=%d\n", c->shown, c->title, (unsigned)c->r.points, c->r.level,
                            (unsigned)c->r.best_points, c->r.new_best, c->r.gold);
    else printf("APP game_result up=1 shown=%d title=%s moves=%d ms=%u best=%d/%u new_best=%d streak=%d gold=%d\n", c->shown, c->title, c->r.moves, (unsigned)c->r.ms,
                c->r.best_moves, (unsigned)c->r.best_ms, c->r.new_best, c->r.streak, c->r.gold);
}

bool game_result_gold(const char *id, uint32_t n) { return s_gold_force ? s_gold_force == 1 : pc_game_gold(games_seed(id, n)); }
void game_result_gold_force(int v) { s_gold_force = (int8_t)(v < 0 ? 0 : v ? 1 : 2); printf("APP game_result gold=%s\n", v < 0 ? "auto" : v ? "1" : "0"); }

void game_result_end(lv_obj_t *board, const game_end_t *e, void (*again)(void))
{
    int gi = games_find(e->id);
    pc_game_score_t tmp, *sc = gi >= 0 && gi < N_GAMES ? &s_score[gi] : &tmp;
    if (sc == &tmp) pc_game_score_reset(&tmp);
    else if (!s_loaded[gi]) { gamesave_load_score(e->id, sc); s_loaded[gi] = true; }   /* the bests of earlier boots */
    if (e->mode == GAME_SCORE_POINTS) {                                 /* a score game (G1): the most points, no streak */
        pc_game_rec_t r = pc_game_score_points(sc, e->variant, e->points, e->ms);
        if (sc != &tmp) gamesave_store_score(e->id, sc);
        const char *snd = r.new_best ? "win" : "game_over";
        sfx_play(snd);
        printf("APP game %s end outcome=%d streak=%d best=%u new_best=%d points=%u level=%d sound=%s\n", e->id, e->outcome, r.streak, (unsigned)r.best_points, r.new_best,
               (unsigned)e->points, e->level, snd);
        game_result_t c = { .title = e->title, .moves = e->moves, .ms = e->ms, .new_best = r.new_best, .gold = e->gold, .mode = GAME_SCORE_POINTS, .points = e->points,
                            .best_points = r.best_points, .level = e->level, .style = e->style };
        game_result_show(board, &c, again);
        return;
    }
    const bool high = e->mode == GAME_SCORE_HIGH;
    pc_game_rec_t r = high ? pc_game_score_record_high(sc, e->variant, e->moves, e->ms) : pc_game_score_record(sc, e->variant, e->outcome, e->vs_device, e->moves, e->ms);
    if (sc != &tmp) gamesave_store_score(e->id, sc);
    const char *snd = high ? (r.new_best || e->outcome == PC_GAME_WIN ? "win" : "lose")
                    : e->outcome == PC_GAME_WIN ? (r.streak_hit ? "streak" : "win") : e->outcome == PC_GAME_LOSS ? "lose" : "draw";
    sfx_play(snd);
    printf("APP game %s end outcome=%d streak=%d best=%d new_best=%d sound=%s\n", e->id, e->outcome, r.streak, r.best_moves, r.new_best, snd);
    game_result_t c = { .title = e->title, .moves = e->moves, .ms = e->ms, .best_moves = r.best_moves, .best_ms = r.best_ms, .new_best = r.new_best,
                        .streak = high ? 0 : r.streak, .gold = e->gold, .mode = e->mode, .unit = e->unit, .style = e->style };
    game_result_show(board, &c, again);
}

void game_result_streak_set(const char *id, int n)
{
    int gi = games_find(id);
    if (gi < 0 || gi >= N_GAMES) { printf("APP game_result streak none\n"); return; }
    s_score[gi].streak = (uint16_t)(n < 0 ? 0 : n);
    printf("APP game_result streak %s=%d\n", id, s_score[gi].streak);
}
