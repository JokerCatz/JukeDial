// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ui.h"
#include "ui_geom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_knob.h"
#include "bt_svc.h"
#include "cfg.h"
#include "lab.h"
#include "crashlog.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "font.h"
#include "screens.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "ui_ring.h"
#include "ui_popring.h"
#include "ui_ninegrid.h"
#include "ui_exit.h"
#include "ui_frame.h"
#include "ui_arena.h"
#include "ui_qs.h"
#include "memlog.h"
#include "pc/pc_pwrpol.h"
#include "pc/pc_volume.h"
#include "pc/pc_wake.h"
#include "psram_alloc.h"
#include "power_svc.h"
#include "display/lv_display_private.h"
#include "indev/lv_indev_private.h"
#include "misc/lv_area_private.h"
#include "misc/lv_timer_private.h"

#define STACK_MAX 8
/* Leak tripwire snapshot, taken before the layer is created: the infinite timers that exist (tm0[n0]) and the object
 * count. Timers are kept as a set, not a count: a lower screen's timer may end by itself while this layer is open
 * (LED rim fade, dice roll), which must not look like a leak of this layer. */
EXT_RAM_BSS_ATTR static struct { const ui_screen_t *s; lv_obj_t *root; void *arg; lv_timer_t **tm0; int16_t n0, objs0; lv_obj_t *back; } s_stack[STACK_MAX];   /* back: the page's Back control (hidden, GS: the up flick runs it), cleared when it is deleted */
static int s_depth;
EXT_RAM_BSS_ATTR static int s_build = -1;                  /* the layer whose create() runs now (a rebuild makes every layer in turn): ui_back_set belongs to it */
static lv_indev_t *s_touch;
static pc_wake_t s_wake = { .screen_on = true };
static int64_t s_last_input_us;
static lv_obj_t *s_vol_arc;
static lv_timer_t *s_vol_hide;
void (*ui_volume_hook)(int vol);
static const ui_host_t k_player_host = { .name = "player", .cfg_writes = true, .haptics = true };
static const ui_host_t *s_host = &k_player_host;
static bool s_underlay;                   /* the host's underlay exists: page roots are transparent */

/* DRV2605L ROM library, lengths measured on the board (console hapticlen): 1-3 strong click 65 ms,
 * 4-6 sharp click 45 ms, 24/25/26 sharp tick 35/30/25 ms. User 2026-11-09: about 2/3 as long as before
 * (tap was a 65 ms strong click, the knob a 45 ms sharp click) -> tap = sharp click, knob = sharp tick. */
/* Strength as the drive limit itself (user 2026-11-09: show millivolts, not 0-3; 1280 mV + 8 ms feels right):
 * OD_CLAMP (reg 0x17) is ~21.33 mV a step on this open-loop LRA. cfg haptic_mv, 0 = off. */
static void haptic_stop_cb(void *arg) { (void)arg; bsp_haptic_stop(); }

static void haptic_len(int effect, int ms);
static void haptic(int effect) { haptic_len(effect, cfg_int("haptic_ms", 8)); }
static void haptic_len(int effect, int ms)
{
    if (!s_host->haptics) return;
    int mv = cfg_haptic_mv();
    if (mv <= 0) return;
    static int applied = -1;
    if (mv != applied) { bsp_haptic_set_clamp((uint8_t)((mv * 100 + 1066) / 2133)); applied = mv; }
    bsp_haptic_play((uint8_t)effect);
    /* length (user 2026-11-09: still felt as long as before): the ROM effects are 30-45 ms, so cut them short */
    static esp_timer_handle_t stop;
    if (!stop) {
        const esp_timer_create_args_t a = { .callback = haptic_stop_cb, .name = "haptic" };
        esp_timer_create(&a, &stop);
    }
    if (stop && ms > 0) { esp_timer_stop(stop); esp_timer_start_once(stop, (uint64_t)ms * 1000); }
}
void ui_haptic_tap(void) { haptic(4); }    /* sharp click 100 %, 45 ms */
void ui_haptic_tick(void) { haptic(25); }  /* sharp tick 80 %, 30 ms */
void ui_haptic_strong(void) { int ms = cfg_int("haptic_ms", 8); haptic_len(1, ms > 0 ? ms * 5 / 2 : 0); }   /* strong click 100 %, 2.5x as long */

void ui_activity(void) { s_last_input_us = esp_timer_get_time(); }
uint32_t ui_input_idle_ms(void)
{
    uint32_t idle_ms = (uint32_t)((esp_timer_get_time() - s_last_input_us) / 1000);
    const uint32_t lv_idle = lv_display_get_inactive_time(NULL);
    return lv_idle < idle_ms ? lv_idle : idle_ms;     /* the screen-timeout's own measure: touch, keys and knob */
}
bool ui_screen_on(void) { return s_wake.screen_on; }

static void drag_end(void);
static void hold_stop(void);
/* The live backlight level of a lit screen: the user's saved brightness, or 100 while the page that asked for full light (the torch) is the top one.
 * Only the live value is ever raised: cfg "brightness" is not touched, so a reboot, the quick settings over the page and every way out show / restore the real one. */
EXT_RAM_BSS_ATTR static const ui_screen_t *s_full_owner;
static uint8_t backlight_level(void) { return s_full_owner && s_depth && s_stack[s_depth - 1].s == s_full_owner ? 100 : (uint8_t)cfg_brightness(); }
void ui_backlight_apply(void) { if (s_wake.screen_on) bsp_display_backlight_set(backlight_level()); }
void ui_backlight_full(const ui_screen_t *owner) { s_full_owner = owner; ui_backlight_apply(); }
static void backlight(bool on)
{
    bsp_display_backlight_set(on ? backlight_level() : 0);
    if (s_touch) lv_indev_enable(s_touch, on);    /* touch never wakes, and blind taps do nothing */
    if (on) ui_arena_kick();                       /* a toy field (ambient scenes) goes on by itself */
    if (!on) {
        drag_end();                                 /* a touch in progress: no drag timer through the dark period */
        hold_stop();                                /* ... and no hold: a pocket touch must not take the player home in the dark */
        ui_popring_close();                         /* the screen is dark: the ring has nothing to show */
        if (s_depth && s_stack[s_depth - 1].s->on_suspend) s_stack[s_depth - 1].s->on_suspend();   /* the page is not seen any more */
        pc_wake_screen_off(&s_wake);
        bsp_display_release_dma();                  /* dark: the last frame's DMA buffers back */
    }
    printf("APP screen %s\n", on ? "on" : "off");
}

void ui_sleep(void) { if (s_wake.screen_on) backlight(false); }   /* console / tests: as the idle timeout does */

void ui_wake(void)                       /* console / tests: as if the knob was turned once */
{
    ui_activity();
    if (!s_wake.screen_on) { pc_wake_on_knob(&s_wake, 1, esp_timer_get_time() / 1000); backlight(true); }
}

static void vol_hide_cb(lv_timer_t *t) { (void)t; lv_obj_add_flag(s_vol_arc, LV_OBJ_FLAG_HIDDEN); }

static void show_volume(int v)
{
#if UI_ROUND
    lv_arc_set_value(s_vol_arc, v);
#else
    lv_bar_set_value(lv_obj_get_child(s_vol_arc, 1), v, LV_ANIM_OFF);
    lv_label_set_text_fmt(lv_obj_get_child(s_vol_arc, 2), "%d", v);
#endif
    lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_vol_arc);
    lv_timer_reset(s_vol_hide);
    lv_timer_resume(s_vol_hide);
}

/* CF2: a board without a knob (the FakePod): the keys are a content page's own input only for KEYS_MS after the last touch or the last key it took (the analogue of the
 * 4 s borrow of the knob); later a key turn falls to the volume (and shows the popup) exactly as a free knob does */
#define KEYS_MS 4000
EXT_RAM_BSS_ATTR static uint32_t s_page_in_ms;
static bool keys_idle(void)
{
    if (UI_HAS_KNOB || !s_depth || !s_stack[s_depth - 1].s->content) return false;
    if (ui_exit_in_flight()) return false;               /* a round is on: keys are the game's, whatever the pause since the last key */
    return lv_tick_elaps(s_page_in_ms) >= KEYS_MS;
}
/* Runs inside the LVGL indev read (lock held). Always consumes: focus navigation is not used. */
static bool knob_hook(int32_t diff)
{
    bool was_on = s_wake.screen_on;
    int d = pc_wake_on_knob(&s_wake, (int)diff, esp_timer_get_time() / 1000);   /* screen off: wakes, then 400 ms lockout */
    ui_activity();
    if (!was_on) { backlight(true); return true; }
    if (d == 0) return true;
    ui_haptic_tick();
    const bool under_overlay = ui_exit_paused();      /* G4b: under the shared exit's pause overlay the knob and the keys are the volume only: no game move, no ring (CF2: except an option of the popup the user borrowed) */
    if (under_overlay && ui_frame_axis_held() && ui_axis_knob(d)) return true;
    const bool keys_free = keys_idle();
    if (keys_free && ui_axis_knob(d)) { s_page_in_ms = lv_tick_get(); return true; }   /* a borrowed axis (an option of the popup, a ring) is the page's whatever the age of the last key */
    if (!under_overlay && !keys_free && s_depth && s_stack[s_depth - 1].s->on_knob && s_stack[s_depth - 1].s->on_knob(d)) { s_page_in_ms = lv_tick_get(); return true; }
    if (!under_overlay && s_depth && ui_ring_knob_in(s_stack[s_depth - 1].root, d)) return true;   /* a ring with several pages: the knob turns its page (ui_ring.h) */
    if (!under_overlay && s_host->knob_default && s_host->knob_default(d)) return true;          /* the mode's own knob (the instrument's volume) */
    int v = pc_volume_step(s_host->volume ? s_host->volume() : app_volume(), d, cfg_int("volume_max", 100));
    app_set_volume(v);
    show_volume(v);
    if (ui_volume_hook) ui_volume_hook(v);
    ui_exit_volume_turn();                            /* a page with a shared exit (ui_exit.h) shows its 返回 pill */
    ui_frame_volume_turn();                           /* a page with a frame (ui_frame.h) shows its overlay */
    return true;
}

void ui_knob_inject(int detents) { knob_hook(detents); }
/* F2: the FakePod blind layer (keys_svc, screen off): the knob's last branch only, one volume step by `d` detents, without waking the screen or drawing the arc */
void ui_volume_step_dark(int d)
{
    const int v = pc_volume_step(s_host->volume ? s_host->volume() : app_volume(), d, cfg_int("volume_max", 100));
    app_set_volume(v);
    if (ui_volume_hook) ui_volume_hook(v);
}
void ui_volume_shown(int v) { if (s_vol_arc) show_volume(v); }

#if BSP_PANEL_OLED
static uint32_t s_press_ms;   /* tick at which the current touch began; a stuck touch long enough = the screen goes off */
static bool s_pressed;
#endif
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_host->cfg_writes) cfg_tick();
    uint32_t idle_ms = (uint32_t)((esp_timer_get_time() - s_last_input_us) / 1000);
    uint32_t lv_idle = lv_display_get_inactive_time(NULL);
    if (lv_idle < idle_ms) idle_ms = lv_idle;
    uint32_t limit = s_host->idle_ms ? s_host->idle_ms() : pc_screen_timeout_ms(power_svc_state());
#if BSP_PANEL_OLED
    limit = pc_oled_idle_limit(limit);                 /* OLED: no host may keep the screen lit for ever (the instrument's 0 = never becomes the maximum) */
    if (s_touch && s_wake.screen_on) {                  /* a touch held down for minutes (stuck panel) is not input: it must not hold the picture lit */
        if (lv_indev_get_state(s_touch) == LV_INDEV_STATE_PRESSED) { if (!s_pressed) { s_pressed = true; s_press_ms = lv_tick_get(); } if (pc_oled_press_stuck(lv_tick_elaps(s_press_ms))) idle_ms = limit + 1; }
        else s_pressed = false;
    }
#endif
    if (s_wake.screen_on && (limit || !s_host->idle_ms) && idle_ms > limit) backlight(false);   /* a host's 0 = never */
    static int ticks;
    if (++ticks % 150 == 0) memlog("periodic");   /* every 30 s */
    crashlog_crumb(ui_top() ? ui_top()->name : NULL, (int)bt_svc_mode(), 0);   /* src: no source-kind query yet */
}

static void any_click_cb(lv_event_t *e) { (void)e; ui_activity(); ui_haptic_tap(); s_page_in_ms = lv_tick_get(); }

/* GS: a slow drag turns nothing (the page turn is the fast left / right flick below). A touch that began on a ring and moves off its spot taps nothing wherever it ends
 * (wait_release); that is all this timer is for. */
EXT_RAM_BSS_ATTR static lv_indev_t *s_drag_indev;
EXT_RAM_BSS_ATTR static lv_point_t s_drag_p0;
EXT_RAM_BSS_ATTR static lv_obj_t *s_drag_ring;                         /* the ring the touch began on, and the stack depth then: a touch-down item that pushed a page ends the drag */
EXT_RAM_BSS_ATTR static int s_drag_depth;
EXT_RAM_BSS_ATTR static lv_timer_t *s_drag_t;                         /* alive only while a touch that began on a ring is down (LVGL sends PRESSING to the object, not to the indev) */
static void drag_end(void) { if (s_drag_t) { lv_timer_delete(s_drag_t); s_drag_t = NULL; } }
static void drag_tick(lv_timer_t *t)
{
    (void)t;
    lv_indev_t *i = s_drag_indev;
    lv_obj_t *r = i && s_depth && lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED ? ui_top_ring() : NULL;
    if (!r || r != s_drag_ring || s_depth != s_drag_depth) { drag_end(); return; }
    lv_point_t p;
    lv_indev_get_point(i, &p);
    if (abs(p.x - s_drag_p0.x) >= UI_TAP_GAP * 2 || abs(p.y - s_drag_p0.y) >= UI_TAP_GAP * 2) {   /* moved off its spot: no tap and no long press from here, wherever the finger ends */
        drag_end();
        lv_indev_wait_release(i);
    }
}
static void drag_cb(lv_event_t *e)
{
    lv_indev_t *i = lv_indev_active();
    if (!i) return;
    drag_end();
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    lv_point_t p;
    lv_indev_get_point(i, &p);
    lv_obj_t *r = s_depth && !ui_exit_paused() ? ui_top_ring() : NULL;
    if (!r) return;
    s_drag_indev = i;
    s_drag_p0 = p;
    s_drag_ring = r;
    s_drag_depth = s_depth;
    s_drag_t = lv_timer_create(drag_tick, LV_DEF_REFR_PERIOD, NULL);
}

static lv_obj_t *new_root(const ui_screen_t *sc)
{
    lv_obj_t *r = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, UI_W, UI_H);
    if (!s_underlay) {                    /* with an underlay the root is transparent: it shows through (the screen's own background is UI_BG) */
        lv_obj_set_style_bg_color(r, UI_BG, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_text_color(r, UI_FG, 0);
    lv_obj_set_style_text_font(r, font_main(), 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (sc->content) ui_frame_enable(r);  /* CF2: a content page (both panels) has the content-first frame (ui_frame.h); list / menu pages never */
    return r;
}

/* Live LVGL timers that repeat forever (a screen's own, not one-shots) and the objects on the active screen and
 * the top layer. Anything a screen leaves behind after destroy shows up as a difference. */
EXT_RAM_BSS_ATTR static lv_obj_t *s_strip;                      /* the square panel's status strip (see "the status strip" below) */
static bool forever(const lv_timer_t *t) { return t->repeat_count == -1; }

static void census(int *timers, int *objs)
{
    int n = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) if (forever(t)) n++;
    *timers = n;
    *objs = (int)(lv_obj_get_child_count(lv_screen_active()) + lv_obj_get_child_count(lv_layer_top())) - (s_strip ? 1 : 0);   /* the status strip is not a page's */
}

static void leak_report(const char *name, int timers, int objs)
{
    if (timers || objs) printf("APP ui leak screen=%s timers=%+d objs=%+d\n", name, timers, objs);
}

/* the snapshot of the layer about to be created at s_stack[d] */
static void snap_take(int d)
{
    int n, o;
    census(&n, &o);
    lv_timer_t **a = n ? psram_malloc((size_t)n * sizeof *a) : NULL;
    int i = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); a && t && i < n; t = lv_timer_get_next(t)) if (forever(t)) a[i++] = t;
    s_stack[d].tm0 = a;
    s_stack[d].n0 = (int16_t)(a ? n : 0);
    s_stack[d].objs0 = (int16_t)o;
}

static bool snapped(int d, const lv_timer_t *t)
{
    for (int i = 0; i < s_stack[d].n0; i++) if (s_stack[d].tm0[i] == t) return true;
    return false;
}
/* every repeating timer the top page made (not in its push-time snapshot): the shared exit pauses them under its overlay */
void ui_top_timers(void (*fn)(lv_timer_t *, void *), void *u)
{
    if (!s_depth) return;
    const int d = s_depth - 1;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t))
        if (forever(t) && !snapped(d, t)) fn(t, u);
}
static void snap_free(int d) { psram_free(s_stack[d].tm0); s_stack[d].tm0 = NULL; s_stack[d].n0 = 0; }

/* ui_pop, after layer d is gone: timers that did not exist before it and still do, objects beyond the snapshot.
 * Known limit: a timer that a LOWER screen started while d was on top counts as d's (single-layer view). */
static void snap_check(int d)
{
    int n, o, extra = 0;
    census(&n, &o);
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t))
        if (forever(t) && !snapped(d, t)) extra++;
    leak_report(s_stack[d].s->name, extra, o - s_stack[d].objs0);
}

/* console uiboxes: the top page's labels, buttons and QR codes with their screen boxes (layout tests); a label inside a button is not listed.
 * `uiboxes clip` adds the visible part (x1 y1 x2 y2 after the box: the box cut by every ancestor's box); an object that is not visible at all is not printed. */
static void boxes_walk(lv_obj_t *o, bool clip, const lv_area_t *vis)
{
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        bool lbl = lv_obj_check_type(c, &lv_label_class), btn = lv_obj_check_type(c, &lv_button_class), qr = lv_obj_check_type(c, &lv_qrcode_class);
        lv_area_t a, v = *vis;
        lv_obj_get_coords(c, &a);
        bool seen = !clip || lv_area_intersect(&v, vis, &a);
        if (!seen) continue;                                   /* nothing of it (or of its children) is visible */
        if (lbl || btn || qr) {
            const char *txt = lbl ? lv_label_get_text(c) : btn && lv_obj_get_child_count(c) && lv_obj_check_type(lv_obj_get_child(c, 0), &lv_label_class) ? lv_label_get_text(lv_obj_get_child(c, 0)) : "";
            printf("APP uibox %s %d %d %d %d ", lbl ? "label" : btn ? "button" : "qr", (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
            if (clip) printf("%d %d %d %d ", (int)v.x1, (int)v.y1, (int)v.x2, (int)v.y2);
            putchar('|');
            for (const char *q = txt; *q; q++) putchar(*q == '\n' ? '/' : *q);
            putchar('\n');
        }
        if (!btn) boxes_walk(c, clip, &v);
    }
}
static void boxes(bool clip)
{
    lv_obj_t *root = s_depth ? s_stack[s_depth - 1].root : lv_screen_active();   /* the ui stack is empty in the instrument and the safe screen: their screen is the active one */
    lv_obj_update_layout(root);
    lv_area_t vis;
    lv_obj_get_coords(root, &vis);
    boxes_walk(root, clip, &vis);
    printf("APP uiboxes end\n");
}
/* console uievents: the LVGL event descriptors summed over every object of every page on the stack (a test that a revisit adds no callback) */
static uint32_t ev_walk(lv_obj_t *o)
{
    uint32_t n = lv_obj_get_event_count(o);
    for (uint32_t i = 0, c = lv_obj_get_child_count(o); i < c; i++) n += ev_walk(lv_obj_get_child(o, (int32_t)i));
    return n;
}
void ui_events(void)
{
    uint32_t n = 0;
    for (int i = 0; i < s_depth; i++) n += ev_walk(s_stack[i].root);
    printf("APP uievents %u\n", (unsigned)n);
}
void ui_boxes(void) { boxes(false); }
void ui_boxes_clip(void) { boxes(true); }

const ui_screen_t *ui_top(void) { return s_depth ? s_stack[s_depth - 1].s : NULL; }
bool ui_visual_paused(lv_obj_t *page_root)
{
    return !s_wake.screen_on || ui_popring_ring() || ui_ninegrid_shown() || (page_root && ui_top_root() != page_root);
}
lv_obj_t *ui_top_root(void) { return s_depth ? s_stack[s_depth - 1].root : NULL; }
const ui_screen_t *ui_layer(int i) { return i >= 0 && i < s_depth ? s_stack[i].s : NULL; }
bool ui_on_stack(const ui_screen_t *s)
{
    for (int i = 0; i < s_depth; i++) if (s_stack[i].s == s) return true;
    return false;
}

void ui_push(const ui_screen_t *s, void *arg)
{
    ui_popring_close();                   /* a tap-revealed ring belongs to the page that is about to be covered */
    if (s_depth == STACK_MAX) { printf("APP ui stack full\n"); return; }
    snap_take(s_depth);                   /* before new_root: the new layer's root is one of the objects it must give back */
    if (s_depth) {
        if (s_stack[s_depth - 1].s->on_suspend) s_stack[s_depth - 1].s->on_suspend();
        lv_obj_add_flag(s_stack[s_depth - 1].root, LV_OBJ_FLAG_HIDDEN);
    }
    s_page_in_ms = lv_tick_get();
    lv_obj_t *r = new_root(s);
    s_stack[s_depth].s = s;
    s_stack[s_depth].root = r;
    s_stack[s_depth].arg = arg;
    s_depth++;
    s_stack[s_depth - 1].back = NULL;
    s_build = s_depth - 1;
    s->create(r, arg);
    s_build = -1;
    if (s_vol_arc) lv_obj_move_foreground(s_vol_arc);
    printf("APP screen push=%s depth=%d\n", s->name, s_depth);
    memlog(s->name);
    ui_backlight_apply();                 /* a page over the torch shows the real brightness */
}

static void pop_one(bool resume)          /* the top page goes (destroy, delete, leak check); the one under it is shown and, with `resume`, told */
{
    s_depth--;
    if (s_stack[s_depth].s->destroy) s_stack[s_depth].s->destroy();
    lv_obj_delete(s_stack[s_depth].root);
    snap_check(s_depth);
    snap_free(s_depth);
    lv_obj_remove_flag(s_stack[s_depth - 1].root, LV_OBJ_FLAG_HIDDEN);
    memlog(s_stack[s_depth - 1].s->name);
    if (resume && s_stack[s_depth - 1].s->on_resume) s_stack[s_depth - 1].s->on_resume();
    ui_backlight_apply();                 /* the torch is on top again: full light; a page without the request: the saved brightness */
}

void ui_pop(void) { drag_end(); if (s_depth > 1) pop_one(true); }

/* the pages in between are not resumed: they were never seen on the way (a cover page among them must not start leaving); only `scr` is,
 * once, and only when something was popped */
bool ui_pop_to(const ui_screen_t *scr)
{
    int at = s_depth - 1;
    while (at >= 0 && s_stack[at].s != scr) at--;
    if (at < 0) return false;
    if (at == s_depth - 1) return true;
    while (s_depth - 1 > at + 1) pop_one(false);
    pop_one(true);
    return true;
}

void ui_open_once(const ui_screen_t *s, void *arg)
{
    if (ui_pop_to(s)) ui_replace(s, arg);
    else ui_push(s, arg);
}

/* ui_rebuild_all, after every layer is destroyed and deleted: what still exists beyond the root's push-time baseline
 * leaked. A forever timer belongs to the layer that was on top when it appeared: absent from that layer's snapshot and
 * present in the next one's (the top layer: absent from its own). Objects are only counted: the non-root objects at
 * layer i's push (objs0[i] minus the i roots below it) grow to the next layer's push (or to now, for the top); each
 * layer, top down, is given that growth, clamped to what is still unattributed (an object a lower screen made and then
 * deleted itself must not be charged). */
static void rebuild_check(void)
{
    int n, o;
    census(&n, &o);
    int left = o - s_stack[0].objs0;                 /* every root is gone: anything above the root baseline leaked */
    for (int i = s_depth - 1; i >= 0; i--) {
        bool top = i == s_depth - 1;
        int extra = 0;
        for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t))
            if (forever(t) && !snapped(i, t) && (top || snapped(i + 1, t))) extra++;
        int grew = (top ? o : s_stack[i + 1].objs0 - (i + 1)) - (s_stack[i].objs0 - i);
        int objs = grew < 0 || left < 0 ? 0 : grew > left ? left : grew;
        left -= objs;
        leak_report(s_stack[i].s->name, extra, objs);
    }
}

void ui_rebuild_all(void)                /* after a theme change: every screen again, same args */
{
    ui_popring_close();
    for (int i = s_depth - 1; i >= 0; i--) {
        if (s_stack[i].s->destroy) s_stack[i].s->destroy();
        lv_obj_delete(s_stack[i].root);
    }
    rebuild_check();                             /* only now: a lower screen's own timers are gone with its destroy */
    for (int i = 0; i < s_depth; i++) snap_free(i);
    for (int i = 0; i < s_depth; i++) {
        snap_take(i);                            /* every timer is a new one now: snapshot again, layer by layer */
        s_stack[i].root = new_root(s_stack[i].s);
        s_stack[i].back = NULL;
        s_build = i;
        s_stack[i].s->create(s_stack[i].root, s_stack[i].arg);
        s_build = -1;
        if (i < s_depth - 1) lv_obj_add_flag(s_stack[i].root, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_vol_arc) {
#if UI_ROUND
        lv_obj_set_style_arc_color(s_vol_arc, UI_A1, LV_PART_INDICATOR);
#else
        lv_obj_set_style_bg_color(s_vol_arc, UI_PANEL, 0);
        lv_obj_set_style_bg_color(lv_obj_get_child(s_vol_arc, 1), UI_A1, LV_PART_INDICATOR);
#endif
        lv_obj_move_foreground(s_vol_arc);
    }
    printf("APP ui rebuilt theme=%s depth=%d\n", ui_theme()->name, s_depth);
}

void ui_home(void) { while (s_depth > 1) ui_pop(); }

void ui_home_by_long_press(void)
{
    ui_haptic_tick();
    drag_end();                                       /* the press that began on the ring may have a drag timer running */
    lv_indev_t *i = lv_indev_active();
    if (!i) i = s_touch;
    if (i) lv_indev_wait_release(i);                  /* the release must neither click the Back that is gone nor touch a deleted ring */
    ui_home();
}

/* CF2: the escape hatch's hold time: cfg home_hold_ms (2000 / 3000 / 4000, default 3000), not LVGL's 400 ms. A press that starts on a content page (anywhere) or on a Back
 * pill / ring Back cell (ui_hold_start) goes home when it has been held that long without moving off its spot; one haptic tick at that moment, the release does nothing. */
EXT_RAM_BSS_ATTR static bool (*s_gest_veto)(lv_point_t);   /* I3: a page whose controls are held (the instrument pads): a press it vetoes neither starts the hold-home nor a flick */
EXT_RAM_BSS_ATTR static bool (*s_gest_hook)(ui_gesture_t);   /* GS: the page that wants every gesture first (the tutorial) */
EXT_RAM_BSS_ATTR static lv_timer_t *s_hold_t;
EXT_RAM_BSS_ATTR static lv_point_t s_hold_p0;
EXT_RAM_BSS_ATTR static uint32_t s_hold_t0, s_hold_ms;
static void hold_stop(void) { if (s_hold_t) { lv_timer_delete(s_hold_t); s_hold_t = NULL; } }
/* every LV_DEF_REFR_PERIOD while the finger is down (the indev sends no PRESSING to the indev: the same reason as drag_tick): the finger lifted, the screen went dark or it left its spot
 * (>= 2 UI_TAP_GAP: sticky, it never comes back) ends the hold; the time up goes home. A finite repeat count (not -1): the leak census counts only endless timers. */
static void hold_tick(lv_timer_t *t)
{
    lv_indev_t *i = s_touch;
    if (t->repeat_count <= 1) s_hold_t = NULL;           /* the last run: LVGL deletes the timer after this one */
    if (!i || !ui_screen_on() || lv_indev_get_state(i) != LV_INDEV_STATE_PRESSED) { if (s_hold_t) hold_stop(); return; }
    lv_point_t p;
    lv_indev_get_point(i, &p);
    if (abs(p.x - s_hold_p0.x) >= UI_TAP_GAP * 2 || abs(p.y - s_hold_p0.y) >= UI_TAP_GAP * 2) { if (s_hold_t) hold_stop(); return; }   /* a drag is no hold */
    if (lv_tick_elaps(s_hold_t0) < s_hold_ms) return;
    hold_stop();
    if (s_gest_hook && s_gest_hook(UI_G_HOLD)) { ui_haptic_tick(); return; }   /* the tutorial's hold step: not home */
    ui_home_by_long_press();
}
void ui_hold_start(void)
{
    hold_stop();                                       /* every press starts clean: an older timer never fires for this one */
    lv_indev_t *i = lv_indev_active() ? lv_indev_active() : s_touch;
    if (!i || (s_depth < 2 && !s_gest_hook) || !s_wake.screen_on) return;   /* the bottom page has nowhere to go (unless its page took the gestures: the broadcast radio's hold = the player) */
    if (s_depth > 0 && !strcmp(s_stack[s_depth - 1].s->name, "langpick")) return;   /* M6: the language pick must be answered, as the up flick does not leave it either */
    lv_indev_get_point(i, &s_hold_p0);
    int ms = cfg_int("home_hold_ms", 3000);
    s_hold_ms = (uint32_t)(ms < 2000 ? 2000 : ms > 4000 ? 4000 : ms);
    s_hold_t0 = lv_tick_get();
    s_hold_t = lv_timer_create(hold_tick, LV_DEF_REFR_PERIOD, NULL);
    lv_timer_set_repeat_count(s_hold_t, (int32_t)(s_hold_ms / LV_DEF_REFR_PERIOD + 8));
}
void ui_hold_stop(void) { hold_stop(); }
/* GS: ONE shared gesture recogniser (spec 2026-11-16-gestures.md). A touch that starts moving within UI_FLICK_START_MS of the press, travels UI_FLICK_PCT % of the panel height
 * (up / down, mostly vertical) or UI_FLICK_H_PCT % of its width (left / right, mostly horizontal) within UI_FLICK_MS of starting to move, then lifts, is a FLICK:
 *   down  = the quick settings (ui_qs.h), on every page, a game in flight included (the page is covered: its on_suspend pauses it);
 *   up    = the quick settings close; else a content page shows its popup; else the page goes back (its Back control / ring on_back, ui_back_set); the home page: nothing;
 *   left / right = the next / previous page of the top ring (else a list scrolls by a screenful).
 * A slower or shorter drag, a hold, a tap and rapid taps are none (a list scrolls 1:1 by LVGL, without a throw). A finite timer (not -1: the leak census counts only endless timers)
 * samples the finger because the indev sends no PRESSING; the verdict is made at the RELEASED event, or by the timer when LVGL swallowed it (wait_release). A page that is
 * showing a tutorial step takes the verdict first (ui_gesture_hook). */
EXT_RAM_BSS_ATTR static lv_timer_t *s_flick_t;
EXT_RAM_BSS_ATTR static lv_point_t s_flick_p0, s_flick_pl;
EXT_RAM_BSS_ATTR static uint32_t s_flick_t0, s_flick_tm, s_flick_tl;   /* press time; the time the finger first left its spot (0: not yet); the last sample */
const char *ui_host_name(void) { return s_host->name; }
void ui_gesture_hook(bool (*fn)(ui_gesture_t)) { s_gest_hook = fn; }
void ui_gesture_veto(bool (*fn)(lv_point_t)) { s_gest_veto = fn; }
static void flick_stop(void) { if (s_flick_t) { lv_timer_delete(s_flick_t); s_flick_t = NULL; } }

void ui_back_set(lv_obj_t *back)
{
    const int at = s_build >= 0 ? s_build : s_depth - 1;                 /* the layer being (re)created, else the top one */
    if (at >= 0 && at < s_depth) s_stack[at].back = back;
}
static void back_run(void)                                       /* the up flick on a page that is not content: the page's own way out */
{
    if (s_depth < 2) return;                                     /* the home page: nowhere to go */
    lv_obj_t *ring = ui_popring_ring();
    if (!ring) ring = ui_top_ring();
    if (ring && ui_ring_back(ring)) return;
    lv_obj_t *b = s_stack[s_depth - 1].back;
    if (b && lv_obj_is_valid(b)) { lv_obj_send_event(b, LV_EVENT_CLICKED, NULL); return; }
    if (strcmp(s_stack[s_depth - 1].s->name, "langpick")) ui_pop();    /* no Back declared (the spectrum page, a cover page): back is the layer below; the language pick must be answered */
}
static void flick_do(ui_gesture_t g)
{
    printf("APP flick %s\n", g == UI_G_UP ? "up" : g == UI_G_DOWN ? "down" : g == UI_G_LEFT ? "left" : g == UI_G_RIGHT ? "right" : "hold");
    if (s_gest_hook && s_gest_hook(g)) { ui_haptic_tick(); return; }
    if (!s_depth) return;
    const ui_screen_t *top = s_stack[s_depth - 1].s;
    const bool qs = ui_qs_is(top);
    if (g == UI_G_DOWN) { if (!qs) { ui_haptic_tick(); ui_qs_show(); } return; }
    if (qs) { if (g == UI_G_UP) { ui_haptic_tick(); ui_qs_close(); } return; }
    if (ui_exit_paused()) return;
    if (g == UI_G_UP) {
        ui_haptic_tick();
        if (top->content && !ui_popring_ring()) ui_frame_show(s_stack[s_depth - 1].root, false, false);
        else back_run();
    } else if (g == UI_G_LEFT || g == UI_G_RIGHT) {
        lv_obj_t *ring = ui_popring_ring();
        if (!ring) ring = ui_top_ring();
        ui_haptic_tick();
        if (ring) ui_ring_page(ring, g == UI_G_LEFT ? 1 : -1);      /* left = next page: the finger pulls the page away */
        else ui_list_flick(s_stack[s_depth - 1].root, g == UI_G_LEFT ? 1 : -1);
    }
}
/* did the touch begin at the edge this flick comes from? (up: the bottom edge, down: the top, left: the right, right: the left) */
static bool flick_from_edge(ui_gesture_t g, lv_point_t p0)
{
    const int x = p0.x - UI_CX, y = p0.y - UI_CY;
#if UI_ROUND
    const int r_in = UI_W / 2 - lab_get(PC_LAB_FLICK_EDGE);
    if (x * x + y * y < r_in * r_in) return false;                   /* not in the rim */
    const bool vert = abs(y) >= abs(x);                              /* which side of the centre the rim point is on */
    switch (g) {
    case UI_G_UP:    return vert && y > 0;
    case UI_G_DOWN:  return vert && y < 0;
    case UI_G_LEFT:  return !vert && x > 0;
    case UI_G_RIGHT: return !vert && x < 0;
    default:         return true;
    }
#else
    switch (g) {
    case UI_G_UP:    return y >= UI_H / 2 - lab_get(PC_LAB_FLICK_EDGE);
    case UI_G_DOWN:  return y < -(UI_H / 2 - lab_get(PC_LAB_FLICK_EDGE));
    case UI_G_LEFT:  return x >= UI_W / 2 - lab_get(PC_LAB_FLICK_EDGE);
    case UI_G_RIGHT: return x < -(UI_W / 2 - lab_get(PC_LAB_FLICK_EDGE));
    default:         return true;
    }
#endif
}
/* the verdict of one touch, from its press point, its last point and the time the finger was last seen */
static void flick_verdict(lv_point_t p, uint32_t t_last)
{
    if (!s_flick_tm || !s_depth || !s_wake.screen_on) return;
    const int dx = p.x - s_flick_p0.x, dy = p.y - s_flick_p0.y;
    if ((uint32_t)(t_last - s_flick_tm) > (uint32_t)lab_get(PC_LAB_FLICK_MS)) return;
    ui_gesture_t g;
    if (abs(dy) >= UI_H * lab_get(PC_LAB_FLICK_PCT) / 100 && abs(dx) * 2 <= abs(dy)) g = dy < 0 ? UI_G_UP : UI_G_DOWN;
    else if (abs(dx) >= UI_W * lab_get(PC_LAB_FLICK_H_PCT) / 100 && abs(dy) * 2 <= abs(dx)) g = dx < 0 ? UI_G_LEFT : UI_G_RIGHT;
    else return;
    if (!flick_from_edge(g, s_flick_p0)) return;                     /* it began in the middle of the page: a scroll or a drag, never a gesture (UI_FLICK_EDGE) */
    if (s_touch->pointer.act_obj) lv_obj_send_event(s_touch->pointer.act_obj, LV_EVENT_PRESS_LOST, s_touch);   /* I2: lv_indev_reset sends INDEV_RESET only: the pressed widget (a pad, a key) must hear that its press is over */
    lv_indev_reset(s_touch, NULL);                                   /* this touch is a flick: LVGL must not also CLICK the object it began on (a button, the page) */
    flick_do(g);
}
static void flick_tick(lv_timer_t *t)
{
    if (t->repeat_count <= 1) s_flick_t = NULL;                      /* the last run: LVGL deletes it after this one */
    lv_indev_t *i = s_touch;
    if (!i) { if (s_flick_t) flick_stop(); return; }
    if (lv_indev_get_state(i) != LV_INDEV_STATE_PRESSED) {           /* released with no RELEASED event for us (LVGL swallowed it): the verdict from the last sample */
        const bool was = s_flick_t != NULL;
        flick_stop();
        if (was) flick_verdict(s_flick_pl, s_flick_tl);
        return;
    }
    lv_point_t p;
    lv_indev_get_point(i, &p);
    if (!s_flick_tm && (abs(p.x - s_flick_p0.x) >= UI_TAP_GAP || abs(p.y - s_flick_p0.y) >= UI_TAP_GAP)) {
        /* "late" is judged by the last sample that still saw the finger on its spot (s_flick_tl), not by now: a starved LVGL task (frames of 60-150 ms: a rotating cover, the spectrum, the popup over them) runs this timer
         * so seldom that the first sample that SEES the move can be past UI_FLICK_START_MS though the finger left its spot at once (fpux2: simframe 100 made every flick a "late drag"). The move began no earlier than that sample. */
        if ((uint32_t)(s_flick_tl - s_flick_t0) > (uint32_t)lab_get(PC_LAB_FLICK_START_MS)) {                               /* it started moving late: a drag, not a flick */
            if (!(i->pointer.act_obj && lv_obj_has_flag(i->pointer.act_obj, UI_FLAG_TAP_ONLY) && !i->wait_until_release)) { if (s_flick_t) flick_stop(); return; }
        } else s_flick_tm = s_flick_tl;                                  /* the finger left no earlier than the last still sample: the 300 ms window counts from there, so a starved sampler only makes it stricter by one gap (on a tap-only control the timer stays to cancel the click below; no verdict without s_flick_tm) */
    }
    lv_obj_t *act = i->pointer.act_obj;                              /* a tap-only control (ui_kit.h UI_FLAG_TAP_ONLY) the finger has left its spot on: no click at the release */
    if (act && !i->wait_until_release && !lv_indev_get_scroll_obj(i) && lv_obj_has_flag(act, UI_FLAG_TAP_ONLY)
        && (abs(p.x - s_flick_p0.x) >= UI_TAP_GAP * 2 || abs(p.y - s_flick_p0.y) >= UI_TAP_GAP * 2)) {
        lv_obj_send_event(act, LV_EVENT_PRESS_LOST, i);
        lv_indev_wait_release(i);                                    /* the flick verdict still comes: this timer sees the lift (above) */
    }
    s_flick_pl = p;
    s_flick_tl = lv_tick_get();
}
static void flick_release(void)
{
    lv_indev_t *i = s_touch;
    const bool was = s_flick_t != NULL;
    flick_stop();
    if (!was || !i) return;
    lv_point_t p;
    lv_indev_get_point(i, &p);
    if (!s_flick_tm && (uint32_t)(s_flick_tl - s_flick_t0) <= (uint32_t)lab_get(PC_LAB_FLICK_START_MS)
        && (abs(p.x - s_flick_p0.x) >= UI_TAP_GAP || abs(p.y - s_flick_p0.y) >= UI_TAP_GAP)) s_flick_tm = s_flick_tl;   /* the whole move fell between two samples of a starved timer: the release sees it all */
    flick_verdict(p, lv_tick_get());
}
void ui_flick_inject(ui_gesture_t g) { flick_do(g); }             /* console "flick up|down|left|right": the verdict without a touch */
static void hold_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    hold_stop();                                                                    /* every press / release ends an older hold first (LVGL sends no RELEASED after a wait_release) */
    if (code == LV_EVENT_PRESSED) {
        flick_stop();
        if (s_gest_veto) {                                             /* I3: the press is on a held control: a hold there is a sustained note, a swipe is a glide */
            lv_point_t vp;
            lv_indev_t *vi = lv_indev_active() ? lv_indev_active() : s_touch;
            if (vi) { lv_indev_get_point(vi, &vp); if (s_gest_veto(vp)) return; }
        }
        if (s_depth) ui_hold_start();                                  /* GS: a hold anywhere on any page = home (no Back cell or pill to hold any more); the tutorial takes it as its last step */
        if (s_depth && !ui_exit_paused() && s_wake.screen_on) {
            lv_indev_t *i = lv_indev_active() ? lv_indev_active() : s_touch;
            if (i) lv_indev_get_point(i, &s_flick_p0);
            s_flick_pl = s_flick_p0;
            s_flick_t0 = s_flick_tl = lv_tick_get();
            s_flick_tm = 0;
            s_flick_t = lv_timer_create(flick_tick, LV_DEF_REFR_PERIOD, NULL);
            lv_timer_set_repeat_count(s_flick_t, (int32_t)((lab_get(PC_LAB_FLICK_START_MS) + lab_get(PC_LAB_FLICK_MS)) / LV_DEF_REFR_PERIOD + 8));
        }
    } else flick_release();
}

lv_obj_t *ui_top_ring(void) { return s_depth ? ui_ring_find(s_stack[s_depth - 1].root) : NULL; }

void ui_replace(const ui_screen_t *s, void *arg)
{
    if (s_depth > 1) ui_pop();
    ui_push(s, arg);
}

/* ---- the message page: no Back button; it leaves after MSG_MS, on a tap anywhere, or on a knob turn (consumed) ---- */
#define MSG_MS 2500
/* a copy: callers pass stack buffers and a rebuild re-runs msg_create later. 128 = the longest caller buffer (chip_ctl s_why). EXT_RAM: internal RAM is at its budget */
EXT_RAM_BSS_ATTR static char s_msg[128];
static bool s_msg_warn;   /* ICON-B: the message page's icon (warning or info) */

/* take layer i (not the bottom one) off the stack from anywhere in it: the pages above stay as they are, and their object
 * baselines drop by what this page held, so their leak check does not count the missing objects */
static void layer_remove(int i)
{
    int n, o0, o1;
    census(&n, &o0);
    if (s_stack[i].s->destroy) s_stack[i].s->destroy();
    lv_obj_delete(s_stack[i].root);
    snap_free(i);
    census(&n, &o1);
    for (int j = i + 1; j < s_depth; j++) {
        s_stack[j].objs0 = (int16_t)(s_stack[j].objs0 - (o0 - o1));
        s_stack[j - 1] = s_stack[j];
    }
    s_depth--;
    printf("APP screen remove depth=%d\n", s_depth);
}

/* the message page whose root is `root` leaves, wherever it is: on top it is a pop, covered it is taken out of the middle,
 * gone already (popped by `back`) nothing happens */
static void msg_leave(lv_obj_t *root)
{
    for (int i = s_depth - 1; i > 0; i--) {
        if (s_stack[i].root != root) continue;
        if (i == s_depth - 1) ui_pop(); else layer_remove(i);
        return;
    }
}
static void msg_timeout(lv_timer_t *t) { msg_leave(lv_timer_get_user_data(t)); }
static void msg_tap(lv_event_t *e) { msg_leave(lv_event_get_current_target_obj(e)); }
static bool msg_knob(int d) { (void)d; if (s_depth > 1) msg_leave(s_stack[s_depth - 1].root); return true; }
static void msg_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_t *l = lv_label_create(p);
    if (font_has_icons()) lv_label_set_text_fmt(l, "%s %s", s_msg_warn ? UI_ICON_L_WARN : UI_ICON_L_INFO, s_msg); else lv_label_set_text(l, s_msg);
    lv_obj_set_width(l, 260);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(l);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p, msg_tap, LV_EVENT_CLICKED, NULL);
    ui_timer_bind(p, msg_timeout, MSG_MS, p);
}
static const ui_screen_t s_msg_screen = { .name = "message", .create = msg_create, .on_knob = msg_knob };

/* ---- the status strip (a build without the round glass: UI_ROUND 0): messages are not pages but a strip over the bottom row of the grid (<= 2 lines, ui_geom.h
 * UI_RGRID_*). A tap on it hides it and does nothing else (the strip takes the touch); a message hides itself after UI_HOLD_MS; a new message shows it again
 * (text and time renewed); a warning stays until it is tapped. Knob and key turns go on to the page below. ---- */
EXT_RAM_BSS_ATTR static lv_timer_t *s_strip_t;
static void strip_hide(void)
{
    if (s_strip_t) { lv_timer_delete(s_strip_t); s_strip_t = NULL; }
    if (s_strip) { lv_obj_t *o = s_strip; s_strip = NULL; lv_obj_delete(o); }
}
static void strip_tap(lv_event_t *e) { (void)e; strip_hide(); }
static void strip_timeout(lv_timer_t *t) { (void)t; s_strip_t = NULL; strip_hide(); }   /* a one-shot: LVGL deletes the timer after this run */
static void strip_show(const char *text, bool warning)
{
    if (s_strip_t) { lv_timer_delete(s_strip_t); s_strip_t = NULL; }
    lv_obj_t *l;
    if (s_strip) l = lv_obj_get_child(s_strip, 0);
    else {
        lv_obj_t *o = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(o);
        const int cut = ui_page_bubble_h() / 2 - UI_RGRID_GAP / 2 + 1;   /* the page-count bubble sits on the seam above this row: the strip starts under it */
        lv_obj_set_size(o, UI_RING_BOX_W - 2 * UI_RGRID_MX, UI_RGRID_CELL_H - cut);
        lv_obj_set_pos(o, UI_RGRID_MX, UI_RGRID_TOP + (UI_RGRID_ROWS - 1) * (UI_RGRID_CELL_H + UI_RGRID_GAP) + cut);
        lv_obj_set_style_radius(o, UI_RGRID_RADIUS, 0);
        lv_obj_set_style_bg_color(o, UI_PANEL2, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_text_color(o, UI_FG, 0);   /* the top layer is not under a page's root: its own font and colour */
        lv_obj_set_style_text_font(o, font_main(), 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(o, strip_tap, LV_EVENT_CLICKED, NULL);
        l = lv_label_create(o);
        lv_obj_set_width(l, UI_RING_BOX_W - 2 * UI_RGRID_MX - 32);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);   /* two lines at most: the rest is cut, never a third line */
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        s_strip = o;
    }
    lv_obj_set_style_border_color(s_strip, warning ? UI_SYS : UI_A1, 0);
    {                                                    /* the label is as high as its text (one or two lines): centred in the strip, not at the top of a two-line box (SQ2) */
        const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
        const int lh = lv_font_get_line_height(f);
        lv_point_t sz;
        lv_text_get_size(&sz, text ? text : "", f, 0, 0, UI_RING_BOX_W - 2 * UI_RGRID_MX - 32, LV_TEXT_FLAG_NONE);   /* the label's own width (its layout is not computed yet) */
        lv_obj_set_height(l, LV_CLAMP(lh, sz.y, 2 * lh));
    }
    if (font_has_icons()) lv_label_set_text_fmt(l, "%s %s", warning ? UI_ICON_L_WARN : UI_ICON_L_INFO, text); else lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_move_foreground(s_strip);
    if (!warning) { s_strip_t = lv_timer_create(strip_timeout, UI_HOLD_MS, NULL); lv_timer_set_repeat_count(s_strip_t, 1); }
}
bool ui_strip_shown(void) { return s_strip != NULL; }
static void push_msg(const char *text, bool warn)   /* the icon flag is set before ui_push builds the page */
{
    if (UI_ROUND) {
        if (text != s_msg) {
            snprintf(s_msg, sizeof s_msg, "%s", text ? text : "");
            if (text && strlen(text) >= sizeof s_msg && ((uint8_t)text[sizeof s_msg - 1] & 0xC0) == 0x80) {   /* cut inside a UTF-8 character: drop the whole character */
                size_t n = sizeof s_msg - 1;
                while (n > 0 && ((uint8_t)s_msg[n - 1] & 0xC0) == 0x80) n--;
                if (n > 0) n--;
                s_msg[n] = 0;
            }
        }
        s_msg_warn = warn;
        ui_push(&s_msg_screen, NULL);
    } else strip_show(text, warn);
}
void ui_push_message(const char *text) { push_msg(text, false); }
void ui_push_warning(const char *text) { push_msg(text, true); }

void ui_async(void (*fn)(void *), void *arg)
{
    bsp_display_lock(-1);
    lv_async_call(fn, arg);
    bsp_display_unlock();
}

/* frame time (console uistat): refresh cycles that rendered something, start to the last flush handed over,
 * with the areas and pixels invalidated */
static int64_t s_refr_t0;
static bool s_rendered;
static uint32_t s_fr_n, s_fr_us, s_fr_max, s_fr_areas, s_fr_px;
static void refr_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    int64_t now = esp_timer_get_time();
    if (c == LV_EVENT_REFR_START) {
        s_refr_t0 = now;
        s_rendered = false;
        lv_display_t *d = lv_event_get_target(e);
        for (uint32_t i = 0; i < d->inv_p; i++) if (!d->inv_area_joined[i]) { s_fr_areas++; s_fr_px += lv_area_get_size(&d->inv_areas[i]); }
    } else if (c == LV_EVENT_RENDER_START) s_rendered = true;
    else if (c == LV_EVENT_REFR_READY && s_rendered) {
        uint32_t us = (uint32_t)(now - s_refr_t0);
        s_fr_n++;
        s_fr_us += us;
        if (us > s_fr_max) s_fr_max = us;
        if (!s_wake.screen_on) bsp_display_release_dma();   /* drawn in the dark: nothing follows to free its DMA buffers */
    }
}
void ui_fps_read(ui_frames_t *out)
{
    uint32_t n = s_fr_n ? s_fr_n : 1;
    *out = (ui_frames_t){ s_fr_n, s_fr_us / n, s_fr_max, s_fr_areas * 100 / n, s_fr_px / n };
    s_fr_n = s_fr_us = s_fr_max = s_fr_areas = s_fr_px = 0;
}
void ui_fps_stat(void)
{
    ui_frames_t f;
    ui_fps_read(&f);
    printf("APP uistat screen=%s frames=%lu frame_avg_us=%lu frame_max_us=%lu areas=%lu.%02lu px_avg=%lu\n", ui_top() ? ui_top()->name : "-",
           (unsigned long)f.n, (unsigned long)f.avg_us, (unsigned long)f.max_us, (unsigned long)(f.areas_x100 / 100),
           (unsigned long)(f.areas_x100 % 100), (unsigned long)f.px_avg);
    bsp_display_flush_stat();                          /* FakePod: where the flushes spent their time (night3 D) */
}

void ui_start(lv_display_t *disp, lv_indev_t *touch) { ui_start_host(disp, touch, &k_player_host); }

void ui_start_host(lv_display_t *disp, lv_indev_t *touch, const ui_host_t *h)
{
    s_host = h;
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    ui_theme_apply(disp);
    s_touch = touch;
    s_last_input_us = esp_timer_get_time();
    /* every press anywhere: haptic + activity */
    if (touch) lv_indev_add_event_cb(touch, any_click_cb, LV_EVENT_PRESSED, NULL);
    if (touch) { lv_indev_add_event_cb(touch, hold_cb, LV_EVENT_PRESSED, NULL); lv_indev_add_event_cb(touch, hold_cb, LV_EVENT_RELEASED, NULL); }
    if (touch) { lv_indev_add_event_cb(touch, drag_cb, LV_EVENT_PRESSED, NULL); lv_indev_add_event_cb(touch, drag_cb, LV_EVENT_RELEASED, NULL); }
#if UI_ROUND
    s_vol_arc = lv_arc_create(lv_layer_top());
    lv_obj_set_size(s_vol_arc, UI_VOL_ARC_D, UI_VOL_ARC_D);
    lv_obj_center(s_vol_arc);
    lv_arc_set_range(s_vol_arc, 0, 100);
    lv_arc_set_bg_angles(s_vol_arc, UI_VOL_A0, UI_VOL_A1);
    lv_obj_remove_style(s_vol_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_vol_arc, UI_VOL_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_vol_arc, UI_VOL_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_vol_arc, UI_A1, LV_PART_INDICATOR);
#else   /* the square panel: a pill (child 0 the speaker icon, 1 the bar, 2 the number); s_vol_arc keeps being "the volume overlay" for every caller */
    s_vol_arc = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_vol_arc);
    lv_obj_set_size(s_vol_arc, UI_VOLP_W, UI_VOLP_H);
    lv_obj_align(s_vol_arc, LV_ALIGN_CENTER, 0, UI_VOLP_Y);
    lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_vol_arc, UI_VOLP_H / 2, 0);
    lv_obj_set_style_bg_color(s_vol_arc, UI_PANEL, 0);
    lv_obj_set_style_bg_opa(s_vol_arc, UI_VOLP_OPA, 0);
    lv_obj_t *ic = lv_label_create(s_vol_arc);
    lv_obj_set_style_text_font(ic, font_main(), 0);
    lv_obj_set_style_text_color(ic, UI_FG, 0);
    lv_label_set_text(ic, font_has_icons() ? UI_ICON_L_VOLUME : "");
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_t *bar = lv_bar_create(s_vol_arc);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, UI_VOLP_BAR_W, UI_VOLP_BAR_H);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, UI_VOLP_BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, UI_VOLP_BAR_H / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, UI_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_A1, LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 72, 0);
    lv_obj_t *num = lv_label_create(s_vol_arc);
    lv_obj_set_style_text_font(num, font_main(), 0);
    lv_obj_set_style_text_color(num, UI_FG, 0);
    lv_label_set_text(num, "0");
    lv_obj_align(num, LV_ALIGN_RIGHT_MID, -24, 0);
#endif
    lv_obj_add_flag(s_vol_arc, LV_OBJ_FLAG_HIDDEN);
    s_vol_hide = lv_timer_create(vol_hide_cb, 1500, NULL);
    lv_timer_pause(s_vol_hide);
    lv_timer_create(tick_cb, 200, NULL);
    bsp_knob_set_hook(knob_hook);
    if (h->underlay) s_underlay = h->underlay(lv_screen_active()) != NULL;   /* before any root: below every page */
}
