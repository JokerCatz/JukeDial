// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bubble wrap (tools toy, spec 2026-11-11-tools-design.md): the shared bubble field (ui_bubbles.h) and the shared exit (ui_exit.h, mode A: a knob turn changes the volume and shows 返回). A touch pops a bubble with a haptic
 * click; one bubble per sheet is the special one (a strong click instead). When the last one is gone the sheet is laid again after a short pause: no
 * animation, no score, endless. The music is not touched (the pop sound is mixed over it: sfx.h). Only composes shared pieces. */
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_bubbles.h"
#include "ui_exit.h"
#include "ui_kit.h"

#define RELAY_MS 300

static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C };   /* CF2: a tap pops a bubble: the popup waits for a free knob */
static lv_obj_t *s_field;
static lv_timer_t *s_relay;

static void relay_cb(lv_timer_t *t)
{
    s_relay = NULL;                               /* a one-shot: it ends itself (ui_timer_guard clears the slot only for a live timer) */
    lv_timer_delete(t);
    ui_bubble_field_reset(s_field);
}

static void popped(int idx, bool special, int left)
{
    (void)idx;
    if (special) ui_haptic_strong(); else ui_haptic_tap();
    sfx_play(special ? "pop_big" : "pop");    /* a chiptune pop over the music (sfx.h) */
    if (left == 0 && !s_relay) s_relay = lv_timer_create(relay_cb, RELAY_MS, NULL);
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_relay = NULL;
    ui_timer_guard(p, &s_relay);
    ui_obj_guard(ui_bubble_field_create(p, popped), &s_field);
    ui_exit_attach(p, &EXIT, NULL);
}

static void destroy(void) { if (s_relay) { lv_timer_delete(s_relay); s_relay = NULL; } }

const ui_screen_t scr_bubbles = { .name = "tools_bubbles", .create = create, .destroy = destroy, .content = true };
