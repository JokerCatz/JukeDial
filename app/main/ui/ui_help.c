// SPDX-License-Identifier: LGPL-2.1-or-later
/* The help piece (header ui_help.h): the list-end row, the popring cell's target, the explain page's content and the QR box. */
#include "ui_help.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "cfg.h"
#include "font.h"
#include "psram_alloc.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define HELP_BUF 4096                                   /* the body of one help page, PSRAM, only while the page is built */

static lv_obj_t *s_row;                                 /* the last help / entry row made (console "help tap"); cleared when it goes */
static const ui_help_t *s_shown;                        /* what the explain page on top shows (console "help show") */
static int s_paras;
EXT_RAM_BSS_ATTR static int s_qrs;                            /* QR boxes on the page (console "help qrs") */
/* the refresh state of the page on top: one struct over 8 bytes, so it lives in PSRAM (nothing internal) */
static struct {
    lv_obj_t *list;                                     /* the list the notes of s_shown are in (cleared when it goes) */
    int first, nnote;                                   /* the child index of the first note and how many notes there are (paragraphs + the "too long" note) */
    int sets, rebuilds;                                 /* console "help stats": notes set in place / rebuilt by ui_help_refresh since the page was filled */
    lv_obj_t *link;                                     /* hires T8e: the link row while it is there (cleared when it goes) */
} R;

const char *ui_help_label(void) { return T(S_HELP) /* Help */; }
const char *ui_status_label(void) { return T(S_STATUS) /* Status */; }

static void row_cb(lv_event_t *e) { ui_help_open((const ui_help_t *)lv_event_get_user_data(e)); }

lv_obj_t *ui_list_entry_row(lv_obj_t *list, const char *text, const ui_help_t *h)
{
    lv_obj_t *r = ui_list_row(list, text, UI_PANEL2, row_cb, (void *)h);
    ui_list_row_look(r, UI_LOOK_NORMAL);
    ui_obj_guard(r, &s_row);
    return r;
}

lv_obj_t *ui_list_help_row(lv_obj_t *list, const ui_help_t *h)
{
    char b[48];
    lv_obj_t *r = ui_list_entry_row(list, ui_icon_text(b, sizeof b, UI_ICON_L_HELP, ui_help_label()), h);   /* ICON-B */
    ui_list_row_look(r, UI_LOOK_HELP);                  /* the shared help colour */
    return r;
}

void ui_help_open(const ui_help_t *h)
{
    if (!h) return;
    ui_axis_end();                                      /* a row of the page below borrowed the knob: the explain page's list scrolls with it again */
    ui_push(&scr_explain, (void *)h);
}

lv_obj_t *ui_qr_box(lv_obj_t *parent, const char *label, const char *url)
{
    lv_obj_t *col = lv_obj_create(parent);              /* QR and address: one column, so nothing can lie over the other */
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, UI_ROW_W);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    if (label && *label) {                              /* which address this is: above its QR */
        lv_obj_t *l = lv_label_create(col);
        lv_obj_set_width(l, UI_ROW_W);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_label_set_text(l, label);
    }
    lv_obj_t *qr = lv_qrcode_create(col);
    lv_qrcode_set_size(qr, UI_QR_D);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, url, (uint32_t)strlen(url));
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    lv_obj_t *u = lv_label_create(col);                 /* the address on a line of its own */
    lv_obj_set_width(u, UI_ROW_W);
    lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(u, LV_LABEL_LONG_MODE_WRAP);
#if !UI_ROUND
    lv_obj_set_style_text_font(u, &lv_font_montserrat_20, 0);   /* the address is ASCII: at the square panel's 24 px it would wrap to two lines (bigger text) */
#endif
    lv_label_set_text(u, url);
    return col;
}

/* the body in b[n] (paragraphs separated by "\n\n"): the text pair, or what body_fn writes. true: it did not fit and was cut, at the start of a character */
static bool body_of(const ui_help_t *h, char *b, size_t n)
{
    bool zh = i18n_zh();
    const char *src = zh ? (h->body_zh ? TZ(h->body_zh) : NULL) : h->body_en;
    b[0] = 0;
    if (h->body_fn) h->body_fn(b, n, zh, h->arg);
    else snprintf(b, n, "%s", src ? src : "");
    b[n - 1] = 0;
    size_t len = strlen(b);
    if (len < n - 1) return false;                      /* room left: nothing was cut */
    size_t k = len;                                     /* the last start byte, and the length its character wants */
    while (k > 0 && ((unsigned char)b[k - 1] & 0xC0) == 0x80) k--;
    if (k > 0) {
        unsigned char lead = (unsigned char)b[k - 1];
        size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (k - 1 + want > len) len = k - 1;            /* its tail is missing: drop it */
        b[len] = 0;
    }
    return true;
}

/* cut b into paragraphs in place; calls fn for each non-empty one; returns how many */
static int each_para(char *b, void (*fn)(const char *p, void *u), void *u)
{
    int k = 0;
    while (*b) {
        char *e = strstr(b, "\n\n");
        if (e) *e = 0;
        if (*b) { if (fn) fn(b, u); k++; }
        if (!e) break;
        b = e + 2;
    }
    return k;
}
static void note_cb(const char *p, void *u) { ui_list_note((lv_obj_t *)u, p); }

/* the notes of one body: one per paragraph, then the "too long" note when it was cut; returns how many */
static int notes_make(lv_obj_t *list, const ui_help_t *h)
{
    char *b = psram_malloc(HELP_BUF);
    if (!b) { ui_list_note(list, T(S_OUT_OF_MEMORY_2) /* (out of memory) */); s_paras = 0; return 1; }   /* never an empty page without a word */
    bool cut = body_of(h, b, HELP_BUF);
    s_paras = each_para(b, note_cb, list);              /* the labels copy their text: the buffer goes at once */
    psram_free(b);
    if (cut) ui_list_note(list, T(S_TEXT_TOO_LONG) /* (content too long) */);
    return s_paras + (cut ? 1 : 0);
}

static void link_cb(lv_event_t *e) { ui_push((const ui_screen_t *)lv_event_get_user_data(e), NULL); }   /* hires T8e: Status -> Cover cache */
static bool link_wanted(const ui_help_t *h) { return h->link && (!h->link_shown || h->link_shown()); }
/* hires T8e fix 0: the link row comes and goes with its condition, just before the notes (R.first moves with it) */
static void link_sync(lv_obj_t *list, const ui_help_t *h)
{
    bool want = link_wanted(h);
    if (want && !R.link) {
        ui_obj_guard(ui_list_row(list, T_RAW(h->link_zh, h->link_en), UI_PANEL, link_cb, (void *)h->link), &R.link);
        lv_obj_move_to_index(R.link, R.first);
        R.first++;
    } else if (!want && R.link) {
        lv_obj_delete(R.link);                          /* the guard clears R.link */
        R.link = NULL;
        R.first--;
    }
}

/* one QR per setup_url address, the primary first: its label is the host's name for a known mirror, else Primary / Backup n */
static void qr_boxes(lv_obj_t *list)
{
    char ub[256], lb[24];
    const char *u[CFG_SETUP_URL_MAX];
    int n = cfg_setup_urls(ub, sizeof ub, u);
    s_qrs = n;
    for (int i = 0; i < n; i++) {
        const char *label;
        if (strstr(u[i], "github")) label = "GitHub";
        else if (strstr(u[i], "gitee")) label = "Gitee";
        else if (i == 0) label = T(S_SETUP_MAIN) /* Primary URL */;
        else { snprintf(lb, sizeof lb, "%s %d", T(S_SETUP_MIRROR) /* Backup URL */, i); label = lb; }
        ui_qr_box(list, n > 1 ? label : NULL, u[i]);   /* one address: no label, as before */
    }
}

void ui_help_fill(lv_obj_t *list, const ui_help_t *h)
{
    s_shown = h;
    s_paras = s_qrs = 0;
    R.nnote = 0;
    R.sets = R.rebuilds = 0;
    ui_obj_guard(list, &R.list);
    if (!h) return;
    if (h->qr) qr_boxes(list);
    const char *web = T_RAW(h->web_zh, h->web_en);
    if (web && *web) ui_list_note(list, web);
    R.link = NULL;
    R.first = (int)lv_obj_get_child_count(list);
    R.nnote = notes_make(list, h);
    link_sync(list, h);                                 /* after the body (fix 1, review M3: link_shown reads the snapshot the body just took) */
}

/* paragraph k of the new body against note k: a changed line is set in place, a missing note is the count changing */
typedef struct { lv_obj_t *list; int k, first, notes; bool same; } cmp_t;
static void cmp_cb(const char *p, void *u)
{
    cmp_t *c = u;
    int k = c->k++;
    lv_obj_t *l = k < c->notes ? lv_obj_get_child(c->list, c->first + k) : NULL;
    if (!l) { c->same = false; return; }
    if (strcmp(lv_label_get_text(l), p)) { lv_label_set_text(l, p); R.sets++; }   /* only a changed line is touched: no flicker */
}

void ui_help_refresh(void)
{
    const ui_help_t *h = s_shown;
    lv_obj_t *list = R.list;
    if (!h || !h->refresh_ms || !list || ui_top() != &scr_explain) return;
    char *b = psram_malloc(HELP_BUF);
    if (!b) return;                                     /* the page keeps what it shows */
    bool cut = body_of(h, b, HELP_BUF);
    link_sync(list, h);                                 /* after the body: the same snapshot (fix 1, review M3) */
    cmp_t c = { list, 0, R.first, s_paras, true };
    int paras = each_para(b, cmp_cb, &c);
    psram_free(b);
    if (c.same && paras == s_paras && R.nnote == s_paras + (cut ? 1 : 0)) return;
    for (int i = (int)lv_obj_get_child_count(list) - 1; i >= R.first; i--) lv_obj_delete(lv_obj_get_child(list, i));   /* another number of lines: the notes are made again */
    R.nnote = notes_make(list, h);
    R.rebuilds++;
}

const ui_help_t *ui_help_find(const char *id)
{
    for (int i = 0; i < ui_help_count(); i++) if (!strcmp(UI_HELPS[i]->id, id)) return UI_HELPS[i];
    return NULL;
}

static bool on_top(lv_obj_t *o)                         /* o is part of the page on top */
{
    lv_obj_t *top = ui_top_root();
    while (o && o != top) o = lv_obj_get_parent(o);
    return o && top;
}

void ui_help_console(const char *args)
{
    char id[32];
    if (sscanf(args, "open %31s", id) == 1) {
        const ui_help_t *h = ui_help_find(id);
        if (h) ui_help_open(h); else printf("APP help unknown %s\n", id);
    } else if (!strcmp(args, "tap")) {
        if (s_row && on_top(s_row)) lv_obj_send_event(s_row, LV_EVENT_CLICKED, NULL);
        else printf("APP help tap none\n");
    } else if (!strcmp(args, "qrs")) {
        printf("APP explain qrs=%d\n", s_qrs);
    } else if (!strcmp(args, "stats")) {
        printf("APP explain sets=%d rebuilds=%d\n", R.sets, R.rebuilds);
    } else if (!strcmp(args, "show")) {
        if (ui_top() == &scr_explain && s_shown)
            printf("APP explain id=%s title=%s paras=%d qr=%d web=%d\n", s_shown->id, T_RAW(s_shown->title_zh, s_shown->title_en), s_paras,
                   s_shown->qr ? 1 : 0, (T_RAW(s_shown->web_zh, s_shown->web_en) != NULL) ? 1 : 0);
        else printf("APP explain none\n");
    } else printf("APP help unknown command\n");
}
