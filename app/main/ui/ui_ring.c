// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ui_theme.h"
#include "ui_ring.h"
#include "ui_geom.h"
#include "screens.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_help.h"
#include "esp_attr.h"
#include "font.h"
#include "icons_gen.h"
#include "psram_alloc.h"
#include "ui_kit.h"

#define RING_R UI_BUBBLE_R
#define ITEM_D UI_BUBBLE_D
#define CENTER_D UI_BUBBLE_CD

typedef struct {
    char **labels;
    int n, page, sel;
    ui_ring_pick_cb on_pick;
    ui_ring_back_cb on_back;
    ui_ring_back_cb on_close;      /* centre tapped with nothing selected; NULL = nothing happens */
    lv_obj_t *items[UI_RING_PER_PAGE];
    lv_obj_t *center, *center_label;
    int style;                     /* 0 bubbles, 1 segmented ring (cfg ui_style) */
    int per;                       /* items a page: 7, or 6 when the ring needs a "next page" segment */
    uint32_t sysmask[4];           /* items drawn in the system colour (first 128) */
    bool immediate;                /* menu: a tap runs the item */
    bool knob_volume;              /* the knob stays volume even with several pages (ui_ring_set_knob_volume) */
    int mark;                      /* menu: this item is drawn as selected (the current value); -1 none */
    uint32_t nowmask;              /* select-then-confirm rings: these items (first 32) run at their first tap (ui_ring_set_item_now) */
    char **hints;                  /* per-item centre text while selected (NULL or n entries, each NULL or a copy) */
    char *center_text;             /* menu: the centre shows this instead of "Close" (NULL: the default); survives every refresh */
    int armed;                     /* menu: the item whose first tap has been made (-1 none); its armed text is in the centre, the cell is marked */
    char **armtext;                /* per-item armed text (NULL or n entries, each NULL or a copy): ui_ring_set_item_arm; NULL where the bit of armmask is set: no memory for the copy, the centre says "Tap again" */
    uint32_t armmask;              /* menu: these items (first 32) need two taps */
    const char *(*armfn)(int);     /* ui_ring_set_arm_text_fn: asked for the armed text of a cell at the moment its first tap arms it (NULL: the copy made by ui_ring_set_item_arm stands) */
    lv_timer_t *arm_t;             /* the one-shot that ends an arm (a ring of its own; none inside a popring, whose close ends it) */
    bool arm_hosted;               /* a popring owns the 4 s: no timer here */
    uint32_t pressmask;            /* menu: these items (first 32) run at the touch-down (ui_ring_set_item_press) */
    bool pressed_done;             /* the touch-down already ran its item: the CLICKED of the same touch does not */
    lv_obj_t *cont, *seg_label[UI_RING_PER_PAGE + 1];
    lv_obj_t *seg_icon[UI_RING_PER_PAGE];   /* the icon of a cell (ui_ring_set_icons), created by it; NULL before */
    const char *const *icons;      /* per item: an UI_ICON_* string (static) or NULL; NULL array = no icons */
    int nicons;
    lv_obj_t *back_btn;            /* bubble style: the Back button (hidden since GS: the up flick runs on_back) */
    lv_obj_t *bubble;              /* GS: the page-count bubble (ui_page_bubble), hidden with one page */
} ring_t;

/* ---- segmented ring (user 2026-11-09: a ring cut into segments instead of bubbles) ----
 * 8 segments of 45 deg: the items clockwise from the bottom left, Back at the bottom; with more than one page
 * the bottom-right segment is "next page" (6 items a page). System segments have their own colour. Centre = confirm. */
#define SEG_RO UI_RING_RO
#define SEG_RI UI_RING_RI
static float seg_angle(int k) { return 135.0f + 45.0f * (float)k; }   /* GS: eight cells round the ring, the bottom one (k 7, 90 deg) is a cell like the others */   /* screen degrees */

/* The square panel's grid (UI_RING_BOX_*, ui_geom.h): cell c = row * 2 + col, rows from UI_RGRID_TOP; the ring's slot k (0..UI_RING_PER_PAGE, the last is Back) is cell k, Back is cell 7. */
static int grid_cell_w(void) { return (UI_RING_BOX_W - 2 * UI_RGRID_MX - UI_RGRID_GAP) / 2; }
/* The square grid has two layouts. 1: the header band (UI_RGRID_HEAD_*) over four rows of UI_RGRID_CELL_H. 2: no header content (a popring with the close cell, no centre text and no two-tap items: the header's only text was "Close"), the four rows stretch over the whole panel height (down to the 26 px the panel's corners keep). Chosen from the ring's
 * content, never by a page. */
/* The square grid's close cell: a menu (popring) ring with a free slot 6 shows "Close" there, left of Back, and no header band (the header's only text was "Close"). A custom centre text
 * (ui_ring_set_center_text) keeps the header; the armed text shows in the header while armed. */
static bool close_cell(const ring_t *r) { return !UI_ROUND && r->immediate && false && r->on_back && !r->center_text && !r->armmask && r->n < UI_RING_PER_PAGE; }   /* GS: no "Close" cell any more: the up flick (or a tap on the centre) closes */
static bool lay2(const ring_t *r) { return close_cell(r); }
static int grid_top(const ring_t *r) { return lay2(r) ? UI_RGRID_LAY2_TOP : UI_RGRID_TOP; }
static int grid_ch(const ring_t *r) { return lay2(r) ? (UI_RGRID_TOP + UI_RGRID_ROWS * UI_RGRID_CELL_H + (UI_RGRID_ROWS - 1) * UI_RGRID_GAP - UI_RGRID_LAY2_TOP - (UI_RGRID_ROWS - 1) * UI_RGRID_GAP) / UI_RGRID_ROWS : UI_RGRID_CELL_H; }
static void grid_rect(const ring_t *r, int slot, int *x, int *y)           /* the top-left of the cell of slot, in the ring container */
{
    int c = slot;                                          /* slot 7 (UI_RING_PER_PAGE) = Back = cell 7 */
    *x = UI_RGRID_MX + (c % 2) * (grid_cell_w() + UI_RGRID_GAP);
    *y = grid_top(r) + (c / 2) * (grid_ch(r) + UI_RGRID_GAP);
}
/* SQ2: a ring of one page with few cells (at most 6 with Back and the close cell) does not copy the round ring's slots: the used cells follow each other, two to a row (a lone last cell and a pair
 * of cells take the whole row), and the rows share the room under the header (at most UI_RGRID_CELL_H * 5 / 4 high, centred in it). Chosen from the ring's content, never by a page. */
static bool slot_used(const ring_t *r, int slot)
{
    return slot < r->per && r->page * r->per + slot < r->n;   /* SQ3: on this page (a last page of a paged ring holds fewer) */
}
static int used_count(const ring_t *r) { int u = 0; for (int k = 0; k < UI_RING_PER_PAGE; k++) u += slot_used(r, k); return u; }
static bool compact(const ring_t *r) { int u; return !UI_ROUND && r->style && (u = used_count(r)) >= 1 && (r->n <= r->per ? u <= 6 : u <= 2); }   /* (GS: the Back cell is gone from the count) SQ3: the page of a paged ring that holds one or two cells is the same two big stacked buttons */
/* SQ3: the page of a paged ring that holds three or four cells keeps only the top 2 x 2 cells of the grid (the cells it uses, never an empty slot); the page bubble sits at their junction */
static bool top4(const ring_t *r) { int u; return !UI_ROUND && r->style && r->n > r->per && (u = used_count(r)) >= 3 && u <= 4; }
typedef struct { int x, y, w, h; bool used; } grect_t;
static void grid_geom(const ring_t *r, int slot, grect_t *g)              /* the cell of slot, in the ring container */
{
    if (!compact(r)) {
        grid_rect(r, slot, &g->x, &g->y);
        g->w = grid_cell_w(); g->h = grid_ch(r); g->used = top4(r) ? slot_used(r, slot) : true;
        return;
    }
    const int u = used_count(r), rows = u <= 2 ? u : (u + 1) / 2, full = 2 * grid_cell_w() + UI_RGRID_GAP;
    const int top = grid_top(r), room = UI_RGRID_TOP + UI_RGRID_ROWS * UI_RGRID_CELL_H + (UI_RGRID_ROWS - 1) * UI_RGRID_GAP - top;
    int h = (room - (rows - 1) * UI_RGRID_GAP) / rows;
    if (h > UI_RGRID_CELL_H * 5 / 4) h = UI_RGRID_CELL_H * 5 / 4;
    const int y0 = top + (room - (rows * h + (rows - 1) * UI_RGRID_GAP)) / 2;
    int i = 0;
    for (int k = 0; k < slot; k++) i += slot_used(r, k);
    g->used = slot_used(r, slot);
    const bool lone = u <= 2 || (u % 2 && i == u - 1);
    g->w = lone ? full : grid_cell_w();
    g->h = h;
    g->x = UI_RGRID_MX + (lone ? 0 : (i % 2) * (grid_cell_w() + UI_RGRID_GAP));
    g->y = y0 + (u <= 2 ? i : i / 2) * (h + UI_RGRID_GAP);
}

/* the page bubble's centre, relative to the ring container's centre (SQ3: it follows the layout of the page): at the seam of the two stacked buttons (below a lone one), at the junction of the top 2 x 2 cells, else where the lower 2 x 2 cells meet */
static int bubble_dy(const ring_t *r)
{
    grect_t g;
    int y;
    if (compact(r)) {
        grid_geom(r, 0, &g);
        y = used_count(r) == 1 ? g.y + g.h + 4 + ui_page_bubble_h() / 2 : g.y + g.h + UI_RGRID_GAP / 2;
    } else if (top4(r)) {
        grid_geom(r, 0, &g);
        y = g.y + g.h + UI_RGRID_GAP / 2;
    } else {
        int gx;
        grid_rect(r, UI_RING_PER_PAGE - 3, &gx, &y);
        y += grid_ch(r) + UI_RGRID_GAP / 2;                /* where the lower 2 x 2 cells meet */
    }
    return y - UI_RING_BOX_H / 2;
}

bool ui_ring_disc(void) { return UI_ROUND; }

/* an item that runs at its first tap, without the select step (an axis: the tap steps it, ui_axis.h) */
static bool now_item(const ring_t *r, int idx) { return idx >= 0 && idx < 32 && (r->nowmask >> idx & 1); }

/* the centre while an item is selected: its hint when it has one, else its name */
static const char *sel_text(ring_t *r) { return r->hints && r->hints[r->sel] ? r->hints[r->sel] : r->labels[r->sel]; }

/* The centre text of a ring (every write of it goes through here, so the width is always the one for this text). A paragraph whose last line is one character
 * alone ("速度與拍號跟著循" / "環", batch B board run) makes the label take the narrowest width that keeps the same number of lines: the lines come out about equal
 * (the text is centred, so one that fits looks the same). Text without such an orphan is wrapped as before. */
static int32_t text_h(const char *t, size_t len, const lv_font_t *f, int32_t w)
{
    char b[96];
    lv_point_t sz;
    if (len >= sizeof b) return -1;                      /* too long to copy whole: never cut it (callers skip the orphan fix) */
    snprintf(b, sizeof b, "%.*s", (int)len, t);
    lv_text_get_size(&sz, b, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return sz.y;
}

static bool has_orphan(const char *text, const lv_font_t *f, int32_t w)
{
    for (const char *p = text; *p; ) {
        size_t len = strcspn(p, "\n"), cut = len;
        while (cut > 0 && ((unsigned char)p[cut - 1] & 0xC0) == 0x80) cut--;     /* a continuation byte: back to the code point's lead byte */
        if (cut > 0) cut--;                                                      /* ... and drop that lead byte: the paragraph without its last code point */
        if (len > 0 && cut > 0) {                                                /* cut == 0: one code point, nothing to orphan */
            int32_t a = text_h(p, cut, f, w), b = text_h(p, len, f, w);
            if (a < 0 || b < 0) return false;                                    /* too long to measure: wrapped as before */
            if (a < b) return true;
        }
        p += len + (p[len] == '\n');
    }
    return false;
}

static int32_t whole_h(const char *t, const lv_font_t *f, int32_t w)   /* the whole text, no copy */
{
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return sz.y;
}

/* a wrapping label at most wmax wide: the orphan rule above (the centre text and, final fix B-5, the segment cells: 「回到播放／器」) */
static void fit_text(lv_obj_t *l, const char *text, int32_t wmax)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    int32_t best = wmax;
    if (has_orphan(text, f, wmax)) {
        int32_t h = whole_h(text, f, wmax);
        for (int32_t w = wmax - 4; w >= wmax / 2 && whole_h(text, f, w) <= h; w -= 4) best = w;
    }
    lv_obj_set_width(l, best);
    lv_label_set_text(l, text);
}

#if !UI_ROUND
/* The square grid's header holds two lines at most (80 px): a text of more lines is packed into lines of the header's width, its own lines joined by " · " and the line break
 * put at one of those dots (SQ2: not a single paragraph that wraps anywhere); what is still longer than two lines ends in dots. */
static int text_w(const char *t, size_t n, const lv_font_t *f)
{
    char b[96];
    if (n >= sizeof b) n = sizeof b - 1;
    memcpy(b, t, n);
    b[n] = 0;
    lv_point_t sz;
    lv_text_get_size(&sz, b, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}
static void header_text(lv_obj_t *l, const char *text)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    const int32_t w = UI_RING_CENTER_TEXT_W, lh = lv_font_get_line_height(f) + lv_obj_get_style_text_line_space(l, 0);
    char b[200];
    const char *use = text;
    if (whole_h(text, f, w) > 2 * lh && strchr(text, '\n')) {
        static const char DOT[] = " \xc2\xb7 ";                      /* " · " */
        const int dot_w = text_w(DOT, sizeof DOT - 1, f);
        size_t n = 0;
        int line_w = 0;
        for (const char *p = text; *p && n + 8 < sizeof b;) {
            const char *e = strchr(p, '\n');
            const size_t sl = e ? (size_t)(e - p) : strlen(p);
            const int sw = text_w(p, sl, f);
            if (n && line_w + dot_w + sw <= w) { memcpy(b + n, DOT, sizeof DOT - 1); n += sizeof DOT - 1; line_w += dot_w + sw; }
            else { if (n) b[n++] = '\n'; line_w = sw; }
            if (n + sl + 8 >= sizeof b) break;
            memcpy(b + n, p, sl);
            n += sl;
            p = e ? e + 1 : p + sl;
        }
        b[n] = 0;
        use = b;
    }
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, w);
    int32_t h = whole_h(use, f, w);
    lv_obj_set_height(l, h > 2 * lh ? 2 * lh : LV_SIZE_CONTENT);
    lv_label_set_text(l, use);
}
#endif
static void centre_text(ring_t *r, const char *text)
{
#if !UI_ROUND
    if (r->style) { header_text(r->center_label, text); lv_obj_center(r->center_label); return; }
#endif
    fit_text(r->center_label, text, r->style ? UI_RING_CENTER_TEXT_W : CENTER_D - 16);
}

/* a 說明 cell: its label is the shared word (ui_help_label): one rule for every ring, grid and popup */
static bool is_help(const ring_t *r, int idx) { return idx >= 0 && idx < r->n && r->labels[idx] && !strcmp(r->labels[idx], ui_help_label()); }
/* the icon of item idx (menu cells, FILLED family; the 說明 cell always has the help icon), NULL = a text cell */
static const char *icon_of(const ring_t *r, int idx)
{
    if (!r->icons || idx < 0 || idx >= r->n || !font_has_icons()) return NULL;
    if (idx < r->nicons && r->icons[idx]) return r->icons[idx];
    return is_help(r, idx) ? UI_ICON_HELP : NULL;
}
static void seg_place(ring_t *r, int k, bool icon)   /* the cell's label (and icon label) position */
{
    if (UI_ROUND) return;
    grect_t g;
    grid_geom(r, k, &g);
    int cx = g.x + g.w / 2 - UI_RING_BOX_W / 2, cy = g.y + g.h / 2 - UI_RING_BOX_H / 2;
    lv_obj_align(r->seg_label[k], LV_ALIGN_CENTER, cx, cy + (icon ? UI_RING_ICON_NAME_DY : 0));
    if (r->seg_icon[k]) lv_obj_align(r->seg_icon[k], LV_ALIGN_CENTER, cx, cy + UI_RING_ICON_DY);
}
static void refresh(ring_t *r)
{
    int pages = (r->n + r->per - 1) / r->per;
    if (r->style) {
        for (int i = 0; i < UI_RING_PER_PAGE; i++) {
            int idx = r->page * r->per + i;
            const char *ic = idx < r->n ? icon_of(r, idx) : NULL;
            fit_text(r->seg_label[i], ic && UI_ROUND ? "" : idx < r->n ? r->labels[idx] : close_cell(r) && i == UI_RING_PER_PAGE - 1 ? T(S_CLOSE) /* Close */ : "", UI_RING_LABEL_W);
            lv_obj_set_style_text_color(r->seg_label[i], is_help(r, idx) && idx != r->sel ? UI_HELP_TEXT : UI_FG, 0);
            if (r->seg_icon[i]) {
                if (ic) { lv_label_set_text(r->seg_icon[i], ic); lv_obj_remove_flag(r->seg_icon[i], LV_OBJ_FLAG_HIDDEN); }
                else lv_obj_add_flag(r->seg_icon[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_text_color(r->seg_icon[i], is_help(r, idx) && idx != r->sel ? UI_HELP_TEXT : UI_FG, 0);
            }
        }
        char closeb[48];                                   /* the centre of a popup ring reads "x Close" (a way to close only the popup, visibly) */
        centre_text(r, r->immediate ? (r->armed >= 0 ? (r->armtext && r->armtext[r->armed] ? r->armtext[r->armed] : T(S_TAP_AGAIN_2) /* Tap again */) : r->center_text ? r->center_text : ui_icon_text(closeb, sizeof closeb, UI_ICON_L_CLOSE, T(S_CLOSE) /* Close */)) : r->sel >= 0 ? sel_text(r) : UI_ROUND ? T(S_PICK_ONE_TAP_THE_CENTRE) /* Pick one, then tap the centre */ : T(S_PICK_ONE) /* the square panel is one tap: no hint to tap the centre again */);
        lv_obj_set_style_text_color(r->center_label, (r->sel >= 0 && !r->immediate) || (r->immediate && r->armed < 0 && !r->center_text) ? UI_FG : UI_DIM, 0);
        if (!UI_ROUND) {
            for (int k = 0; k < UI_RING_PER_PAGE; k++)             /* the labels follow the layout (it can change with the ring's content) */
                seg_place(r, k, r->page * r->per + k < r->n && icon_of(r, r->page * r->per + k));
            if (close_cell(r) && r->armed < 0) lv_obj_add_flag(r->center, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(r->center, LV_OBJ_FLAG_HIDDEN);
        }
        ui_page_bubble_set(r->bubble, r->page, pages);
        if (!UI_ROUND) lv_obj_align(r->bubble, LV_ALIGN_CENTER, 0, bubble_dy(r));
        lv_obj_invalidate(r->cont);
        return;
    }
    for (int i = 0; i < UI_RING_PER_PAGE; i++) {
        int idx = r->page * r->per + i;
        lv_obj_t *b = r->items[i];
        if (i >= r->per || idx >= r->n) { lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
        const char *ic = icon_of(r, idx);
        lv_label_set_text(lv_obj_get_child(b, 0), ic ? ic : r->labels[idx]);   /* the bubble's text is the icon alone (the name shows in the centre when picked) */
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), ic ? font_icon(UI_RING_ICON_PX) : font_main(), 0);
        bool sysitem = idx < 128 && (r->sysmask[idx >> 5] >> (idx & 31) & 1);
        lv_obj_set_style_bg_color(b, idx == r->sel ? UI_SEL : is_help(r, idx) ? UI_HELP : sysitem ? UI_SYS : ui_cell_used(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), is_help(r, idx) && idx != r->sel ? UI_HELP_TEXT : UI_FG, 0);
    }
    centre_text(r, r->sel >= 0 ? sel_text(r) : "");
    ui_page_bubble_set(r->bubble, r->page, pages);
}

static void item_cb(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    int slot = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    int idx = r->page * r->per + slot;
    if ((idx == r->sel || now_item(r, idx)) && r->on_pick) { r->on_pick(idx); return; }   /* the selected cell again = the centre */
    r->sel = idx;
    refresh(r);
}

static void center_cb(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    if (r->sel >= 0 && r->on_pick) r->on_pick(r->sel);
    else if (r->sel < 0 && r->on_close) r->on_close();
}

static void delete_cb(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    if (r->arm_t) lv_timer_delete(r->arm_t);
    for (int i = 0; i < r->n; i++) psram_free(r->labels[i]);
    psram_free(r->labels);
    if (r->hints) for (int i = 0; i < r->n; i++) psram_free(r->hints[i]);
    psram_free(r->hints);
    if (r->armtext) for (int i = 0; i < r->n; i++) psram_free(r->armtext[i]);
    psram_free(r->armtext);
    psram_free(r->center_text);
    psram_free(r);
}

/* The look of segment k (0..UI_RING_PER_PAGE, the last is Back): the one place that decides a segment's colour. */
static lv_color_t cell_look(const ring_t *r, int k)
{
    int idx = r->page * r->per + k;
    bool sys = false;
    bool used = idx < r->n;   /* GS: no Back cell, no Next page cell: the up flick goes back, the side flicks turn pages */
    bool sysitem = !sys && idx < 128 && (r->sysmask[idx >> 5] >> (idx & 31) & 1);
    bool lit = idx == r->sel || (r->immediate && (idx == r->mark || idx == r->armed));
    return !used ? ui_cell_empty() : lit && !sys ? UI_SEL : is_help(r, idx) ? UI_HELP : sys || sysitem ? UI_SYS : ui_cell_used();
}

static void seg_draw(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(r->cont, &a);
    float cx = (float)(a.x1 + UI_RING_CX), cy = (float)(a.y1 + UI_RING_CY);
    for (int k = 0; k < UI_RING_PER_PAGE; k++) {            /* GS: the bottom (Back) cell is not drawn */
        lv_color_t col = cell_look(r, k);
        if (!UI_ROUND) {                           /* the square panel: a rounded cell of the grid */
            grect_t g;
            grid_geom(r, k, &g);
            if (!g.used) continue;
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = col;
            d.bg_opa = LV_OPA_COVER;
            d.radius = UI_RGRID_RADIUS;
            lv_area_t ca = { a.x1 + g.x, a.y1 + g.y, a.x1 + g.x + g.w - 1, a.y1 + g.y + g.h - 1 };
            lv_draw_rect(layer, &d, &ca);
            continue;
        }
        float m = seg_angle(k);
        {                                          /* an arc per segment, a 3 deg gap between */
            lv_draw_arc_dsc_t d;
            lv_draw_arc_dsc_init(&d);
            d.center.x = (int32_t)cx;
            d.center.y = (int32_t)cy;
            d.radius = SEG_RO;
            d.width = SEG_RO - SEG_RI;
            d.start_angle = m - 21.0f;
            d.end_angle = m + 21.0f;
            d.color = col;
            lv_draw_arc(layer, &d);
        }
    }
}

/* where the pointer is on the segmented ring: -1 the centre, else k as seg_angle numbers the segments from the bottom (0 = Back, clockwise) */
static int seg_cell(ring_t *r)
{
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(r->cont, &a);
    if (!UI_ROUND) {                               /* the grid: above its first row = the header (the centre); every other point belongs to its nearest cell */
        int y = pt.y - a.y1, x = pt.x - a.x1;
        if (!lay2(r) && y < UI_RGRID_TOP - UI_RGRID_GAP / 2) return -1;
        if (compact(r) || top4(r)) {               /* the used cell under the point, else the nearest one */
            int best = -1;
            long bd = 0;
            for (int k = 0; k < UI_RING_PER_PAGE; k++) {
                grect_t g;
                grid_geom(r, k, &g);
                if (!g.used) continue;
                long dx = x < g.x ? g.x - x : x > g.x + g.w - 1 ? x - (g.x + g.w - 1) : 0, dy = y < g.y ? g.y - y : y > g.y + g.h - 1 ? y - (g.y + g.h - 1) : 0, d = dx * dx + dy * dy;
                if (best < 0 || d < bd) { best = k; bd = d; }
            }
            return best < 0 ? -1 : best + 1;
        }
        int row = (y - grid_top(r)) / (grid_ch(r) + UI_RGRID_GAP), col = x >= UI_RING_BOX_W / 2;
        row = row < 0 ? 0 : row > UI_RGRID_ROWS - 1 ? UI_RGRID_ROWS - 1 : row;
        int c = row * 2 + col;
        return c + 1;
    }
    float dx = (float)(pt.x - a.x1 - UI_RING_CX), dy = (float)(pt.y - a.y1 - UI_RING_CY), d = sqrtf(dx * dx + dy * dy);
    if (d < SEG_RI - 4) return -1;
    float ang = atan2f(dy, dx) * 57.29578f;        /* screen degrees, 90 = down */
    int sec = (int)floorf(fmodf(ang - 90.0f + 22.5f + 720.0f, 360.0f) / 45.0f);   /* 0 = the bottom cell (slot 7), then clockwise: slot sec - 1 */
    return sec == 0 ? UI_RING_PER_PAGE : sec;
}

/* ---- arm: a menu cell that needs two taps (ui_ring_set_item_arm) ---- */
static void disarm(ring_t *r)
{
    if (r->arm_t) { lv_timer_delete(r->arm_t); r->arm_t = NULL; }
    if (r->armed < 0) return;
    r->armed = -1;
    refresh(r);
}
static void arm_timeout(lv_timer_t *t) { ring_t *r = lv_timer_get_user_data(t); r->arm_t = NULL; disarm(r); }   /* a one-shot: LVGL deletes it after this run */
static void arm_cell(ring_t *r, int idx)
{
    if (r->armfn && r->armtext && idx < r->n) {      /* the text is the state at the first tap, not at the ring's open */
        char *c = psram_strdup(r->armfn(idx));
        if (c) { psram_free(r->armtext[idx]); r->armtext[idx] = c; }     /* no memory: the older copy stays */
    }
    r->armed = idx;
    refresh(r);
    if (!r->arm_hosted) {
        r->arm_t = lv_timer_create(arm_timeout, UI_HOLD_MS, r);
        lv_timer_set_repeat_count(r->arm_t, 1);
    }
    lv_obj_send_event(r->cont, LV_EVENT_VALUE_CHANGED, NULL);   /* a first tap is an action too: a popring starts its 4 s again */
}

/* a tap on a menu ring's item idx (-1 the centre): any other cell than the armed one ends the arm; an armed cell asks for a second tap first.
 * on_pick may delete the ring: nothing of r is touched after it. */
static void tap_cell(ring_t *r, int idx)
{
    bool again = idx >= 0 && r->armed == idx;
    if (r->armed >= 0 && !again) disarm(r);
    if (idx >= 0 && idx < 32 && !again && (r->armmask >> idx & 1)) { arm_cell(r, idx); return; }
    if (again) disarm(r);
    if (r->on_pick) r->on_pick(idx);
}

static void seg_click(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    if (r->pressed_done) { r->pressed_done = false; return; }   /* the touch-down of this tap already ran its item */
    int k = seg_cell(r);
    if (k < 0) {                                   /* centre: confirm (menu: close) */
        if (r->immediate) { tap_cell(r, -1); return; }
        if (r->sel >= 0 && r->on_pick) r->on_pick(r->sel);
        else if (r->sel < 0 && r->on_close) r->on_close();
        return;
    }
    int slot = k - 1, idx = r->page * r->per + slot;   /* k 1 = bottom left (135 deg) */
    if (idx >= r->n) { disarm(r); return; }
    if (r->immediate) { tap_cell(r, idx); return; }
    if ((idx == r->sel || now_item(r, idx)) && r->on_pick) { r->on_pick(idx); return; }   /* the selected cell again = the centre */
    r->sel = idx;
    refresh(r);
}

/* a press cell (ui_ring_set_item_press) runs at the touch-down; its CLICKED is then ignored */
static void seg_pressed(lv_event_t *e)
{
    ring_t *r = lv_event_get_user_data(e);
    r->pressed_done = false;
    if (!r->immediate || !r->pressmask) return;
    int k = seg_cell(r);
    if (k <= 0) return;
    int slot = k - 1, idx = r->page * r->per + slot;
    if (idx >= r->n || idx >= 32 || !(r->pressmask >> idx & 1)) return;
    r->pressed_done = true;
    tap_cell(r, idx);
}

static lv_obj_t *seg_create(lv_obj_t *parent, ring_t *r)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, UI_RING_BOX_W, UI_RING_BOX_H);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(cont, r);
    lv_obj_add_event_cb(cont, delete_cb, LV_EVENT_DELETE, r);
    lv_obj_add_event_cb(cont, seg_draw, LV_EVENT_DRAW_MAIN, r);
    lv_obj_add_event_cb(cont, seg_pressed, LV_EVENT_PRESSED, r);
    lv_obj_add_event_cb(cont, seg_click, LV_EVENT_CLICKED, r);
    r->cont = cont;
    float rm = (SEG_RO + SEG_RI) / 2.0f - (float)UI_RING_LABEL_IN;          /* a little inward: side labels stay on the glass */
    for (int k = 0; k < UI_RING_PER_PAGE; k++) {
        lv_obj_t *l = lv_label_create(cont);
        lv_obj_set_width(l, UI_RING_LABEL_W);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        float m = seg_angle(k) * 0.0174533f;
        if (!UI_ROUND) {
            grect_t g;
            grid_geom(r, k, &g);
            lv_obj_align(l, LV_ALIGN_CENTER, g.x + g.w / 2 - UI_RING_BOX_W / 2, g.y + g.h / 2 - UI_RING_BOX_H / 2);
        } else
        lv_obj_align(l, LV_ALIGN_CENTER, (int32_t)lroundf(rm * cosf(m)), (int32_t)lroundf(rm * sinf(m)));
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(l, "");                   /* GS: the bottom label (the old Back) stays empty */
        r->seg_label[k] = l;
    }
    r->center = lv_obj_create(cont);
    lv_obj_remove_style_all(r->center);
    if (!UI_ROUND) lv_obj_set_size(r->center, UI_RING_BOX_W - 2 * UI_RGRID_MX, UI_RGRID_HEAD_H);   /* the header band */
    else lv_obj_set_size(r->center, 2 * SEG_RI - UI_RING_DISC_INSET, 2 * SEG_RI - UI_RING_DISC_INSET);
    lv_obj_set_style_radius(r->center, UI_ROUND ? LV_RADIUS_CIRCLE : UI_RGRID_RADIUS, 0);
    lv_obj_set_style_bg_opa(r->center, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->center, UI_PANEL, 0);
    lv_obj_set_style_border_width(r->center, 2, 0);
    lv_obj_set_style_border_color(r->center, UI_A1, 0);
    lv_obj_remove_flag(r->center, LV_OBJ_FLAG_CLICKABLE);
    if (UI_ROUND) lv_obj_center(r->center);
    else lv_obj_align(r->center, LV_ALIGN_TOP_MID, 0, UI_RGRID_HEAD_Y);
    r->center_label = lv_label_create(r->center);
    lv_obj_set_width(r->center_label, UI_RING_CENTER_TEXT_W);
    lv_label_set_long_mode(r->center_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(r->center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(r->center_label);
    if (UI_ROUND) r->bubble = ui_page_bubble(cont, LV_ALIGN_CENTER, 0, SEG_RI - UI_RING_DISC_INSET / 2);   /* GS: on the seam between the centre disc and the (gone) bottom cell */
    else r->bubble = ui_page_bubble(cont, LV_ALIGN_CENTER, 0, bubble_dy(r));
    refresh(r);
    return cont;
}

static bool s_force_menu;
void ui_ring_set_icons(lv_obj_t *ring, const char *const *icons, int n)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || !icons) return;
    r->icons = icons;
    r->nicons = n;
    const lv_font_t *f = font_icon(UI_RING_ICON_PX);
    if (r->style && f) {
        for (int k = 0; k < UI_RING_PER_PAGE && !r->seg_icon[k]; k++) {
            lv_obj_t *l = lv_label_create(r->cont);
            lv_obj_set_style_text_font(l, f, 0);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_text(l, "");
            if (UI_ROUND) {
                float m = seg_angle(k) * 0.0174533f, rm = (SEG_RO + SEG_RI) / 2.0f - (float)UI_RING_ICON_IN;
                lv_obj_align(l, LV_ALIGN_CENTER, (int32_t)lroundf(rm * cosf(m)), (int32_t)lroundf(rm * sinf(m)));
            }
            r->seg_icon[k] = l;
        }
        lv_obj_move_foreground(r->center);
        if (r->bubble) lv_obj_move_foreground(r->bubble);   /* the page bubble sits on the disc's rim: it stays above the disc */
    }
    refresh(r);
}

lv_obj_t *ui_ring_create_menu(lv_obj_t *parent, const char *const *labels, int n, ui_ring_pick_cb on_pick, ui_ring_back_cb on_back)
{
    s_force_menu = true;
    lv_obj_t *r = ui_ring_create(parent, labels, n, on_pick, on_back);
    s_force_menu = false;
    return r;
}

/* out of memory: no ring (NULL; every ui_ring_* call tolerates it), but the way out stays: a bare Back button */
static lv_obj_t *ring_failed(lv_obj_t *parent, ui_ring_back_cb on_back)
{
    if (!on_back) return NULL;
    ui_back_button_do(parent, on_back);
    return NULL;
}

lv_obj_t *ui_ring_create(lv_obj_t *parent, const char *const *labels, int n, ui_ring_pick_cb on_pick, ui_ring_back_cb on_back)
{
    ring_t *r = psram_calloc(1, sizeof *r);
    if (!r) return ring_failed(parent, on_back);
    r->labels = psram_calloc((size_t)(n ? n : 1), sizeof(char *));
    if (!r->labels) { psram_free(r); return ring_failed(parent, on_back); }
    for (int i = 0; i < n; i++) if (!(r->labels[i] = psram_strdup(labels[i] ? labels[i] : ""))) r->labels[i] = psram_calloc(1, 1);   /* never NULL text */
    r->n = n;
    r->sel = n == 1 ? 0 : -1;
    r->on_pick = on_pick;
    r->on_back = on_back;
    r->style = s_force_menu || !UI_ROUND ? 1 : ui_menu_style();   /* the square panel has the grid only (the bubble ring is the round board's) */
    r->immediate = s_force_menu;
    r->mark = -1;
    r->armed = -1;
    r->per = r->style ? UI_RING_PER_PAGE : UI_RING_PER_PAGE - 1;   /* GS: eight cells a page on the segmented ring and the grid; the bubble ring keeps seven (its bottom arc is empty) */
    if (r->style) return seg_create(parent, r);

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, UI_DESIGN_D, UI_DESIGN_D);
    lv_obj_center(cont);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(cont, r);
    lv_obj_add_event_cb(cont, delete_cb, LV_EVENT_DELETE, r);

    /* items from 130 deg to 410 deg (screen angles, 90 = down): the bottom 80 deg is left for Back */
    for (int i = 0; i < UI_RING_PER_PAGE; i++) {
        float a = ((float)UI_BUBBLE_A0 + ((float)UI_BUBBLE_SPAN / 6.0f) * (float)i) * 3.14159265f / 180.0f;
        lv_obj_t *b = lv_button_create(cont);
        lv_obj_set_size(b, ITEM_D, ITEM_D);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, (int32_t)lroundf(RING_R * cosf(a)), (int32_t)lroundf(RING_R * sinf(a)));
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, r);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_width(l, ITEM_D - 8);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        r->items[i] = b;
    }
    r->center = lv_button_create(cont);
    lv_obj_set_size(r->center, CENTER_D, CENTER_D);
    lv_obj_set_style_radius(r->center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r->center, UI_PANEL2, 0);
    lv_obj_center(r->center);
    lv_obj_add_event_cb(r->center, center_cb, LV_EVENT_CLICKED, r);
    r->center_label = lv_label_create(r->center);
    lv_obj_set_width(r->center_label, CENTER_D - 16);
    lv_label_set_long_mode(r->center_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(r->center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(r->center_label);
    r->bubble = ui_page_bubble(cont, LV_ALIGN_CENTER, 0, UI_RING_BUBBLE_PAGE_Y);
    if (on_back) {
        r->back_btn = ui_back_button_do(cont, on_back);
    }
    refresh(r);
    return cont;
}

void ui_ring_page(lv_obj_t *ring, int delta)
{
    if (!ring) return;
    ring_t *r = lv_obj_get_user_data(ring);
    int pages = (r->n + r->per - 1) / r->per;
    if (pages <= 1) return;
    disarm(r);                                     /* a page turn (knob or the "next page" cell) ends an arm */
    r->page = ((r->page + delta) % pages + pages) % pages;
    refresh(r);
    lv_obj_send_event(ring, LV_EVENT_VALUE_CHANGED, NULL);
}

bool ui_ring_back(lv_obj_t *ring)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || !r->on_back) return false;
    disarm(r);
    r->on_back();
    return true;
}

lv_obj_t *ui_ring_find(lv_obj_t *o)
{
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_event_count(o); i++)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(o, i)) == delete_cb) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *f = ui_ring_find(lv_obj_get_child(o, (int32_t)i));
        if (f) return f;
    }
    return NULL;
}

bool ui_ring_knob_in(lv_obj_t *root, int d)
{
    lv_obj_t *ring = ui_ring_find(root);
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || r->knob_volume || r->n <= r->per) return false;
    ui_ring_page(ring, d > 0 ? 1 : -1);
    return true;
}

void ui_ring_set_item_hint(lv_obj_t *ring, int idx, const char *text)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || idx < 0 || idx >= r->n) return;
    if (!r->hints) { if (!text) return; r->hints = psram_calloc((size_t)r->n, sizeof(char *)); if (!r->hints) return; }
    psram_free(r->hints[idx]);
    r->hints[idx] = text ? psram_strdup(text) : NULL;
    if (idx == r->sel) refresh(r);
}

void ui_ring_set_item_now(lv_obj_t *ring, int idx) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; if (r && idx >= 0 && idx < 32) r->nowmask |= 1u << idx; }


void ui_ring_set_knob_volume(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; if (r) r->knob_volume = true; }

void ui_ring_print(lv_obj_t *ring)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r) { printf("APP ring none\n"); return; }
    char c[96];
    snprintf(c, sizeof c, "%s", lv_label_get_text(r->center_label));
    for (char *q = c; *q; q++) if (*q == '\n') *q = '/';             /* one console line */
    printf("APP ring page=%d/%d sel=%d center=%s n=%d\n", r->page, (r->n + r->per - 1) / r->per, r->sel, c, r->n);   /* n: the cells, Back not counted */
}

int ui_ring_per(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; return r ? r->per : UI_RING_PER_PAGE; }
int ui_ring_get_page(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; return r ? r->page : 0; }

int ui_ring_selected(lv_obj_t *ring) { return ring ? ((ring_t *)lv_obj_get_user_data(ring))->sel : -1; }

void ui_ring_set_center_text(lv_obj_t *ring, const char *text)
{
    if (!ring) return;
    ring_t *r = lv_obj_get_user_data(ring);
    if (r->immediate && r->style) {                    /* menu mode: kept, so a page turn does not bring "Close" back */
        if (text && r->center_text && !strcmp(text, r->center_text)) return;   /* unchanged (a page's poll): no copy, no redraw */
        psram_free(r->center_text);
        r->center_text = text ? psram_strdup(text) : NULL;
        refresh(r);
    } else if (text) centre_text(r, text);
}

void ui_ring_set_close(lv_obj_t *ring, ui_ring_back_cb on_close)
{
    if (ring) ((ring_t *)lv_obj_get_user_data(ring))->on_close = on_close;
}

void ui_ring_set_system(lv_obj_t *ring, int idx)
{
    if (!ring) return;
    ring_t *r = lv_obj_get_user_data(ring);
    if (idx < 0 || idx >= 128) return;
    r->sysmask[idx >> 5] |= 1u << (idx & 31);
    refresh(r);
}

void ui_ring_set_center_hidden(lv_obj_t *ring, bool hidden)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r) return;
    if (hidden) lv_obj_add_flag(r->center, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(r->center, LV_OBJ_FLAG_HIDDEN);
}

void ui_ring_set_label(lv_obj_t *ring, int idx, const char *text)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || idx < 0 || idx >= r->n || !text || !strcmp(r->labels[idx], text)) return;
    char *c = psram_strdup(text);
    if (!c) return;                                    /* no memory: the old label stays */
    psram_free(r->labels[idx]);
    r->labels[idx] = c;
    refresh(r);
}

void ui_ring_set_mark(lv_obj_t *ring, int idx)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r) return;
    r->mark = idx;
    lv_obj_invalidate(r->cont ? r->cont : ring);
}

void ui_ring_set_item_arm(lv_obj_t *ring, int idx, const char *armed_text)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r || !r->immediate || idx < 0 || idx >= r->n || idx >= 32) return;     /* menu rings only */
    if (!r->armtext && armed_text) r->armtext = psram_calloc((size_t)r->n, sizeof(char *));
    if (r->armed == idx) disarm(r);
    if (r->armtext) { psram_free(r->armtext[idx]); r->armtext[idx] = armed_text ? psram_strdup(armed_text) : NULL; }
    if (armed_text) r->armmask |= 1u << idx;      /* still two taps when a copy failed: the text is then "Tap again" */
    else r->armmask &= ~(1u << idx);
    refresh(r);                                   /* SQ2: an armed item keeps the header band (the square grid's layout follows the ring's content): the labels and the header follow at once, not at the next refresh */
}

void ui_ring_set_arm_text_fn(lv_obj_t *ring, const char *(*fn)(int idx))
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (r && r->immediate) r->armfn = fn;
}

void ui_ring_set_item_press(lv_obj_t *ring, int idx)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (r && r->immediate && idx >= 0 && idx < 32) r->pressmask |= 1u << idx;
}

void ui_ring_set_arm_hosted(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; if (r) r->arm_hosted = true; }

int ui_ring_armed(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; return r ? r->armed : -1; }

const char *ui_ring_centre_text(lv_obj_t *ring) { ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL; return r ? lv_label_get_text(r->center_label) : ""; }

void ui_ring_tap(lv_obj_t *ring, int idx)
{
    ring_t *r = ring ? lv_obj_get_user_data(ring) : NULL;
    if (!r) return;
    if (idx == -3) { disarm(r); if (r->on_back) r->on_back(); return; }
    if (!r->immediate || idx >= r->n) return;
    tap_cell(r, idx);
}
