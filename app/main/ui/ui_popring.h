// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_axis.h"
#include "ui_help.h"
/* The tap-revealed ring (UI unify T3, spec 2.5): a page shows nothing until it is tapped, then the dim layer and a segmented menu ring appear
 * over it; 4 s after the last tap (a tap on any cell, an armed first tap, a page turn) they go. The playing ring and the album ring are this.
 * One at a time: the state is one PSRAM block and one static pointer. All calls from the LVGL task. */
typedef struct {
    int n;                                  /* cells, at most 14 (POP_CELLS), or 13 with a help cell (12 with a status cell too): a longer def is cut (a line "APP popring def has ..." says so) so the help cell always fits */
    const char *(*label)(int i);            /* asked at every open and relabel; the ring keeps a copy */
    void (*pick)(int i);                    /* a cell ran; -1 = the centre. Not asked for axis cells (step runs for them) */
    const char *(*centre)(void);            /* centre text; NULL: the centre reads "Close" and a tap on it runs pick(-1) if set and closes the ring; with a text of its own pick(-1) decides */
    void (*back)(void);                     /* the Back cell; NULL = close the ring (bottom page of a mode) */
    uint32_t axis_mask; ui_axis_step_fn step;    /* axis cells: a tap = borrow the knob + step (ui_axis_borrow on the page); the cell is drawn as current while borrowed */
    uint32_t arm_mask;  const char *(*armed)(int i);   /* two-tap cells: the first tap shows armed(i) in the centre (asked again at that tap) */
    uint32_t press_mask;                    /* cells that run at the touch-down */
    uint32_t sys_mask;                      /* system colour (reboots, leave the mode) */
    bool knob_volume;                       /* the playing ring's rule: the knob stays volume with two pages */
    bool no_dim;                            /* the instrument: no 70 % layer over the LED rim */
    const ui_help_t *status;                /* hires T7: one extra cell ui_status_label() just before the help cell (counts toward the 14, like help); its pick closes the ring and opens the Status page. Needs no help cell: a def may have either or both */
    const ui_help_t *help;                  /* one extra LAST cell ui_help_label() (counts toward the 14; a def of 14 own cells keeps 13): its pick closes the ring and opens the explain page. The masks above apply to the cells < n only */
} ui_popring_t;
/* Every step of an axis cell (tap or knob turn) restarts the ring's 4 s; `step` need not relabel. A tap whose step returns false keeps no borrow. A ring open with another def closes first (its borrow ends).
 * Opens `def` on `page` at ring page `page_no` (a ring already open goes first, silently: the axis borrow is not ended). `def` must outlive the ring.
 * false: no memory. For the ring itself the dim layer and the ring's own no-memory Back button still appear; for the small state block (24 bytes) nothing opens at all, no Back either. */
bool ui_popring_open(lv_obj_t *page, const ui_popring_t *def, int page_no);
void ui_popring_close(void);                /* no-op when none is open; ends the knob the page borrowed for an axis cell; the ring goes. No callback of `def` runs */
bool ui_popring_up(const ui_popring_t *def);   /* this ring is open */
void ui_popring_relabel(void);              /* labels/centre changed: the same ring, the same page again, a fresh 4 s */
void ui_popring_refresh(void);              /* the same, in place (no new ring, the 4 s run on, an arm stays): each cell's label and the centre asked again, set when changed (a page's poll) */
void ui_popring_pick(int i);                /* a cell ran without a touch (console of the pages that use it): -1 the centre, else the cell; axis cells borrow + step */
lv_obj_t *ui_popring_ring(void);            /* the ring object of the open popring (its page, its pages: ui_ring_get_page / ui_ring_page), NULL when none is open or the ring had no memory */
void ui_popring_raise(void);                /* an open ring moves above whatever its page added after it */
void ui_popring_console(int pick);          /* console "popring N": -2 a tap on the top page, -1 centre, -3 Back, -4 next page, -5 state only, >=0 a cell; prints "APP popring open= page= cells= armed= centre=" */
void ui_popring_dim_force(int v);           /* console "popring dim 1|0|auto" (debug, not saved): 1 every ring opens with the dim layer, 0 without, -1 (auto) as its def says; prints "APP popring dim=<1|0|auto>" */
