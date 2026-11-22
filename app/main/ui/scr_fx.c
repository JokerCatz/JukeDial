// SPDX-License-Identifier: LGPL-2.1-or-later
/* Effects: the filter list (fx apps T7, spec 6.2). A header per family, one row per app in chain order (name + on / off / on, paused), the 說明 row last.
 * A tap opens the app's page. It only composes shared pieces (ui_fxparam.h); the texts of the help live in ui_help_texts.c. arg: NULL. */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "fx_state.h"
#include "screens.h"
#include "ui_fxparam.h"
#include "ui_help.h"
#include "ui_kit.h"

static lv_obj_t *s_list;

static const char *family(int fam)
{
    switch (fam) {
    case PC_FXFAM_GAIN: return T(S_GAIN) /* Gain */;
    case PC_FXFAM_STEREO: return T(S_STEREO) /* Stereo */;
    case PC_FXFAM_TONE: return T(S_TONE) /* Tone */;
    case PC_FXFAM_TUBE: return T(S_TUBE) /* Tube */;
    case PC_FXFAM_DYN: return T(S_DYNAMICS) /* Dynamics */;
    case PC_FXFAM_REVERB: return T(S_REVERB) /* Reverb */;
    default: return T(S_LIMITER) /* Limiter */;
    }
}

static void row_cb(lv_event_t *e) { ui_push(&scr_fxapp, lv_event_get_user_data(e)); }
static void changed(void) { ui_fxparam_refresh(s_list); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_SOUND_FX) /* Effects */), &s_list);
    int last = -1;
    for (int i = 0; i < fx_count(); i++) {
        if (fx_app(i)->fam != last) { last = fx_app(i)->fam; ui_list_header(s_list, family(last)); }
        ui_fxparam_state_row(s_list, i, false, row_cb, (void *)(intptr_t)i);
    }
    ui_list_help_row(s_list, &HELP_FX);
    ui_back_button(p);
    fx_set_change_cb(changed);
}

static void destroy(void) { fx_set_change_cb(NULL); }
static void resume(void) { fx_set_change_cb(changed); ui_fxparam_refresh(s_list); }
static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_fx = { .name = "fx", .create = create, .destroy = destroy, .on_knob = on_knob, .on_resume = resume };

void scr_fx_console(const char *args)
{
    lv_obj_t *l = ui_top() == &scr_fx ? s_list : ui_top() == &scr_fxapp ? ui_fxparam_list() : NULL;
    if (!l) { printf("APP fxui none\n"); return; }
    int n;
    if (!strcmp(args, "rows")) ui_fxparam_dump(l);
    else if (sscanf(args, "tap %d", &n) == 1) ui_fxparam_tap(l, n);
    else printf("APP fxui unknown\n");
}
