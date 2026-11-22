// SPDX-License-Identifier: LGPL-2.1-or-later
/* Cover cache (hires T8e): the background pass of the cover pack (thumb_svc.h, the rules in pc_thumbbg.h). A titled list:
 * the state (building / paused: <why> / done / manual), the numbers (n / total・%・N songs left), pause / resume, build now, the mode auto / manual is on the web page (cover_auto),
 * 說明 last. Opened from Settings (the row Cover cache) and from Status (its Cover cache row). Renews its text twice a second (a note's text is set only when it changed).
 * Composed of shared pieces only; built with -msmall-data-limit=0 (its statics are PSRAM .bss). Console: `covers`, `covers open`, `covers tap pause|build`. */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "screens.h"
#include "thumb_svc.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"

enum { ROW_PAUSE, ROW_BUILD, ROW_N };
static lv_obj_t *s_list, *s_state, *s_count, *s_row[ROW_N];

static void set_text(lv_obj_t *label, const char *text) { if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text); }

static void refresh(void)
{
    if (!s_list) return;
    pc_tbg_prog_t p;
    thumb_svc_progress_want();
    thumb_svc_progress(&p);
    char b[96];
    pc_tbg_state_text(&p, i18n_zh(), b, sizeof b);
    set_text(s_state, b);
    pc_tbg_count_text(&p, i18n_zh(), b, sizeof b);
    set_text(s_count, b);
    ui_list_row_set_text(s_row[ROW_PAUSE], p.user_paused ? T(S_RESUME) /* Resume */ : T(S_PAUSE) /* Pause */);
}

static void row_cb(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    pc_tbg_prog_t p;
    thumb_svc_progress(&p);
    if (r == ROW_PAUSE) thumb_svc_user_pause(!p.user_paused);
    else thumb_svc_build_now();
    refresh();
}

static void tick(lv_timer_t *t) { (void)t; refresh(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_COVER_CACHE) /* Cover cache */), &s_list);
    s_state = ui_list_note(s_list, "");
    s_count = ui_list_note(s_list, "");
    s_row[ROW_PAUSE] = ui_list_row(s_list, "", UI_PANEL, row_cb, (void *)(intptr_t)ROW_PAUSE);
    s_row[ROW_BUILD] = ui_list_row(s_list, T(S_BUILD_NOW) /* Build now */, UI_PANEL, row_cb, (void *)(intptr_t)ROW_BUILD);
    ui_list_help_row(s_list, &HELP_COVERS);
    ui_back_button(p);
    ui_timer_bind(p, tick, 500, NULL);
    refresh();
}

static void destroy(void) { s_list = s_state = s_count = NULL; memset(s_row, 0, sizeof s_row); }
static void resume(void) { refresh(); }
static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_covers = { .name = "covers", .create = create, .destroy = destroy, .on_knob = on_knob, .on_resume = resume };

/* the settings row's text: Cover cache: auto / manual */
void scr_covers_row_label(char *b, size_t n)
{
    pc_tbg_prog_t p;
    thumb_svc_progress(&p);
    snprintf(b, n, "%s%s", T(S_COVER_CACHE_2) /* Cover cache: */, p.manual ? T(S_MANUAL) /* Manual */ : T(S_AUTO) /* Auto */);
}

void covers_cmd(const char *a)
{
    pc_tbg_prog_t p;
    thumb_svc_progress(&p);
    if (!strcmp(a, "open")) { ui_push(&scr_covers, NULL); return; }
    if (!strncmp(a, "tap ", 4)) {
        int r = !strcmp(a + 4, "pause") ? ROW_PAUSE : !strcmp(a + 4, "build") ? ROW_BUILD : -1;
        if (r < 0 || ui_top() != &scr_covers) { printf("APP covers tap none\n"); return; }
        lv_obj_send_event(s_row[r], LV_EVENT_CLICKED, NULL);
        return;
    }
    char st[64], ct[64];
    pc_tbg_state_text(&p, false, st, sizeof st);
    pc_tbg_count_text(&p, false, ct, sizeof ct);
    printf("APP covers state=%s why=%s done=%lu total=%lu pct=%d mode=%s user=%d unchecked=%lu tidy=%d row=%d | %s | %s\n",
           p.state == PC_TBG_ST_BUILDING ? "building" : p.state == PC_TBG_ST_HELD ? "held" : p.state == PC_TBG_ST_MANUAL ? "manual" : "done",
           pc_tbg_why_name(p.why), (unsigned long)p.done, (unsigned long)p.total, pc_tbg_pct(p.done, p.total), p.manual ? "manual" : "auto", p.user_paused,
           (unsigned long)p.unchecked, p.tidy, pc_tbg_status_row(&p), st, ct);
    if (ui_top() == &scr_covers) {
        printf("APP covers page state=%s count=%s", lv_label_get_text(s_state), lv_label_get_text(s_count));
        for (int i = 0; i < ROW_N; i++) printf(" | %s", lv_label_get_text(lv_obj_get_child(s_row[i], 0)));
        printf("\n");
    }
}
