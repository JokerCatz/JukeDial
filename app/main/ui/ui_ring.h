// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lvgl.h"
#include "bsp/bsp_geom.h"
/* Circular menu (spec 4.7): items around the rim, tap selects, tap the centre to confirm,
 * bottom button = back. More than UI_RING_PER_PAGE items -> pages turned with ui_ring_page(). */
#define UI_RING_PER_PAGE (BSP_LCD_ROUND ? 8 : 6)   /* the round ring has 8 cells; the square grid 2 x 3 (bigger cells to hit) */
#define UI_AXIS_RING_MAX 8                          /* a ring of axes (ui_axis.h) holds this many cells; on the square grid more than UI_RING_PER_PAGE page like any ring */
bool ui_ring_disc(void);                          /* the ring has a centre disc a page may put its own text over (the round glass); the square panel's grid has a header band that is the ring's alone */
typedef void (*ui_ring_pick_cb)(int idx);
typedef void (*ui_ring_back_cb)(void);
/* labels are copied. on_back NULL hides the back button. NULL when out of memory (then only a Back button is on the screen
 * when on_back is set); the other ui_ring_* functions accept that NULL. */
lv_obj_t *ui_ring_create(lv_obj_t *parent, const char *const *labels, int n, ui_ring_pick_cb on_pick, ui_ring_back_cb on_back);
void ui_ring_page(lv_obj_t *ring, int delta);    /* wraps; sends LV_EVENT_VALUE_CHANGED to the ring when it turned */
int  ui_ring_per(lv_obj_t *ring);                /* the cells a page of this ring holds (8 on the segmented ring, 7 on the bubble ring); 8 for NULL */
int  ui_ring_get_page(lv_obj_t *ring);           /* 0-based; 0 for NULL */
int  ui_ring_selected(lv_obj_t *ring);           /* -1 none */
void ui_ring_set_center_text(lv_obj_t *ring, const char *text);   /* menu mode: the centre shows this instead of "Close" (copied; NULL = the default); other rings: shown until the next selection */
void ui_ring_set_close(lv_obj_t *ring, ui_ring_back_cb on_close);   /* select-then-confirm rings: a tap on the centre with nothing selected calls this (the page's own way to close the ring) */
void ui_ring_set_system(lv_obj_t *ring, int idx);
/* The rules every ring shares (UI rework Task 3), all in ui_ring.c:
 * - select-then-confirm rings: a tap on the cell that is already selected confirms it, like a tap on the centre
 * - the knob: a ring with more than one page turns its page (one page per detent direction, wraps); ui.c asks ui_ring_knob_in() for the top page's ring
 *   after the page's own on_knob declined, so a page needs no knob code for its ring. One page: the knob stays volume.
 * - a long press on the Back cell (or the bare Back button) goes to the main menu (ui_home) */
lv_obj_t *ui_ring_find(lv_obj_t *root);                 /* the first visible ring below root, NULL when there is none */
bool ui_ring_knob_in(lv_obj_t *root, int detents);      /* true: the ring below root turned its page */
/* GS: no Back cell and no Next page cell. Pages turn by the fast left / right flick (ui.c) or the knob; a small bubble ("2/3", ui_page_bubble) shows the page when there is more than one.
 * ui_ring_back: runs the ring's on_back (the up flick on a page that is not content, or on a popring: it closes); false when the ring has none. */
bool ui_ring_back(lv_obj_t *ring);
void ui_ring_set_item_hint(lv_obj_t *ring, int idx, const char *text);   /* select-then-confirm rings: the centre shows this (copied) while item idx is selected, instead of its name; a cell with a consequence says it. NULL clears */
void ui_ring_set_item_now(lv_obj_t *ring, int idx);     /* select-then-confirm rings: item idx (< 32) runs at its first tap (an axis cell: the tap steps it, ui_axis.h) */
void ui_ring_set_knob_volume(lv_obj_t *ring);           /* this ring opts out: the knob stays volume (the playing ring) */
void ui_ring_print(lv_obj_t *ring);                     /* console "ring": APP ring page=<p>/<pages> sel=<i> center=<text> n=<cells, Back not counted> (ring NULL: APP ring none) */
/* the playback menu (spec 5.8): always the segmented ring, a tap runs the item at once; centre tap -> on_pick(-1) */
lv_obj_t *ui_ring_create_menu(lv_obj_t *parent, const char *const *labels, int n, ui_ring_pick_cb on_pick, ui_ring_back_cb on_back);   /* draw this item in the system colour (shuffle all, rescan) */
void ui_ring_set_center_hidden(lv_obj_t *ring, bool hidden);   /* the centre disc and its text are not drawn; a tap in the middle still does what it did */
/* Icons on a menu ring (ICON-A, the themes-icons design notes "Icons"): icons[i] is an UI_ICON_* string of the FILLED family (static, not copied; NULL = a text cell), n entries. Round: the cell shows the icon alone and the
 * centre names it after the first tap (two taps as before); square: icon above the short name, one tap. A 說明 cell gets the help icon by itself. Without ui-icon.ttf nothing changes (the names stay). */
void ui_ring_set_icons(lv_obj_t *ring, const char *const *icons, int n);
void ui_ring_set_label(lv_obj_t *ring, int idx, const char *text);   /* the label of item idx (copied); an axis cell that changes its name with the kind (ui_axis.h) */
void ui_ring_set_mark(lv_obj_t *ring, int idx);                /* menu mode: this item is drawn in the selected colour (the current value); -1 none */
/* Menu rings only (ui_ring_create_menu): UI unify T3, spec 2.4.
 * armed cell: the first tap shows `armed_text` in the centre and marks the cell (copied; NULL clears), a second tap on it before UI_HOLD_MS runs it
 * (on_pick); a tap on any other cell, the centre or Back, or a page turn, ends the arm. Alone, the ring ends it after UI_HOLD_MS with a one-shot
 * of its own; inside a popring (ui_ring_set_arm_hosted) the ring's close is the end. A first tap sends LV_EVENT_VALUE_CHANGED to the ring. */
void ui_ring_set_item_arm(lv_obj_t *ring, int idx, const char *armed_text);
/* the armed text is asked again from `fn(idx)` at every first tap of an armed cell (before the centre shows it): state that changes while the ring is open. The text given to ui_ring_set_item_arm stays the first one. */
void ui_ring_set_arm_text_fn(lv_obj_t *ring, const char *(*fn)(int idx));
/* that cell runs at the touch-down (PRESSED), not at the release (tap tempo); the CLICKED of the same touch does nothing. Segmented ring only. */
void ui_ring_set_item_press(lv_obj_t *ring, int idx);
void ui_ring_set_arm_hosted(lv_obj_t *ring);       /* a popring owns the 4 s: this ring starts no timer for an arm */
int  ui_ring_armed(lv_obj_t *ring);                /* the armed item, -1 none */
const char *ui_ring_centre_text(lv_obj_t *ring);   /* what the centre shows now */
void ui_ring_tap(lv_obj_t *ring, int idx);         /* console: a tap on item idx (-1 the centre, -3 Back) exactly as the touch does it (arm, press cells included) */
