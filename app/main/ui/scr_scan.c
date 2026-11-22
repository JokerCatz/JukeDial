// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include "font.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "screens.h"
#include "ui_frame.h"
#include "ui_kit.h"

static lv_obj_t *s_label;
static lv_timer_t *s_timer;
static bool s_done;                                   /* the scan ended while another page (the alarm) covered this one */

static void tick(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text_fmt(s_label, "%s\n%d", T(S_SCANNING) /* Scanning... */, library_scan_done());
}

static void leave(void *arg)
{
    (void)arg;
    s_done = false;
    if (ui_top() != &scr_scan) return;
    ui_pop();                                         /* scan screen */
    if (ui_top() == &scr_mp3) ui_replace(&scr_mp3, NULL);   /* started from the MP3 page: its folders from the new library; from Settings: back there */
}
static void scanned(void)
{
    library_on_scanned = NULL;
    if (ui_top() == &scr_scan) leave(NULL);
    else s_done = true;                               /* covered: it leaves when it is on top again */
}
static void on_resume(void) { if (s_done) { lv_async_call_cancel(leave, NULL); lv_async_call(leave, NULL); } }   /* not from inside ui_pop */

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    /* hires T8e: playback goes on; the swap (library_svc.c) moves the player's indices into the new library (mp3_ctl_library_changed) */
    s_label = ui_text(p, UI_SLOT_CENTER, NULL);
    library_on_scanned = scanned;
    s_timer = NULL;
    s_done = false;
    if (!library_scanning() && !library_rescan()) {     /* no card: say so, with a way back */
        library_on_scanned = NULL;
        printf("APP scan refused (no card)\n");
        lv_label_set_text(s_label, T(S_NO_TF_CARD_CANNOT_SCAN) /* No TF card, cannot scan */);
        ui_back_button(p);
        return;
    }
    tick(NULL);
    s_timer = ui_timer_bind(p, tick, 500, NULL);
    ui_back_button(p);                                  /* leaving does not stop the scan */
}

static void destroy(void)
{
    library_on_scanned = NULL;
    s_done = false;
    lv_async_call_cancel(leave, NULL);
    s_timer = NULL;                /* bound to the root */
}

const ui_screen_t scr_scan = { .name = "scan", .create = create, .destroy = destroy, .on_resume = on_resume };
