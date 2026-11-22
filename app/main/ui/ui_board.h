// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The board (shared piece, games hub G0): a grid of cells inside the display's design circle, between the HEAD row and Back, drawn by ONE LVGL object
 * (custom draw event, no object per cell). Used by every board game: tic-tac-toe, Connect Four now; 2048, minesweeper, sliding puzzle, sudoku, chess, Go ... later.
 * - Layout: SQUARE (cols x rows, up to 9 x 9) is built. HEX and POLAR are named in ui_board_layout_t for later (ui_board_create returns NULL for them
 *   today); a zoom mode (tap a block, then a cell: sudoku, chess) is not built either. The pitch is the largest that fits the band and the circle, so
 *   cells may be under UI_TAP_MIN (a 7 x 6 board has 31 px cells): the piece owns that rule, like ui_bubbles.
 * - Hit test: the nearest cell centre, at most 3/4 of a pitch away (ui_board_hit); a touch farther from every cell does nothing. The object covers the grid
 *   plus a margin that never crosses the HEAD row or Back, so a touch there is the margin rule's, not a neighbour's.
 * - Content: each cell has a short text (up to 5 bytes, cut at a character boundary: a letter, a digit, one CJK character; NULL or "" = empty) and a look from the theme: NORMAL, mark A,
 *   mark B, HIGHLIGHT, DIM. `round` cells are drawn as discs filled by the look (a hole when NORMAL) instead of rounded squares with the text in the look's colour.
 *   Only a cell that changed is redrawn.
 * - Style (G0b): ui_board_set_style gives the board a game_style (game_style.h: palette, mark shapes, the grid's line look); a cell set with look A or B
 *   holds that side's MARK, drawn in the style's shape (the glyph style draws the cell's text); HI and DIM keep the mark (HI: the highlight, DIM: the
 *   mark at 40 %), NORMAL clears it. The text stays the cell's data (the console dump) whatever the style draws.
 * - Effects (G0b): game_fx draws over the board (a child object) with ui_board_draw_piece, so a moving mark looks like a placed one; ui_board_hide keeps
 *   a cell's mark undrawn while an effect carries it.
 * State: one PSRAM block (header + 7 bytes a cell) freed with the object; the one static pointer (the console's) is .bss in PSRAM (the file is built
 * with -msmall-data-limit=0). All calls from the LVGL task.
 * Console (ui_board_cmd, `board ...`): "" dumps the open board (APP board w=<cols> h=<rows>, then APP board row <r> <cells> and APP board look <r> <looks> per row:
 * a cell is its text or "-", a look is one of n a b h d); "tap <i>" a touch on cell i (then the dump); "at <i>" its screen centre; "hit <x> <y>" the cell a touch there
 * would take (-1 none). */

typedef enum { UI_BOARD_SQUARE = 0, UI_BOARD_HEX, UI_BOARD_POLAR } ui_board_layout_t;
typedef enum { UI_CELL_NORMAL = 0, UI_CELL_A, UI_CELL_B, UI_CELL_HI, UI_CELL_DIM, UI_CELL_LOOKS } ui_cell_look_t;
typedef struct {
    ui_board_layout_t layout;     /* UI_BOARD_SQUARE only so far */
    int cols, rows;               /* 1..9 each */
    bool round;                   /* cells drawn as discs (Connect Four) */
} ui_board_def_t;
typedef void (*ui_board_tap_cb)(int idx, void *user);   /* a touch took cell idx = row * cols + col */
struct game_style;                                       /* game_style.h */

/* The board in `parent`; `cb` (may be NULL) runs for every touch that lands on a cell. All cells empty, NORMAL. NULL: unsupported layout or size, or no memory. */
lv_obj_t *ui_board_create(lv_obj_t *parent, const ui_board_def_t *def, ui_board_tap_cb cb, void *user);
/* Sets a cell (text NULL or "": empty); redraws it only when the text or the look changed. false: no such cell. */
bool ui_board_set(lv_obj_t *board, int idx, const char *text, ui_cell_look_t look);
bool ui_board_set_look(lv_obj_t *board, int idx, ui_cell_look_t look);
void ui_board_clear(lv_obj_t *board);                    /* every cell empty and NORMAL (one redraw of the grid) */
int ui_board_cols(const lv_obj_t *board);                /* 0 for NULL */
int ui_board_rows(const lv_obj_t *board);
const char *ui_board_text(const lv_obj_t *board, int idx);        /* "" for an empty or missing cell */
int ui_board_hit(const lv_obj_t *board, int x, int y);   /* screen coordinates -> the cell, -1 none (too far from every cell) */
bool ui_board_cell_xy(const lv_obj_t *board, int idx, int *x, int *y);   /* screen centre of a cell */
void ui_board_set_style(lv_obj_t *board, const struct game_style *st);   /* kept by pointer (the caller's static or the table); NULL: style 0; redraws */
const struct game_style *ui_board_style(const lv_obj_t *board);          /* never NULL for a board (style 0 when none was set) */
int  ui_board_pitch(const lv_obj_t *board);              /* px between two cell centres, 0 for NULL */
bool ui_board_is_round(const lv_obj_t *board);
int  ui_board_mark(const lv_obj_t *board, int idx);      /* 1 side A, 2 side B, 0 none */
void ui_board_hide(lv_obj_t *board, int idx, bool hidden);   /* the cell's mark is not drawn (an effect draws it); ui_board_clear shows every cell again */
bool ui_board_centre(const lv_obj_t *board, int idx, int *x, int *y);   /* screen centre of a cell without a layout update (draw events) */
/* draws a mark of `mark` (1, 2; `text` for the glyph style) as the board draws it, centred at (cx, cy) screen px, at pct % of a cell, opacity opa (game_fx) */
void ui_board_draw_piece(const lv_obj_t *board, lv_layer_t *layer, int mark, const char *text, int cx, int cy, int pct, lv_opa_t opa);
/* Tiles (ideas G4 15 puzzle): a cell may be a TILE instead of a mark: a filled rounded square (an outline in a lines style) in the mix of the style's two mark
 * colours (tint 0 = side A's colour .. 100 = side B's) with its text (up to 5 bytes: a number, a letter) on it; look HI draws it with the highlight border. A tile
 * is set by ui_board_set_tile (ui_board_set / ui_board_clear turn it back into an ordinary cell; ui_board_set_look keeps it a tile). An empty cell with look DIM is
 * the sliding puzzle's blank (a hole in the line colour). Slide: ui_board_slide(to, from, ms) draws the tile now in cell `to` moving in from cell `from` over ms (ease out,
 * 30 fps on a timer bound to the board, one slide at a time: a new one ends the one in flight; the screen off or no timer: no slide, false; the board's own cells are
 * the state, so a slide never changes content). Key cursor (FakePod, no knob): ui_board_set_cursor draws a ring round a cell (-1 none); the page moves it, the board
 * only draws it. ui_board_keys: this build has no knob, so its keys stand in (the page then gives them the cursor and the tap). Console: `board slide` prints
 * "APP board slide on=<0|1> to=<cell> from=<cell> cursor=<cell> keys=<0|1>". */
bool ui_board_set_tile(lv_obj_t *board, int idx, const char *text, int tint, ui_cell_look_t look);
bool ui_board_slide(lv_obj_t *board, int to, int from, int ms);
void ui_board_set_cursor(lv_obj_t *board, int idx);
int  ui_board_cursor(const lv_obj_t *board);
bool ui_board_keys(const lv_obj_t *board);
void ui_board_cmd(const char *arg);
