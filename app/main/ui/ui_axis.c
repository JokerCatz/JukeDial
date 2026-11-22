// SPDX-License-Identifier: LGPL-2.1-or-later
/* The borrowed knob (header ui_axis.h). The mechanics lifted out of the spectrum page's style ring (UI rework Task 7). */
#include "ui_axis.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "font.h"
#include "ui_kit.h"
#include "screens.h"
#include "ui_ring.h"
#include "ui_geom.h"

#define HINT_MS 1500

/* everything of the one borrow that is on; freed when it ends */
typedef struct {
    lv_obj_t *owner;              /* its deletion ends this block: the borrow's owner, the ring's overlay, during the hint the first line */
    lv_timer_t *t;                /* a one-shot: the 4 s (the hint's 1.5 s); NULL once it ran */
    int axis;
    ui_axis_step_fn step;
    ui_axis_end_fn end;           /* a borrow's live end */
    const ui_axis_ring_t *def;    /* a ring of axes; NULL: a borrow */
    lv_obj_t *ov, *ring;          /* the ring (and its overlay) while it is open */
    lv_obj_t *l1, *l2;            /* the two lines (ring open, then the hint) */
    bool hint;                    /* the ring closed, the lines say so */
} axis_t;
static axis_t *s_a;
static uint8_t s_mode;                             /* 0: the knob; 1: a step runs because of a tap (ui_axis_from_tap); 2: a half tap of a selected row (ui_axis_clamping). One byte, the old s_from_tap's place */

bool ui_axis_from_tap(void) { return s_mode == 1; }
bool ui_axis_clamping(void) { return s_mode == 2; }
int ui_axis_idx(int i, int d, int n)
{
    if (s_mode == 2) { const int j = i + d; return j < 0 ? 0 : j > n - 1 ? n - 1 : j; }   /* a half tap: both ends stop */
    return ((i + d) % n + n) % n;                                                       /* the knob: a ring, as pc_wrapi_step */
}
bool ui_axis_would_wrap(int cur, int nxt, int d) { return s_mode == 2 && ((d > 0 && nxt < cur) || (d < 0 && nxt > cur)); }
bool ui_axis_half(int d)
{
    s_mode = 2;
    const bool r = ui_axis_knob(d);                  /* the knob's own step function and 4 s */
    s_mode = 0;
    return r;
}

int ui_axis_step(int v, int lo, int hi, int step, int detents, bool from_tap)
{
    if (from_tap) {
        v = v < lo ? lo : v > hi ? hi : v;         /* a stored value outside the range (the web page, the card) starts from its end */
        return v >= hi ? lo : (v + step > hi ? hi : v + step);
    }
    long n = (long)v + (long)step * detents;
    return (int)(n < lo ? lo : n > hi ? hi : n);
}

static void tap_step(ui_axis_step_fn f, int axis)   /* a step the tap asks for */
{
    s_mode = 1;
    f(axis, 1);
    s_mode = 0;
}

static void owner_gone(lv_event_t *e);
static void timer_cb(lv_timer_t *t);

static void set_owner(lv_obj_t *o)
{
    if (s_a->owner) lv_obj_remove_event_cb(s_a->owner, owner_gone);
    s_a->owner = o;
    if (o) lv_obj_add_event_cb(o, owner_gone, LV_EVENT_DELETE, NULL);
}

static void arm(uint32_t ms)                       /* (re)start the one-shot */
{
    if (s_a->t) { lv_timer_set_period(s_a->t, ms); lv_timer_reset(s_a->t); return; }
    s_a->t = lv_timer_create(timer_cb, ms, NULL);
    lv_timer_set_repeat_count(s_a->t, 1);
}

static void release(void)                          /* the block goes; no callbacks */
{
    axis_t *a = s_a;
    if (!a) return;
    if (a->owner) set_owner(NULL);
    s_a = NULL;
    if (a->t) lv_timer_delete(a->t);
    heap_caps_free(a);
}

static axis_t *block(void)
{
    axis_t *a = heap_caps_calloc(1, sizeof *a, MALLOC_CAP_SPIRAM);
    if (a) s_a = a;
    return a;
}

static bool seen(lv_obj_t *o)                      /* no hidden ancestor: its page is the one on top */
{
    for (; o; o = lv_obj_get_parent(o)) if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}

/* ---- the ring's lines ---- */
static void lines_set(void)
{
    axis_t *a = s_a;
    const ui_axis_ring_t *d = a->def;
    char b[160];
    if (a->hint) {                                 /* the ring is gone: the page's own summary, wide, one line */
        lv_obj_set_width(a->l1, UI_AXIS_HINT_W);
        const char *second = d->after(b, sizeof b);
        lv_label_set_text(a->l1, b);
        lv_label_set_text(a->l2, second);
    } else {                                       /* the current axis' value alone, inside the ring (the highlighted cell names the axis; R55) */
        lv_obj_set_width(a->l1, UI_AXIS_LINE_W);
        lv_label_set_text(a->l1, d->value(a->axis));
        if (d->hint) lv_label_set_text(a->l2, d->hint(a->axis));
        else lv_label_set_text_fmt(a->l2, "%s%s%s", ui_word_knob(), T(S_STR_7) /* = */, d->name(a->axis));
    }
    lv_obj_update_layout(a->l1);                   /* the dots of a clipped line are made now, so the text read back is the text shown */
}

static void mark(void)                             /* after every tap or turn: the cell, the lines, 4 s again */
{
    ui_ring_set_mark(s_a->ring, s_a->axis);
    const int n = s_a->def->n < UI_AXIS_RING_MAX ? s_a->def->n : UI_AXIS_RING_MAX;
    for (int i = 0; i < n; i++) ui_ring_set_label(s_a->ring, i, s_a->def->name(i));   /* a cell whose name follows something (the spectrum page's kind-own axis) */
    lines_set();
    arm(UI_HOLD_MS);
}

static void lines_end(void)                        /* the lines go, then the block; the page hears it last */
{
    void (*gone)(void) = s_a->def ? s_a->def->gone : NULL;
    lv_obj_t *l1 = s_a->l1, *l2 = s_a->l2;
    if (s_a->owner == l1) set_owner(NULL);
    release();
    if (l1) lv_obj_delete(l1);
    if (l2) lv_obj_delete(l2);
    if (gone) gone();
}

static void owner_gone(lv_event_t *e)              /* the page (or the object) is being deleted with this block on */
{
    if (!s_a || lv_event_get_target_obj(e) != s_a->owner) return;
    s_a->owner = NULL;                             /* it is going: no event to take off */
    if (s_a->ov && s_a->def->store) s_a->def->store();   /* a ring open: what its axes changed is stored all the same */
    release();                                     /* the lines are the page's children: they go with it */
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_a) return;
    s_a->t = NULL;                                 /* a one-shot: LVGL deletes it after this run */
    if (s_a->ov) ui_axis_ring_close(true);
    else if (s_a->hint) lines_end();
    else ui_axis_end();
}

/* ---- borrow ---- */
void ui_axis_borrow(lv_obj_t *owner, int axis, ui_axis_step_fn step, ui_axis_end_fn end, bool step_now)
{
    if (!(s_a && !s_a->def && s_a->owner == owner && s_a->axis == axis && s_a->step == step)) {
        ui_axis_stop();
        axis_t *a = block();
        if (a) {
            a->axis = axis;
            a->step = step;
            a->end = end;
            set_owner(owner);
        }
    }
    if (step_now) tap_step(step, axis);            /* may rebuild the page (theme): the block may be gone or new after it */
    if (s_a) arm(UI_HOLD_MS);
}

static __attribute__((noinline)) bool alive(void) { return s_a != NULL; }   /* the step may end or replace the borrow: asked again, not assumed (the analyzer sees the function pointer call as pure) */
bool ui_axis_knob(int d)
{
    if (!s_a || s_a->hint || !s_a->owner || !seen(s_a->owner)) return false;
    s_a->step(s_a->axis, d);                       /* all detents at once; the knob's one haptic is ui.c's */
    if (!alive()) return true;
    if (s_a->ov) mark();
    else arm(UI_HOLD_MS);
    return true;
}

int ui_axis_current_of(lv_obj_t *owner) { return s_a && !s_a->hint && s_a->owner == owner ? s_a->axis : -1; }

void ui_axis_end(void)
{
    if (!s_a || s_a->def) return;
    ui_axis_end_fn e = s_a->end;
    release();
    if (e) e();
}

void ui_axis_touch(void)
{
    if (!s_a || s_a->hint) return;
    if (s_a->ov) mark();
    else arm(UI_HOLD_MS);
}

void ui_axis_stop(void)
{
    if (!s_a) return;
    if (s_a->ov) ui_axis_ring_close(false);
    else if (s_a->hint) lines_end();
    else ui_axis_end();
}

/* ---- the ring of axes ---- */
static void ring_pick(int idx)
{
    if (!s_a || !s_a->ov) return;
    if (idx < 0) {                                 /* the centre */
        if (s_a->def->centre) s_a->def->centre();
        else ui_axis_ring_close(true);
        return;
    }
    if (idx >= s_a->def->n) return;
    if (s_a->def->action && s_a->def->action(idx)) return;   /* an action cell (說明): done, the ring may be gone */
    if (idx != s_a->axis) s_a->axis = idx;         /* only the choice of the axis: no value changes. The tap's one haptic is ui.c's (every PRESSED) */
    else tap_step(s_a->step, idx);
    if (s_a && s_a->ov) mark();
}
static void ring_back(void) { if (s_a && s_a->ov && s_a->def->back) s_a->def->back(); }

bool ui_axis_ring_open(lv_obj_t *page, const ui_axis_ring_t *def, int axis, bool step_now)
{
    if (s_a && s_a->ov && s_a->def == def) { ui_axis_touch(); return true; }
    ui_axis_stop();                                /* a borrow ends, a hint goes: a new ring replaces what it said */
    const char *labels[UI_AXIS_RING_MAX];
    int n = def->n < UI_AXIS_RING_MAX ? def->n : UI_AXIS_RING_MAX;
    for (int i = 0; i < n; i++) labels[i] = def->name(i);
    lv_obj_t *ov = lv_obj_create(page);            /* transparent: the page stays as it is and keeps moving */
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, UI_RING_BOX_W, UI_RING_BOX_H);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ring = ui_ring_create_menu(ov, labels, n, ring_pick, def->back ? ring_back : NULL);
    axis_t *a = ring ? block() : NULL;
    if (!a) { lv_obj_delete(ov); return false; }   /* no memory: the knob stays where it was, the caller sees to it that the tap is not a dead end */
    a->def = def;
    a->step = def->step;
    a->axis = axis;
    a->ov = ov;
    a->ring = ring;
    set_owner(ov);
    ui_ring_set_center_hidden(ring, true);
    a->l1 = ui_label(page, NULL, -UI_AXIS_LINE_Y, UI_AXIS_LINE_W);
    lv_label_set_long_mode(a->l1, LV_LABEL_LONG_MODE_DOTS);   /* one line, clipped with dots rather than wrapped */
    lv_obj_set_height(a->l1, lv_font_get_line_height(lv_obj_get_style_text_font(a->l1, LV_PART_MAIN)));
    a->l2 = ui_label(page, NULL, UI_AXIS_LINE_Y, UI_AXIS_LINE_W);         /* the knob hint, low in the centre (white-only pictures such as the scope clash with text in the middle) */
    lv_label_set_long_mode(a->l2, LV_LABEL_LONG_MODE_DOTS);   /* the hint is one line in its zone too: a long text is clipped with dots, never wrapped over the page */
    lv_obj_set_height(a->l2, lv_font_get_line_height(lv_obj_get_style_text_font(a->l2, LV_PART_MAIN)));
    if (step_now) tap_step(def->step, axis);
    if (s_a) mark();
    return true;
}

void ui_axis_ring_close(bool hint)
{
    if (!s_a || !s_a->ov) return;
    const ui_axis_ring_t *d = s_a->def;
    if (d->store) d->store();                      /* everything the axes changed is stored when the ring closes */
    lv_obj_t *ov = s_a->ov;
    set_owner(NULL);
    s_a->ov = s_a->ring = NULL;
    lv_obj_delete(ov);                             /* the ring is in it */
    if (hint && d->after && s_a->l1) {
        s_a->hint = true;
        set_owner(s_a->l1);
        lines_set();
        arm(HINT_MS);
    } else lines_end();
}

bool ui_axis_ring_up(const ui_axis_ring_t *def) { return s_a && s_a->ov && s_a->def == def; }
int ui_axis_ring_axis(const ui_axis_ring_t *def) { return ui_axis_ring_up(def) ? s_a->axis : -1; }
bool ui_axis_hinting(const ui_axis_ring_t *def) { return s_a && s_a->hint && s_a->def == def; }
void ui_axis_ring_pick(int idx) { ring_pick(idx); }
const char *ui_axis_line(const ui_axis_ring_t *def, int i)
{
    lv_obj_t *l = s_a && s_a->def == def ? (i ? s_a->l2 : s_a->l1) : NULL;
    return l ? lv_label_get_text(l) : NULL;
}
