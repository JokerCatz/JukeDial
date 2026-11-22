// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lvgl.h"
#include "pc/pc_fortune.h"
#include "ui_axis.h"
#include "icons_gen.h"
/* The 4 s rule, one value for all of it: an armed two-tap button, a borrowed knob (ui_axis), the playing ring and the album ring close after this long without a tap or turn. */
#define UI_HOLD_MS 4000

/* Timers that end with an object (spec 2026-11-11-ui-kit-diagnostics 3.2). All calls from the LVGL task. */

/* A timer for as long as `owner` lives: created here, deleted when `owner` gets LV_EVENT_DELETE. The caller never deletes
 * it (no lv_timer_delete, no repeat count: a one-shot would delete itself and then be deleted again) and must not use it
 * after `owner` is gone. NULL when the timer or the event could not be allocated; nothing is registered then. With
 * LV_USE_ASSERT_MALLOC (on in app/sdkconfig) a failed lv_timer_create halts in LVGL first, so the callers do not
 * check for NULL beyond what they did with lv_timer_create. */
lv_timer_t *ui_timer_bind(lv_obj_t *owner, lv_timer_cb_t cb, uint32_t period_ms, void *user);

/* For a timer the page stops and starts itself: it creates and deletes it as before (and sets *slot = NULL when it does,
 * a one-shot also in its callback); when `owner` is deleted and *slot is not NULL, the timer is deleted and *slot cleared.
 * `slot` must be static storage (it outlives `owner`). Once per slot per create. If the event cannot be allocated nothing
 * is registered and the page's own deletes are all there is (as before). */
void ui_timer_guard(lv_obj_t *owner, lv_timer_t **slot);

/* A page's static pointer to one of its objects (its list): when `obj` is deleted, *slot is cleared, but only while it still points at `obj`
 * (a newer page of the same type may have taken the slot). `slot` must be static storage. Returns obj (store it and guard it in one line). */
lv_obj_t *ui_obj_guard(lv_obj_t *obj, lv_obj_t **slot);

/* Shared small widgets (spec 2026-11-11-ui-kit-diagnostics 3.1). All calls from the LVGL task. */

/* A tap-only control (list rows): a touch that began on it and moved off its spot (2 x UI_TAP_GAP) clicks nothing wherever it lifts (ui.c flick_tick; * a drag begun on a row of a list too short to scroll changed that row's setting on release). A list that does scroll never clicked (LVGL's own rule). */
#define UI_FLAG_TAP_ONLY LV_OBJ_FLAG_USER_1

/* A centred, wrapping, non-clickable label `w` wide at centre offset (0, y). `font` NULL keeps the parent's font. */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, int y, int w);

/* A w x h button at centre offset (x, y) with a label inside (centred, no text set). `cb` NULL adds no event; `label_out`
 * may be NULL. Sets no colour and no text: the caller does, on the returned object. */
lv_obj_t *ui_button(lv_obj_t *parent, int w, int h, int x, int y, lv_event_cb_t cb, void *user, lv_obj_t **label_out);

/* The shared random generator for free draws (dice, slot, wheel, gacha, lists, vocab quiz), seeded from esp_random() on first use. */
pc_rng_t *ui_rng(void);

/* The one scrolling list (Task 11): settings-style rows, the knob scrolls, one tap chooses. All calls from the LVGL task. */
/* A scroll column, 250 x 250 at centre offset (0, -20), rows 4 px apart, vertical scroll only, the first row UI_LIST_PAD_TOP below the top (clear of the round edge). */
lv_obj_t *ui_list(lv_obj_t *parent);
/* A 240 wide, 48 high button (a row with `text`; `text` NULL: at least 48); CLICKED calls cb(user). Its label is 220 wide, ONE line (fixed height: one line of the current font), cut with dots.
 * `text` NULL adds no label (the caller adds its own child); `cb` NULL = not clickable. Returns the button. */
lv_obj_t *ui_list_row(lv_obj_t *list, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user);
/* The one place a list row gets a look (UI_LOOK_NORMAL: UI_PANEL2 / UI_FG text (as ui_list_row_mark and ui_choice_set), SEL: UI_SEL, DIM: UI_PANEL2 / UI_DIM text, SYS: UI_SYS, ON: UI_ON; REC is not a row look and reads as NORMAL).
 * Sets the row's background and its label's (child 0) text colour; safe on a row without a label. */
void ui_list_row_look(lv_obj_t *row, int look);
/* The look a row shows now, read back from its colours (console dumps and tests): UI_LOOK_NORMAL / SEL / DIM / SYS / ON. */
int ui_list_row_look_get(lv_obj_t *row);
/* Axis rows: a row whose tap borrows the knob (ui_axis_borrow) is declared with ui_list_row_axis right after ui_list_row (it adds the "-" and "+" chips, children 1 and 2).
 * The row's callback is `ui_list_row_axis_tap(e, owner, axis, step, end)`: a tap on an unselected row only SELECTS it (no step); on the selected row the LEFT HALF steps -1 and the RIGHT HALF +1
 * (one detent through the knob's own step function; the ends stop: ui_axis_half). While selected (ui_list_row_look SEL, or ui_list_row_axis_sync after a page that sets the colour itself) the
 * chips show at the row's two ends (UI_ROW_PM_W wide, the row's height) and the text is centred between them (wrapping to two lines when it must). Every tap restarts the 4 s. */
void ui_list_row_axis(lv_obj_t *row);
void ui_list_row_axis_tap(lv_event_t *e, lv_obj_t *owner, int axis, ui_axis_step_fn step, ui_axis_end_fn end);
void ui_list_row_axis_force(int dir);   /* tests: the half a pointer-less click counts as (-1 left, anything else right) */
void ui_list_row_axis_sync(lv_obj_t *row);
/* A dim, centred, non-clickable, wrapping line (empty-list text, "... N more"). Returns its label (a page that renews the text, hires T11, keeps it). */
lv_obj_t *ui_list_note(lv_obj_t *list, const char *text);
/* Sets the text of a row's label (child 0) when it differs (a row renewed by a timer: no redraw for the same text). Safe on a row without a label. */
void ui_list_row_set_text(lv_obj_t *row, const char *text);
/* A row whose text may take two lines (a long action): like ui_list_row, but the label (child 0, for ui_arm_tap) wraps and the row grows to fit it, at least 48 high. */
lv_obj_t *ui_list_row_wrap(lv_obj_t *list, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user);
/* A group heading inside a list (UI_A1, left aligned, non-clickable). */
void ui_list_header(lv_obj_t *list, const char *text);
/* The knob: one row (UI_ROW_H + UI_ROW_GAP) a detent, bounded, no animation. Returns true (the page's on_knob can return it); `list` NULL -> false. */
bool ui_list_knob(lv_obj_t *list, int detents);
/* Scroll so that `row` is fully visible (the current item). No animation. */
void ui_list_show(lv_obj_t *list, lv_obj_t *row);

/* A tap on the page itself (its target is `page`, not a child control): cb() runs. Marks the page clickable. Today's root_click pattern, once. */
void ui_page_tap(lv_obj_t *page, void (*cb)(void));

/* A two-tap button (Task 12; the settings page moves onto it at the merge). Call it from the button's CLICKED handler:
 * the first tap shows `armed_text` on the button's label (child 0) for 4 s and returns false; a second tap on the same button
 * within that time restores the label and returns true (do the action). The text comes back by itself after 4 s, when the
 * page (the button) is deleted, and when another button is armed. One armed button at a time. Static state: 3 pointers
 * (12 bytes); the saved label text is one lv_strdup copy that lives while armed. All calls from the LVGL task. */
bool ui_arm_tap(lv_obj_t *btn, const char *armed_text);
/* Put the armed button's text back now (the page changed something else); no-op when none is armed. */
void ui_arm_cancel(void);

/* ---- the page kit (UI unification T4): a page names a slot (a role with a y, a width and the glass edge built in) and a piece; it never sets a position ---- */
typedef enum { UI_SLOT_TITLE, UI_SLOT_HEAD, UI_SLOT_UPPER, UI_SLOT_CENTER, UI_SLOT_LOWER, UI_SLOT_FOOT, UI_SLOT_BASE, UI_SLOT_N } ui_slot_t;
/* look of a choice cell (ui_choice_set) */
enum { UI_LOOK_NORMAL = 0, UI_LOOK_SEL = 1, UI_LOOK_DIM = 2, UI_LOOK_SYS = 3, UI_LOOK_REC = 4, UI_LOOK_ON = 5, UI_LOOK_HELP = 6 };   /* ON: the second accent (UI_ON), a switched-on item. REC: selected colour + a 3 px UI_WARN border (the instrument's recording track) */

/* A centred, wrapping, non-clickable label in the slot (width fitted to the glass for one line of `f`; NULL keeps the parent's font). */
lv_obj_t *ui_text(lv_obj_t *parent, ui_slot_t s, const lv_font_t *f);
/* A transparent, non-clickable box h high in the slot (width as ui_text): the parent of a picture or glyph text that sits in a slot (the vocab deck title). */
lv_obj_t *ui_slot_holder(lv_obj_t *parent, ui_slot_t s, int h);
lv_obj_t *ui_text_dim(lv_obj_t *parent, ui_slot_t s, const lv_font_t *f);      /* the same in UI_DIM */
int ui_slot_y(ui_slot_t s);                    /* centre offset of the slot */
int ui_slot_w(ui_slot_t s, int h);             /* the slot's width, narrowed where the glass is narrower than that at the far edge of an element h px high */
/* Controls (each at least UI_TAP_MIN high; a page keeps them UI_TAP_GAP apart by choosing slots). The label of every button is its child 0. */
lv_obj_t *ui_pill(lv_obj_t *parent, ui_slot_t s, const char *text, lv_event_cb_t cb, void *user);   /* one 48 high button */
void ui_pill_set_text(lv_obj_t *pill, const char *text);   /* the pill's label (child 0); NULL pill or text: nothing */
/* n equal cells in a row (a transparent container; the cells are its children, user = the index; `cells` may be NULL; n is cut to what fits with every cell at least UI_TAP_MIN wide, so fewer cells than n may exist). Texts and looks through ui_choice_set. */
lv_obj_t *ui_choice_row(lv_obj_t *parent, ui_slot_t s, int n, lv_event_cb_t cb, lv_obj_t **cells);
/* cols x rows (1..2 each) cells UI_GRID_W x UI_GRID_H, user = row * cols + col; cells[] gets them (may be NULL; the entries past the 1..4 cells made, up to the size asked, are set NULL). 2 x 2: x +-UI_GRID_DX, row centres UI_GRID_Y0 / UI_GRID_Y1. */
lv_obj_t *ui_choice_grid(lv_obj_t *parent, int cols, int rows, lv_event_cb_t cb, lv_obj_t **cells);
void ui_choice_set(lv_obj_t *cell, const char *text, int look);              /* text NULL: unchanged */
/* A control with three or more values outside a list or ring: shows text(axis); a tap borrows the knob and steps once (step), then the knob steps it for UI_HOLD_MS; the label follows
 * every step. The button owns the borrow (its deletion ends it); its user data is one small PSRAM block, freed with it. */
lv_obj_t *ui_axis_button(lv_obj_t *parent, ui_slot_t s, int axis, ui_axis_step_fn step, const char *(*text)(int axis));
/* The page's one main action: a UI_BIG_D round button in the slot, `label` (may be NULL) gets its label. */
lv_obj_t *ui_big_button(lv_obj_t *parent, ui_slot_t s, lv_event_cb_t cb, void *user, lv_obj_t **label);
lv_obj_t *ui_progress_bar(lv_obj_t *parent, ui_slot_t s);     /* 0..1000 (lv_bar_set_value), not clickable */
/* `max` dots in the slot (a transparent container, not clickable). ui_dots_set: the first n are shown, dot `lit` (0-based, -1 none) in colour c, the others in UI_PANEL2. */
lv_obj_t *ui_dots(lv_obj_t *parent, ui_slot_t s, int max);
void ui_dots_set(lv_obj_t *d, int n, int lit, lv_color_t c);
/* A centred round tap target `d` px (at least UI_TAP_MIN): cb() on CLICKED. */
lv_obj_t *ui_hub(lv_obj_t *parent, int d, void (*cb)(void));
/* The 360 design box (UI_DESIGN_D): pages and pieces drawn on it (clock faces, the fortune wheel, the slot reels) sit in the middle of any panel.
 * ui_design_origin: the box's top left in page coordinates; ui_design_centre: its middle; ui_design_place: the top left of a w x h block centred in the box. */
void ui_design_origin(int *x, int *y);
void ui_design_centre(int *x, int *y);
void ui_design_place(int w, int h, int *x, int *y);
/* ui_scale: a 360-based drawn size (or offset) enlarged to this panel's content box (identity on the round panel).
 * ui_text_airy: the square column's letter spacing and line height on a label (no change on the round panel; the font sizes stay a fixed set). */
int  ui_scale(int v);
int  ui_scale_big(int v);          /* ui_scale and a third more again: the one drawn thing a square page wants large (identity on the round panel) */
int  ui_scale_pct(int v, int pct); /* ui_scale and pct % of it again (identity on the round panel): the ones above are pct 130 and 150 */
int  ui_scale_fill(int v);         /* a centre object that nearly fills the panel (SQ2): ui_scale and half again (identity on the round panel) */
int  ui_by_panel(int round_v, int square_v);   /* SQ3: the value for this panel, as is (no scaling): a page's few numbers that differ between the glass and the square */
int  ui_edge_d(int round_d);      /* a disc that reaches the panel edge: round_d on the round panel, the content box less a margin on the square one */
void ui_text_airy(lv_obj_t *label);
lv_obj_t *ui_label_d(lv_obj_t *parent, const lv_font_t *font, int y, int w);   /* ui_label with y and w given in 360 design units, scaled to the panel */
int  ui_cover_dy(void);           /* the cover disc's offset below the panel's middle (0 on the round panel; the larger square disc sits under the title line) */
int  ui_cover_scale(void);        /* the now-playing cover disc's zoom in 1/256: 256 on the round panel, larger on the square one */
void ui_list_row_centre(lv_obj_t *row);   /* the row's text centred on the square panel (no change on the round one) */
void ui_content_box(lv_obj_t *o);                             /* size an object to the content box (UI_CONTENT_D) and centre it */
lv_obj_t *ui_content(lv_obj_t *parent);                       /* the drawing box of a content piece: UI_CONTENT_D square, centred, not clickable */
/* A list with a title in UI_SLOT_TITLE; the list is UI_LIST_TITLED_H high at UI_LIST_TITLED_Y. */
lv_obj_t *ui_list_titled(lv_obj_t *parent, const char *title);
lv_obj_t *ui_list_titled_inline(lv_obj_t *parent, const char *title);   /* ui_list_titled on the round panel; on the square one a plain list whose first line is the title (lines joined) */
lv_obj_t *ui_list_title(lv_obj_t *list);           /* the title label of a list made by ui_list_titled (to change its text later) */
/* A row of the list; the current item (`current`) in UI_SEL, the others in UI_PANEL2. */
lv_obj_t *ui_list_row_mark(lv_obj_t *list, const char *text, bool current, lv_event_cb_t cb, void *user);

/* GS: the page-count bubble: a small, not clickable pill ("2/3", UI_PAGE_BUBBLE_FONT) at `align` + (x, y) in `parent`, hidden until ui_page_bubble_set(b, page, pages) is given more than one page. */
lv_obj_t *ui_page_bubble(lv_obj_t *parent, lv_align_t align, int x, int y);
int ui_page_bubble_h(void);   /* its height: the square status strip starts below the bubble (never covers it) */
void ui_page_bubble_set(lv_obj_t *b, int page, int pages);
void ui_list_pager(lv_obj_t *list, int page, int pages, void (*fill)(int page, void *u), void *u);   /* LIB: `list` (the newest ui_list, already holding page `page`, 0-based) is one of `pages`: the side flick turns the page (wrapping), the knob past the last / first row of a page goes on to the next / previous one, the bubble "n-N" sits under the list (not made for one page). fill(page, u) adds the rows of that page to the emptied list. */
void ui_list_pager_resume(lv_obj_t *list);   /* a paged page that is on top again (on_resume): its list takes the flick and the knob again */
void ui_list_flick(lv_obj_t *page, int dir);       /* GS: a left (+1) / right (-1) flick on a page with a list: it scrolls by a screenful, no animation; no-op without a list */

/* ICON-B: an inline LINE icon in front of a text ("<icon> <text>"; the text alone when the icon font is not loaded). `icon` is a UI_ICON_L_* string (NULL: none). Returns out. */
const char *ui_icon_text(char *out, size_t n, const char *icon, const char *text);

/* The zoom (256 = 1:1) that shows a picture w px wide across the whole panel width less a 10 px margin each side on the square panel; round: 256 (the picture is drawn for the round glass) */
int ui_picture_zoom(int w);
