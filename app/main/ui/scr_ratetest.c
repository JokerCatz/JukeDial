// SPDX-License-Identifier: LGPL-2.1-or-later
/* Sample rate test (hires T11, H18): the page of the rate test. A list: the note (what is tested now, or the result), one row (tap when both tones were heard; after the
 * end: test again), 說明 last. The test (rate_test.c) starts when the page is made and ends when it goes (Back, home, a page over it, the screen going dark; its 50 ms timer is the test's clock): nothing
 * is stored then. Composed of shared pieces only. arg: NULL. */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "pc/pc_dacmax.h"
#include "pc/pc_ratetest.h"
#include "rate_test.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_kit.h"

static lv_obj_t *s_list, *s_note, *s_row;

static void set_text(lv_obj_t *label, const char *text)         /* only a changed text is set (no redraw 10 times a second) */
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void refresh(void)
{
    if (!s_list) return;
    const pc_ratetest_t *t = rate_test_state();
    char b[96];
    if (!t) snprintf(b, sizeof b, "%s", rate_test_refused() ? T(S_BLUETOOTH_IS_ON_CANNOT_TEST) /* Bluetooth output active, cannot test */ : "");
    else if (t->state == PC_RT_RUN) snprintf(b, sizeof b, "%s%s", T(S_TESTING) /* Testing: */, pc_dacmax_label(t->tiers[t->tier]));
    else if (t->end == PC_RT_END_LEFT || !t->confirmed) snprintf(b, sizeof b, "%s", T(S_NOTHING_CONFIRMED_SETTING_UNCHANGED) /* Not confirmed, setting unchanged */);
    else if (t->end == PC_RT_END_FAULT) snprintf(b, sizeof b, "%s%s%s%s", T(S_LIMIT) /* Maximum: */, pc_dacmax_label(t->confirmed), T(S_DROPPED_OUT) /* ・not smooth: */, pc_dacmax_label(t->tiers[t->tier]));
    else snprintf(b, sizeof b, "%s%s", T(S_LIMIT) /* Maximum: */, pc_dacmax_label(t->confirmed));
    set_text(s_note, b);
    ui_list_row_set_text(s_row, t && t->state == PC_RT_RUN ? T(S_TAP_WHEN_YOU_HEAR_BOTH) /* Tap if you heard both tones */ : T(S_TEST_AGAIN) /* Test again */);
}

static void row_cb(lv_event_t *e)
{
    (void)e;
    const pc_ratetest_t *t = rate_test_state();
    if (t && t->state == PC_RT_RUN) rate_test_tap(); else rate_test_start();
    refresh();
}

static void poll_tick(lv_timer_t *t) { (void)t; rate_test_poll(); }
static void tick(lv_timer_t *t) { (void)t; refresh(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_RATE_TEST) /* Sample rate test */), &s_list);
    s_note = ui_list_note(s_list, "");
    s_row = ui_list_row_mark(s_list, "", false, row_cb, NULL);
    ui_list_help_row(s_list, &HELP_RATETEST);
    ui_back_button(p);
    ui_timer_bind(p, poll_tick, 50, NULL);                       /* the test: tones, timeout, fault watch (rate_test.c) */
    ui_timer_bind(p, tick, 100, NULL);
    if (rate_test_want_take()) rate_test_start();                   /* not on a re-creation (theme rebuild under another page) */
    refresh();
}

static void destroy(void)
{
    rate_test_stop();
    s_list = s_note = s_row = NULL;
}
static void suspend(void) { rate_test_stop(); }              /* another page over it (help), or the screen went dark: the test is over */
static void resume(void) { refresh(); }
static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_ratetest = { .name = "ratetest", .create = create, .destroy = destroy, .on_knob = on_knob, .on_suspend = suspend, .on_resume = resume };
