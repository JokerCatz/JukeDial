// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* Cards (shared piece, ideas G4): piles of tiles or cards inside the display's design circle, between the HEAD row and Back, drawn by ONE LVGL object (custom
 * draw event, no object per card). First user: dominoes. Built once for every card game that follows (card games: blackjack, Texas hold'em, solitaire is dropped by the owner ...).
 * - Faces are CODE-DRAWN (no image strip): a face set is a small drawing routine over a face number. Cheaper than a flash strip (no asset, no generated file,
 *   no RAM, both screens' sizes for free) and the faces scale with the band. Today UI_CARDS_SET_DOMINO: a face is v1 * 7 + v2 (pips 0..6 of the tile's
 *   first and second half; 49 oriented faces); UI_CARDS_SET_PLAYING (52 faces: rank * 4 + suit, rank and suit drawn with the glyph font) is NAMED for the
 *   later games and not built (ui_cards_create returns NULL for it). The face set decides the card's aspect (domino 1 : 2).
 * - Layout: the object is the band between the HEAD row and Back (the numbers ui_board uses; round panel: as wide as keeps its corners inside the circle).
 *   Each pile owns a ZONE (a rectangle in permille of the object) and a layout:
 *     FAN    one row of upright cards centred in the zone, a step of a card plus a gap that shrinks when the pile is long (overlap scaled to fit; the
 *            last cards stay visible, a card never leaves the zone); a selected card is lifted by a tenth of the zone;
 *     SNAKE  cards lying on their side in rows that run left to right, then right to left (the dominoes' chain): the size is the largest that holds
 *            `cap` cards in the zone and stays the same as the pile grows; the face of a card in a right-to-left row is turned half a circle.
 *   `scale_pct` shrinks a FAN pile's cards (the other side's hand). A pile can be a SOURCE (a tap selects a card in it) and/or a TARGET (a tap with a card
 *   selected asks `move`).
 * - Taps (CLICKED): the nearest card (the front one where cards overlap) within the pile it lands in, else the pile whose zone holds the point with its
 *   NEAREST card (an empty pile: idx -1); outside every zone nothing. A tap on a card of a source pile selects it (select(pile, idx), a tick; the same card
 *   again deselects, select(pile, -1)); with a selection, a tap on a target pile calls move(src_pile, src_idx, pile, idx) once: true = taken (the selection is
 *   cleared), false = refused (the selection stays; the page says why). `enabled` false (the other side's turn): taps do nothing. No pressed look (haptic only).
 * - Looks per card: DOWN (back showing), SEL (lifted, outlined), HI (a thin outline: it can be played), DIM (faded), GOLD (the golden tile). Per pile, `ends`
 *   (bit 0: the first card, bit 1: the last) draws the highlight ring on the chain's open ends. Colours from the game's style (game_style.h): Classic bone tiles with
 *   dark pips, Neon dark tiles with glowing rims, Candy pastel tiles with coloured pips.
 * - think: three dots pulse at the right end of a pile's zone while the device thinks (a timer bound to the object, 4 Hz, paused when off).
 * State: one PSRAM block (header + 4 piles of 28 cards) freed with the object (the timer goes with it); the one static pointer (the console's) is .bss in PSRAM
 * (the file is built with -msmall-data-limit=0). A change redraws the object (it is small and turn based). All calls from the LVGL task.
 * Console (ui_cards_cmd, `cards ...`): "" dumps "APP cards set=<name> piles=<n> sel=<pile>:<idx> en=<0|1> think=<0|1> w=<px> h=<px> card=<w>x<h>" and per pile
 * "APP cards pile <i> n=<n> faces=<f,f> flags=<hex,hex> ends=<mask> step=<px>"; "tap <pile> <idx>" a touch on that card (as the finger gives it, then the dump);
 * "at <pile> <idx>" its screen centre; "hit <x> <y>" what a touch there would take ("APP cards hit <pile> <idx>", pile -1 nothing). */
#define UI_CARDS_PILES 4
#define UI_CARDS_MAX   28
typedef enum { UI_CARDS_SET_DOMINO = 0, UI_CARDS_SET_PLAYING } ui_cards_set_t;
typedef enum { UI_PILE_FAN = 0, UI_PILE_SNAKE } ui_pile_layout_t;
enum { UI_CARD_DOWN = 1, UI_CARD_SEL = 2, UI_CARD_HI = 4, UI_CARD_DIM = 8, UI_CARD_GOLD = 16 };
typedef struct {
    ui_pile_layout_t layout;
    int x0, y0, x1, y1;           /* the zone, permille of the object (0..1000) */
    int scale_pct;                /* FAN: card size, % of what the zone allows (0: 100) */
    int cap;                      /* SNAKE: the cards the zone must be able to hold (<= UI_CARDS_MAX) */
    bool src, dst;                /* a tap selects a card here / a tap here with a selection moves it */
} ui_pile_def_t;
typedef struct {
    ui_cards_set_t set;
    int piles;                    /* 1..UI_CARDS_PILES */
    ui_pile_def_t pile[UI_CARDS_PILES];
    void (*select)(int pile, int idx, void *user);                 /* a card was selected (idx >= 0) or the selection cleared (-1); may be NULL */
    bool (*move)(int sp, int si, int dp, int di, void *user);      /* the move; may be NULL (then nothing moves) */
} ui_cards_def_t;
struct game_style;                                                 /* game_style.h */

lv_obj_t *ui_cards_create(lv_obj_t *parent, const ui_cards_def_t *def, void *user);   /* def and user must outlive it; NULL: unsupported set or no memory */
void ui_cards_set_style(lv_obj_t *c, const struct game_style *st);   /* kept by pointer; NULL: style 0; redraws */
void ui_cards_set(lv_obj_t *c, int pile, const uint16_t *faces, const uint8_t *flags, int n);   /* the pile's cards (flags may be NULL); clears a selection that pointed there */
void ui_cards_flag(lv_obj_t *c, int pile, int idx, int flag, bool on);
void ui_cards_ends(lv_obj_t *c, int pile, int mask);              /* bit 0: the first card is an open end, bit 1: the last */
void ui_cards_select(lv_obj_t *c, int pile, int idx);             /* select a card (idx -1: clear); calls select() when it changed */
bool ui_cards_selected(const lv_obj_t *c, int *pile, int *idx);
void ui_cards_enabled(lv_obj_t *c, bool on);
void ui_cards_think(lv_obj_t *c, int pile, bool on);              /* three dots pulse at the right end of that pile's zone */
int  ui_cards_hit(const lv_obj_t *c, int x, int y, int *idx);     /* screen coordinates -> the pile (-1 none), *idx the card (-1 none) */
bool ui_cards_xy(const lv_obj_t *c, int pile, int idx, int *x, int *y);   /* a card's screen centre */
void ui_cards_tap(lv_obj_t *c, int pile, int idx);                /* a touch on that card as the finger gives it: tests, the console */
void ui_cards_cmd(const char *arg);
