// SPDX-License-Identifier: LGPL-2.1-or-later
/* The tap-revealed ring (header ui_popring.h). The dim layer, the 4 s rule, the axis marks and the silent end with the page were copied in
 * now_menu.c and scr_album.c; they live here once. */
#include "ui_popring.h"
#include "ui_arena.h"
#include <stdio.h>
#include <string.h>
#include "pc/pc_utf.h"
#include "psram_alloc.h"
#include "ui.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

#define POP_CELLS (2 * UI_RING_PER_PAGE)           /* the ring holds 14 cells at most (a long ring pages: UI_RING_PER_PAGE per page, one less plus a "next page" cell in menu style, see ui_ring.c) */

typedef struct {
    const ui_popring_t *def;
    lv_obj_t *page, *dim, *ring;
    lv_timer_t *t;                                 /* the 4 s one-shot; NULL once it ran */
    int n;                                         /* cells shown: own + the status cell + the help cell */
    int own;                                       /* the def's own cells kept (n minus the status and help cells) */
    bool refused;                                  /* the tap's axis step said "inert now": the tap keeps no borrow (as ui_axis_button) */
} pop_t;
static pop_t *s_p;
static int8_t s_dim_force = -1;                   /* console override of def->no_dim: -1 none, 0 no layer, 1 the layer */

static void ring_pick(int i);
static void back_cell(void);
static void changed(lv_event_t *e);

static void timeout(lv_timer_t *t)
{
    (void)t;
    if (!s_p) return;
    s_p->t = NULL;                                 /* a one-shot: LVGL deletes it after this run */
    ui_popring_close();
}

static void timer_restart(void)
{
    if (!s_p) return;
    if (s_p->t) { lv_timer_reset(s_p->t); return; }
    s_p->t = lv_timer_create(timeout, UI_HOLD_MS, NULL);
    lv_timer_set_repeat_count(s_p->t, 1);
}

static void unmark(void) { if (s_p && s_p->ring) ui_ring_set_mark(s_p->ring, -1); }
static void mark_axis(void)                        /* the borrowed axis cell is drawn as current */
{
    if (s_p && s_p->ring) ui_ring_set_mark(s_p->ring, ui_axis_current_of(s_p->page));
}

static bool is_axis(const ui_popring_t *d, int i) { return d->step && i >= 0 && s_p && i < s_p->own && i < 32 && (d->axis_mask >> i & 1); }

static void release(void)                          /* the block goes; the objects are the caller's */
{
    pop_t *p = s_p;
    s_p = NULL;
    if (!p) return;
    if (p->t) lv_timer_delete(p->t);
    psram_free(p);
}

static void drop(void)                             /* the ring goes; a borrowed axis stays (a reopen) */
{
    pop_t *p = s_p;
    if (!p) return;
    lv_obj_t *dim = p->dim;
    release();                                     /* first: the DELETE of dim finds nothing to do */
    lv_obj_delete(dim);                            /* the ring goes with it */
    ui_arena_kick();                               /* a toy field under it goes on */
}

void ui_popring_close(void)
{
    if (!s_p) return;
    if (ui_axis_current_of(s_p->page) >= 0) ui_axis_end();   /* the knob is volume again */
    drop();
}

static void dim_deleted(lv_event_t *e)             /* the page went away with the ring open: silently, no callback of def */
{
    if (!s_p || lv_event_get_target_obj(e) != s_p->dim) return;
    release();
}

static void dim_clicked(lv_event_t *e)             /* a tap on the dim layer itself (not on a cell) closes */
{
    if (s_p && lv_event_get_target_obj(e) == s_p->dim) ui_popring_close();
}

bool ui_popring_open(lv_obj_t *page, const ui_popring_t *def, int page_no)
{
    if (s_p && s_p->def != def) ui_popring_close();   /* another ring: the old one closes for real (its borrow ends) */
    else drop();                                      /* the same ring again (relabel, rebuild): the borrow stays */
    pop_t *p = psram_calloc(1, sizeof *p);
    if (!p) return false;
    p->def = def;
    p->page = page;
    int extra = (def->status ? 1 : 0) + (def->help ? 1 : 0);
    int room = POP_CELLS - extra;                  /* the status and help cells always fit: the def's own cells give way, not they */
    p->own = def->n < room ? def->n : room;
    if (p->own < def->n && !def->status) printf("APP popring def has %d cells, %d kept (the help cell takes one of %d)\n", def->n, p->own, POP_CELLS);
    else if (p->own < def->n) printf("APP popring def has %d cells, %d kept (the status and help cells take %d of %d)\n", def->n, p->own, extra, POP_CELLS);
    p->n = p->own + extra;
    lv_obj_t *dim = lv_obj_create(page);
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, UI_RING_BOX_W, UI_RING_BOX_H);
    if (s_dim_force < 0 ? !def->no_dim : s_dim_force) {
        lv_obj_set_style_bg_color(dim, UI_BG, 0);
        lv_obj_set_style_bg_opa(dim, LV_OPA_70, 0);
    }
    lv_obj_add_flag(dim, LV_OBJ_FLAG_CLICKABLE);    /* taps on the dim layer do not reach the page below */
    lv_obj_add_event_cb(dim, dim_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(dim, dim_clicked, LV_EVENT_CLICKED, NULL);
    p->dim = dim;
    s_p = p;
    const char *labels[POP_CELLS];
    for (int i = 0; i < p->n; i++) labels[i] = i < p->own ? (def->label ? def->label(i) : "") : (def->status && i == p->own) ? ui_status_label() : ui_help_label();
    lv_obj_t *ring = ui_ring_create_menu(dim, labels, p->n, ring_pick, back_cell);
    p->ring = ring;
    if (ring) {
        if (def->knob_volume) ui_ring_set_knob_volume(ring);
        ui_ring_set_arm_hosted(ring);
        if (def->armed) ui_ring_set_arm_text_fn(ring, def->armed);   /* read at the first tap, not only at the open (the page's state may have moved) */
        ui_ring_set_center_text(ring, def->centre ? def->centre() : NULL);
        for (int i = 0; i < p->own && i < 32; i++) {   /* the masks: the def's own cells, never the help cell */
            if (def->sys_mask >> i & 1) ui_ring_set_system(ring, i);
            if (def->press_mask >> i & 1) ui_ring_set_item_press(ring, i);
            if ((def->arm_mask >> i & 1) && def->armed) ui_ring_set_item_arm(ring, i, def->armed(i));
        }
        if (page_no > 0) ui_ring_page(ring, page_no);   /* before the callback exists */
        mark_axis();
        lv_obj_add_event_cb(ring, changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
    timer_restart();
    lv_obj_update_layout(dim);                     /* laid out now: a press in the same frame hits the new cells, not their pre-layout boxes (batch B fix A-m1) */
    return ring != NULL;
}

bool ui_popring_up(const ui_popring_t *def) { return s_p && s_p->def == def; }

void ui_popring_refresh(void)
{
    if (!s_p || !s_p->ring) return;
    const ui_popring_t *d = s_p->def;
    for (int i = 0; i < s_p->own && d->label; i++) ui_ring_set_label(s_p->ring, i, d->label(i));   /* copied at once: a shared buffer of label() is fine */
    if (d->centre) ui_ring_set_center_text(s_p->ring, d->centre());
}

lv_obj_t *ui_popring_ring(void) { return s_p ? s_p->ring : NULL; }

void ui_popring_raise(void) { if (s_p) lv_obj_move_foreground(s_p->dim); }

void ui_popring_relabel(void)
{
    if (!s_p) return;
    ui_popring_open(s_p->page, s_p->def, ui_ring_get_page(s_p->ring));
}

/* a page turn (knob or the "next page" cell) or an armed first tap is an action: the 4 s start again. A page turn keeps a borrowed axis (touched);
 * an armed first tap is a tap on another cell: the knob is volume again and only the armed cell is lit */
static void changed(lv_event_t *e)
{
    (void)e;
    timer_restart();
    if (!s_p) return;
    bool borrowed = ui_axis_current_of(s_p->page) >= 0;
    if (borrowed && ui_ring_armed(s_p->ring) >= 0) ui_axis_end();
    else if (borrowed) ui_axis_touch();
}

/* every step of a borrowed axis (the tap and each knob turn) is an action: the ring's 4 s start again, whatever the caller's step does */
static bool step_wrap(int axis, int d)
{
    if (!s_p || !s_p->def->step) return false;
    bool r = s_p->def->step(axis, d);
    if (s_p && !r && ui_axis_from_tap()) s_p->refused = true;   /* the step may have reopened the ring: this is the block now */
    timer_restart();
    return r;
}

static void ring_pick(int i)
{
    if (!s_p) return;
    const ui_popring_t *d = s_p->def;
    lv_obj_t *page = s_p->page;
    if (d->status && i == s_p->own) {                  /* the Status cell, as the help cell below */
        ui_popring_close();
        ui_help_open(d->status);
        return;
    }
    if (d->help && i == s_p->own + (d->status ? 1 : 0)) {   /* the help cell: the ring and a borrowed knob go, the explain page opens */
        ui_popring_close();
        ui_help_open(d->help);
        return;
    }
    if (is_axis(d, i)) {
        s_p->refused = false;
        ui_axis_borrow(page, i, step_wrap, unmark, true);   /* steps: may relabel (reopen) */
        if (s_p && s_p->refused && ui_axis_current_of(page) == i) ui_axis_end();   /* refused: the knob does what it does without a borrow */
    } else {
        if (ui_axis_current_of(page) >= 0) ui_axis_end();                /* any other cell: the knob is volume again */
        if (d->pick) d->pick(i);
        if (i < 0 && !d->centre && s_p && s_p->def == d) ui_popring_close();   /* the centre reads Close: it closes */
    }
    if (!s_p) return;                              /* the pick closed the ring */
    mark_axis();
    timer_restart();
}

void ui_popring_pick(int i) { if (s_p && s_p->ring && i < s_p->n) ui_ring_tap(s_p->ring, i); else ring_pick(i); }   /* a touch and the console take the same path (an armed cell asks for its second tap); a cell the ring does not have goes to the pick as before */

static void back_cell(void)
{
    if (!s_p) return;
    void (*b)(void) = s_p->def->back;
    if (b) b();
    else ui_popring_close();
}

void ui_popring_console(int pick)
{
    if (pick == -2) {
        lv_obj_t *top = ui_top_root();
        if (top) lv_obj_send_event(top, LV_EVENT_CLICKED, NULL);   /* what a tap on the page does (the handler ui_page_tap put there) */
    } else if (s_p && s_p->ring) {
        if (pick == -4) ui_ring_page(s_p->ring, 1);
        else if (pick == -3) ui_ring_tap(s_p->ring, pick);
        else if (pick == -1 || pick >= 0) ui_popring_pick(pick);
    }
    if (!s_p) { printf("APP popring open=0 page=0 cells=0 armed=-1 centre=-\n"); return; }
    char c[96];
    pc_utf8_copy(c, sizeof c, s_p->ring ? ui_ring_centre_text(s_p->ring) : "-");   /* a long text is cut at a code point (final review I-3) */
    for (char *q = c; *q; q++) if (*q == '\n') *q = '/';            /* one console line */
    printf("APP popring open=1 page=%d cells=%d armed=%d centre=%s\n", ui_ring_get_page(s_p->ring), s_p->n, ui_ring_armed(s_p->ring), c);
}

void ui_popring_dim_force(int v)
{
    s_dim_force = (int8_t)(v < 0 ? -1 : v ? 1 : 0);
    printf("APP popring dim=%s\n", s_dim_force < 0 ? "auto" : s_dim_force ? "1" : "0");
}
