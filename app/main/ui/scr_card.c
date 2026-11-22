// SPDX-License-Identifier: LGPL-2.1-or-later
/* Memory card (cardfmt): the card page and the format flow. Offered only when the last mount failed with gpt / nofs (sd_format.h);
 * the settings list's first row 「Card unusable (<reason>)」 opens the card page: the reason in plain words, 「Format card…」 and 說明.
 * The flow is one page (scr_tfcardfmt) that shows one step at a time; a step change replaces the page with itself (the guard stays, s_stepping):
 *   WARN  the size, what the card becomes, the warning, Continue, Back
 *   CODE  the code pad (ui_code_pad.h): up up down down left right left right BA (Konami) by taps; a wrong tap clears the dots (one haptic tick); 30 s without a tap: WARN again
 *   ARM   「Final confirm: format <size>」, two taps within 4 s (ui_arm_tap)
 *   RUN   「Formatting... do not remove the card」, no Back, the screen kept on; then the result (Done or the error in plain words) with Back
 * The guard (pc_fmtguard_t) lives for one session of the page: Back (or anything else that takes the page away) ends it, and nothing formats; a page
 * over it or the screen going dark sends CODE / ARM back to WARN. Audio stops when the flow opens. Composed of shared pieces only. */
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_geom.h"
#include "cfg.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "font.h"
#include "player_ctl.h"
#include "sd_format.h"
#include "screens.h"
#include "ui_code_pad.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "usb_mode.h"
#include "pc/pc_sdguide.h"

_Static_assert((int)UI_CODE_UP == (int)PC_FG_UP && (int)UI_CODE_DOWN == (int)PC_FG_DOWN && (int)UI_CODE_LEFT == (int)PC_FG_LEFT && (int)UI_CODE_RIGHT == (int)PC_FG_RIGHT &&
               (int)UI_CODE_B == (int)PC_FG_B && (int)UI_CODE_A == (int)PC_FG_A, "the pad's keys are the guard's keys");

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

const char *scr_tfcard_reason_text(const char *r)
{
    if (!r) return T(S_READY) /* Usable */;
    if (!strcmp(r, "gpt")) return T(S_GPT_PARTITIONS) /* GPT partition */;
    if (!strcmp(r, "nofs")) return T(S_NO_READABLE_FILE_SYSTEM) /* No readable file system */;   /* any f_mount failure but GPT: the format re-checks the card first (review m3) */
    if (!strcmp(r, "nocard")) return T(S_NO_CARD) /* No card */;
    if (!strcmp(r, "stuck")) return T(S_NOT_RESPONDING) /* not responding */;   /* the card answered but cannot be set up: not "no card", never formatted */
    return T(S_READ_ERROR) /* Read error */;
}

void scr_tfcard_row_label(char *b, size_t n) { snprintf(b, n, "%s%s%s", T(S_CARD_CANNOT_BE_USED) /* Card unusable ( */, scr_tfcard_reason_text(sd_format_reason()), T(S_STR) /* ) */); }

/* ---- the card page ---- */
static lv_obj_t *s_clist, *s_crow;
static bool s_coffer, s_stepping;

static void open_flow(lv_event_t *e) { (void)e; ui_push(&scr_tfcardfmt, NULL); }

static void card_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_CARD) /* Memory card */), &s_clist);
    const char *r = sd_format_reason();
    if (!r && g_sd_ok) ui_list_note(s_clist, T(S_THE_CARD_IS_READY) /* The card is usable */);
    else if (!r || !strcmp(r, "nocard")) { ui_list_note(s_clist, T(S_NO_CARD_2) /* No memory card */); ui_list_note(s_clist, T(S_INSERT_A_CARD) /* Insert a memory card */); }
    else if (!strcmp(r, "gpt")) { ui_list_note(s_clist, T(S_THIS_CARD_HAS_GPT_PARTITIONS) /* This card uses GPT */); ui_list_note(s_clist, T(S_THE_PLAYER_CANNOT_READ_THEM) /* the player cannot read it */); }
    else if (!strcmp(r, "nofs")) ui_list_note(s_clist, T(S_NO_FILE_SYSTEM_THE_PLAYER_CAN_READ) /* No readable file system on the card */);
    else if (!strcmp(r, "stuck")) { ui_list_note(s_clist, T(S_CARD_NOT_RESPONDING_PROPERLY) /* Card not responding properly */); ui_list_note(s_clist, BSP_LCD_ROUND ? T(S_REPOWER_SWITCH) /* Switch the device off and on with the power switch (round: the card is inside, the board has a power switch) */ : T(S_REINSERT_OR_REPOWER) /* Re-insert the card, or power the device fully off and on (FakePod: the slot is outside; the battery keeps the card powered through a reboot) */); }
    else { ui_list_note(s_clist, T(S_THE_CARD_CANNOT_BE_READ) /* Memory card read error */); ui_list_note(s_clist, T(S_REINSERT_OR_TRY_ANOTHER) /* Re-insert the card, or try another card */); }
    printf("APP card page reason=%s guide=%s\n", r ? r : "-", pc_sd_guide_key(r) ? pc_sd_guide_key(r) : "-");   /* gpt / nofs: the guide is the format row below (offered only when the entry rule allows) */
    s_coffer = sd_format_offered();
    s_crow = s_coffer ? ui_list_row(s_clist, T(S_FORMAT_THE_CARD) /* Format card... */, UI_SYS, open_flow, NULL) : NULL;
    ui_list_help_row(s_clist, &HELP_CARD);
    ui_back_button(p);
}

static void card_rebuild(void *a) { (void)a; if (ui_top() == &scr_tfcard) ui_replace(&scr_tfcard, NULL); }
static void card_resume(void) { if (!s_stepping && s_coffer != sd_format_offered()) { lv_async_call_cancel(card_rebuild, NULL); lv_async_call(card_rebuild, NULL); } }   /* after a format: what the card is now */
static void card_destroy(void) { lv_async_call_cancel(card_rebuild, NULL); s_clist = s_crow = NULL; }
static bool card_knob(int d) { return ui_list_knob(s_clist, d); }

const ui_screen_t scr_tfcard = { .name = "card", .create = card_create, .destroy = card_destroy, .on_knob = card_knob, .on_resume = card_resume };

/* ---- the flow ---- */
enum { V_GUARD, V_RUN, V_RESULT };
static pc_fmtguard_t s_g;
static int s_view, s_shown;              /* s_shown: the guard step this page was built for */
static uint32_t s_built_ms;              /* when this step's page was built: taps in its first SETTLE_MS are ignored */
#define SETTLE_MS 250                    /* review m1: an eager double tap on the last key (or on Continue) must not land on the next step's row */
static bool settled(void) { return (uint32_t)(now_ms() - s_built_ms) >= SETTLE_MS; }
static uint64_t s_bytes;
static pc_fmt_plan_t s_plan;
static lv_obj_t *s_list, *s_pad, *s_row;

static void step_cb(void *a) { (void)a; if (ui_top() != &scr_tfcardfmt) return; s_stepping = true; ui_replace(&scr_tfcardfmt, NULL); }
static void step(void) { lv_async_call_cancel(step_cb, NULL); lv_async_call(step_cb, NULL); }   /* after the event that asked for it, never inside it */

static void size_text(char *b, size_t n) { pc_fmt_size_label(s_bytes, b, n); }

static void cont_cb(lv_event_t *e) { (void)e; if (settled() && pc_fmtguard_continue(&s_g, now_ms())) step(); }

static void key_cb(int k)
{
    int r = pc_fmtguard_key(&s_g, k, now_ms());
    if (r == PC_FG_KEY_WRONG) { ui_haptic_tick(); ui_code_pad_progress(s_pad, 0); }
    else if (r == PC_FG_KEY_OK) ui_code_pad_progress(s_pad, s_g.got);
    else if (r == PC_FG_KEY_DONE || r == PC_FG_KEY_TIMEOUT) { ui_code_pad_progress(s_pad, r == PC_FG_KEY_DONE ? s_g.got : 0); step(); }
}

static void arm_cb(lv_event_t *e)
{
    (void)e;
    if (!settled()) return;
    char sz[16], b[64];
    size_text(sz, sizeof sz);
    snprintf(b, sizeof b, "%s%s", T(S_TAP_AGAIN_FORMAT) /* Tap again: format  */, sz);
    if (!ui_arm_tap(s_row, b)) return;
    if (pc_fmtguard_armed(&s_g) && sd_format_start(&s_g)) s_view = V_RUN;
    step();
}

static void guard_tick(lv_timer_t *t)    /* the step moved without a tap here (30 s timeout, a page over this one, the screen went dark): show it */
{
    (void)t;
    pc_fmtguard_tick(&s_g, now_ms());
    if (s_g.step != s_shown) step();
}

static void run_tick(lv_timer_t *t)
{
    (void)t;
    ui_activity();                       /* the screen stays on while the card is written */
    if (sd_format_state() != SDF_RUN) { s_view = V_RESULT; step(); }
}

/* why nothing was written (PC_FMT_OUT_UNTOUCHED only) */
static const char *untouched_why(int e)
{
    switch (e) {
    case ESP_ERR_INVALID_STATE: return T(S_THE_CARD_READS_NOW_RESTART) /* The card is readable now, please reboot */;   /* review m6: mounted with boot defaults */
    case ESP_ERR_NOT_FOUND: return T(S_NO_CARD_FOUND) /* Card not found */;
    case ESP_ERR_INVALID_SIZE: return T(S_ANOTHER_CARD_IS_IN_THE_SLOT) /* The card was changed */;
    case ESP_ERR_NO_MEM: return T(S_OUT_OF_MEMORY) /* Out of memory */;
    default: return T(S_COULD_NOT_START) /* Cannot start */;
    }
}

/* the result page: what happened to the card, by how far the format got (pc_fmt_outcome; review I1) */
static void result_notes(lv_obj_t *list)
{
    char b[80];
    switch (sd_format_outcome()) {
    case PC_FMT_OUT_DONE:
        snprintf(b, sizeof b, "%s%s%s", T(S_THE_CARD_IS_FORMATTED) /* Card formatted ( */, pc_fmt_fs_name(sd_format_plan()->fs), T(S_STR) /* ) */);
        ui_list_note(list, b);
        ui_list_note(list, T(S_MUSIC_GOES_INTO_THE_EXPORT_FOLDER) /* Put music in the export folder */);
        break;
    case PC_FMT_OUT_UNTOUCHED:
        ui_list_note(list, untouched_why(sd_format_err()));
        ui_list_note(list, T(S_NOT_FORMATTED_THE_CARD_IS_AS_IT_WAS) /* Not formatted, the card data is unchanged */);
        break;
    case PC_FMT_OUT_UNMOUNTED:
        ui_list_note(list, T(S_FORMATTING_IS_DONE) /* Format done */);
        ui_list_note(list, T(S_BUT_THE_CARD_DID_NOT_MOUNT) /* but remount failed */);
        ui_list_note(list, T(S_RE_INSERT_THE_CARD_OR_RESTART) /* Replug the card or reboot */);
        break;
    default:                             /* BROKEN: written, not finished */
        ui_list_note(list, T(S_THE_CARD_CANNOT_BE_USED_NOW) /* The card is unusable now */);
        ui_list_note(list, T(S_IT_CAN_BE_FORMATTED_AGAIN) /* You can format again */);
        break;
    }
}

static const char *result_title(void)
{
    switch (sd_format_outcome()) {
    case PC_FMT_OUT_DONE: return T(S_DONE) /* Done */;
    case PC_FMT_OUT_UNTOUCHED: return T(S_NOT_FORMATTED) /* Not formatted */;
    case PC_FMT_OUT_UNMOUNTED: return T(S_FORMATTED) /* Formatted */;
    default: return T(S_FORMAT_FAILED) /* Format failed */;
    }
}

static void flow_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    bool fresh = !s_stepping;
    s_stepping = false;
    s_list = s_pad = s_row = NULL;
    if (fresh) {
        if (sd_format_state() == SDF_RUN) s_view = V_RUN;
        else {
            const now_ops_t *o = player_ctl_now();
            if (o && o->stop) o->stop();                               /* nothing plays during the flow */
            bool sized = sd_format_probe(&s_bytes, &s_plan);
            pc_fmtguard_begin(&s_g, sd_format_reason(), g_sd_ok, usb_mode_active());
            if (!sized) pc_fmtguard_back(&s_g);                        /* no size, no plan: no session */
            s_view = V_GUARD;
        }
    }
    s_shown = s_g.step;
    s_built_ms = now_ms();
    char b[80], sz[16];
    size_text(sz, sizeof sz);
    if (s_view == V_RUN) {
        lv_label_set_text(ui_text(p, UI_SLOT_CENTER, NULL), T(S_FORMATTING_DO_NOT_PULL_THE_CARD) /* Formatting... do not remove the card */);
        ui_timer_bind(p, run_tick, 200, NULL);
    } else if (s_view == V_RESULT) {
        ui_obj_guard(ui_list_titled(p, result_title()), &s_list);
        result_notes(s_list);
        ui_back_button(p);
    } else if (s_g.step == PC_FG_WARN) {
        ui_obj_guard(ui_list_titled(p, T(S_FORMAT_THE_CARD_2) /* Format card */), &s_list);
        snprintf(b, sizeof b, "%s%s", T(S_SIZE) /* Size: */, sz);
        ui_list_note(s_list, b);
        snprintf(b, sizeof b, "%s%s", T(S_BECOMES_MBR) /* It becomes: MBR・ */, pc_fmt_fs_name(s_plan.fs));
        ui_list_note(s_list, b);
        ui_list_note(s_list, T(S_EVERYTHING_ON_THE_CARD_IS_ERASED) /* All data on the card will be lost */);
        ui_list_note(s_list, T(S_AND_CANNOT_BE_BROUGHT_BACK) /* Cannot be undone */);
        s_row = ui_list_row(s_list, T(S_CONTINUE) /* Continue */, UI_SYS, cont_cb, NULL);
        ui_list_row_centre(s_row);
        ui_back_button(p);
        ui_timer_bind(p, guard_tick, 500, NULL);
    } else if (s_g.step == PC_FG_CODE) {
        lv_label_set_text(ui_text(p, UI_SLOT_TITLE, NULL), T(S_CODE) /* Confirm code */);
        ui_obj_guard(ui_code_pad_create(p, PC_FG_CODE_LEN, key_cb), &s_pad);
        ui_code_pad_progress(s_pad, s_g.got);
        ui_back_button(p);
        ui_timer_bind(p, guard_tick, 500, NULL);
    } else if (s_g.step == PC_FG_ARM) {
        ui_obj_guard(ui_list_titled(p, T(S_LAST_CHECK) /* Final confirm */), &s_list);
        snprintf(b, sizeof b, "%s%s", T(S_LAST_CHECK_FORMAT) /* Final confirm: format  */, sz);
        s_row = ui_list_row_wrap(s_list, b, UI_SYS, arm_cb, NULL);
        ui_back_button(p);
        ui_timer_bind(p, guard_tick, 500, NULL);
    } else {                                                           /* no session: the card is mounted, gone, unreadable, or in the USB drive mode */
        ui_obj_guard(ui_list_titled(p, T(S_FORMAT_THE_CARD_2) /* Format card */), &s_list);
        ui_list_note(s_list, T(S_THIS_CARD_CANNOT_BE_FORMATTED_HERE) /* This card cannot be formatted here */);
        ui_back_button(p);
    }
    printf("APP cardfmt page=%s step=%s\n", s_view == V_RUN ? "run" : s_view == V_RESULT ? "result" : "guard", pc_fmtguard_step_name(s_g.step));
}

static void flow_destroy(void)
{
    lv_async_call_cancel(step_cb, NULL);
    if (!s_stepping) { pc_fmtguard_back(&s_g); if (s_view == V_RESULT) s_view = V_GUARD; }   /* Back (or home): the session ends, nothing formats */
    s_list = s_pad = s_row = NULL;
}

static void flow_suspend(void) { if (s_view == V_GUARD) pc_fmtguard_restart(&s_g); }   /* covered or dark: CODE / ARM start over at the warning */
static void flow_resume(void) { if (s_view == V_GUARD && s_g.step != s_shown) step(); }

static bool flow_knob(int d)
{
    if (s_view == V_GUARD && s_g.step == PC_FG_ARM) ui_arm_cancel();  /* turning cancels the armed row */
    return s_list ? ui_list_knob(s_list, d) : false;
}

const ui_screen_t scr_tfcardfmt = { .name = "cardfmt", .create = flow_create, .destroy = flow_destroy, .on_knob = flow_knob, .on_suspend = flow_suspend, .on_resume = flow_resume };

/* console "cardfmt": "" the state (APP cardfmt view= step= got= top= playing=<an owner of playback> row=<the flow's row text or -> then the service's APP sd format line), "open" (the card page), "row" (a tap on the
 * card page's Format row), "continue" (a tap on Continue), "arm" (a tap on the final row) */
void scr_tfcard_cmd(const char *a)
{
    const ui_screen_t *top = ui_top();
    if (!a[0]) {
        const char *row = top == &scr_tfcardfmt && s_row ? lv_label_get_text(lv_obj_get_child(s_row, 0)) : "-";
        printf("APP cardfmt view=%s step=%s got=%d top=%s playing=%d row=%s\n", s_view == V_RUN ? "run" : s_view == V_RESULT ? "result" : "guard", pc_fmtguard_step_name(s_g.step),
               s_g.got, top ? top->name : "-", player_ctl_now() != NULL, row);
        sd_format_print();
    } else if (!strcmp(a, "open")) ui_push(&scr_tfcard, NULL);
    else if (!strcmp(a, "row")) { if (top == &scr_tfcard && s_crow) lv_obj_send_event(s_crow, LV_EVENT_CLICKED, NULL); else printf("APP cardfmt no row\n"); }
    else if (!strcmp(a, "continue")) { if (top == &scr_tfcardfmt && s_view == V_GUARD && s_g.step == PC_FG_WARN && s_row) lv_obj_send_event(s_row, LV_EVENT_CLICKED, NULL); else printf("APP cardfmt no row\n"); }
    else if (!strcmp(a, "arm")) { if (top == &scr_tfcardfmt && s_view == V_GUARD && s_g.step == PC_FG_ARM && s_row) lv_obj_send_event(s_row, LV_EVENT_CLICKED, NULL); else printf("APP cardfmt no row\n"); }
    else printf("APP cardfmt unknown %s\n", a);
}
