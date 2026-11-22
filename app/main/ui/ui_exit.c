// SPDX-License-Identifier: LGPL-2.1-or-later
/* The shared exit (ui_exit.h). */
#include "ui_exit.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "font.h"
#include "ui.h"
#include "misc/lv_timer_private.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_frame.h"
#include "ui_theme.h"

#define A_MS        4000
#define BTN_W       UI_POP_BTN_W
#define MAX_FROZEN  24

static lv_obj_t *s_page, *s_a, *s_pause, *s_b_resume, *s_b_back;
static lv_timer_t *s_hide;
static const ui_exit_def_t *s_def;
static void *s_user;
static bool s_hold;
static bool s_framed;                          /* the page has a frame (ui_frame.h): the A pill is the Back of its overlay, which also shows the page's options */
static uint32_t s_t_pause;                    /* lv_tick at the pause */
static struct { lv_timer_t *t; lv_timer_cb_t cb; } s_frozen[MAX_FROZEN];   /* the timer and its callback: an address reused by another timer is not resumed */
static int s_nfrozen;

/* ---- the page's timers ---- */
static void freeze_cb(lv_timer_t *t, void *u)
{
    (void)u;
    if (t == s_hide || lv_timer_get_paused(t)) return;
    if (s_nfrozen >= MAX_FROZEN) { printf("APP exit warn too many timers to pause (max %d): one keeps running\n", MAX_FROZEN); return; }
    lv_timer_pause(t);
    s_frozen[s_nfrozen].t = t;
    s_frozen[s_nfrozen++].cb = t->timer_cb;
}
static bool alive(lv_timer_t *t, lv_timer_cb_t cb)
{
    for (lv_timer_t *x = lv_timer_get_next(NULL); x; x = lv_timer_get_next(x)) if (x == t) return t->timer_cb == cb && lv_timer_get_paused(t);
    return false;
}
static void thaw(void)
{
    for (int i = 0; i < s_nfrozen; i++)
        if (alive(s_frozen[i].t, s_frozen[i].cb)) { lv_timer_reset(s_frozen[i].t); lv_timer_resume(s_frozen[i].t); }
    s_nfrozen = 0;
}

/* ---- the A pill ---- */
static void hide_a(void)
{
    if (s_framed) { ui_frame_hide(s_page); return; }
    if (s_a) lv_obj_add_flag(s_a, LV_OBJ_FLAG_HIDDEN);
    if (s_hide) lv_timer_pause(s_hide);
}
static void hide_cb(lv_timer_t *t) { (void)t; hide_a(); }
static void show_a(void)
{
    if (!s_a) return;
    if (s_framed) { ui_frame_show(s_page, s_hold, false); return; }
    lv_obj_remove_flag(s_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_a);
    if (s_hide && !s_hold) { lv_timer_reset(s_hide); lv_timer_resume(s_hide); }
    else if (s_hide) lv_timer_pause(s_hide);
}
void ui_exit_volume_turn(void)
{
    if (!s_page || s_pause || !ui_screen_on() || ui_top_root() != s_page) return;
    show_a();
}

/* ---- the pause overlay ---- */
static void back_cb(lv_event_t *e) { (void)e; ui_pop(); }

static void resume_now(void)
{
    if (!s_pause) return;
    lv_obj_t *o = s_pause, *rb = s_b_resume, *bb = s_b_back;
    s_pause = s_b_resume = s_b_back = NULL;
    if (s_framed) { ui_frame_exit_items(s_page, NULL, NULL); ui_frame_hide(s_page); if (rb) lv_obj_delete(rb); if (bb) lv_obj_delete(bb); }   /* the pills are children of the page then */
    lv_obj_delete(o);
    if (s_framed) ui_frame_hide(s_page);
    thaw();
    if (s_def && s_def->resume) s_def->resume(s_user, lv_tick_elaps(s_t_pause));
    printf("APP exit resumed\n");
}
static void resume_cb(lv_event_t *e) { (void)e; ui_haptic_tap(); resume_now(); }

static lv_obj_t *pill(lv_obj_t *parent, int y, const char *text, lv_color_t bg, lv_event_cb_t cb)
{
    lv_obj_t *l;
    lv_obj_t *b = ui_button(parent, BTN_W, UI_BACK_H, 0, y, cb, NULL, &l);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_label_set_text(l, text);
    return b;
}

static void pause_open(void)
{
    if (!s_page || s_pause || ui_top_root() != s_page) return;
    hide_a();
    s_t_pause = lv_tick_get();
    if (s_def && s_def->pause) s_def->pause(s_user);
    s_nfrozen = 0;
    ui_top_timers(freeze_cb, NULL);
    lv_obj_t *o = lv_obj_create(s_page);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_pct(100), lv_pct(100));
    lv_obj_center(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);            /* the content is not dimmed: the shared popup card (ui_frame_card) holds Continue / Back */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);               /* the scrim takes every touch: nothing reaches the field */
    ui_obj_guard(o, &s_pause);
    if (!s_framed) ui_frame_card(o, UI_POP_PAUSE_W, UI_POP_PAUSE_H, UI_POP_PAUSE_Y);   /* a framed page has ONE card: its options, Continue and Back (ui_frame_exit_items) */
    s_b_resume = pill(s_framed ? s_page : o, UI_POP_PAUSE_Y - (UI_BACK_H + UI_POP_GAP) / 2, T(S_RESUME) /* Continue */, UI_SEL, resume_cb);
    s_b_back = pill(s_framed ? s_page : o, UI_POP_PAUSE_Y + (UI_BACK_H + UI_POP_GAP) / 2, T(S_LEAVE) /* Leave */, UI_SYS, back_cb);
    ui_obj_guard(s_b_resume, &s_b_resume);
    ui_obj_guard(s_b_back, &s_b_back);
    if (s_framed) { ui_frame_exit_items(s_page, s_b_resume, s_b_back); ui_frame_show(s_page, true, true); }   /* the page's options and 說明, Continue and Back: one card */
    ui_haptic_tap();
    printf("APP exit paused frozen=%d\n", s_nfrozen);
}
bool ui_exit_paused(void) { return s_pause != NULL; }

EXT_RAM_BSS_ATTR static int s_mode_ovr;                         /* console `exit mode a|b|c`: mode + 1 (0 = none; zero-init: PSRAM bss); tests run any page in any mode */
static ui_exit_mode_t cur_mode(void) { return s_mode_ovr > 0 ? (ui_exit_mode_t)(s_mode_ovr - 1) : s_def->mode; }
/* CF2: a round is in flight (the page's running hook; a page without one: never): a board without a knob sends its keys to the game then (ui.c keys_idle) */
bool ui_exit_in_flight(void) { return s_page && s_def && s_def->running && s_def->running(s_user); }
/* a touch on the field (CF2): A (taps unused): the popup shows; B (knob borrowed, taps unused): while the game runs it PAUSES and the popup (options above Continue / Back) shows;
 * C (knob and taps both used): nothing (the popup waits for a free knob). true = the touch is consumed */
bool ui_exit_tap(void)
{
    if (!s_page || !s_def || s_pause || ui_top_root() != s_page) return false;
    if (cur_mode() == UI_EXIT_A) return s_framed && ui_frame_content_tap(s_page);
    if (cur_mode() != UI_EXIT_B) return false;
    if (s_def->running && !s_def->running(s_user)) return false;
    pause_open();
    return s_pause != NULL;
}

/* ---- attach ---- */
static void page_deleted(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) != s_page) return;
    s_page = s_a = s_pause = s_b_resume = s_b_back = NULL;
    s_framed = false;
    s_mode_ovr = 0;
    s_hide = NULL;                                           /* bound to the pill: deleted with it */
    s_nfrozen = 0;
    s_def = NULL;
}
lv_obj_t *ui_exit_attach(lv_obj_t *page, const ui_exit_def_t *def, void *user)
{
    s_page = page;
    s_def = def;
    s_user = user;
    s_pause = s_b_resume = s_b_back = s_a = NULL;
    s_mode_ovr = 0;
    s_hide = NULL;
    s_nfrozen = 0;
    s_hold = false;
    s_framed = false;
    lv_obj_add_event_cb(page, page_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *l;
    lv_obj_t *a = ui_button(page, UI_BACK_W, UI_BACK_H, 0, UI_BACK_Y, back_cb, NULL, &l);
    lv_obj_set_style_bg_color(a, UI_SYS, 0);
    lv_label_set_text(l, T(S_LEAVE) /* Leave: re-set below when the page has no frame */);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    ui_obj_guard(a, &s_a);
    s_hide = ui_timer_bind(a, hide_cb, A_MS, NULL);
    if (s_hide) lv_timer_pause(s_hide);
    s_framed = ui_frame_adopt_exit(page, a);
    if (!s_framed) lv_label_set_text(l, T(S_BACK) /* Back: a page without the frame shows it as its plain exit */);
    return a;
}

/* ---- console ---- */
static lv_obj_t *button_of(const char *w)
{
    if (!strcmp(w, "back") || !strcmp(w, "返回")) return s_pause ? s_b_back : s_a;
    if (!strcmp(w, "resume") || !strcmp(w, "繼續")) return s_b_resume;
    return NULL;
}
static void dump(void)
{
    printf("APP exit attached=%d mode=%c a=%d pause=%d frozen=%d hold=%d\n", s_page != NULL, s_def ? "ABC"[cur_mode()] : '-',
           s_a && !lv_obj_has_flag(s_a, LV_OBJ_FLAG_HIDDEN), s_pause != NULL, s_nfrozen, s_hold);
}
void ui_exit_cmd(const char *arg)
{
    char w[24], m;
    int v;
    if (!arg[0]) { dump(); return; }
    if (!strcmp(arg, "show")) { s_hold = true; show_a(); }
    else if (!strcmp(arg, "hide")) hide_a();
    else if (sscanf(arg, "hold %d", &v) == 1) s_hold = v != 0;
    else if (sscanf(arg, "mode %c", &m) == 1 && m >= 'a' && m <= 'c') s_mode_ovr = m - 'a' + 1;
    else if (!strcmp(arg, "tap field")) { printf("APP exit field %d\n", ui_exit_tap()); return; }
    else if (!strcmp(arg, "pause")) pause_open();
    else if (!strcmp(arg, "resume")) resume_now();
    else if (sscanf(arg, "tap %23s", w) == 1) {
        lv_obj_t *b = button_of(w);
        if (!b || lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN)) { printf("APP exit tap none %s\n", w); return; }
        lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
    }
    else if (sscanf(arg, "at %23s", w) == 1) {
        lv_obj_t *b = button_of(w);
        if (!b) { printf("APP exit at none %s\n", w); return; }
        lv_area_t o;
        lv_obj_update_layout(b);
        lv_obj_get_coords(b, &o);
        printf("APP exit at %s x=%d,y=%d\n", w, (int)((o.x1 + o.x2) / 2), (int)((o.y1 + o.y2) / 2));
        return;
    }
    else { printf("APP exit unknown %s\n", arg); return; }
    dump();
}
