// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Screen stack, backlight timeout, knob = volume, haptics (spec 4.7). All calls from the LVGL task. */
typedef struct ui_screen {
    const char *name;
    void (*create)(lv_obj_t *parent, void *arg);
    void (*destroy)(void);              /* may be NULL */
    bool (*on_knob)(int detents);       /* may be NULL; true = consumed (knob does not change volume) */
    void (*on_suspend)(void);           /* may be NULL; the page stops being seen but stays on the stack: another page is pushed over it, or the screen goes dark */
    void (*on_resume)(void);            /* may be NULL; the page is on top again because the page over it was popped (not called for a screen that wakes) */
    bool content;                       /* CF2: a CONTENT page (game, toy, ambient, counter ...): content-first, its chrome is the shared popup (ui_frame.h) and a long press anywhere goes home (ui_hold_*) */
} ui_screen_t;

/* What a boot mode asks of the shared host (UI unify T10, spec 2.1). Tables are const (flash); ui.c keeps one pointer to the one in use. */
typedef struct {
    const char *name;                         /* "player", "inst", "usb" */
    bool (*knob_default)(int detents);        /* the knob when no page, ring or axis took it; NULL = the player's volume (app_set_volume) */
    int  (*volume)(void);                     /* the value the default knob path starts from; NULL = app_volume() */
    uint32_t (*idle_ms)(void);                /* screen timeout; NULL = pc_screen_timeout_ms(power); a function returning 0 = never */
    bool cfg_writes;                          /* false: the tick never calls cfg_tick() (USB mode: the card is the computer's) */
    bool haptics;                             /* false: ui_haptic_* do nothing */
    lv_obj_t *(*underlay)(lv_obj_t *scr);     /* made once below every page root; the roots are then transparent (the instrument's LED ring) */
} ui_host_t;
void ui_start_host(lv_display_t *disp, lv_indev_t *touch, const ui_host_t *h);   /* LVGL lock held; `h` must outlive the mode (a const table) */
void ui_start(lv_display_t *disp, lv_indev_t *touch);   /* = ui_start_host(.., the player's host): no change for the player */
void ui_volume_shown(int v);                             /* a host that sets the volume itself shows it on the shared arc (1.5 s) */
void ui_push(const ui_screen_t *s, void *arg);
void ui_pop(void);
void ui_replace(const ui_screen_t *s, void *arg);       /* pop + push */
/* Pops pages until `scr` is on top (the highest layer of that type). Returns false (and pops nothing) when `scr` is not on the stack.
 * Each popped page is destroyed as by ui_pop; on_resume runs once, for `scr`, when anything was popped (not for the pages in between). */
bool ui_pop_to(const ui_screen_t *scr);
/* A page that must be on the stack once (a list or picker: its state is file statics): one already on the stack is popped to and replaced
 * by a fresh one with `arg` (the pages above it go); otherwise it is pushed. */
void ui_open_once(const ui_screen_t *s, void *arg);
void ui_home(void);                                      /* pop every page above the bottom of the stack (top first, each destroy runs); playback goes on; no-op at the bottom */
void ui_hold_start(void);                                /* CF2: a press that began on a Back: goes home after cfg home_hold_ms unless released / moved (content pages: automatic, anywhere) */
void ui_hold_stop(void);
/* GS gestures (ui.c, one recogniser): a fast flick up / down / left / right, or the hold of home_hold_ms. The page that wants them all (the tutorial) installs a hook for as long as it is on top:
 * it returns true when it took the gesture (nothing else happens); NULL removes it. ui_back_set: the Back control of the page being created (hidden; the up flick clicks it). */
typedef enum { UI_G_UP, UI_G_DOWN, UI_G_LEFT, UI_G_RIGHT, UI_G_HOLD } ui_gesture_t;
void ui_gesture_hook(bool (*fn)(ui_gesture_t));
void ui_gesture_veto(bool (*fn)(lv_point_t));            /* I3: fn(press point) true = this press is on a held control (an instrument pad): no hold-home, no flick. NULL clears (the page's destroy) */
void ui_flick_inject(ui_gesture_t g);                    /* console "flick up|down|left|right" */
void ui_back_set(lv_obj_t *back);
const char *ui_host_name(void);                          /* the boot mode's host table name: "player", "inst", "ext" */
void ui_home_by_long_press(void);                        /* a long press on any Back: one haptic tick, the release after it is swallowed, then ui_home() */
void ui_top_timers(void (*fn)(lv_timer_t *, void *), void *u);   /* every repeating timer the top page made since its push (not in its snapshot): ui_exit.h pauses them */
lv_obj_t *ui_top_ring(void);                             /* the ring on the top page (console "ring"), NULL when it has none */
void ui_rebuild_all(void);                               /* re-create every screen (theme change) */
const ui_screen_t *ui_top(void);
lv_obj_t *ui_top_root(void);                             /* the top page's root object (console "popring -2": a tap on the page), NULL when there is none */
void ui_events(void);                                    /* console "uievents": the event descriptors of every page on the stack, summed */
void ui_boxes_clip(void);                                /* console "uiboxes clip": the same with the visible part of every box (after the box), invisible ones left out */
void ui_boxes(void);                                     /* console uiboxes: boxes of the top page's labels / buttons / QR codes */
bool ui_on_stack(const ui_screen_t *s);                 /* any layer of the stack is s */
const ui_screen_t *ui_layer(int i);                     /* layer i from the bottom, NULL past the top (console uistack) */
void ui_haptic_tap(void);
void ui_haptic_tick(void);
void ui_haptic_strong(void);                             /* strong click, longer: a landing, a big win */
void ui_volume_step_dark(int d);                        /* F2: one volume step with the screen off (the FakePod blind keys); no wake, no arc */
void ui_knob_inject(int detents);                       /* the physical knob's own path (wake-up, top page's on_knob, else volume); console / tests */
void ui_activity(void);
uint32_t ui_input_idle_ms(void);                        /* ms since the last touch / key / knob (the screen timeout's measure); the auto power-off reads it */
void ui_sleep(void);                                     /* screen off, as the idle timeout would */
void ui_wake(void);                                      /* screen on, as a knob turn would */
void ui_backlight_full(const ui_screen_t *owner);        /* the page `owner` (while it is the top one) lights the panel at 100 %; NULL (its destroy) ends it. Live value only: cfg "brightness" stays */
void ui_backlight_apply(void);                           /* the live backlight of a lit screen from cfg "brightness" or the full-light page (after cfg changes it) */
bool ui_visual_paused(lv_obj_t *page_root);                      /* THE predicate for a picture that animates on its own timer (rotating cover, spectrum, album photos, arena / dial games): true when the screen is off, a popup ring (ui_popring) is open over it, the nine-grid's labels are shown (ui_ninegrid, FakePod), or `page_root` (NULL: not asked) is not the top page (quick settings, another page, the exit pause). The tick then draws nothing, invalidates nothing and keeps its last frame; audio is never asked. 2026-11-22 */
bool ui_screen_on(void);                                 /* backlight on: animations may run */                                  /* counts as user input for the backlight timeout */
/* "later stage" placeholder */
void ui_push_message(const char *text);                  /* round glass: a message page (2.5 s, a tap leaves); square panel: the status strip over the bottom row (4 s, a tap hides it and nothing else) */
void ui_push_warning(const char *text);                  /* as a message, but on the square panel the strip stays until it is tapped (the round glass: the same page as a message) */
bool ui_strip_shown(void);                               /* square panel: the status strip is up (console "ui strip") */
void ui_fps_stat(void);                                /* console uistat: frame times since the last call */
typedef struct { uint32_t n, avg_us, max_us, areas_x100, px_avg; } ui_frames_t;
void ui_fps_read(ui_frames_t *out);                    /* the same numbers as ui_fps_stat prints, read and reset (the instrument's inst uistat) */
/* lv_async_call from a task that is not the LVGL task (lv_async_call itself takes no lock) */
void ui_async(void (*fn)(void *), void *arg);
/* filled in by later tasks: called when the volume changes */
extern void (*ui_volume_hook)(int vol);
