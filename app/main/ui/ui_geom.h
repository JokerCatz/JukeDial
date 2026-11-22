// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* UI geometry: every size and position the shared pieces (ui.c, ui_common.c, ui_kit.c, ui_ring.c, ui_axis.c, now_menu.c) use.
 * Pages never include this file (they compose pieces; ui_lint enforces it). Derived from bsp/bsp_geom.h only: never include the board pin map
 * here (it pulls the driver headers, which the host compiler and the simulator do not have).
 * The #if UI_ROUND column is this board (360 round); the #else column is the proposal for a 460 rectangular panel (the port tunes it). */
#include "bsp/bsp_geom.h"

#define UI_W        BSP_LCD_H_RES
#define UI_H        BSP_LCD_V_RES
#define UI_CX       (UI_W / 2)
#define UI_CY       (UI_H / 2)
#define UI_ROUND    BSP_LCD_ROUND
#define UI_HAS_KNOB BSP_HAS_KNOB   /* 1: a rotary knob exists (ui_word_knob says "旋鈕", knob); 0: the keys stand in for it */

#if BSP_LCD_ROUND
#define UI_TAP_MIN  48      /* smallest tap target */
#else
#define UI_TAP_MIN  64      /* the square panel's smallest tap target is one list row (UI_ROW_H): pills, choice cells, axis buttons, Back, the popup card's items */
#endif
#define UI_TAP_GAP  8       /* smallest gap between two targets */
#define UI_FLICK_START_MS 150    /* a FAST flick (CF2 gesture): the finger starts moving within this long of the press ... */
#define UI_FLICK_MS       300    /* ... travels UI_FLICK_PCT % of the panel height within this long of starting to move ... */
#define UI_FLICK_PCT      40     /* ... (up / down, mostly vertical) and lifts: GS gestures, ui.c: down = quick settings, up = popup / back; slower or shorter is no flick */
#define UI_FLICK_H_PCT    30     /* the same sideways, as a share of the panel width: left = next page, right = previous */
/* (first day with the FakePod in hand: a quick scroll of a list was taken for a flick): a flick counts only when the touch BEGINS at the edge it comes from
 * (up from the bottom edge, down from the top, left from the right, right from the left), as on a phone. Square panel: within UI_FLICK_EDGE px of that side.
 * Round panel: in the outer rim (within UI_FLICK_EDGE px of the circle) and on that side of the centre (the larger of |x|, |y| from the centre picks the side). */
#define UI_FLICK_EDGE     (UI_ROUND ? 50 : 56)
#define UI_PAGE_BUBBLE_FONT (font_unscii8_xn(2))       /* UNSCII 8 doubled pixel for pixel (font.c); 2026-11-16: UNSCII 8 with "1-2" (the "/" looked out of place). GS: the page-count bubble's font, ONE switch: lv_font_montserrat_10 / lv_font_montserrat_12 / lv_font_unscii_8 (owner picks from the sim shots in scratchpad gs_font/; only the chosen one is enabled in sdkconfig.defaults) */

#if UI_ROUND
#define UI_DESIGN_D          360
#define UI_CONTENT_D         360
#define UI_RING_RO           176
#define UI_RING_RI           104
#define UI_RING_LABEL_W      96
#define UI_FONT_MAIN_PX      20            /* font_main / font_large pixel sizes (font.c): the round glass keeps 20 / 28 */
#define UI_FONT_LARGE_PX     28
#define UI_RING_ICON_PX      44            /* a menu cell with an icon (ui_ring_set_icons): the icon font size; the round cell shows the icon alone (the name is in the centre after the first tap) */
#define UI_RING_ICON_DY      0             /* the icon above the cell centre, and the short name below it (the square panel only) */
#define UI_RING_ICON_NAME_DY 0
#define UI_RING_LABEL_IN     2             /* round: text cells sit this far inside the cell middle (side labels stay on the glass) */
#define UI_RING_ICON_IN      2             /* round: icon cells, the same for the icons */
#define UI_LIST_W            250
#define UI_LIST_H            250
#define UI_LIST_Y            (-20)
#define UI_LIST_BUBBLE_DY    4        /* the page-count bubble of a paged list: this far below the list's bottom edge (ui_list_pager) */
#define UI_ROW_W             240
#define UI_ROW_LABEL_W       220
#define UI_ROW_PM_W          44      /* a selected axis row (ui_list_row_axis): the width of each of its two sign chips */
#define UI_LIST_TITLED_H     240
#define UI_LIST_TITLED_Y     (-14)    /* was -10; with Back at 114..162 the list ends 8 px above it */
#define UI_BACK_W            88
#define UI_BACK_H            48
#define UI_BACK_Y            138
#define UI_BAND_BOTTOM       (UI_BACK_Y - UI_BACK_H / 2 - UI_TAP_GAP)   /* where a page without the popup frame ends its content band: above its (round) Back row */
#define UI_BIG_D             96
#define UI_AXIS_HINT_W       280
#define UI_SLOT_TITLE_Y      (-150)
#define UI_SLOT_TITLE_W      200
#define UI_SLOT_HEAD_Y       (-112)
#define UI_SLOT_HEAD_W       260
#define UI_SLOT_UPPER_Y      (-62)
#define UI_SLOT_UPPER_W      280
#define UI_SLOT_CENTER_Y     0
#define UI_SLOT_CENTER_W     300
#define UI_SLOT_LOWER_Y      62
#define UI_SLOT_LOWER_W      280
#define UI_SLOT_FOOT_Y       82
#define UI_SLOT_FOOT_W       280
#define UI_SLOT_BASE_Y       150      /* the title slot's mirror at the bottom edge (a page with a field centred between two text lines) */
#define UI_SLOT_BASE_W       200
#define UI_LIST_PAD_TOP      24
#define UI_RING_BOX_W        UI_DESIGN_D   /* the ring's container */
#define UI_RING_BOX_H        UI_DESIGN_D
#define UI_RING_CENTER_TEXT_W (2 * UI_RING_RI - UI_RING_DISC_LABEL_INSET)
#define UI_RGRID_MX          0             /* the square panel's menu grid (ui_ring.c); unused on the round glass */
#define UI_RGRID_HEAD_Y      0
#define UI_RGRID_HEAD_H      0
#define UI_RGRID_TOP         0
#define UI_RGRID_GAP         0
#define UI_RGRID_CELL_H      1
#define UI_RGRID_ROWS        4             /* (unused on the round glass) */
#define UI_RGRID_RADIUS      0
#define UI_RGRID_LAY2_TOP    0
#define UI_CODEPAD_KEY       (UI_TAP_MIN + 4)            /* the code pad's key (ui_code_pad.c) and where its cross sits */
#define UI_CODEPAD_CROSS_X   (-UI_CONTENT_D * 7 / 36)
#define UI_GRID_W            136     /* ui_choice_grid cell (2 x 2: x +-UI_GRID_DX, rows centred at UI_GRID_Y0 / UI_GRID_Y1) */
#define UI_GRID_H            64
#define UI_GRID_DX           76
#define UI_GRID_Y0           (-88)
#define UI_GRID_Y1           72
#define UI_PADS_M            0                            /* the drum pads' grid (inst_pads.c, square only) and their hub and label width */
#define UI_PADS_GAP          0
#define UI_PADS_HUB_D        0
#define UI_PADS_LBL_W        64
#define UI_RIM_SQ_RR         0                            /* the LED border's corner radius (ledrim.c, square only) */
#define UI_RIM_SQ_INSET      0
#else
#define UI_DESIGN_D          360
#define UI_CONTENT_D         460
#define UI_RING_RO           176
#define UI_RING_RI           104
#define UI_RING_LABEL_W      200
#define UI_FONT_MAIN_PX      24            /* larger text on the square panel (rasterised at run time by FreeType: a size is a number, not a file) */
#define UI_FONT_LARGE_PX     32
#define UI_RING_ICON_PX      52            /* the square grid: icon above, short name below, one tap (bigger cells and text for the finger: 2 x 3 cells a page) */
#define UI_RING_ICON_DY      (-24)
#define UI_RING_ICON_NAME_DY 31
#define UI_RING_LABEL_IN     0
#define UI_RING_ICON_IN      0
#define UI_LIST_W            420
#define UI_LIST_H            (6 * UI_ROW_H + 5 * UI_ROW_GAP)   /* 6 rows of 64 + 5 gaps of 10 = 434: 13 px above and below on the 460 panel (fppolish: was 362 at y -18, 67 px blank below) */
#define UI_LIST_Y            0
#define UI_LIST_BUBBLE_DY    2
#define UI_ROW_W             420
#define UI_ROW_LABEL_W       396
#define UI_ROW_PM_W          56      /* a selected axis row (ui_list_row_axis): the width of each of its two sign chips */
#define UI_LIST_TITLED_H     (5 * UI_ROW_H + 4 * UI_ROW_GAP)   /* 5 rows (was 6 of 48 in 312; the area under the title is the same) */
#define UI_LIST_TITLED_Y     22     /* 5 rows = 336: 84 .. 420 (the title line sits above, the bottom margin is 40) */
#define UI_BACK_W            200
#define UI_BACK_H            UI_TAP_MIN
#define UI_BACK_Y            196
#define UI_BAND_BOTTOM       (UI_H / 2 - 2 * UI_TAP_GAP)   /* Back is hidden on a page without the frame (the up flick clicks it), so the content band runs to the panel edge less the frame's margin: no 58 px Back row (night2) */
#define UI_BIG_D             144      /* SQ2: the progress bar of the loop page (UPPER, -80) and the 140 row bound it */
#define UI_AXIS_HINT_W       360
#define UI_SLOT_TITLE_Y      (-204)
#define UI_SLOT_TITLE_W      400
#define UI_SLOT_HEAD_Y       (-164)
#define UI_SLOT_HEAD_W       400
#define UI_SLOT_UPPER_Y      (-96)    /* night2: was -80; a 24 px text line there touched the 144 px big button (top at -72) on the tuner tone page */
#define UI_SLOT_UPPER_W      400
#define UI_SLOT_CENTER_Y     0
#define UI_SLOT_CENTER_W     400
#define UI_SLOT_LOWER_Y      80
#define UI_SLOT_LOWER_W      400
#define UI_SLOT_FOOT_Y       140
#define UI_SLOT_FOOT_W       400
#define UI_SLOT_BASE_Y       204      /* the title slot's mirror at the bottom edge */
#define UI_SLOT_BASE_W       400
#define UI_LIST_PAD_TOP      0
/* the menu ring is a grid of 2 columns x 3 rows (UI_RING_PER_PAGE cells) under a header band (3 lines of text) that is the ring's centre
 * (a tap = confirm / close). 8 + 80 + 8 + 3 * 108 + 2 * 6 = 432: 28 px under the grid for the panel's rounded corners. */
#define UI_RING_BOX_W        UI_W
#define UI_RING_BOX_H        UI_H
#define UI_RGRID_MX          14
#define UI_RGRID_HEAD_Y      8
#define UI_RGRID_HEAD_H      80
#define UI_RGRID_TOP         96
#define UI_RGRID_GAP         6
#define UI_RGRID_ROWS        3             /* 2 columns x 3 rows = 6 cells a page (was 2 x 4 of 80) */
#define UI_RGRID_CELL_H      114
#define UI_RGRID_RADIUS      22
#define UI_RGRID_LAY2_TOP    8      /* the grid without its header band: the rows start here and stretch to the same bottom */
#define UI_CODEPAD_KEY       76
#define UI_CODEPAD_CROSS_X   (-78)
#define UI_GRID_W            200     /* ui_choice_grid cell (2 x 2: x +-UI_GRID_DX, rows centred at UI_GRID_Y0 / UI_GRID_Y1): large on the square panel (SQ2) */
#define UI_GRID_H            96
#define UI_GRID_DX           108
#define UI_GRID_Y0           (-126)
#define UI_GRID_Y1           106
#define UI_PADS_M            12
#define UI_PADS_GAP          6
#define UI_PADS_HUB_D        132
#define UI_PADS_LBL_W        120
#define UI_RIM_SQ_RR         56
#define UI_RIM_SQ_INSET      4
#define UI_RING_CENTER_TEXT_W (UI_W - 2 * UI_RGRID_MX - 96)   /* the header's text leaves room for the page number at its right */
/* the static now-playing page of the square panel (now_fp.c; no column on the round glass: it keeps the ring page). Top-left coordinates of the 460 panel unless a name says CX / CY (a centre).
 * Style A, cover + info (封面＋資訊): the cover square top left (about 2/3 of the width), a row of passive status icons right of it, the text block under the cover, the time row at the bottom.
 * Style B, full cover: the cover is the whole panel and nothing else. The taps come from the nine-grid (UI_NG_* below). */
#define UI_NOW_M             20      /* side margin of the text and the time row (the panel's rounded corners are clear of it) */
#define UI_NOW_BAR_H         8
#define UI_NOWA_COVER_Y      16      /* (was 12: the cover's rounded corner is 11 px inside the glass's ~60 px corner circle now, was 8) */
#define UI_NOWA_COVER_D      296     /* 64 % of the width; x = UI_NOW_M */
#define UI_NOWA_COVER_R      26
#define UI_NOWA_COL_CX       384     /* the status icons' centre: it fills UI_NOWA_COVER_D + UI_NOW_M .. UI_W - UI_NOW_M */
#define UI_NOWA_TEXT_Y       310     /* the title label's top (its glyphs start about 9 px lower); artist and format lines follow it by the pitches below */
#define UI_NOW_TITLE_PITCH   44      /* label top to label top: the labels are one font line (taller than the glyphs) and overlap by design */
#define UI_NOW_LINE_PITCH    32
#define UI_NOWA_TEXT_W       (UI_W - 2 * UI_NOW_M)   /* the title, artist and format lines take the whole width (no button beside them since 2026-11-22) */
#define UI_NOWA_TIME_X       40      /* the elapsed / total labels start this far from the panel edge: clear of the corner circle (fppolish; was UI_NOW_M + 4 = 24) */
#define UI_NOWA_TIME_CY      430     /* the elapsed label, the bar and the total label share this centre line */
/* the nine-grid of invisible tap zones (ui_ninegrid.c; FakePod only): three equal zones each way over the whole page; shown, each zone holds a translucent cell with its label,
 * and (cover / spectrum pages) a title band and a time band sit in the two strips between the rows. Zone = UI_W / 3 (153); the cell is inset from it. */
#define UI_NG_CELL_W         141
#define UI_NG_CELL_H         104
#define UI_NG_RADIUS         14
#define UI_NG_PAD            6       /* the label's side padding inside a cell */
#define UI_NG_BAND_H         44      /* the title / time band (the strip between two rows is 57 px high) */
#define UI_NG_CELL_OPA       LV_OPA_80
#define UI_NG_BAND_OPA       LV_OPA_70
#define UI_NG_BACK_OPA       LV_OPA_60    /* the dim layer behind the cells of a page that has its own text */
/* the status icons of 封面＋資訊 (passive: playing / paused, shuffle, repeat), a row in the column right of the cover */
#define UI_NOWA_STATUS_D     36
#define UI_NOWA_STATUS_GAP   6
#endif

/* the same on both */
/* design values are 360-based; UI_SCALE(v) enlarges a drawn size to this panel's content box (identity on the 360 round panel) */
#define UI_SCALE(v)          ((v) * UI_CONTENT_D / UI_DESIGN_D)
#if UI_ROUND
#define UI_QR_D              88      /* the help pages' QR code (the square panel makes it large: a phone reads it at arm's length) */
#define UI_TEXT_SPACE        0       /* the square column raises letter spacing and line height: the font sizes are the per-panel UI_FONT_*_PX (font.c rasterises them at run time) */
#define UI_TEXT_LINE_EXTRA   0
#else
#define UI_QR_D              206     /* (was 230 with the 20 px font: the QR box has to fit the 5 rows of a titled list) */
#define UI_TEXT_SPACE        1       /* (was 2 / 6 at the 20 px font; the 24 px font is airy enough) */
#define UI_TEXT_LINE_EXTRA   4
#endif
#if UI_ROUND
#define UI_ROW_H             48
#else
#define UI_ROW_H             64      /* six rows fill the list (fppolish: 57 -> 64, 404 px; was 48: seven rows) */
#endif
#if UI_ROUND
#define UI_ROW_GAP           4
#else
#define UI_ROW_GAP           10      /* rows too close to aim at on the FakePod; 6 rows of 64 + 5 gaps = 434 (13 px above and below) */
#endif
#define UI_LIST_STEP         (UI_ROW_H + UI_ROW_GAP)   /* one knob detent scrolls exactly one row (R58) */
#define UI_HUB_BACK_Y        62
#define UI_ICON_D            48
#define UI_AXIS_LINE_Y       62
#define UI_AXIS_LINE_W       160

/* the popup card (ui_frame.c): ONE solid (opaque) rounded modal card, centred, holding the page title, the options and Back; the content behind is not dimmed (a full-screen alpha blend costs fps).
 * Every popup user takes the same card: the page frame's popup, the shared exit's pause overlay (UI_POP_PAUSE_Y: its Continue / Back card; the frame's options card sits above it) */
#if UI_ROUND
#define UI_POP_PAD           12      /* card inner padding */
#define UI_POP_GAP           UI_TAP_GAP   /* between two items of the card */
#define UI_POP_BTN_W         120     /* the pause card's Continue / Back */
#else
#define UI_POP_PAD           16      /* (fppolish: was 12 / 8 with 48 px items; the items are UI_TAP_MIN = one list row now) */
#define UI_POP_GAP           12
#define UI_POP_BTN_W         200
#endif
#if UI_ROUND
#define UI_POP_RADIUS        20
#define UI_POP_W_MAX         290     /* the circle: the card's corners must stay inside the glass */
#define UI_POP_PAUSE_Y       48      /* centre of the pause card (below the middle; the options card above it) */
#else
#define UI_POP_RADIUS        26
#define UI_POP_W_MAX         420     /* the panel width less a margin of 20 */
#define UI_POP_PAUSE_Y       60
#endif
#define UI_POP_TOP           (UI_SLOT_TITLE_Y + 26)   /* the card never reaches above this (centre offset): the status / score line of the page (the title slot) stays visible above it */
#if UI_ROUND
#define UI_POP_W_MIN         0
#else
#define UI_POP_W_MIN         UI_POP_W_MAX   /* the square card is always as wide as the game page's: no content edge shows beside it */
#endif
#define UI_POP_PAUSE_W       (UI_POP_BTN_W + 2 * UI_POP_PAD)
#define UI_POP_PAUSE_H       (2 * UI_BACK_H + UI_POP_GAP + 2 * UI_POP_PAD)

/* page kit controls (ui_kit.c); the choice grid's cells are in the panel columns above */
#define UI_PILL_W            200     /* ui_pill and ui_axis_button, at most (the slot's glass fit may be narrower) */
#define UI_BAR_W             240     /* ui_progress_bar, at most */
#define UI_BAR_H             10
#define UI_DOT_D             12      /* ui_dots */
#define UI_DOT_GAP           8

/* ring (ui_ring.c): the centre of its 360 container, the segment ring, the bubble ring */
#define UI_RING_CX           (UI_RING_BOX_W / 2)
#define UI_RING_CY           (UI_RING_BOX_H / 2)
#define UI_RING_PAGE_Y       (-14)   /* page-number label, from the bottom of the centre disc */
#define UI_RING_DISC_INSET        16 /* centre disc: 2 * UI_RING_RI - this */
#define UI_RING_DISC_LABEL_INSET  40 /* centre text width: 2 * UI_RING_RI - this */
#define UI_BUBBLE_R          124
#define UI_BUBBLE_D          84
#define UI_BUBBLE_CD         132
#define UI_BUBBLE_A0         130     /* degrees, screen angles (90 = down) */
#define UI_BUBBLE_SPAN       280
#define UI_RING_BUBBLE_PAGE_Y 88     /* bubble ring page label, from the centre */

/* rim arcs and the volume arc */
#define UI_RIM_PROGRESS_D    358
#define UI_RIM_SLEEP_D       348
#define UI_RIM_W             3
#define UI_VOL_ARC_D         352
#define UI_VOL_ARC_W         6
#define UI_VOL_A0            135
#define UI_VOL_A1            45
#if !UI_ROUND   /* the square panel's volume popup: a pill in the lower third instead of the round board's arc */
#define UI_VOLP_W            340
#define UI_VOLP_H            72
#define UI_VOLP_Y            150     /* centre, from the panel centre: bottom edge at 186 of 230 */
#define UI_VOLP_BAR_W        190
#define UI_VOLP_BAR_H        12
#define UI_VOLP_OPA          LV_OPA_COVER   /* opaque: a cover or text behind must not shine through */
_Static_assert(UI_VOLP_Y + UI_VOLP_H / 2 <= UI_H / 2 - 40, "the volume popup clears the bottom strip");
#endif

_Static_assert(UI_RING_RO <= UI_CX, "the ring must fit the panel");
_Static_assert(UI_BACK_H >= UI_TAP_MIN, "Back is a tap target");
#if UI_ROUND
_Static_assert(UI_SLOT_FOOT_Y + UI_TAP_MIN / 2 + UI_TAP_GAP <= UI_BACK_Y - UI_BACK_H / 2, "FOOT row clears Back by one gap");
_Static_assert(UI_LIST_Y + UI_LIST_H / 2 + UI_TAP_GAP <= UI_BACK_Y - UI_BACK_H / 2, "a list ends one gap above Back");
_Static_assert(UI_LIST_TITLED_Y + UI_LIST_TITLED_H / 2 + UI_TAP_GAP <= UI_BACK_Y - UI_BACK_H / 2, "a titled list ends one gap above Back");
#else   /* the square panel has no Back button on the page (it lives in the popup card): a list may reach the bottom margin the top one has */
_Static_assert(UI_LIST_Y + UI_LIST_H / 2 <= UI_H / 2 - 12 && UI_LIST_Y - UI_LIST_H / 2 >= -UI_H / 2 + 12, "a list keeps a 12 px margin of the panel");
_Static_assert(UI_LIST_TITLED_Y + UI_LIST_TITLED_H / 2 <= UI_H / 2 - 12, "a titled list keeps a 12 px bottom margin");
_Static_assert(UI_LIST_H == 6 * UI_ROW_H + 5 * UI_ROW_GAP && UI_LIST_TITLED_H == 5 * UI_ROW_H + 4 * UI_ROW_GAP, "no row is cut at the list's edge");
#endif
_Static_assert(UI_BACK_Y + UI_BACK_H / 2 <= UI_H / 2, "Back stays on the panel");
#if !UI_ROUND
_Static_assert(3 * UI_NOWA_STATUS_D + 2 * UI_NOWA_STATUS_GAP <= UI_W - UI_NOW_M - UI_NOWA_COVER_D - UI_NOW_M, "A: the status icons fit the column right of the cover");
_Static_assert(UI_NG_CELL_W <= UI_W / 3 && UI_NG_CELL_H + UI_NG_BAND_H <= UI_H / 3, "the nine-grid's cells leave room for a band between two rows");
#endif
#if UI_ROUND
_Static_assert(UI_BACK_Y + UI_BACK_H / 2 <= UI_H / 2 - 18, "Back keeps the glass margin of the round panel");
#endif
#ifdef UI_GEOM_SELFTEST_BAD
_Static_assert(0, "selftest");
#endif
