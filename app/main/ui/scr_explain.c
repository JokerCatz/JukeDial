// SPDX-License-Identifier: LGPL-2.1-or-later
/* The explain page of every 說明 entry (header ui_help.h): the title, the entry's text (a QR and address first for the settings entry), Back.
 * An entry with refresh_ms (the 現況 page) re-runs its text every refresh_ms. It only composes shared pieces; the texts live in ui_help_texts.c. arg: const ui_help_t *. */
#include "font.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_kit.h"

static lv_obj_t *s_list;

static void refresh_tick(lv_timer_t *t) { (void)t; ui_help_refresh(); }   /* bound to the page: it goes with it */

static void create(lv_obj_t *p, void *arg)
{
    const ui_help_t *h = arg;
    char tt[64];                                        /* ICON-B: the help icon in front of the title */
    ui_obj_guard(ui_list_titled(p, ui_icon_text(tt, sizeof tt, UI_ICON_L_HELP, h ? T_RAW(h->title_zh, h->title_en) : ui_help_label())), &s_list);
    ui_help_fill(s_list, h);
    ui_back_button(p);
    if (h && h->refresh_ms) ui_timer_bind(p, refresh_tick, h->refresh_ms, NULL);
}

static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_explain = { .name = "explain", .create = create, .on_knob = on_knob };
