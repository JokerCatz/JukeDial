// SPDX-License-Identifier: LGPL-2.1-or-later
/* Network radio (spec 2026-11-09-radio-design.md): the stations as a scrolling list (ruling R23); the playing one is marked and in view; a tap plays it. */
#include <stdio.h>
#include <string.h>
#include "bt_svc.h"
#include "cfg.h"
#include "font.h"
#include "radio_svc.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"

static lv_obj_t *s_list;

static void pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    printf("APP pick radio %d\n", idx);
    if (radio_current() != idx && radio_play(idx)) return;
    now_open();
}
static void reopen(void *arg) { (void)arg; ui_replace(&scr_radio, NULL); }
static const char *list_label(const char *stem) { return stem[0] ? stem : T(S_RADIO_LIST_DEFAULT) /* Default */; }
static void next_list(lv_event_t *e)                 /* the list chooser row: the next list file; the page is built again (later: the row that was tapped goes with it) */
{
    (void)e;
    char st[8][PC_RADIO_STEM_MAX];
    int n = radio_lists(st, 8), at = 0;
    for (int i = 0; i < n; i++) if (!strcmp(st[i], radio_list_stem())) at = i;
    if (n < 2) return;
    printf("APP radio list=%s\n", st[(at + 1) % n][0] ? st[(at + 1) % n] : "radio");
    radio_list_select(st[(at + 1) % n]);
    ui_async(reopen, NULL);
}
static void cast_row(lv_event_t *e) { (void)e; ui_push(&scr_cast_ask, NULL); }   /* Broadcast radio: the boot mode (Wi-Fi in, Auracast out); a question first, then the reboot */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    cfg_set_str("last_screen", "radio");
    int n = radio_load();
    ui_obj_guard(ui_list(p), &s_list);           /* cleared when this page's list goes (only if it is still this one) */
    lv_obj_t *cur = NULL;
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = ui_list_row(s_list, radio_station(i)->name, i == radio_current() ? UI_SEL : UI_PANEL, pick, (void *)(intptr_t)i);
        if (i == radio_current()) cur = r;
    }
    if (!n) {
        char note[96], path[64];
        radio_list_path(path, sizeof path);
        snprintf(note, sizeof note, "%s%s", T_RAW("沒有電台\n", "No stations\n"), path);
        ui_list_note(s_list, note);
    }
    char st[8][PC_RADIO_STEM_MAX];
    if (radio_lists(st, 8) > 1) {                        /* more than one list on the card: a row names the one in use, a tap takes the next */
        char row[64];
        snprintf(row, sizeof row, "%s%s%s", T(S_STATIONS) /* Station list */, i18n_zh() ? "：" : ": ", list_label(radio_list_stem()));
        ui_list_row(s_list, row, UI_PANEL, next_list, NULL);
    }
    if (bt_svc_available()) ui_list_row(s_list, T(S_CAST_RADIO) /* Broadcast radio */, UI_PANEL, cast_row, NULL);
    ui_list_entry_row(s_list, T(S_EDIT_STATIONS_ON_PC) /* Edit stations on a computer */, &HELP_RADIO);
    ui_back_button(p);
    lv_obj_update_layout(s_list);
    ui_list_show(s_list, cur);
}
static bool on_knob(int d) { return ui_list_knob(s_list, d); }
const ui_screen_t scr_radio = { .name = "radio", .create = create, .on_knob = on_knob };
