// SPDX-License-Identifier: LGPL-2.1-or-later
/* The first-boot language picker (i18n phase B, no build flag, one behaviour everywhere): shown over the menu when the config file has no "lang"
 * key (and the card is mounted, so the choice can be kept). Three rows of the shared titled list, each in its own language; one tap stores the choice and goes on to
 * the menu. Later changes: Settings > Language, the web page, the config file, the console (cfgset lang). Safe mode and the USB drive mode never come here (they do not
 * push pages from app_main's order). No 說明 entry: the page is three self-evident names, a help text would have to be in all three languages at once. */
#include <stdio.h>
#include "cfg.h"
#include "font.h"
#include "screens.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "ui_tour.h"

bool langpick_first_boot(void) { return g_sd_ok && !cfg_has("lang"); }

static lv_obj_t *s_list;

static void pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PC_LANG_COUNT) return;
    printf("APP langpick pick=%d\n", idx);
    i18n_choose_lang((pc_lang_t)idx);
    ui_pop();                                        /* back to the menu, built in the old language: */
    ui_rebuild_all();                                /* every page again in the chosen one */
    if (tour_needed()) ui_push(&scr_tour, NULL);     /* GS: the gesture tutorial, once (cfg tour_done) */
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    static const char *const ascii[PC_LANG_COUNT] = { "Chinese (Traditional)", "Chinese (Simplified)", "English" };   /* no CJK font (a brand-new card is exactly the first boot): no tofu */
    const bool cjk = font_has_cjk();
    char t[96];
    if (cjk) snprintf(t, sizeof t, "%s · %s\n%s", I18N_TW[S_LANGUAGE], I18N_CN[S_LANGUAGE], I18N_EN[S_LANGUAGE]);   /* the word in every language: no new text */
    else snprintf(t, sizeof t, "Language");
    ui_obj_guard(ui_list_titled_inline(p, t), &s_list);
    for (int i = 0; i < PC_LANG_COUNT; i++) ui_list_row(s_list, cjk ? i18n_lang_name((pc_lang_t)i) : ascii[i], UI_PANEL, pick, (void *)(intptr_t)i);
}

static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_langpick = { .name = "langpick", .create = create, .on_knob = on_knob };
