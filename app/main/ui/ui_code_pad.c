// SPDX-License-Identifier: LGPL-2.1-or-later
/* The code pad (ui_code_pad.h). Layout from the design circle's centre (ui_geom.h): keys K = UI_TAP_MIN + 4 square, one step = K + UI_TAP_GAP.
 * The cross is centred at x = -UI_CONTENT_D * 7 / 36 (-70 on the 360 circle); B sits right of the cross's right key (10 px apart) and lower,
 * A right of B (14 px apart) and higher. Every key's box is inside the circle with the glass margin and above Back's row. */
#include "ui_code_pad.h"
#include <stdio.h>
#include <string.h>
#include "psram_alloc.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define KEY      UI_CODEPAD_KEY
#define STEP     (KEY + UI_TAP_GAP)
#define CROSS_X  UI_CODEPAD_CROSS_X
#define B_X      (CROSS_X + STEP + KEY + UI_TAP_GAP + 2)
#define B_Y      (KEY / 2 + UI_TAP_GAP)
#define A_X      (B_X + KEY + 14)
#define A_Y      (-KEY / 4)
#define REC_MAX  32
_Static_assert(KEY >= UI_TAP_MIN, "a key is a tap target");
_Static_assert(CROSS_X + STEP + KEY / 2 + UI_TAP_GAP <= B_X - KEY / 2, "B keeps the gap to the cross's right key");
_Static_assert(STEP + KEY / 2 <= UI_BACK_Y - UI_BACK_H / 2 - UI_TAP_GAP && B_Y + KEY / 2 <= UI_BACK_Y - UI_BACK_H / 2 - UI_TAP_GAP, "the lower keys clear Back's row");
_Static_assert(-STEP - KEY / 2 >= UI_SLOT_HEAD_Y + UI_DOT_D / 2 + UI_TAP_GAP, "the upper key clears the dots");

static const char KEYCH[UI_CODE_KEYS] = { 'U', 'D', 'L', 'R', 'B', 'A' };
static const char *const KEYLB[UI_CODE_KEYS] = { "↑", "↓", "←", "→", "B", "A" };
static const int16_t KX[UI_CODE_KEYS] = { CROSS_X, CROSS_X, CROSS_X - STEP, CROSS_X + STEP, B_X, A_X };
static const int16_t KY[UI_CODE_KEYS] = { -STEP, STEP, 0, 0, B_Y, A_Y };

typedef struct {
    lv_obj_t *obj, *dots, *key[UI_CODE_KEYS];
    ui_code_pad_cb cb;
    int len, lit, nrec;
    char rec[REC_MAX + 1];
} cp_t;

static cp_t *s_live;                     /* the pad on the open page (console) */

static void tap(cp_t *p, int k)
{
    if (k < 0 || k >= UI_CODE_KEYS) return;
    if (p->nrec < REC_MAX) { p->rec[p->nrec++] = KEYCH[k]; p->rec[p->nrec] = 0; }
    if (p->cb) p->cb(k);
}

static void click_cb(lv_event_t *e)
{
    cp_t *p = lv_obj_get_user_data(lv_obj_get_parent(lv_event_get_current_target_obj(e)));
    tap(p, (int)(intptr_t)lv_event_get_user_data(e));
}

static void delete_cb(lv_event_t *e)
{
    cp_t *p = lv_event_get_user_data(e);
    if (s_live == p) s_live = NULL;
    psram_free(p);
}

lv_obj_t *ui_code_pad_create(lv_obj_t *parent, int len, ui_code_pad_cb cb)
{
    cp_t *p = psram_calloc(1, sizeof *p);
    if (!p) return NULL;
    if (len < 1) len = 1;
    p->len = len;
    p->cb = cb;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_CONTENT_D, UI_CONTENT_D);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(o, p);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, p);
    p->obj = o;
    for (int k = 0; k < UI_CODE_KEYS; k++) {
        lv_obj_t *b = lv_obj_create(o);
        lv_obj_remove_style_all(b);                                   /* no pressed look: the touch's haptic is the feedback */
        lv_obj_set_size(b, KEY, KEY);
        lv_obj_align(b, LV_ALIGN_CENTER, KX[k], KY[k]);
        lv_obj_set_style_radius(b, k >= UI_CODE_B ? LV_RADIUS_CIRCLE : 12, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, KEYLB[k]);
        lv_obj_set_style_text_color(l, UI_FG, 0);
        lv_obj_center(l);
        p->key[k] = b;
    }
    p->dots = ui_dots(parent, UI_SLOT_HEAD, len);
    ui_code_pad_progress(o, 0);
    s_live = p;
    return o;
}

static cp_t *of(const lv_obj_t *o) { return o ? lv_obj_get_user_data((lv_obj_t *)o) : NULL; }

void ui_code_pad_progress(lv_obj_t *pad, int lit)
{
    cp_t *p = of(pad);
    if (!p) return;
    if (lit < 0) lit = 0;
    if (lit > p->len) lit = p->len;
    p->lit = lit;
    if (!lit) { p->nrec = 0; p->rec[0] = 0; }
    for (int i = 0; i < p->len; i++) lv_obj_set_style_bg_color(lv_obj_get_child(p->dots, i), i < lit ? UI_SEL : UI_PANEL2, 0);
}


static int key_of(const char *a)
{
    static const char *const W[UI_CODE_KEYS] = { "u", "d", "l", "r", "b", "a" };
    for (int k = 0; k < UI_CODE_KEYS; k++) if (!strcmp(a, W[k])) return k;
    return -1;
}

void ui_code_pad_cmd(const char *arg)
{
    cp_t *p = s_live;
    if (!p) { printf("APP codepad none\n"); return; }
    if (!arg[0]) {
        printf("APP codepad len=%d lit=%d keys=%s\n", p->len, p->lit, p->rec);
    } else if (!strncmp(arg, "tap ", 4) && key_of(arg + 4) >= 0) {
        tap(p, key_of(arg + 4));
    } else if (!strncmp(arg, "at ", 3) && key_of(arg + 3) >= 0) {
        lv_area_t a;
        lv_obj_t *b = p->key[key_of(arg + 3)];
        lv_obj_update_layout(b);
        lv_obj_get_coords(b, &a);
        printf("APP codepad at %s x=%d,y=%d\n", arg + 3, (int)((a.x1 + a.x2) / 2), (int)((a.y1 + a.y2) / 2));
    } else printf("APP codepad unknown %s\n", arg);
}
