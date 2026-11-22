// SPDX-License-Identifier: LGPL-2.1-or-later
/* Photos (main menu): the slideshow on its own, no text over the photos. A tap opens a small menu: next photo,
 * order, and what the now-playing screen shows (spec 2026-11-11-album-design.md). */
#include <stdio.h>
#include <string.h>
#include "album_svc.h"
#include "album_view.h"
#include "cfg.h"
#include "font.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_ring.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_theme.h"

static lv_obj_t *s_root, *s_view, *s_msg;
static lv_timer_t *s_t;

static void refresh(void)
{
    if (!s_root) return;
    if (album_view_showing(s_view)) { lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN); return; }
    char b[200];
    int total = album_svc_total(), ready = album_svc_ready();
    const char *w = album_svc_waiting();
    if (!g_sd_ok) snprintf(b, sizeof b, "%s", T(S_NO_TF_CARD) /* No TF card */);
    else if (total == 0) snprintf(b, sizeof b, "%s", T(S_NO_PHOTOS_YET_PUT_JPEGS_IN_PHOTOS_ON) /* No photos (JPEG) yet in the TF card's photos folder */);
    else if (ready > 0) snprintf(b, sizeof b, "%s", T(S_LOADING) /* Reading... */);
    else if (w && !strcmp(w, "battery")) snprintf(b, sizeof b, "%s %d/%d\n%s", T(S_PHOTOS_2) /* Photos */, ready, total, T(S_PLUG_IN_USB_POWER_TO_PREPARE_THEM) /* Preparation starts once USB power is plugged in */);
    else if (w && (!strcmp(w, "lowmem") || !strcmp(w, "low_internal"))) snprintf(b, sizeof b, "%s %d/%d\n%s", T(S_PHOTOS_2) /* Photos */, ready, total, T(S_LOW_MEMORY_LATER) /* Not enough memory, will prepare later */);
    else snprintf(b, sizeof b, "%s %d/%d", T(S_PREPARING_PHOTOS) /* Preparing photos */, ready, total);
    if (strcmp(lv_label_get_text(s_msg), b)) lv_label_set_text(s_msg, b);
    lv_obj_remove_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
}
static void tick(lv_timer_t *t) { (void)t; refresh(); }

enum { M_NEXT, M_ORDER, M_NOW, M_N };            /* the ring's cells */
static void menu_close(void) { ui_popring_close(); }   /* also ends a borrowed axis: the knob is volume again */
static void menu_back(void) { menu_close(); ui_pop(); }
/* Play view (three values) is an axis (rule R21): the tap steps it, then the knob does for 4 s; Order (two values) stays a plain tap; the interval is on the web page (album_sec) */
static bool menu_step(int axis, int d)
{
    if (axis == M_NOW) album_now_mode_step(d);
    else return false;
    ui_popring_relabel();                                /* labels changed; a fresh 4 s */
    return true;
}
static const char *menu_label(int i)
{
    static char l2[48], l3[48];                  /* two lines fit a ring segment */
    switch (i) {
    case M_NEXT: return T(S_NEXT_2) /* Next photo */;
    case M_ORDER: snprintf(l2, sizeof l2, "%s\n%s", T(S_ORDER) /* Order */, album_order_label()); return l2;
    default: snprintf(l3, sizeof l3, "%s\n%s", T(S_PLAYING) /* Play view */, album_now_label()); return l3;
    }
}
static void menu_pick(int i)
{
    if (i == M_NEXT) { menu_close(); album_view_next(); return; }
    if (i == M_ORDER) { album_order_next(); ui_popring_relabel(); return; }   /* labels changed */
    menu_close();                                        /* centre */
}
static const ui_popring_t s_def = { .n = M_N, .label = menu_label, .pick = menu_pick, .back = menu_back,
                                    .axis_mask = 1u << M_NOW, .step = menu_step };
static void menu_open(void) { ui_popring_open(s_root, &s_def, 0); }
void scr_album_menu(int pick) { if (s_root) { if (pick == -2) menu_open(); else if (ui_popring_up(&s_def)) ui_popring_pick(pick); } }   /* console */

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_root = p;
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);   /* photos sit on black, whatever the theme */
    s_view = album_view_create(p);
    s_msg = ui_text(p, UI_SLOT_CENTER, NULL);
    lv_obj_set_style_text_color(s_msg, lv_color_white(), 0);   /* the page is black in every theme: its message is light in every theme */
    lv_label_set_text(s_msg, "");
    ui_page_tap(p, menu_open);
    s_t = ui_timer_bind(p, tick, 1000, NULL);
    refresh();
}
static void destroy(void)
{
    if (ui_popring_up(&s_def)) menu_close();
    s_t = NULL;                                          /* bound to the root */
    s_root = NULL;
}
const ui_screen_t scr_album = { .name = "album", .create = create, .destroy = destroy, .on_knob = ui_axis_knob };   /* a borrowed cell takes the knob, else volume */
