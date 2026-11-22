// SPDX-License-Identifier: LGPL-2.1-or-later
/* A game page's HEAD row and its ring (game_ring.h). */
#include "game_ring.h"
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "game_style.h"
#include "games.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_frame.h"
#include "ui_popring.h"

enum { C_RULE, C_STYLE, C_AXIS0 };                    /* then the page's axes (0..GR_AXES_MAX), then 重來 */
static const game_ring_t *s_def;
static game_sel_t *s_sel;
static lv_obj_t *s_page, *s_cells[2];
static ui_popring_t s_pop;                            /* the ring's def (its help cell is the game's 說明, set at open) */
static bool has_action(void) { return s_def->action_text != NULL; }
static int axes(void) { return s_def->axes > 0 ? (s_def->axes > GR_AXES_MAX ? GR_AXES_MAX : s_def->axes) : 0; }
static int again_cell(void) { return C_AXIS0 + axes(); }
static int action_cell(void) { return again_cell() + 1; }

static int wrap(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }
static int count(int what) { return what == GR_MODE ? s_def->modes : what == GR_RULE ? s_def->rules : s_def->style_fix ? 1 : game_style_count(); }
static int *slot(int what) { return what == GR_MODE ? &s_sel->mode : what == GR_RULE ? &s_sel->rule : &s_sel->style; }

void game_ring_refresh(void)
{
    if (!s_def) return;
    if (s_cells[0]) ui_choice_set(s_cells[0], s_def->mode_text(s_sel->mode), UI_LOOK_NORMAL);
    if (s_cells[1]) ui_choice_set(s_cells[1], s_def->rule_name(s_sel->rule), UI_LOOK_NORMAL);
}

void game_ring_set(int what, int v)
{
    if (!s_def || !s_page || what < GR_MODE || what > GR_STYLE) return;
    *slot(what) = what == GR_STYLE && s_def->style_fix ? s_def->style_fix - 1 : wrap(v, count(what));
    game_ring_refresh();
    if (ui_popring_up(&s_pop)) ui_popring_relabel();
    s_def->changed(what, *slot(what));
}

static bool mode_step(int axis, int d) { (void)axis; game_ring_set(GR_MODE, s_sel->mode + d); return true; }

static const char *cell_label(int i)
{
    static char b[C_AXIS0 + GR_AXES_MAX + 2][48];         /* the ring asks for every cell before it copies any */
    if (i >= C_AXIS0 && i < again_cell()) snprintf(b[i], sizeof b[i], "%s\n%s", s_def->axis_name(i - C_AXIS0), s_def->axis_value(i - C_AXIS0));
    else if (i == C_RULE) snprintf(b[i], sizeof b[i], "%s\n%s", s_def->rule_zh ? T_RAW(s_def->rule_zh, s_def->rule_en) : T(S_RULES) /* Rule */, s_def->rule_name(s_sel->rule));
    else if (i == C_STYLE) snprintf(b[i], sizeof b[i], "%s\n%s", s_def->style_zh ? T_RAW(s_def->style_zh, s_def->style_en) : T(S_STYLE) /* Style */, game_style_name(s_sel->style));
    else if (has_action() && i == action_cell()) snprintf(b[i], sizeof b[i], "%s", s_def->action_text());
    else snprintf(b[i], sizeof b[i], "%s", s_def->again_text ? s_def->again_text() : T(S_RESTART) /* Restart */);
    return b[i];
}
static bool ring_step(int axis, int d)
{
    if (axis == C_RULE) game_ring_set(GR_RULE, s_sel->rule + d);
    else if (axis == C_STYLE) { if (s_def->style_fix) return false; game_ring_set(GR_STYLE, s_sel->style + d); }
    else if (axis >= C_AXIS0 && axis < again_cell()) {
        if (!s_def->axis_step(axis - C_AXIS0, d)) return false;
        if (ui_popring_up(&s_pop)) ui_popring_relabel();
    }
    else return false;
    return true;
}
static const char *action_armed(int i) { (void)i; return s_def->action_armed ? s_def->action_armed() : T(S_TAP_AGAIN_TO_CONFIRM) /* Tap again to confirm */; }
static void ring_pick(int i)
{
    if (i == again_cell()) { ui_popring_close(); s_def->changed(GR_AGAIN, 0); }
    else if (has_action() && i == action_cell()) { ui_popring_close(); if (s_def->action_run) s_def->action_run(); }
    else if (i == C_STYLE) return;                                  /* a fixed style (CF2) or an axis cell: nothing to tap-cycle */
    else if (i == C_RULE) game_ring_set(GR_RULE, s_sel->rule + 1);   /* two rules: a tap cycles (an axis cell steps through ring_step instead) */
    else if (i < 0) ui_popring_close();
}

void game_ring_open(void)
{
    if (!s_def || !s_page) return;
    s_pop = (ui_popring_t){ .n = again_cell() + 1 + (has_action() ? 1 : 0), .label = cell_label, .pick = ring_pick, .step = ring_step,
                            .axis_mask = (s_def->rules >= 3 ? 1u << C_RULE : 0u) | (game_style_count() >= 3 && !s_def->style_fix ? 1u << C_STYLE : 0u) | (((1u << axes()) - 1u) << C_AXIS0),
                            .arm_mask = has_action() && !s_def->action_direct ? 1u << action_cell() : 0u, .armed = action_armed,
                            .help = s_def->help ? s_def->help : games_help_entry(games_find(s_def->id)) };
    ui_popring_open(s_page, &s_pop, 0);
}

static void head_tap(lv_event_t *e)
{
    if ((int)(intptr_t)lv_event_get_user_data(e) == 0) ui_axis_borrow(s_page, 0, mode_step, NULL, true);
    else game_ring_open();
}

void game_ring_head(lv_obj_t *page, const game_ring_t *def, game_sel_t *sel)
{
    s_def = def;
    s_sel = sel;
    s_page = page;
    sel->mode = wrap(sel->mode, def->modes);
    sel->rule = wrap(sel->rule, def->rules);
    sel->style = def->style_fix ? def->style_fix - 1 : wrap(sel->style, game_style_count());
    s_cells[0] = s_cells[1] = NULL;
    ui_obj_guard(page, &s_page);
    ui_frame_option(ui_choice_row(page, UI_SLOT_HEAD, 2, head_tap, s_cells));   /* the page frame's overlay shows the HEAD row (ui_frame.h) */
    ui_obj_guard(s_cells[0], &s_cells[0]);
    ui_obj_guard(s_cells[1], &s_cells[1]);
    game_ring_refresh();
}

const char *game_ring_id(void) { return s_def && s_page ? s_def->id : ""; }

bool game_ring_cmd(const char *arg) { return s_def && s_page && s_def->cmd && s_def->cmd(arg); }

void game_ring_print(void)
{
    if (!s_def || !s_page) { printf("APP game_ring none\n"); return; }
    printf("APP game_ring id=%s mode=%d rule=%d style=%d open=%d\n", s_def->id, s_sel->mode, s_sel->rule, s_sel->style, ui_popring_up(&s_pop));
}
