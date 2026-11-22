// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ui_kit.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_random.h"
#include "ui_theme.h"
#include "font.h"
#include "ui_geom.h"
#include "ui_frame.h"
#include "psram_alloc.h"

static void bound_deleted(lv_event_t *e) { lv_timer_delete(lv_event_get_user_data(e)); }

lv_timer_t *ui_timer_bind(lv_obj_t *owner, lv_timer_cb_t cb, uint32_t period_ms, void *user)
{
    lv_timer_t *t = lv_timer_create(cb, period_ms, user);
    if (t && !lv_obj_add_event_cb(owner, bound_deleted, LV_EVENT_DELETE, t)) { lv_timer_delete(t); t = NULL; }
    return t;
}

static void guarded_deleted(lv_event_t *e)
{
    lv_timer_t **slot = lv_event_get_user_data(e);
    if (*slot) { lv_timer_delete(*slot); *slot = NULL; }
}

void ui_timer_guard(lv_obj_t *owner, lv_timer_t **slot) { lv_obj_add_event_cb(owner, guarded_deleted, LV_EVENT_DELETE, slot); }

static void obj_deleted(lv_event_t *e)
{
    lv_obj_t **slot = lv_event_get_user_data(e);
    if (*slot == lv_event_get_target_obj(e)) *slot = NULL;
}
lv_obj_t *ui_obj_guard(lv_obj_t *obj, lv_obj_t **slot)
{
    *slot = obj;
    if (obj) lv_obj_add_event_cb(obj, obj_deleted, LV_EVENT_DELETE, slot);
    return obj;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *ui_label_d(lv_obj_t *parent, const lv_font_t *font, int y, int w) { return ui_label(parent, font, UI_SCALE(y), UI_SCALE(w)); }

lv_obj_t *ui_button(lv_obj_t *parent, int w, int h, int x, int y, lv_event_cb_t cb, void *user, lv_obj_t **label_out)
{
    lv_obj_t *b = lv_button_create(parent);
    if (ui_theme_is_light()) lv_obj_set_style_text_color(b, UI_FG, 0);   /* LVGL's button text is light in either mode */
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_center(l);
    if (label_out) *label_out = l;
    return b;
}

pc_rng_t *ui_rng(void)
{
    static pc_rng_t rng;
    static bool seeded;
    if (!seeded) { pc_rng_seed(&rng, ((uint64_t)esp_random() << 32) | esp_random()); seeded = true; }
    return &rng;
}

/* ---- the shared scrolling list (geometry and colours of the old per-page copies; Task 11) ---- */
EXT_RAM_BSS_ATTR static lv_obj_t *s_last_list;      /* GS: the newest list (guarded): a side flick scrolls it by a screenful */
/* A PAGED list (library folders and songs): the page owns the data and fills one page of rows; this piece owns the page number, the bubble ("n-N"), the side flick
 * and the knob running off either end of a page. A few lists can be paged at once (a page under another one keeps its state); the flick and the knob act on the newest list
 * (a page that comes back to the top adopts its list again: ui_list_pager_resume). */
typedef struct { lv_obj_t *list, *bubble; int page, pages; void (*fill)(int page, void *u); void *u; } pg_t;
EXT_RAM_BSS_ATTR static pg_t s_pg[4];

static pg_t *pg_of(const lv_obj_t *l)
{
    if (l) for (int i = 0; i < 4; i++) if (s_pg[i].list == l) return &s_pg[i];
    return NULL;
}
static pg_t *pg_on(const lv_obj_t *l) { pg_t *p = pg_of(l); return p && p->pages > 1 ? p : NULL; }
static void pg_gone(lv_event_t *e) { pg_t *p = pg_of(lv_event_get_target_obj(e)); if (p) memset(p, 0, sizeof *p); }
/* show page np (0-based): the rows are rebuilt by the page's fill callback, the view is at the top (at_end false) or at the bottom */
static void pg_turn(pg_t *p, int np, bool at_end)
{
    p->page = np;
    lv_obj_clean(p->list);
    p->fill(np, p->u);
    ui_page_bubble_set(p->bubble, p->page, p->pages);   /* fill may have restarted the pager (a library swapped in: page 0, new count) */
    lv_obj_update_layout(p->list);
    lv_obj_scroll_by_bounded(p->list, 0, at_end ? -1000000 : 1000000, LV_ANIM_OFF);
}
static void adopt(lv_obj_t *list)
{
    s_last_list = list;                               /* ui_list guarded it when it was made (cleared on delete); no new callback per visit */
}
void ui_list_pager(lv_obj_t *list, int page, int pages, void (*fill)(int page, void *u), void *u)
{
    if (!list) return;
    pg_t *p = pg_of(list);
    if (!p) {
        for (int i = 0; i < 4 && !p; i++) if (!s_pg[i].list) p = &s_pg[i];
        if (!p) return;
        memset(p, 0, sizeof *p);
        p->list = list;
        lv_obj_add_event_cb(list, pg_gone, LV_EVENT_DELETE, NULL);
    }
    if (p->bubble) { lv_obj_delete(p->bubble); p->bubble = NULL; }   /* a new level of the same list: its own count */
    p->page = page;
    p->pages = pages;
    p->fill = fill;
    p->u = u;
    if (pages > 1) {
        p->bubble = ui_page_bubble(lv_obj_get_parent(list), LV_ALIGN_CENTER, 0, UI_LIST_Y + UI_LIST_H / 2 + ui_page_bubble_h() / 2 + UI_LIST_BUBBLE_DY);
        ui_page_bubble_set(p->bubble, page, pages);
    }
    adopt(list);
}
void ui_list_pager_resume(lv_obj_t *list) { if (list && lv_obj_is_valid(list)) adopt(list); }

void ui_list_flick(lv_obj_t *page, int dir)
{
    lv_obj_t *l = s_last_list;
    pg_t *pg;
    if (!l || !page) return;
    for (lv_obj_t *o = l; o; o = lv_obj_get_parent(o)) {
        if (o != page) continue;
        if (lv_obj_has_flag(l, LV_OBJ_FLAG_HIDDEN)) return;
        if ((pg = pg_on(l))) { pg_turn(pg, ((pg->page + dir) % pg->pages + pg->pages) % pg->pages, false); return; }   /* a left flick = the next page, wrapping like the rings */
        return;   /* a list that scrolls (settings, tracks) is moved by the finger only: no page turn on a side flick (it also scrolled past the end into blank space) */
    }
}

#if !UI_ROUND
/* SQ3: a list on the square panel whose rows do not fill it sits in the middle of it, not at the top (short pages looked too high). One pass after the page built its rows;
 * a list that fills (or scrolls) keeps its top. */
static void list_centre_cb(void *a)
{
    lv_obj_t *l = a;
    const int n = (int)lv_obj_get_child_count(l);
    if (n < 1) return;
    lv_obj_set_style_pad_top(l, UI_LIST_PAD_TOP, 0);
    lv_obj_update_layout(l);
    lv_obj_t *first = lv_obj_get_child(l, 0), *last = lv_obj_get_child(l, n - 1);
    const int top = lv_obj_get_y(first), bottom = lv_obj_get_y(last) + lv_obj_get_height(last);
    const int room = lv_obj_get_content_height(l), used = bottom - top;
    if (used > 0 && used < room) lv_obj_set_style_pad_top(l, UI_LIST_PAD_TOP + (room - used) / 2, 0);
}
static void list_centre_del(lv_event_t *e) { lv_async_call_cancel(list_centre_cb, lv_event_get_target(e)); }
#endif

lv_obj_t *ui_list(lv_obj_t *parent)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, UI_LIST_W, UI_LIST_H);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, UI_LIST_Y);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, UI_ROW_GAP, 0);
    lv_obj_set_style_pad_top(l, UI_LIST_PAD_TOP, 0);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);   /* a finger drag moves the rows 1:1 and they stop where it lets go */
    ui_obj_guard(l, &s_last_list);
    return l;
}

lv_obj_t *ui_list_row(lv_obj_t *list, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(list);
    lv_obj_set_width(b, UI_ROW_W);
    lv_obj_add_flag(b, UI_FLAG_TAP_ONLY);
    lv_obj_set_style_min_height(b, UI_ROW_H, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    if (ui_theme_is_light()) lv_obj_set_style_text_color(b, UI_FG, 0);   /* LVGL's button text is light in either mode */
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    else lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        int lh = lv_font_get_line_height(lv_obj_get_style_text_font(l, 0));
        lv_obj_set_height(b, UI_ROW_H);                       /* a one-line row is exactly UI_ROW_H: the knob's one detent (UI_LIST_STEP) is one row */
        lv_obj_set_style_pad_ver(b, lh < UI_ROW_H ? (UI_ROW_H - lh) / 2 : 0, 0);
        lv_obj_set_size(l, UI_ROW_LABEL_W, lh);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(l, text);
    }
    return b;
}

void ui_list_row_centre(lv_obj_t *row)
{
#if !UI_ROUND
    lv_obj_t *l = lv_obj_get_child(row, 0);              /* the square panel: a one-action row ("Continue") reads centred; the round panel keeps its left text */
    if (l) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
#else
    (void)row;
#endif
}

void ui_list_row_look(lv_obj_t *row, int look)
{
    lv_color_t bg = UI_PANEL2, fg = UI_FG;
    if (look == UI_LOOK_SEL) bg = UI_SEL;
    else if (look == UI_LOOK_SYS) bg = UI_SYS;
    else if (look == UI_LOOK_ON) bg = UI_ON;
    else if (look == UI_LOOK_HELP) { bg = UI_HELP; fg = UI_HELP_TEXT; }
    else if (look == UI_LOOK_DIM) fg = UI_DIM;
    lv_obj_set_style_bg_color(row, bg, 0);
    lv_obj_t *l = lv_obj_get_child_count(row) ? lv_obj_get_child(row, 0) : NULL;
    if (l && lv_obj_check_type(l, &lv_label_class)) lv_obj_set_style_text_color(l, fg, 0);
    ui_list_row_axis_sync(row);
}

/* An axis row: while it is the selected one (a borrowed axis) it shows a "-" chip at its far left and a "+" chip at its far right (child 1 and 2, made by ui_list_row_axis; each
 * UI_ROW_PM_W wide and the row's whole height) and its text sits centred between them; the label is narrower then, so a text that does not fit WRAPS to a second line (the row is 48 px high on the
 * round panel and a 20 px line is 24: two lines fit) and a text of more than two lines scrolls instead (nothing is lost to dots). The left half of the selected row is -1, the right half +1. */
#define UI_FLAG_AXIS_ROW LV_OBJ_FLAG_USER_2
static bool axis_row(lv_obj_t *row) { return row && lv_obj_has_flag(row, UI_FLAG_AXIS_ROW) && lv_obj_get_child_count(row) >= 3; }
void ui_list_row_axis_sync(lv_obj_t *row)
{
    if (!axis_row(row)) return;
    const bool sel = lv_color_eq(lv_obj_get_style_bg_color(row, 0), UI_SEL);
    lv_obj_t *l = lv_obj_get_child(row, 0);
    lv_obj_set_flag(lv_obj_get_child(row, 1), LV_OBJ_FLAG_HIDDEN, !sel);
    lv_obj_set_flag(lv_obj_get_child(row, 2), LV_OBJ_FLAG_HIDDEN, !sel);
    const int lh = lv_font_get_line_height(lv_obj_get_style_text_font(l, 0));
    if (!sel) {                                                              /* the plain row: the whole label width, left, dots */
        if (lv_label_get_long_mode(l) == LV_LABEL_LONG_MODE_DOTS && lv_obj_get_width(l) == UI_ROW_LABEL_W) return;
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(l, UI_ROW_LABEL_W, lh);
        lv_obj_set_align(l, LV_ALIGN_DEFAULT);
        lv_obj_set_pos(l, 0, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_AUTO, 0);
        return;
    }
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, UI_ROW_W - 2 * (UI_ROW_PM_W + UI_TAP_GAP));
    if (lv_label_get_long_mode(l) != LV_LABEL_LONG_MODE_WRAP) lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_obj_update_layout(l);
    if (lv_obj_get_height(l) > 2 * lh + 2) {                                 /* more than two lines: one scrolling line */
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_height(l, lh);
    }
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
}
static lv_obj_t *axis_chip(lv_obj_t *row, const char *t)   /* a big chip with one sign, the row's height, in the theme's contrast colours */
{
    lv_obj_t *c = lv_label_create(row);
    lv_obj_set_style_text_font(c, font_large(), 0);                      /* the signs are big */
    const int lh = lv_font_get_line_height(font_large());
    lv_label_set_text(c, t);
    lv_obj_set_style_text_color(c, UI_FG, 0);
    lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(c, UI_BG, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, lv_obj_get_style_radius(row, LV_PART_MAIN), 0);
    lv_obj_set_width(c, UI_ROW_PM_W);
    lv_obj_set_style_pad_ver(c, lh < UI_ROW_H ? (UI_ROW_H - lh) / 2 : 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}
void ui_list_row_axis(lv_obj_t *row)
{
    if (!row || axis_row(row) || lv_obj_get_child_count(row) != 1) return;
    lv_obj_add_flag(row, UI_FLAG_AXIS_ROW);
    lv_obj_align(axis_chip(row, "-"), LV_ALIGN_LEFT_MID, -lv_obj_get_style_pad_left(row, LV_PART_MAIN), 0);   /* x counts from the content box: back out the padding */
    lv_obj_align(axis_chip(row, "+"), LV_ALIGN_RIGHT_MID, lv_obj_get_style_pad_right(row, LV_PART_MAIN), 0);
}
EXT_RAM_BSS_ATTR static int s_half_force;                  /* console `axishalf -1|1`: the half a click without a pointer (a test's `settings key`) counts as; 0 = the right half */
void ui_list_row_axis_force(int dir) { s_half_force = dir; }
/* A tap on an axis row. Not selected: only select it (borrow the knob, NO step: nothing changes by accident). Selected: the left half steps -1, the right half +1, both through the
 * knob's own step function (ui_axis_half), stopping at the ends. A click without a pointer (the console) is the right half unless ui_list_row_axis_force says otherwise. */
void ui_list_row_axis_tap(lv_event_t *e, lv_obj_t *owner, int axis, ui_axis_step_fn step, ui_axis_end_fn end)
{
    if (ui_axis_current_of(owner) != axis) {
        ui_axis_borrow(owner, axis, step, end, false);
        lv_obj_t *r = lv_event_get_current_target_obj(e);                  /* the selection shows at once (no step ran to refresh the page); a page's own refresh agrees */
        if (r && ui_axis_current_of(owner) == axis) ui_list_row_look(r, UI_LOOK_SEL);
        return;
    }
    int dir = s_half_force < 0 ? -1 : 1;
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    lv_indev_t *in = lv_indev_active();
    if (row && in) {
        lv_point_t p;
        lv_area_t a;
        lv_indev_get_point(in, &p);
        lv_obj_get_coords(row, &a);
        dir = p.x < (a.x1 + a.x2) / 2 ? -1 : 1;
    }
    ui_axis_half(dir);
}

int ui_list_row_look_get(lv_obj_t *row)
{
    lv_obj_t *l = row && lv_obj_get_child_count(row) ? lv_obj_get_child(row, 0) : NULL;
    lv_color_t bg = row ? lv_obj_get_style_bg_color(row, 0) : UI_PANEL2;
    if (lv_color_eq(bg, UI_SEL)) return UI_LOOK_SEL;
    if (lv_color_eq(bg, UI_SYS)) return UI_LOOK_SYS;
    if (lv_color_eq(bg, UI_ON)) return UI_LOOK_ON;
    if (l && lv_obj_check_type(l, &lv_label_class) && lv_color_eq(lv_obj_get_style_text_color(l, 0), UI_DIM)) return UI_LOOK_DIM;
    return UI_LOOK_NORMAL;
}

#if !UI_ROUND
/* The square panel's explain text (SQ2): its lines break between clauses, not in the middle of a word. A unit is a run of text that ends at a clause mark (，。；：！？、）」 and the
 * ASCII space): the units are packed into lines of `width` px; a unit wider than a line is left to the label's own wrap. The wrap is the same for Traditional / Simplified / English (the English
 * units are its words). Returns a PSRAM copy with the breaks as newlines, NULL when there is no room (the label then wraps by itself). */
static bool clause_end(const char *p, int n)
{
    if (n == 1) return *p == ' ';
    if (n != 3) return false;
    const unsigned char *u = (const unsigned char *)p;
    const uint32_t c = (uint32_t)(u[0] & 0x0F) << 12 | (uint32_t)(u[1] & 0x3F) << 6 | (u[2] & 0x3F);
    return c == 0xFF0C || c == 0x3002 || c == 0xFF1B || c == 0xFF1A || c == 0xFF01 || c == 0xFF1F || c == 0x3001 || c == 0xFF09 || c == 0x300D;
}
static char *clause_wrap(const char *text, const lv_font_t *f, int width)
{
    const size_t len = strlen(text);
    char *out = psram_malloc(len * 2 + 2);
    if (!out) return NULL;
    size_t o = 0;
    int line_w = 0;
    const char *p = text, *unit = text;
    while (1) {
        int n = 1;
        if (*p) { const unsigned char c = (unsigned char)*p; n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; for (int k = 1; k < n; k++) if (!p[k]) { n = k; break; } }   /* M7: a character cut by the end of the text is as long as what is there: never past the NUL */
        const bool end = !*p, cut = !end && clause_end(p, n);
        if (end || cut || *p == '\n') {
            const char *u_end = end || *p == '\n' ? p : p + n;
            if (u_end > unit) {
                char tmp[160];
                size_t ul = (size_t)(u_end - unit);
                if (ul >= sizeof tmp) ul = sizeof tmp - 1;
                memcpy(tmp, unit, ul);
                tmp[ul] = 0;
                lv_point_t sz;
                lv_text_get_size(&sz, tmp, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                if (line_w > 0 && line_w + sz.x > width && sz.x <= width) { out[o++] = '\n'; line_w = 0; }   /* the unit fits a line of its own: it starts one */
                memcpy(out + o, unit, (size_t)(u_end - unit));
                o += (size_t)(u_end - unit);
                line_w += sz.x;
                if (sz.x > width) line_w = sz.x % width;                                                  /* the label wraps it: what is left is on the last line */
            }
            if (end) break;
            unit = u_end;
            if (*p == '\n') { out[o++] = '\n'; line_w = 0; unit = p + 1; }
            p = *p == '\n' ? p + 1 : p + n;
            continue;
        }
        p += n;
    }
    out[o] = 0;
    return out;
}
#endif

lv_obj_t *ui_list_note(lv_obj_t *list, const char *text)
{
    lv_obj_t *l = lv_label_create(list);
    lv_obj_set_width(l, UI_ROW_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(l, UI_DIM, 0);
#if UI_ROUND
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
#else
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);          /* the square panel's explain text reads from the left, broken between clauses (SQ2) */
    char *w = text ? clause_wrap(text, lv_obj_get_style_text_font(l, 0), UI_ROW_W - 4) : NULL;
    lv_label_set_text(l, w ? w : text);
    psram_free(w);
#endif
    return l;
}

void ui_list_row_set_text(lv_obj_t *row, const char *text)
{
    lv_obj_t *l = row && lv_obj_get_child_count(row) ? lv_obj_get_child(row, 0) : NULL;
    if (l && lv_obj_check_type(l, &lv_label_class) && strcmp(lv_label_get_text(l), text)) lv_label_set_text(l, text);
}

lv_obj_t *ui_list_row_wrap(lv_obj_t *list, const char *text, lv_color_t bg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_list_row(list, NULL, bg, cb, user);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_width(l, UI_ROW_LABEL_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
    return b;
}

void ui_list_header(lv_obj_t *list, const char *text)
{
    lv_obj_t *l = lv_label_create(list);
    lv_obj_set_style_text_color(l, UI_A1, 0);
    lv_label_set_text(l, text);
}

bool ui_list_knob(lv_obj_t *list, int detents)
{
    if (!list) return false;
    pg_t *pg = pg_on(list);
    const int before = lv_obj_get_scroll_y(list);
    lv_obj_scroll_by_bounded(list, 0, -UI_LIST_STEP * detents, LV_ANIM_OFF);
    if (pg) {                                           /* a paged list: the knob that runs off one end of the page goes on to the next / previous page (no wrap) */
        const int moved = lv_obj_get_scroll_y(list) - before, want = UI_LIST_STEP * detents;
        if (detents > 0 && moved < want && lv_obj_get_scroll_bottom(list) <= 0 && pg->page + 1 < pg->pages) pg_turn(pg, pg->page + 1, false);
        else if (detents < 0 && moved > want && lv_obj_get_scroll_top(list) <= 0 && pg->page > 0) pg_turn(pg, pg->page - 1, true);
    }
    return true;
}

void ui_list_show(lv_obj_t *list, lv_obj_t *row)
{
    if (list && row) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

/* ---- the two-tap button ---- */
/* Own timer lifetime on purpose: ui_timer_guard/ui_timer_bind only delete the timer when the button goes, but here the
 * button's delete must also free the saved label text (and an early disarm must stop the timer and remove the callback,
 * which the guard cannot undo: it would pile up one more event callback per arming). */
static lv_obj_t *s_arm_btn;       /* the armed button, NULL when none */
static char *s_arm_orig;          /* its label text before arming (lv_strdup) */
static lv_timer_t *s_arm_timer;   /* one-shot, UI_HOLD_MS */

static void arm_deleted(lv_event_t *e);
static void arm_clear(bool restore)
{
    if (!s_arm_btn) return;
    lv_obj_t *b = s_arm_btn;
    s_arm_btn = NULL;
    lv_obj_remove_event_cb_with_user_data(b, arm_deleted, NULL);
    if (s_arm_timer) { lv_timer_delete(s_arm_timer); s_arm_timer = NULL; }
    if (restore && s_arm_orig && lv_obj_get_child_count(b)) lv_label_set_text(lv_obj_get_child(b, 0), s_arm_orig);
    lv_free(s_arm_orig);
    s_arm_orig = NULL;
}
static void arm_deleted(lv_event_t *e) { (void)e; arm_clear(false); }               /* the page went: nothing to restore */
static void arm_timeout(lv_timer_t *t) { (void)t; s_arm_timer = NULL; arm_clear(true); }   /* repeat count 1: LVGL deletes the timer after this */

void ui_arm_cancel(void) { arm_clear(true); }

bool ui_arm_tap(lv_obj_t *btn, const char *armed_text)
{
    if (s_arm_btn == btn) { arm_clear(true); return true; }
    arm_clear(true);                                      /* another button was armed: its text comes back */
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (!l) return false;
    char *orig = lv_strdup(lv_label_get_text(l));
    lv_timer_t *t = lv_timer_create(arm_timeout, UI_HOLD_MS, NULL);
    if (!orig || !t || !lv_obj_add_event_cb(btn, arm_deleted, LV_EVENT_DELETE, NULL)) {   /* nothing armed: the tap is just ignored */
        lv_free(orig);
        if (t) lv_timer_delete(t);
        return false;
    }
    lv_timer_set_repeat_count(t, 1);
    s_arm_btn = btn;
    s_arm_orig = orig;
    s_arm_timer = t;
    lv_label_set_text(l, armed_text);
    return false;
}

static void page_tap_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e)) return;   /* a child control's tap bubbled up */
    void (*cb)(void) = (void (*)(void))lv_event_get_user_data(e);
    if (cb) cb();
}

void ui_page_tap(lv_obj_t *page, void (*cb)(void))
{
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, page_tap_cb, LV_EVENT_CLICKED, (void *)cb);
}

/* ---- the page kit (UI unification T4) ---- */
int ui_slot_y(ui_slot_t s)
{
    static const int y[UI_SLOT_N] = { UI_SLOT_TITLE_Y, UI_SLOT_HEAD_Y, UI_SLOT_UPPER_Y, UI_SLOT_CENTER_Y, UI_SLOT_LOWER_Y, UI_SLOT_FOOT_Y, UI_SLOT_BASE_Y };
    return s >= 0 && s < UI_SLOT_N ? y[s] : 0;
}

static int isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

int ui_slot_w(ui_slot_t s, int h)
{
    static const int w[UI_SLOT_N] = { UI_SLOT_TITLE_W, UI_SLOT_HEAD_W, UI_SLOT_UPPER_W, UI_SLOT_CENTER_W, UI_SLOT_LOWER_W, UI_SLOT_FOOT_W, UI_SLOT_BASE_W };
    int sw = s >= 0 && s < UI_SLOT_N ? w[s] : UI_W;
    if (UI_ROUND) {
        int r = UI_W / 2, far = (ui_slot_y(s) < 0 ? -ui_slot_y(s) : ui_slot_y(s)) + (h + 1) / 2;
        int chord = far < r ? 2 * isqrt(r * r - far * far) : 0;
        if (chord < sw) sw = chord;
    }
    return sw < UI_TAP_MIN ? UI_TAP_MIN : sw;
}

lv_obj_t *ui_text(lv_obj_t *parent, ui_slot_t s, const lv_font_t *f)
{
    int h = lv_font_get_line_height(f ? f : lv_obj_get_style_text_font(parent, 0));
    return ui_label(parent, f, ui_slot_y(s), ui_slot_w(s, h));
}

static lv_obj_t *holder(lv_obj_t *parent, int w, int h, int y);
lv_obj_t *ui_slot_holder(lv_obj_t *parent, ui_slot_t s, int h)
{
    return holder(parent, ui_slot_w(s, h), h, ui_slot_y(s));
}

lv_obj_t *ui_text_dim(lv_obj_t *parent, ui_slot_t s, const lv_font_t *f)
{
    lv_obj_t *l = ui_text(parent, s, f);
    lv_obj_set_style_text_color(l, UI_DIM, 0);
    return l;
}

/* a round-ended button of the kit: w x UI_TAP_MIN (or h) at centre offset (x, y), its label child 0 one line wide enough for it */
static lv_obj_t *kit_button(lv_obj_t *parent, int w, int h, int x, int y, lv_event_cb_t cb, void *user)
{
    lv_obj_t *l;
    lv_obj_t *b = ui_button(parent, w, h, x, y, cb, user, &l);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_label_set_text(l, "");
    lv_obj_set_size(l, w - 16, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return b;
}

static int pill_w(ui_slot_t s)
{
    int w = ui_slot_w(s, UI_TAP_MIN);
    return w < UI_PILL_W ? w : UI_PILL_W;
}

lv_obj_t *ui_pill(lv_obj_t *parent, ui_slot_t s, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = kit_button(parent, pill_w(s), UI_TAP_MIN, 0, ui_slot_y(s), cb, user);
    if (text) lv_label_set_text(lv_obj_get_child(b, 0), text);
    return b;
}

/* a transparent holder of the cells of a row or grid: not clickable (a tap beside a cell is the page's), no scroll */
static lv_obj_t *holder(lv_obj_t *parent, int w, int h, int y)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, y);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *ui_choice_row(lv_obj_t *parent, ui_slot_t s, int n, lv_event_cb_t cb, lv_obj_t **cells)
{
    if (n < 1) n = 1;
    int w = ui_slot_w(s, UI_TAP_MIN), max_n = (w + UI_TAP_GAP) / (UI_TAP_MIN + UI_TAP_GAP);
    if (n > max_n) n = max_n > 1 ? max_n : 1;          /* a cell is never narrower than UI_TAP_MIN: the extra cells are not made (a caller asking for more is a defect) */
    int cw = (w - (n - 1) * UI_TAP_GAP) / n, tw = n * cw + (n - 1) * UI_TAP_GAP;
    lv_obj_t *row = holder(parent, tw, UI_TAP_MIN, ui_slot_y(s));
    for (int i = 0; i < n; i++) {
        lv_obj_t *c = kit_button(row, cw, UI_TAP_MIN, -tw / 2 + cw / 2 + i * (cw + UI_TAP_GAP), 0, cb, (void *)(intptr_t)i);
        if (cells) cells[i] = c;
    }
    return row;
}

lv_obj_t *ui_choice_grid(lv_obj_t *parent, int cols, int rows, lv_event_cb_t cb, lv_obj_t **cells)
{
    const int asked = (cols < 1 ? 1 : cols) * (rows < 1 ? 1 : rows);
    if (cols < 1) cols = 1;
    if (cols > 2) cols = 2;
    if (rows < 1) rows = 1;
    if (rows > 2) rows = 2;
    if (cells) for (int i = cols * rows; i < asked; i++) cells[i] = NULL;   /* the cells this made are 1..4: the rest of what the caller sized the array for is NULL */
    const int mid = (UI_GRID_Y0 + UI_GRID_Y1) / 2;
    lv_obj_t *g = holder(parent, cols == 1 ? UI_GRID_W : 2 * UI_GRID_DX + UI_GRID_W, rows == 1 ? UI_GRID_H : UI_GRID_Y1 - UI_GRID_Y0 + UI_GRID_H, mid);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int x = cols == 1 ? 0 : c ? UI_GRID_DX : -UI_GRID_DX;
            int y = rows == 1 ? 0 : (r ? UI_GRID_Y1 : UI_GRID_Y0) - mid;
            lv_obj_t *b = kit_button(g, UI_GRID_W, UI_GRID_H, x, y, cb, (void *)(intptr_t)(r * cols + c));
            if (cells) cells[r * cols + c] = b;
        }
    return g;
}

void ui_pill_set_text(lv_obj_t *pill, const char *text)
{
    lv_obj_t *l = pill ? lv_obj_get_child(pill, 0) : NULL;
    if (l && text) lv_label_set_text(l, text);
}

void ui_choice_set(lv_obj_t *cell, const char *text, int look)
{
    lv_obj_t *l = lv_obj_get_child(cell, 0);
    if (text && l) lv_label_set_text(l, text);
    lv_color_t bg = UI_PANEL2, fg = UI_FG;
    if (look == UI_LOOK_SEL || look == UI_LOOK_REC) bg = UI_SEL;
    else if (look == UI_LOOK_SYS) bg = UI_SYS;
    else if (look == UI_LOOK_ON) bg = UI_ON;
    else if (look == UI_LOOK_DIM) fg = UI_DIM;
    lv_obj_set_style_bg_color(cell, bg, 0);
    lv_obj_set_style_border_width(cell, look == UI_LOOK_REC ? 3 : 0, 0);
    lv_obj_set_style_border_color(cell, UI_WARN, 0);
    if (l) lv_obj_set_style_text_color(l, fg, 0);
}

/* ---- the axis button ---- */
typedef struct {
    int axis;
    ui_axis_step_fn step;
    const char *(*text)(int axis);
} axbtn_t;
static lv_obj_t *s_axb;           /* the button that holds the knob now (static: +4 B); cleared when that button goes */
static bool s_axb_refused;        /* the tap's step said "inert now" (+1 B): the tap keeps no borrow */

static void axb_label(lv_obj_t *b)
{
    axbtn_t *a = lv_obj_get_user_data(b);
    if (a && a->text) lv_label_set_text(lv_obj_get_child(b, 0), a->text(a->axis));
}
static bool axb_step(int axis, int d)                  /* the shared step of every axis button: the page's step, then the label of the borrowing button */
{
    lv_obj_t *b = s_axb;
    axbtn_t *a = b ? lv_obj_get_user_data(b) : NULL;
    if (!a) return false;
    bool r = a->step(axis, d);
    if (!r && ui_axis_from_tap()) s_axb_refused = true;
    if (s_axb == b) axb_label(b);                      /* the step may have rebuilt the page */
    return r;
}
static void axb_click(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target_obj(e);
    if (!lv_obj_get_user_data(b)) return;
    s_axb = b;
    s_axb_refused = false;
    ui_axis_borrow(b, ((axbtn_t *)lv_obj_get_user_data(b))->axis, axb_step, NULL, true);
    if (s_axb_refused && s_axb == b) ui_axis_stop();   /* a refused step keeps no borrow: the knob does what it does on the page without one */
}
static void axb_deleted(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target_obj(e);
    if (s_axb == b) s_axb = NULL;
    psram_free(lv_obj_get_user_data(b));
}

lv_obj_t *ui_axis_button(lv_obj_t *parent, ui_slot_t s, int axis, ui_axis_step_fn step, const char *(*text)(int axis))
{
    lv_obj_t *b = kit_button(parent, pill_w(s), UI_TAP_MIN, 0, ui_slot_y(s), axb_click, NULL);
    axbtn_t *a = psram_calloc(1, sizeof *a);
    if (a) {                                           /* no memory: a button that shows its value and does nothing */
        a->axis = axis; a->step = step; a->text = text;
        lv_obj_set_user_data(b, a);
        lv_obj_add_event_cb(b, axb_deleted, LV_EVENT_DELETE, NULL);
    }
    if (text) lv_label_set_text(lv_obj_get_child(b, 0), text(axis));
    return b;
}

lv_obj_t *ui_big_button(lv_obj_t *parent, ui_slot_t s, lv_event_cb_t cb, void *user, lv_obj_t **label)
{
    lv_obj_t *l;
    lv_obj_t *b = ui_button(parent, UI_BIG_D, UI_BIG_D, 0, ui_slot_y(s), cb, user, &l);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, UI_SEL, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_label_set_text(l, "");
    lv_obj_set_size(l, UI_BIG_D - 16, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (label) *label = l;
    return b;
}

lv_obj_t *ui_progress_bar(lv_obj_t *parent, ui_slot_t s)
{
    int w = ui_slot_w(s, UI_BAR_H);
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w < UI_BAR_W ? w : UI_BAR_W, UI_BAR_H);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, ui_slot_y(s));
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, UI_PANEL2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, UI_SEL, LV_PART_INDICATOR);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

lv_obj_t *ui_dots(lv_obj_t *parent, ui_slot_t s, int max)
{
    if (max < 1) max = 1;
    lv_obj_t *d = holder(parent, max * UI_DOT_D + (max - 1) * UI_DOT_GAP, UI_DOT_D, ui_slot_y(s));
    for (int i = 0; i < max; i++) {
        lv_obj_t *o = lv_obj_create(d);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, UI_DOT_D, UI_DOT_D);
        lv_obj_set_pos(o, i * (UI_DOT_D + UI_DOT_GAP), 0);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, UI_PANEL2, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
    return d;
}

void ui_dots_set(lv_obj_t *d, int n, int lit, lv_color_t c)
{
    int max = (int)lv_obj_get_child_count(d);
    for (int i = 0; i < max; i++) {
        lv_obj_t *o = lv_obj_get_child(d, i);
        if (i >= n) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(o, i == lit ? c : UI_PANEL2, 0);
    }
}

static void hub_click(lv_event_t *e) { void (*cb)(void) = (void (*)(void))lv_event_get_user_data(e); if (cb) cb(); }
lv_obj_t *ui_hub(lv_obj_t *parent, int d, void (*cb)(void))
{
    if (d < UI_TAP_MIN) d = UI_TAP_MIN;
    lv_obj_t *l;
    lv_obj_t *b = ui_button(parent, d, d, 0, 0, hub_click, (void *)cb, &l);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
    lv_label_set_text(l, "");
    return b;
}

void ui_design_origin(int *x, int *y) { if (x) *x = (UI_W - UI_DESIGN_D) / 2; if (y) *y = (UI_H - UI_DESIGN_D) / 2; }
void ui_design_centre(int *x, int *y) { if (x) *x = UI_CX; if (y) *y = UI_CY; }
void ui_design_place(int w, int h, int *x, int *y)
{
    int ox, oy;
    ui_design_origin(&ox, &oy);
    if (x) *x = ox + (UI_DESIGN_D - w) / 2;
    if (y) *y = oy + (UI_DESIGN_D - h) / 2;
}
int ui_scale(int v) { return UI_SCALE(v); }
int ui_cover_scale(void) { return UI_ROUND ? 256 : 442; }   /* SQ2: 240 px thumbnail -> 414 px disc on the 460 panel: under the title line, down to the bottom */
int ui_cover_dy(void) { return UI_ROUND ? 0 : 19; }
int ui_scale_pct(int v, int pct) { return UI_ROUND ? v : UI_SCALE(v) * pct / 100; }
int ui_scale_big(int v) { return ui_scale_pct(v, 130); }
int ui_scale_fill(int v) { return ui_scale_pct(v, 150); }
int ui_by_panel(int round_v, int square_v) { return UI_ROUND ? round_v : square_v; }
int ui_edge_d(int round_d) { return UI_ROUND ? round_d : UI_CONTENT_D - 12; }
void ui_text_airy(lv_obj_t *l)
{
#if !UI_ROUND
    lv_obj_set_style_text_letter_space(l, UI_TEXT_SPACE, 0);
    lv_obj_set_style_text_line_space(l, UI_TEXT_LINE_EXTRA, 0);
#else
    (void)l;
#endif
}
void ui_content_box(lv_obj_t *o)
{
    lv_obj_set_size(o, UI_CONTENT_D, UI_CONTENT_D);
    lv_obj_center(o);
}
lv_obj_t *ui_content(lv_obj_t *parent)
{
    lv_obj_t *c = holder(parent, UI_CONTENT_D, UI_CONTENT_D, 0);
    return c;
}

lv_obj_t *ui_list_titled(lv_obj_t *parent, const char *title)
{
    lv_obj_t *t = ui_text(parent, UI_SLOT_TITLE, NULL);
    lv_label_set_text(t, title ? title : "");
    ui_frame_chrome(t);                                 /* a page frame (ui_frame.h) shows the title in its overlay only */
    lv_obj_t *l = ui_list(parent);
#if !UI_ROUND
    lv_obj_add_event_cb(l, list_centre_del, LV_EVENT_DELETE, NULL);   /* SQ3: a titled list that is short sits in the middle under its title */
    lv_async_call(list_centre_cb, l);
#endif
    if (UI_ROUND || !ui_frame_active(parent)) {
        lv_obj_set_height(l, UI_LIST_TITLED_H);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, UI_LIST_TITLED_Y);
    }                                                    /* the square panel: the title lives in the overlay, the list is the plain list */
    return l;
}

lv_obj_t *ui_list_titled_inline(lv_obj_t *parent, const char *title)
{
#if UI_ROUND
    return ui_list_titled(parent, title);
#else
    lv_obj_t *l = ui_list(parent);                      /* the square panel: the (several-line) title is the list's first line, the lines joined: a page without a Back has no overlay to hold it */
    char b[128];
    size_t n = 0;
    for (const char *c = title ? title : ""; *c && n + 8 < sizeof b; c++) {
        if (*c == '\n') n += (size_t)snprintf(b + n, sizeof b - n, " \xc2\xb7 ");
        else b[n++] = *c;
    }
    b[n] = 0;
    ui_list_header(l, b);
    lv_obj_set_style_text_color(lv_obj_get_child(l, -1), UI_FG, 0);                    /* a page title (the round glass shows it in the text colour), not a section header's accent */
    return l;
#endif
}

lv_obj_t *ui_list_title(lv_obj_t *list)
{
    int32_t i = lv_obj_get_index(list);
    lv_obj_t *t = i > 0 ? lv_obj_get_child(lv_obj_get_parent(list), i - 1) : NULL;   /* ui_list_titled creates the title right before the list */
    return t && lv_obj_check_type(t, &lv_label_class) ? t : NULL;                    /* a plain ui_list has no title: whatever stands before it is not one */
}

lv_obj_t *ui_list_row_mark(lv_obj_t *list, const char *text, bool current, lv_event_cb_t cb, void *user)
{
    return ui_list_row(list, text, current ? UI_SEL : UI_PANEL2, cb, user);
}

const char *ui_icon_text(char *out, size_t n, const char *icon, const char *text)
{
    if (icon && font_has_icons()) snprintf(out, n, "%s %s", icon, text ? text : ""); else snprintf(out, n, "%s", text ? text : "");
    return out;
}

int ui_picture_zoom(int w)
{
#if UI_ROUND
    (void)w;
    return 256;
#else
    return w > 0 ? (UI_W - 20) * 256 / w : 256;
#endif
}
