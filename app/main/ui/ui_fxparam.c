// SPDX-License-Identifier: LGPL-2.1-or-later
/* The sound-effect rows (header ui_fxparam.h). A row knows what it shows by its user data: TAG_STATE | page << 16 | app, or TAG_PARAM | app << 8 | axis. */
#include "ui_fxparam.h"
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "fx_state.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_kit.h"
#include "ui_theme.h"

#define TAG_STATE 0x10000000
#define TAG_PARAM 0x20000000
#define AX_MIX PC_FXP_MAX                            /* the axis id of the Mix row */

static int s_app;                                    /* the page's app (the borrow's step has no user pointer) */
static int s_shown;                                  /* the equalizer page: the bands its rows show (-1: not an equalizer) */
static lv_obj_t *s_list;

const char *ui_fxparam_name(const pc_fxparam_t *p, bool zh)
{
    const char *n = zh ? p->zh : p->en;
    if (p->flags & PC_FXP_BAND) { const char *a = strchr(n, ' '); a = a ? strchr(a + 1, ' ') : NULL; if (a) return (zh ? TZ(a) : a) + 1; }   /* " Type": the suffix with its space is the data literal */
    return zh ? TZ(n) : n;
}

static const pc_fxparam_t *param_of(int app, int axis) { return axis == AX_MIX ? &pc_fxapp_mix_param : &fx_app(app)->param[axis]; }
static float value_of(int app, int axis) { return fx_get(app, axis == AX_MIX ? FX_P_MIX : axis); }

/* ---- state rows ---- */
static void state_text(lv_obj_t *row, int app, bool page)
{
    const char *why = NULL;
    fx_look_t lk = fx_look(app, &why);
    char b[96];
    if (page) {
        if (lk == FX_DIM) snprintf(b, sizeof b, why ? T(S_ON_PAUSED_S) /* On, paused: %s */ : T(S_ON_PAUSED) /* On, paused */, why ? why : "");
        else if (lk == FX_OFF && why && (fx_app(app)->flags & PC_FXA_WIRED_ONLY)) snprintf(b, sizeof b, T(S_OFF_S) /* Off: %s */, why);   /* an app that cannot run now says so even when it is off (T17) */
        else snprintf(b, sizeof b, "%s", lk == FX_RUN ? T(S_ON_2) /* On */ : T(S_OFF_2) /* Off */);
    } else {
        const fx_look_t k = lk;
        const char *nm = i18n_zh() ? TZ(fx_app(app)->zh) : fx_app(app)->en, *st = k == FX_RUN ? T(S_ON) /* On */ : k == FX_DIM ? T(S_ON_PAUSED_2) /* On, paused */ : T(S_OFF) /* Off */;
        if (why && (fx_app(app)->flags & PC_FXA_WIRED_ONLY)) snprintf(b, sizeof b, T(S_S_S_S) /* %s  %s: %s */, nm, st, why);   /* the row of a wired-only app names the reason (T17) */
        else snprintf(b, sizeof b, "%s  %s", nm, st);
    }
    lv_label_set_text(lv_obj_get_child(row, 0), b);
    ui_list_row_look(row, lk == FX_RUN ? UI_LOOK_ON : lk == FX_DIM ? UI_LOOK_DIM : UI_LOOK_NORMAL);
}

lv_obj_t *ui_fxparam_state_row(lv_obj_t *list, int app, bool page, lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = ui_list_row_wrap(list, "", UI_PANEL2, cb, user);
    lv_obj_set_user_data(r, (void *)(intptr_t)(TAG_STATE | (page ? 0x10000 : 0) | app));
    state_text(r, app, page);
    return r;
}

/* ---- parameter rows ---- */
static void param_text(lv_obj_t *list, lv_obj_t *row, int app, int axis)
{
    const pc_fxparam_t *p = param_of(app, axis);
    bool zh = i18n_zh();
    char v[48], b[112];
    pc_fxparam_text(p, value_of(app, axis), zh, v, sizeof v);
    snprintf(b, sizeof b, "%s: %s", ui_fxparam_name(p, zh), v);
    lv_label_set_text(lv_obj_get_child(row, 0), b);
    ui_list_row_look(row, ui_axis_current_of(list) == axis ? UI_LOOK_SEL : UI_LOOK_NORMAL);
}

void ui_fxparam_refresh(lv_obj_t *list)
{
    if (!list) return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(list); i++) {
        lv_obj_t *r = lv_obj_get_child(list, (int32_t)i);
        intptr_t u = (intptr_t)lv_obj_get_user_data(r);
        if (u & TAG_STATE) state_text(r, u & 0xff, (u & 0x10000) != 0);
        else if (u & TAG_PARAM) param_text(list, r, (u >> 8) & 0xff, u & 0xff);
    }
}

static bool is_plain(const pc_fxparam_t *p) { return p->unit == PC_FXU_BOOL || (p->unit == PC_FXU_CHOICE && p->hi - p->lo < 1.5f); }
static int count_axis(int app)                       /* the equalizer's band count parameter, -1 for every other app */
{
    const pc_fxapp_t *a = fx_app(app);
    if (!(a->flags & PC_FXA_BANDS)) return -1;
    for (int i = 0; i < a->nparam; i++) if (!(a->param[i].flags & PC_FXP_BAND) && !strcmp(a->param[i].id, "count")) return i;
    return -1;
}

/* THE one place that decides "the rows no longer match the value" (the equalizer's band count): rebuild the page. Never while a borrow is on (its end asks again), never
 * while another page covers it (resume asks again). Asked after a borrow ends, after the page is top again and after every change (reset, import, web). Later: it runs
 * inside a tap or a timer of the page that is about to go. */
static int bands_now(int app) { int ca = count_axis(app); return ca < 0 ? -1 : (int)(fx_get(app, ca) + 0.5f); }
static void sync_async(void *u)
{
    (void)u;
    if (!s_list || ui_top() != &scr_fxapp || ui_axis_current_of(s_list) >= 0) return;
    if (bands_now(s_app) != s_shown) ui_replace(&scr_fxapp, (void *)(intptr_t)s_app);
}
void ui_fxparam_sync(void)
{
    if (!s_list || bands_now(s_app) == s_shown || ui_top() != &scr_fxapp) return;   /* nothing to do (nothing is queued then); covered: resume asks again (and nothing is left queued under another page) */
    lv_async_call_cancel(sync_async, NULL);
    lv_async_call(sync_async, NULL);
}
static bool axis_step(int axis, int d)
{
    const pc_fxparam_t *p = param_of(s_app, axis);
    float v = value_of(s_app, axis), nv = pc_fxparam_step(p, v, d, ui_axis_from_tap());
    if (nv != v) fx_set(s_app, axis == AX_MIX ? FX_P_MIX : axis, nv);   /* the change callback refreshes the rows */
    else ui_fxparam_refresh(s_list);                                      /* nothing changed (a clamp): the row's selection look only */
    return true;
}
static void axis_end(void)
{
    ui_fxparam_refresh(s_list);
    ui_fxparam_sync();
}

static void row_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e), *list = lv_obj_get_parent(row);
    int app = ((int)(intptr_t)lv_event_get_user_data(e) >> 8) & 0xff, axis = (int)(intptr_t)lv_event_get_user_data(e) & 0xff;
    const pc_fxparam_t *p = param_of(app, axis);
    ui_arm_cancel();
    if (is_plain(p)) {
        if (ui_axis_current_of(list) >= 0) ui_axis_end();
        fx_set(app, axis == AX_MIX ? FX_P_MIX : axis, pc_fxparam_step(p, value_of(app, axis), 1, true));
        ui_fxparam_refresh(list);
        return;
    }
    ui_list_row_axis_tap(e, list, axis, axis_step, axis_end);
}

static lv_obj_t *param_row(lv_obj_t *list, int app, int axis)
{
    lv_obj_t *r = ui_list_row(list, "", UI_PANEL2, row_cb, (void *)(intptr_t)(app << 8 | axis));
    lv_obj_set_user_data(r, (void *)(intptr_t)(TAG_PARAM | app << 8 | axis));
    if (!is_plain(param_of(app, axis))) ui_list_row_axis(r);
    param_text(list, r, app, axis);
    return r;
}

int ui_fxparam_rows(lv_obj_t *list, int app)
{
    const pc_fxapp_t *a = fx_app(app);
    s_app = app;
    ui_obj_guard(list, &s_list);
    int n = 0, nb = 0, first = -1;
    if (a->flags & PC_FXA_MIX) { param_row(list, app, AX_MIX); n++; }
    nb = bands_now(app); s_shown = nb;
    for (int i = 0; i < a->nparam; i++) {
        if (a->param[i].flags & PC_FXP_BAND) {
            if (first < 0) first = i;
            int b = (i - first) / 4;
            if (b >= nb) break;                      /* the bands follow the count: the rest are not shown */
            if ((i - first) % 4 == 0) { char h[32]; snprintf(h, sizeof h, "%s %d", T(S_BAND) /* Band */, b + 1); if (i18n_zh()) snprintf(h, sizeof h, TZ("第 %d 段"), b + 1); ui_list_header(list, h); }
        }
        param_row(list, app, i); n++;
    }
    return n;
}

/* ---- console ---- */
lv_obj_t *ui_fxparam_list(void) { return s_list; }

static const char *look_name(lv_obj_t *row)
{
    static const char *const n[] = { "normal", "sel", "dim", "sys", "rec", "on", "help" };
    return n[ui_list_row_look_get(row)];
}

void ui_fxparam_dump(lv_obj_t *list)
{
    int k = 0;
    for (uint32_t i = 0; list && i < lv_obj_get_child_count(list); i++) {
        lv_obj_t *c = lv_obj_get_child(list, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) printf("APP fxui row=%d look=%s text=%s\n", k++, look_name(c), lv_label_get_text(lv_obj_get_child(c, 0)));
        else if (lv_obj_check_type(c, &lv_label_class)) printf("APP fxui header text=%s\n", lv_label_get_text(c));
    }
    printf("APP fxui end\n");
}

void ui_fxparam_tap(lv_obj_t *list, int n)
{
    int k = 0;
    for (uint32_t i = 0; list && i < lv_obj_get_child_count(list); i++) k += lv_obj_has_flag(lv_obj_get_child(list, (int32_t)i), LV_OBJ_FLAG_CLICKABLE);
    if (n < 0) n += k;                                   /* -1 the last row, -2 the one before */
    k = 0;
    for (uint32_t i = 0; list && n >= 0 && i < lv_obj_get_child_count(list); i++) {
        lv_obj_t *c = lv_obj_get_child(list, (int32_t)i);
        if (!lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) continue;
        if (k++ == n) { lv_obj_send_event(c, LV_EVENT_CLICKED, NULL); return; }
    }
    printf("APP fxui tap none\n");
}
