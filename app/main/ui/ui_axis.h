// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
/* The borrowed knob (UI rework Task 7; the owner's rule R21: every control with three or more values takes the knob).
 * A control is an "axis": a tap on it makes it the current one and steps it +1 (so taps alone still work); then the knob steps it
 * both ways (wrapping, applied at once); 4 s after the last tap or turn the knob goes back to its normal job and the value is stored.
 * Two-value switches stay plain taps. One borrow at a time: its state is one small block (PSRAM) that lives while it is on and is
 * freed when it ends; the only static state is that block's pointer. All calls from the LVGL task.
 * The knob reaches it through the page: a page with axes starts its on_knob with `if (ui_axis_knob(d)) return true;`. */

typedef bool (*ui_axis_step_fn)(int axis, int detents);   /* change `axis` by `detents` (signed, any size, wraps); false: the axis is inert now (nothing changed) */
typedef void (*ui_axis_end_fn)(void);

/* True while a step runs because of a tap (a borrow's step_now, a ring cell's tap); false for the knob. A bounded axis passes it to ui_axis_step. */
bool ui_axis_from_tap(void);
/* A half tap of a selected row (ui_list_row_axis_tap in ui_kit.h): the same step function as the knob, one detent, with the ends STOPPING (no wrap by touch). ui_axis_clamping() is true during
 * it; a step function that wraps a ring of choices asks ui_axis_idx (clamps while a half tap runs, wraps for the knob) instead of pc_wrapi_step; a step that cannot know its index asks
 * ui_axis_would_wrap(cur, next, d) for a list ordered by value (true: skip the change; pass d negated for a list that runs down). A ring of choices a page declares as a ring (theme, language) ignores it. */
bool ui_axis_half(int d);
bool ui_axis_clamping(void);
int  ui_axis_idx(int i, int d, int n);
bool ui_axis_would_wrap(int cur, int nxt, int d);
/* The one rule of a bounded axis (brightness, volume limit, BPM ...), written here only. Knob (from_tap false): clamp(v + step*detents, lo, hi).
 * Tap (from_tap true): v == hi ? lo : min(v + step, hi), so taps alone reach every value. Callers pass ui_axis_from_tap(). */
int ui_axis_step(int v, int lo, int hi, int step, int detents, bool from_tap);

/* ---- a borrowed control without a ring of its own: a cell of a mixed ring (actions and axes), a list row ---- */
/* `owner`: the object that holds the control (the page root, the list). Its deletion ends the borrow silently (`end` does not run:
 * the page is going); a hidden ancestor (another page on top) means the knob is not taken. A borrow of the same owner, axis and
 * step again only steps (step_now) and restarts the 4 s; any other borrow, ring or hint first ends the current one.
 * step_now: step +1 now (the tap). No memory for the block: the step still happens, the knob stays where it was. */
void ui_axis_borrow(lv_obj_t *owner, int axis, ui_axis_step_fn step, ui_axis_end_fn end, bool step_now);
bool ui_axis_knob(int detents);          /* steps the current axis, 4 s again, true; false: nothing borrowed, its page covered, or the ring's hint */
int  ui_axis_current_of(lv_obj_t *owner);   /* the axis `owner` has borrowed now (borrow or ring), -1 none */
void ui_axis_end(void);                  /* a borrow ends now (`end` runs); a ring is not touched */
void ui_axis_touch(void);                /* the page did something: 4 s again (a ring: its mark and lines again too) */
void ui_axis_stop(void);                 /* whatever is on ends now without a hint: a ring closes (values stored), a hint goes, a borrow ends */

/* ---- a ring whose cells are all axes (the spectrum page's style ring, the clock face) ---- */
/* The ring is the segmented menu ring on a transparent overlay of `page`, its centre hidden (what is behind stays visible), Back at the
 * bottom. Two lines over the page: y -62 "<value>" (160 wide, one line, dots), y +62 the knob hint. A tap on a cell that is not
 * the current axis makes it current, a tap on the current one steps it +1; the knob steps the current axis; each restarts the 4 s;
 * at 4 s the ring closes with the hint. */
typedef struct {
    int n;                                        /* the axes = the cells, at most UI_AXIS_RING_MAX (the square grid pages a ring of more than 6) */
    const char *(*name)(int axis);                /* the cell and the first line; asked again after every tap or turn, so a cell may change its name */
    const char *(*value)(int axis);               /* the first line after the dot */
    const char *(*hint)(int axis);                /* the second line; NULL: "旋鈕＝<name>" ("Knob = <name>") */
    ui_axis_step_fn step;
    void (*store)(void);                          /* write what the axes changed: at every close, also when the page goes with the ring open; may be NULL */
    void (*centre)(void);                         /* the centre tap; NULL: ui_axis_ring_close(true) */
    void (*back)(void);                           /* the Back cell; the page closes the ring itself (ui_axis_ring_close) */
    const char *(*after)(char *line, size_t cap); /* the hint after a close: fills the first line (280 wide), returns the second; NULL: no hint */
    void (*gone)(void);                           /* the lines went (the hint ended, a close without hint, another ring): not when the page is deleted */
    bool (*action)(int axis);                     /* NULL, or asked first on a tap of a cell: true = it was an action cell (a 說明 cell) and has been run (it may close the ring); the cell does not become current and nothing steps. False: the usual tap. */
} ui_axis_ring_t;

/* false: no memory (nothing on the screen changed but the previous borrow/hint ending). Already open (this def): ui_axis_touch.
 * axis: the current axis at open; step_now: step it +1 at once (the tap that opened it was on that axis). */
bool ui_axis_ring_open(lv_obj_t *page, const ui_axis_ring_t *def, int axis, bool step_now);
void ui_axis_ring_close(bool hint);      /* the open ring (if any) closes: store, then the hint (when asked and def->after) or the lines go */
bool ui_axis_ring_up(const ui_axis_ring_t *def);        /* this ring is open */
int  ui_axis_ring_axis(const ui_axis_ring_t *def);      /* its current axis while it is open, else -1 */
bool ui_axis_hinting(const ui_axis_ring_t *def);        /* this ring closed and its hint still shows */
void ui_axis_ring_pick(int idx);         /* console: a tap on cell idx (-1 the centre), as the ring does it */
const char *ui_axis_line(const ui_axis_ring_t *def, int i);   /* the text of line i (0 top, 1 hint), NULL when not shown */
