// SPDX-License-Identifier: LGPL-2.1-or-later
/* The content-first page frame (ui_frame.h). */
#include "ui_frame.h"
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_exit.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_theme.h"

#define POOL      12
#define MAX_CHR   10
#define SHOW_MS   4000
#define POLL_MS   250

typedef struct {
    lv_obj_t *root, *back, *scrim, *card, *close, *ex[2];   /* ex: the exit's Continue / Back while it is paused (items of the same card) */
    lv_obj_t *chr[MAX_CHR];
    struct { lv_align_t al; int32_t x, y; bool ok; } sv[MAX_CHR + 1];   /* where the page had each item (chr[i], [MAX_CHR] = back) while the card stacks them */
    int nchr;
    int32_t back_w;                                 /* the page's own width of its Back pill while the card halves it for the Close pill beside it */
    ui_frame_mode_t mode;
    bool card_wired, armed, shown, mode_a_wired, opts_only, hold;
    lv_timer_t *poll;
    uint32_t t_show;
} frame_t;

static frame_t s_pool[POOL];

static frame_t *of_root(lv_obj_t *root)
{
    for (int i = 0; i < POOL; i++) if (s_pool[i].root && s_pool[i].root == root) return &s_pool[i];
    return NULL;
}
static frame_t *of_top(void) { lv_obj_t *r = ui_top_root(); return r ? of_root(r) : NULL; }

/* the content band (centre offsets from the middle of the panel) the field pieces (board, cards, dial) fill */
void ui_frame_band(lv_obj_t *page, int *top, int *bottom)
{
    if (ui_frame_active(page)) { *top = UI_SLOT_TITLE_Y + 20; *bottom = UI_H / 2 - (UI_ROUND ? UI_TAP_MIN : 0) - 2 * UI_TAP_GAP; }   /* under the status line, down to the panel edge (the round glass keeps the old margin: a rectangle must fit the circle): no HEAD row, no Back, no bubble (CF2) */
    else { *top = UI_SLOT_HEAD_Y + UI_TAP_MIN / 2 + UI_TAP_GAP; *bottom = UI_BAND_BOTTOM; }
}
/* the radius of a round field with a frame: the panel's short side less a margin (the square panel 8: the bezel has a black margin of its own, SQ2; the round glass 10), centred on the panel */
int ui_frame_field_centred(void) { return -UI_SLOT_TITLE_Y - 26; }
int ui_frame_field_r(void) { return LV_MIN(UI_W, UI_H) / 2 - (UI_ROUND ? 10 : 8); }

/* the round field of a frame page: under the status line (the title slot, kept in the content) down to the field margin, so its rim never runs through the status text (CF2 review 8) */
void ui_frame_field(int *R, int *mid)
{
    const int top = UI_SLOT_TITLE_Y + 22, bottom = ui_frame_field_r();
    *R = (bottom - top) / 2;
    *mid = (top + bottom) / 2;
}

/* ---- the overlay ---- */
static void hide_obj(lv_obj_t *o, bool h)
{
    if (!o) return;
    if (h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void scrim_cb(lv_event_t *e);
static void wire(frame_t *f, lv_obj_t *o);
lv_obj_t *ui_frame_card(lv_obj_t *parent, int w, int h, int cy)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, cy);
    lv_obj_set_style_bg_color(c, UI_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);               /* solid: no alpha layer */
    lv_obj_set_style_border_color(c, UI_DIM, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, UI_POP_RADIUS, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);                 /* a tap on the card's own body goes nowhere (it must not reach the scrim) */
    return c;
}
/* z-order [.. scrim, card, items.. back] at the end of the page: a move to the front invalidates the whole parent (a full-page redraw), so it is done only when a page object came in above them */
static void to_front(lv_obj_t **o, int n)
{
    if (!n) return;
    lv_obj_t *root = lv_obj_get_parent(o[0]);
    bool ok = lv_obj_get_child_count(root) >= (uint32_t)n;
    for (int i = 0; ok && i < n; i++) ok = lv_obj_get_child(root, -(n - i)) == o[i];
    if (ok) return;
    for (int i = 0; i < n; i++) lv_obj_move_foreground(o[i]);
}
static void stash(frame_t *f, int i, lv_obj_t *o)             /* remember the page's own place of an item once */
{
    if (!o || f->sv[i].ok) return;
    f->sv[i].al = lv_obj_get_style_align(o, 0);
    f->sv[i].x = lv_obj_get_style_x(o, 0);
    f->sv[i].y = lv_obj_get_style_y(o, 0);
    f->sv[i].ok = true;
}
static void restore(frame_t *f, int i, lv_obj_t *o)
{
    if (!o || !f->sv[i].ok) return;
    lv_obj_align(o, f->sv[i].al, f->sv[i].x, f->sv[i].y);
    f->sv[i].ok = false;
}
static void hide_overlay(frame_t *f);
static void close_cb(lv_event_t *e) { hide_overlay(lv_event_get_user_data(e)); }   /* the popup only: the page and its game go on */
static void back_restore(frame_t *f)                          /* Back is the page's again (its own width and place) */
{
    if (f->back && f->back_w > 0) lv_obj_set_width(f->back, f->back_w);
    f->back_w = 0;
    hide_obj(f->close, true);
}
/* stack the items in the card: texts first, then the options, then Back (not with the exit's own pause overlay: opts_only, the card sits above its Continue / Back card) */
static void layout(frame_t *f, bool opts_only)
{
    lv_obj_t *it[MAX_CHR + 3];
    int idx[MAX_CHR + 3], n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < f->nchr; i++)
            if (!lv_obj_has_flag(f->chr[i], LV_OBJ_FLAG_CLICKABLE) == (pass == 0)) { it[n] = f->chr[i]; idx[n++] = i; }
    if (f->back && !opts_only) { it[n] = f->back; idx[n++] = MAX_CHR; }
    if (opts_only) for (int e = 0; e < 2; e++) if (f->ex[e]) { it[n] = f->ex[e]; idx[n++] = -1; }
    if (!n) return;
    lv_obj_update_layout(f->root);
    int w = 0, tot = 0;
    const bool pair = f->back && !opts_only;                    /* a popup has its own Close: the last row is [Close | Leave], the same height (no new row) */
    if (pair) {
        int wo = 0;
        for (int i = 0; i < n; i++) if (it[i] != f->back) wo = LV_MAX(wo, (int)lv_obj_get_width(it[i]));
        if (!f->back_w) f->back_w = lv_obj_get_width(f->back);
        const int row = LV_MAX(wo, 3 * UI_POP_BTN_W / 2), half = (row - UI_POP_GAP) / 2;
        lv_obj_set_width(f->back, half);
        if (!f->close) {
            lv_obj_t *l = NULL;
            f->close = ui_button(f->root, half, lv_obj_get_height(f->back), 0, 0, close_cb, f, &l);
            lv_obj_set_style_bg_color(f->close, UI_PANEL2, 0);
            wire(f, f->close);
        }
        lv_obj_set_width(f->close, half);
        lv_obj_set_height(f->close, lv_obj_get_height(f->back));
        char cb[48];
        lv_label_set_text(lv_obj_get_child(f->close, 0), ui_icon_text(cb, sizeof cb, UI_ICON_L_CLOSE, T(S_CLOSE) /* Close */));
        hide_obj(f->close, false);
        w = row;
    }
    for (int i = 0; i < n; i++) {
        w = LV_MAX(w, (int)lv_obj_get_width(it[i]));
        tot += lv_obj_get_height(it[i]) + (i ? UI_POP_GAP : 0);
    }
#if !UI_ROUND
    {   /* the square panel's items are UI_TAP_MIN (one list row) high: with more of them than the panel holds, the buttons give up height together (never below 48; the most any page has today is three items) */
        const int room = UI_H - 2 * 24, over = tot + 2 * UI_POP_PAD - room;
        int nb = 0;
        for (int i = 0; i < n; i++) nb += lv_obj_has_flag(it[i], LV_OBJ_FLAG_CLICKABLE) ? 1 : 0;
        if (over > 0 && nb > 0) {
            const int per = (over + nb - 1) / nb;
            for (int i = 0; i < n; i++)
                if (lv_obj_has_flag(it[i], LV_OBJ_FLAG_CLICKABLE)) lv_obj_set_height(it[i], LV_MAX((int)lv_obj_get_height(it[i]) - per, 48));
            lv_obj_update_layout(f->root);
            tot = 0;
            for (int i = 0; i < n; i++) tot += lv_obj_get_height(it[i]) + (i ? UI_POP_GAP : 0);
        }
    }
#endif
    const int cw = LV_MAX(LV_MIN(w + 2 * UI_POP_PAD, UI_POP_W_MAX), UI_POP_W_MIN), ch = tot + 2 * UI_POP_PAD;
    int cy = 0;
    if (cy - ch / 2 < UI_POP_TOP) cy = UI_POP_TOP + ch / 2;      /* below the status line */
    if (!f->card) f->card = ui_frame_card(f->root, cw, ch, cy);
    else { lv_obj_set_size(f->card, cw, ch); lv_obj_align(f->card, LV_ALIGN_CENTER, 0, cy); }
    if (!f->card_wired) { wire(f, f->card); f->card_wired = true; }
    hide_obj(f->card, false);
    lv_obj_t *z[MAX_CHR + 3];
    int nz = 0;
    if (!opts_only && f->scrim) z[nz++] = f->scrim;
    z[nz++] = f->card;
    for (int i = 0; i < n; i++) z[nz++] = it[i];
    if (pair) z[nz++] = f->close;
    to_front(z, nz);
    int y = cy - tot / 2;
    for (int i = 0; i < n; i++) {
        int hh = lv_obj_get_height(it[i]);
        if (idx[i] >= 0) stash(f, idx[i], it[i]);
        lv_obj_align(it[i], LV_ALIGN_CENTER, pair && it[i] == f->back ? (lv_obj_get_width(f->back) + UI_POP_GAP) / 2 : 0, y + hh / 2);
        if (pair && it[i] == f->back) lv_obj_align(f->close, LV_ALIGN_CENTER, -(lv_obj_get_width(f->back) + UI_POP_GAP) / 2, y + hh / 2);
        y += hh + UI_POP_GAP;
    }
}
static void hide_overlay(frame_t *f)
{
    if (!f->shown) return;
    f->shown = false;
    f->opts_only = false;
    f->hold = false;
    for (int i = 0; i < f->nchr; i++) { hide_obj(f->chr[i], true); restore(f, i, f->chr[i]); }
    hide_obj(f->back, true);
    restore(f, MAX_CHR, f->back);
    back_restore(f);
    hide_obj(f->card, true);
    if (f->scrim) lv_obj_remove_flag(f->scrim, LV_OBJ_FLAG_CLICKABLE);   /* kept (made once) and never hidden (a show / hide invalidates the whole page): a transparent object that is not clickable takes no touch */
    if (f->poll) lv_timer_pause(f->poll);
}
/* full: scrim + options + Back; opts_only (the shared exit's pause overlay is the scrim and has its own Continue / Back): the options and 說明 only */
static void show_overlay_ex(frame_t *f, bool opts_only)
{
    if (!f->armed || !f->root) return;
    f->t_show = lv_tick_get();
    if (f->shown) {
        if (opts_only && !f->opts_only) {                     /* the exit's pause overlay came up over a shown overlay: it covers the options, so they go to the front again; it has its own scrim and Back */
            if (f->scrim) lv_obj_remove_flag(f->scrim, LV_OBJ_FLAG_CLICKABLE);
            hide_obj(f->back, true);
            restore(f, MAX_CHR, f->back);
            back_restore(f);
            layout(f, true);
            f->opts_only = true;
            if (f->poll) lv_timer_pause(f->poll);
        }
        return;
    }
    f->shown = true;
    f->opts_only = opts_only;
    if (!opts_only) {
        if (!f->scrim) {
            lv_obj_t *s = lv_obj_create(f->root);
            lv_obj_remove_style_all(s);
            lv_obj_set_size(s, lv_pct(100), lv_pct(100));
            lv_obj_center(s);
            lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);    /* the content is not dimmed (no full-screen blend) */
            lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(s, scrim_cb, LV_EVENT_CLICKED, f);
            lv_obj_add_event_cb(s, scrim_cb, LV_EVENT_DELETE, f);
            f->scrim = s;
        }
        lv_obj_add_flag(f->scrim, LV_OBJ_FLAG_CLICKABLE);   /* a tap beside the card closes the overlay and reaches nothing under it */
    }
    for (int i = 0; i < f->nchr; i++) hide_obj(f->chr[i], false);
    if (f->back && !opts_only) hide_obj(f->back, false);
    layout(f, opts_only);
    if (f->poll && !opts_only) { lv_timer_reset(f->poll); lv_timer_resume(f->poll); }
}
static void show_overlay(frame_t *f) { show_overlay_ex(f, false); }
static void scrim_cb(lv_event_t *e)
{
    frame_t *f = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) { if (f->scrim == lv_event_get_target_obj(e)) f->scrim = NULL; return; }
    hide_overlay(f);
}
static bool axis_held(frame_t *f)
{
    if (ui_axis_current_of(f->root) >= 0) return true;
    for (int i = 0; i < f->nchr; i++) if (ui_axis_current_of(f->chr[i]) >= 0) return true;
    return false;
}
static void poll_cb(lv_timer_t *t)
{
    frame_t *f = lv_timer_get_user_data(t);
    if (f->shown && !f->opts_only) layout(f, false);          /* the status text may have changed under the card */
    if (!f->shown || f->opts_only || f->hold) { if (!f->shown) lv_timer_pause(t); return; }
    if (ui_popring_ring()) { hide_overlay(f); return; }     /* an option opened the page's ring: that is the page's face now */
    uint32_t quiet = lv_tick_elaps(f->t_show), touch = lv_display_get_inactive_time(NULL);
    if (touch < quiet) quiet = touch;
    if (axis_held(f)) { f->t_show = lv_tick_get(); return; }
    if (quiet >= SHOW_MS) hide_overlay(f);
}

/* ---- the objects ---- */
static void obj_gone(lv_event_t *e)                          /* a chrome object, the Back pill deleted by the page (a rebuild) */
{
    frame_t *f = lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_target_obj(e);
    if (!f->root) return;                                    /* the page is going: the frame is already cleared */
    for (int i = 0; i < f->nchr; i++) if (f->chr[i] == o) { f->chr[i] = f->chr[f->nchr - 1]; f->sv[i] = f->sv[f->nchr - 1]; f->sv[--f->nchr].ok = false; i--; }
    if (f->card == o) { f->card = NULL; f->card_wired = false; }
    if (f->close == o) f->close = NULL;
    if (f->back == o) { hide_overlay(f); f->back = NULL; f->armed = false; }   /* hide first: a shown overlay would leave its scrim up */
}
static void root_gone(lv_event_t *e)
{
    frame_t *f = lv_event_get_user_data(e);
    if (lv_event_get_target_obj(e) != f->root) return;
    memset(f, 0, sizeof *f);
}
static void bg_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e)) return;
    frame_t *f = lv_event_get_user_data(e);
    if (f->mode == UI_FRAME_A) show_overlay(f);
}

static void wire(frame_t *f, lv_obj_t *o)
{
    lv_obj_remove_event_cb_with_user_data(o, obj_gone, f);   /* a hint flip wires the same label again: never a second delete callback on it */
    lv_obj_add_event_cb(o, obj_gone, LV_EVENT_DELETE, f);
}
static void apply(frame_t *f)                                  /* the mode's objects, made when both the mode and the Back pill are known */
{
    if (!f->armed) return;
    if (f->mode == UI_FRAME_A && !f->mode_a_wired) {
        lv_obj_add_flag(f->root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(f->root, bg_cb, LV_EVENT_CLICKED, f);
        f->mode_a_wired = true;
    }
}

void ui_frame_enable(lv_obj_t *page)
{
    if (!page || of_root(page)) return;
    for (int i = 0; i < POOL; i++) {
        if (s_pool[i].root) continue;
        memset(&s_pool[i], 0, sizeof s_pool[i]);
        s_pool[i].root = page;
        s_pool[i].mode = UI_FRAME_C;
        lv_obj_add_event_cb(page, root_gone, LV_EVENT_DELETE, &s_pool[i]);
        return;
    }
    printf("APP frame warn pool full\n");
}
void ui_frame_mode(lv_obj_t *page, ui_frame_mode_t m)
{
    frame_t *f = of_root(page);
    if (!f) return;
    f->mode = m;
    apply(f);
}
bool ui_frame_active(lv_obj_t *page) { return of_root(page) != NULL; }

void ui_frame_chrome(lv_obj_t *obj)
{
    frame_t *f = obj ? of_root(lv_obj_get_parent(obj)) : NULL;
    if (!f) return;
    if (f->nchr >= MAX_CHR) { printf("APP frame warn chrome full (max %d): an object stays visible\n", MAX_CHR); return; }
    for (int i = 0; i < f->nchr; i++) if (f->chr[i] == obj) return;
    f->chr[f->nchr++] = obj;
    wire(f, obj);
    if (f->armed && !f->shown) hide_obj(obj, true);
}
void ui_frame_option(lv_obj_t *obj) { ui_frame_chrome(obj); }
/* GS: a status label that is an INSTRUCTION right now ("tap to serve", "paused") is popup chrome while it is one; the score line it shows otherwise stays in the content */
void ui_frame_hint(lv_obj_t *label, bool hint)
{
    frame_t *f = label ? of_root(lv_obj_get_parent(label)) : NULL;
    if (!f) return;
    int at = -1;
    for (int i = 0; i < f->nchr; i++) if (f->chr[i] == label) at = i;
    if (hint) { if (at < 0) ui_frame_chrome(label); return; }
    if (at < 0) return;
    restore(f, at, label);
    for (int i = at; i + 1 < f->nchr; i++) { f->chr[i] = f->chr[i + 1]; f->sv[i] = f->sv[i + 1]; }
    f->sv[--f->nchr].ok = false;
    hide_obj(label, false);
}

void ui_frame_back(lv_obj_t *back)
{
    frame_t *f = back ? of_root(lv_obj_get_parent(back)) : NULL;
    if (!f) return;
    f->back = back;
    wire(f, back);
    f->armed = true;
    for (int i = 0; i < f->nchr; i++) hide_obj(f->chr[i], !f->shown);
    hide_obj(back, !f->shown);
    if (!f->poll) f->poll = ui_timer_bind(f->root, poll_cb, POLL_MS, f);
    if (f->poll && !f->shown) lv_timer_pause(f->poll);
    apply(f);
}
/* the shared exit (ui_exit.c): on a frame page it hands its A pill over as the Back of the overlay and drives the overlay itself (its knob turn, its pause overlay) */
bool ui_frame_adopt_exit(lv_obj_t *page, lv_obj_t *back)
{
    frame_t *f = of_root(page);
    if (!f) return false;
    f->mode = UI_FRAME_X;
    ui_frame_back(back);
    return true;
}
void ui_frame_show(lv_obj_t *page, bool hold, bool opts_only)
{
    frame_t *f = of_root(page);
    if (!f) return;
    f->hold = hold;
    show_overlay_ex(f, opts_only);
}
/* the shared exit's paused state: its Continue / Back (children of the page) join the options in the card; NULL, NULL when it resumes */
void ui_frame_exit_items(lv_obj_t *page, lv_obj_t *resume, lv_obj_t *back)
{
    frame_t *f = of_root(page);
    if (!f) return;
    f->ex[0] = resume;
    f->ex[1] = back;
}
void ui_frame_hide(lv_obj_t *page) { frame_t *f = of_root(page); if (f) hide_overlay(f); }

void ui_frame_volume_turn(void)
{
    frame_t *f = of_top();
    if (!f || !f->armed || f->mode == UI_FRAME_X || ui_exit_paused() || !ui_screen_on() || ui_popring_ring()) return;   /* UI_FRAME_X: ui_exit_volume_turn does it */
    show_overlay(f);
}
/* a tap on a content field of a mode A page (the field is a clickable child, so the root's own click never sees it): the page's field calls this first (ui_exit_tap) */
bool ui_frame_content_tap(lv_obj_t *page)
{
    frame_t *f = of_root(page);
    if (!f || !f->armed || f->shown) return false;
    show_overlay(f);
    return true;
}
/* the popup's own option (an axis its chrome or the page root borrowed) holds the knob: under the exit's pause overlay that axis, and no game axis, still takes the knob (ui.c) */
bool ui_frame_axis_held(void) { frame_t *f = of_top(); return f && f->armed && axis_held(f); }

/* the screen centre of a popup item (the simulator's simtaprow): "back", or the option row's cell `cell` (the row itself when it has no cells) */
static lv_obj_t *named(frame_t *f, const char *w, int i);
bool ui_frame_item_at(const char *w, int cell, int *x, int *y)
{
    frame_t *f = of_top();
    lv_obj_t *o = f ? named(f, w, 0) : NULL;
    if (!o || (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) && o != f->root)) return false;
    if (!lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_child_count(o) > (uint32_t)cell) o = lv_obj_get_child(o, cell);
    lv_area_t a;
    lv_obj_update_layout(o);
    lv_obj_get_coords(o, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}
/* ---- console ---- */
static lv_obj_t *named(frame_t *f, const char *w, int i)
{
    if (!strcmp(w, "back")) return f->back;
    if (!strcmp(w, "bg")) return f->root;
    if (!strcmp(w, "row")) { for (int k = 0; k < f->nchr; k++) if (!lv_obj_check_type(f->chr[k], &lv_label_class)) return f->chr[k]; return NULL; }   /* the option row (the card moves it: tests tap its cells here, not at the page's own slot) */
    if (!strcmp(w, "opt")) return i >= 0 && i < f->nchr ? f->chr[i] : NULL;
    return NULL;
}
static void dump(frame_t *f)
{
    printf("APP frame armed=%d mode=%c shown=%d chrome=%d\n", f ? f->armed : 0, f ? "CAX"[f->mode] : '-',
           f ? f->shown : 0, f ? f->nchr : 0);
}
void ui_frame_cmd(const char *arg)
{
    frame_t *f = of_top();
    char w[16];
    int i = 0, cell = 0;
    if (!f || !arg[0]) { dump(f); return; }
    if (!strcmp(arg, "show")) show_overlay(f);
    else if (!strcmp(arg, "hide")) hide_overlay(f);
    else if (!strcmp(arg, "hold")) { f->hold = true; show_overlay(f); }
    else if (!strcmp(arg, "mode a") || !strcmp(arg, "mode c")) ui_frame_mode(f->root, arg[5] == 'a' ? UI_FRAME_A : UI_FRAME_C);   /* tests: any page in either mode */
    else if (sscanf(arg, "tap %15s %d %d", w, &i, &cell) >= 1) {
        if (!strcmp(w, "row")) cell = i;
        lv_obj_t *o = named(f, w, i);
        if (!o || (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) && o != f->root)) { printf("APP frame tap none %s\n", w); return; }
        if (!lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_child_count(o) > (uint32_t)cell) o = lv_obj_get_child(o, cell);   /* an option row: its cell */
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
    else if (sscanf(arg, "at %15s %d", w, &i) >= 1) {
        lv_obj_t *o = named(f, w, i);
        if (!o) { printf("APP frame at none %s\n", w); return; }
        lv_area_t a;
        lv_obj_update_layout(o);
        lv_obj_get_coords(o, &a);
        printf("APP frame at %s x=%d,y=%d\n", w, (int)((a.x1 + a.x2) / 2), (int)((a.y1 + a.y2) / 2));
        return;
    }
    else { printf("APP frame unknown %s\n", arg); return; }
    dump(f);
}
