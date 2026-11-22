// SPDX-License-Identifier: LGPL-2.1-or-later
/* The filter page (fx apps T7, spec 6.3): generated from the app's parameter table. On/Off, the parameter rows (ui_fxparam), Reset (two taps), 說明 last.
 * It only composes shared pieces. arg: (void *)(intptr_t)app index. */
#include "font.h"
#include "fx_state.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_fxparam.h"
#include "ui_help.h"
#include "ui_kit.h"

static lv_obj_t *s_list;
static int s_app;

static void tog_cb(lv_event_t *e)
{
    (void)e;
    ui_arm_cancel();
    if (ui_axis_current_of(s_list) >= 0) ui_axis_end();
    fx_set_on(s_app, !fx_on(s_app));
    ui_fxparam_refresh(s_list);
}

static void reset_cb(lv_event_t *e)
{
    if (ui_axis_current_of(s_list) >= 0) ui_axis_end();
    if (!ui_arm_tap(lv_event_get_target(e), T(S_TAP_AGAIN_BACK_TO_DEFAULTS) /* Tap again: back to defaults */)) return;
    fx_reset_app(s_app);
    ui_fxparam_refresh(s_list);
}

static void changed(void) { ui_fxparam_refresh(s_list); ui_fxparam_sync(); }   /* a look, a mode, a reset, an import or a web change */

static void create(lv_obj_t *p, void *arg)
{
    s_app = (int)(intptr_t)arg;
    const pc_fxapp_t *a = fx_app(s_app);
    ui_obj_guard(ui_list_titled(p, T_RAW(a->zh, a->en)), &s_list);
    ui_fxparam_state_row(s_list, s_app, true, tog_cb, NULL);
    ui_fxparam_rows(s_list, s_app);
    ui_list_row_mark(s_list, T(S_RESET) /* Reset */, false, reset_cb, NULL);
    ui_list_help_row(s_list, ui_help_fx_app(s_app));
    ui_back_button(p);
    fx_set_change_cb(changed);
}

static void destroy(void) { fx_set_change_cb(NULL); }
static void resume(void) { fx_set_change_cb(changed); ui_fxparam_refresh(s_list); ui_fxparam_sync(); }
static bool on_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    return ui_list_knob(s_list, d);
}

const ui_screen_t scr_fxapp = { .name = "fxapp", .create = create, .destroy = destroy, .on_knob = on_knob, .on_suspend = ui_axis_stop, .on_resume = resume };
