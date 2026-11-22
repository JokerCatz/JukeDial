// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The art styles of the games (G0b game feel, game-feel.md 2: styles live in the shared area, one table every game reads).
 * A style is ONE combined value (the 風格 axis of a game's ring): a palette + the mark shapes + the board's line look; cheap parameters only
 * (feedback: cheap params selectable). ui_board draws cells and marks by the style it is given (ui_board_set_style), game_fx and game_result take
 * their colours from it, so a game page never names a colour or a shape.
 *   0 經典 / Classic  the UI theme's colours, the cell's own text as the mark (X / O), rounded tiles; discs filled (四子棋)
 *   1 霓虹 / Neon     near-black field, thin grid lines instead of tiles, marks drawn as glowing strokes (a cross, a ring); discs as rings
 *   2 糖果 / Candy    soft pastel tiles, round corners, solid marks (a dot, a rounded square); discs with a shine
 * The golden game (a rare surprise, pc_game_gold): any style with gold and silver marks (game_style_gold). Tables are const (flash); no RAM.
 * G1 (ui_sectors, four-colour memory): every style also has FOUR sector colours (sec[], game_style_sector) for the games whose pieces are colours, not sides;
 * the classic style has its own four (the theme has only two accents, four colours must be told apart).
 * A field game (ui_arena, G1 ring breakout) reads the same palette through game_style_field: a = its main pieces (bricks), b = the tough ones,
 * hi = the player's (paddle, ball), fx = effects; Neon also pulses with the music (`pulse`). */
typedef enum { GS_MARK_GLYPH = 0, GS_MARK_STROKE, GS_MARK_SOLID } gs_mark_t;
typedef enum { GS_GRID_TILES = 0, GS_GRID_LINES } gs_grid_t;
typedef struct game_style {
    const char *zh, *en;              /* the name on the 風格 cell */
    bool theme;                       /* the palette is the UI theme's (cell PANEL, line PANEL2, a A1, b ON, hi A3/SEL, fx A1); the rgb fields are not used */
    uint32_t cell, line, a, b, hi, fx;   /* rgb: an empty cell (a hole), the grid lines / hole rims, side A's marks, side B's, the highlight, effects */
    uint8_t mark;                     /* gs_mark_t */
    uint8_t grid;                     /* gs_grid_t */
    uint8_t stroke;                   /* px: strokes and grid lines at a 62 px pitch (scaled with the pitch) */
    uint8_t radius_pct;               /* a tile's corner radius, % of half a cell */
    bool gold;                        /* the golden game's copy (game_style_gold) */
    uint32_t sec[4];                  /* rgb: the four sector colours (ui_sectors: lit at this colour, a darker mix when not lit) */
    bool pulse;                       /* G1: a field game's pieces pulse with the playing music's energy (ui_arena_energy: the ring breakout's bricks); board games ignore it */
} game_style_t;
typedef struct { lv_color_t cell, line, a, b, hi, fx, bg; } game_palette_t;   /* resolved colours (bg: the page behind the board) */
/* A field game's colours (ui_arena, G1), resolved from the same style: the field (fill, rim, the paddle's track), the pieces by kind and the player's */
typedef struct { lv_color_t fill, rim, track, piece, tough, boom, power, gold, player, ball, fx, warn; } game_field_palette_t;

#define GAME_STYLES 3
int game_style_count(void);                              /* GAME_STYLES */
const game_style_t *game_style(int i);                   /* i wrapped into range */
const char *game_style_name(int i);                      /* T() name */
void game_style_gold(const game_style_t *base, game_style_t *out);   /* the same style with gold (A) and silver (B) marks and a gold highlight */
void game_style_palette(const game_style_t *s, game_palette_t *p);   /* NULL s: style 0 */
lv_color_t game_style_sector(const game_style_t *s, int k);          /* sector colour k (wrapped into 0..3); NULL s: style 0 */
lv_color_t game_style_hue(int deg);                                 /* G5: a rainbow colour (soft, bright) at deg degrees round the wheel, any integer */
void game_style_field(const game_style_t *s, game_field_palette_t *f);   /* NULL s: style 0 */
