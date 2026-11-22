// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lvgl.h"
#include "pc/pc_viz.h"
#include "pc/pc_vizdraw.h"
#include "ui_tokens.h"
/* UI colour theme and spectrum look (spec 5.7). cfg keys: theme=, viz_ramp=, viz_style=<kind>, viz_var_<kind>, viz_w, viz_floor. LVGL task. */
const pc_theme_t *ui_theme(void);
static inline lv_color_t ui_c(uint32_t rgb) { return lv_color_hex(rgb); }
#define UI_BG      ui_c(ui_theme()->bg)
#define UI_PANEL   ui_c(ui_theme()->panel)
#define UI_PANEL2  ui_c(ui_theme()->panel2)
#define UI_FG      ui_c(ui_theme()->fg)
#define UI_DIM     ui_c(ui_theme()->dim)
#define UI_A1      ui_c(ui_theme()->a1)
#define UI_A2      ui_c(ui_theme()->a2)
#define UI_A3      ui_c(ui_theme()->a3)
#define UI_ON      lv_color_mix(ui_c(ui_theme()->on), UI_PANEL, 110)   /* "switched on" (the second accent, generated per theme: hue >= 60 deg from a1) */
#define UI_WARN    ui_palc(UI_PAL_warn, 0)
#define UI_SEL     lv_color_mix(UI_A1, UI_PANEL, 110)   /* a selected row / ring item */
#define UI_CARD    lv_color_mix(UI_FG, UI_PANEL, 30)    /* the modal popup card (ui_frame_card): solid, a step lighter than the panel so it reads on the page */
#define UI_GOLD    ui_palc(UI_PAL_gold, 0)   /* a win / a best / a lucky mark */
/* every help cell / row / popup item: ONE token pair (fill + text on it). Today the plain cell look (cyan is the selection colour; the colour choice moves to the final theme step) */
static inline lv_color_t ui_help_c(ui_pal_id_t id, lv_color_t fallback) { return ui_tok(id) == UI_PAL_UNSET ? fallback : ui_palc(id, 0); }
#define UI_HELP       ui_help_c(UI_PAL_help, ui_cell_used())        /* a theme file may set the third accent: key help / help_text (ui_tokens.inc) */
#define UI_HELP_TEXT  ui_help_c(UI_PAL_help_text, UI_FG)
#define UI_SYS     lv_color_mix(UI_A2, UI_PANEL, 80)    /* system items: Back, next page */
const char *ui_theme_label(void);            /* the colour scheme's name for the row: a card theme's name_tc / name_sc / name_en by language, else the built-in label */
bool ui_theme_is_light(void);                /* the page background is light (a card theme; every built-in is dark) */
lv_color_t ui_text_on(lv_color_t surface);   /* the text colour for a surface: UI_FG or UI_BG, whichever contrasts more with it (contrast ratio; black or white when neither reaches 4.5) (the one rule for text on a fill whose colour is not the theme's own) */
lv_color_t ui_ink_on(lv_color_t want, lv_color_t surface);   /* `want` (an accent) when it reads on the surface, else ui_text_on(surface) */
lv_color_t ui_hue_on(lv_color_t want, lv_color_t surface);   /* a fixed (dark-tuned) colour on a light theme: darkened toward UI_FG until it reads at 3:1 on the surface, hue kept; a dark theme: unchanged */
lv_color_t ui_cell_used(void);               /* a ring / grid / sector cell that holds something: the panel2 tone on a dark theme (unchanged); a light theme: the page mixed ~14 % toward the text colour, clearly darker than the page and the empty cells */
lv_color_t ui_cell_empty(void);              /* an empty slot of a ring or grid: UI_PANEL on a dark theme (unchanged); a light theme: the page mixed ~4 % toward the text colour (the closest of the three to the page) */
lv_color_t ui_edge_on_page(void);            /* the outline of a light face (a die, a card) on the page: a light theme: UI_BG mixed toward UI_FG until it holds 3:1 against the page; a dark theme: UI_BG (invisible: the outline is for light themes) */
void ui_face_outline(lv_obj_t *o, int width);   /* gives a face object that outline on a light theme; a dark theme: nothing changes */
void ui_theme_apply(lv_display_t *disp);     /* LVGL default theme from the current palette */
void ui_theme_next(void);                    /* cycle the palette, re-theme, rebuild every screen */
void ui_theme_step(int detents);             /* the same by detents (signed, wraps); no change, no rebuild */
pc_ramp_t ui_viz_ramp(void);
void ui_viz_ramp_next(void);
void ui_viz_ramp_set(pc_ramp_t r);
pc_vd_map_t ui_viz_map(void);                   /* viz_map: how bars and ring take colours from the ramp */
void ui_viz_map_set(pc_vd_map_t m);
int  ui_menu_style(void);                     /* 0 bubbles, 1 segmented ring (cfg ui_style, default ring) */
void ui_menu_style_next(void);
const char *ui_menu_style_label(void);
pc_vd_kind_t ui_viz_kind(void);                 /* cfg viz_style; unknown -> bars */
void ui_viz_kind_set(pc_vd_kind_t k);
int  ui_viz_var(pc_vd_kind_t k);                /* cfg viz_var_<name>; out of range or negative -> 0 */
void ui_viz_var_set(pc_vd_kind_t k, int v);     /* wrapped into range, then stored */
int  ui_viz_bar_w(void);                        /* cfg viz_w: 5|7|9, else 7 */
void ui_viz_bar_w_set(int w);                   /* 5|7|9, anything else is ignored */
void ui_viz_bar_w_next(void);                   /* 5 -> 7 -> 9 -> 5 (from the default when the stored value is not one of them) */
bool ui_viz_peaks(void);                        /* cfg viz_peaks: the falling peak marks (default on) */
void ui_viz_peaks_set(bool on);
pc_vd_order_t ui_viz_order(void);               /* cfg viz_order: the ring kind's band order (seq|zigzag|spread), else seq */
void ui_viz_order_set(pc_vd_order_t o);
int  ui_viz_ring_gap(void);                     /* cfg viz_gap: the ring wedges' gap 0|2|4 px, else 2 */
void ui_viz_ring_gap_set(int px);               /* 0|2|4, anything else is ignored */
bool ui_viz_fall_full(void);                    /* cfg viz_fall: the full-size waterfall (false: the 256x200 box; always false when PC_VD_ROUND_SCREEN is 0) */
void ui_viz_fall_full_set(bool full);
void ui_viz_floor_set(int db);                  /* -60|-48|-36, anything else is ignored */
void ui_viz_floor_next(void);                   /* -60 -> -48 -> -36 -> -60 */
bool ui_viz_zoom(void);                        /* cfg viz_zoom (default on): the square board shows the bars, ring, scope and voice pictures zoomed to 440 px; off = the original size (sharper, no scaling); the round board never reads it */
void ui_viz_zoom_set(bool on);
float ui_viz_floor_db(void);                    /* cfg viz_floor: -60|-48|-36, else -48 */
const char *ui_viz_kind_label(pc_vd_kind_t k);
const char *ui_ramp_label(pc_ramp_t r);
