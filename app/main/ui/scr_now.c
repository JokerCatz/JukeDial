// SPDX-License-Identifier: LGPL-2.1-or-later
/* Now playing (spec 4.7): rotating round cover, title on the top band (the spectrum is its own page, scr_viz.c); fav / next / stop / list buttons around the rim; tap the cover for details. */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "ui_theme.h"
#include "audio_svc.h"
#include "cfg.h"
#include "esp_heap_caps.h"
#include "lowmem.h"
#include "pc/pc_lowmem.h"
#include "font.h"
#include "library_svc.h"
#include "player_ctl.h"
#include "pc/pc_thumb.h"
#include "power_svc.h"
#include "screens.h"
#include "stats_svc.h"
#include "thumb_svc.h"
#include "now_menu.h"
#include "now_fp.h"
#include "ui_ninegrid.h"
#include "album_svc.h"
#include "album_view.h"
#include "ui_axis.h"
#include "ui_kit.h"

#define S PC_THUMB_SIZE
#define SD (S * ui_cover_scale() / 256)   /* the shown cover: S on the round panel, larger on the square one (rotated straight to this size, no second scaling) */

static lv_obj_t *s_cover, *s_img, *s_title;
static lv_obj_t *s_root, *s_name;             /* the screen, white noise's name */
static struct {                               /* one struct > 8 B: libmain's .bss is PSRAM (linker.lf), small-data objects are not */
    lv_obj_t *note;                           /* the owner's note under the cover (now_ops_t.cover_note: the phone's cover refused for low memory) */
    const char *note_txt;                     /* the text it shows (set only when it changes: no redraw per on_change) */
    bool req_jpeg;                            /* s_req is a cover_jpeg decode (its end is told to the owner) */
} s_cv;
static bool s_stat;                           /* the square panel's static layout (now_fp.c, style A or B) is up instead of the rotating disc */
static bool alive(void) { return s_stat || s_title; }
static lv_timer_t *s_t_rot;
static lv_image_dsc_t s_dsc;
static uint16_t *s_px;
static int32_t s_rot;
static uint32_t s_req;

/* album mode (cfg now_view=album, spec 2026-11-11-album-design.md): photos over everything, no text; a tap still
 * opens the playback menu. Until a photo is ready the normal cover shows. */
static lv_obj_t *s_album, *s_under;
static lv_timer_t *s_t_album;
static bool album_on(void) { return s_album && album_view_showing(s_album); }

static int s_rot_step = 3, s_rot_ms = 50;      /* 0.3 deg every 50 ms: one turn a minute, small steps (user 2026-11-09) */
static uint32_t s_rot_n, s_rot_us;
static uint16_t *s_rpx;                    /* s_px rotated by s_rot, disc baked in: what the image shows */

static void show_rotated(void)
{
    int64_t t0 = esp_timer_get_time();
    const bool smooth = cfg_int("cover_smooth", 0) != 0;
    if (SD != S) pc_thumb_rotate_scaled(s_px, S, s_rpx, SD, (int)s_rot, lv_color_to_u16(UI_BG), smooth);
    else if (smooth) pc_thumb_rotate_smooth(s_px, s_rpx, S, (int)s_rot, lv_color_to_u16(UI_BG));   /* bilinear, soft rim */
    else pc_thumb_rotate(s_px, s_rpx, S, (int)s_rot, lv_color_to_u16(UI_BG));    /* default: nearest + soft rim, ~4x cheaper */
    s_rot_us += (uint32_t)(esp_timer_get_time() - t0);
    if (++s_rot_n == 100) { printf("APP rot step=%d ms=%d rotate_avg_us=%lu\n", s_rot_step, s_rot_ms, (unsigned long)(s_rot_us / 100)); s_rot_n = s_rot_us = 0; }
    lv_image_cache_drop(&s_dsc);
    lv_obj_invalidate(s_img);
}

static void set_pixels_n(uint16_t *px, int n)
{
    if (s_stat) {                                  /* no rotation, no second buffer: the piece draws the picture itself (an n x n one is stretched to the cover box when n differs from it) */
        nowfp_set_cover(NULL);
        lv_image_cache_drop(&s_dsc);
        if (s_px) heap_caps_free(s_px);
        s_px = px;
        if (!px) return;
        s_dsc = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = n, .h = n, .stride = n * 2 },
                                  .data_size = (uint32_t)n * n * 2, .data = (const uint8_t *)s_px };
        nowfp_set_cover(&s_dsc);
        return;
    }
    if (s_px) heap_caps_free(s_px);
    s_px = px;
    if (px && !s_rpx) s_rpx = heap_caps_malloc(SD * SD * 2, MALLOC_CAP_SPIRAM);
    if (px && s_rpx) {
        s_dsc = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SD, .h = SD, .stride = SD * 2 },
                                  .data_size = SD * SD * 2, .data = (const uint8_t *)s_rpx };
        show_rotated();
        lv_image_set_src(s_img, &s_dsc);
        lv_obj_remove_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);   /* placeholder: the cover circle's own colour */
    }
}

static void set_pixels(uint16_t *px) { set_pixels_n(px, S); }

static uint16_t c565(uint32_t rgb) { return lv_color_to_u16(ui_c(rgb)); }
static void show_default(void)                 /* no cover: a record in the theme's colours */
{
    const int n = s_stat && nowfp_cover_size() > S ? nowfp_cover_size() : S;   /* the static layout draws the record at the size of its cover box: no stretching */
    uint16_t *px = heap_caps_malloc((size_t)n * n * 2, MALLOC_CAP_SPIRAM);
    if (!px) { set_pixels(NULL); return; }
    const pc_theme_t *t = ui_theme();
    lv_color_t sheen = lv_color_mix(ui_c(t->fg), ui_c(t->panel2), 60);
    pc_thumb_default(px, n, c565(t->panel), c565(t->panel2), lv_color_to_u16(sheen), c565(t->a1), c565(t->bg));
    set_pixels_n(px, n);
}

static void note_refresh(void)
{
    if (!s_cv.note && !s_stat) return;
    const now_ops_t *o = player_ctl_now();
    const char *t = o && o->playing() && !o->no_art && o->cover_note ? o->cover_note() : NULL;
    if (t == s_cv.note_txt) return;
    s_cv.note_txt = t;
    if (s_stat) { nowfp_set_note(t); return; }
    if (t) { lv_label_set_text(s_cv.note, t); lv_obj_remove_flag(s_cv.note, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(s_cv.note, LV_OBJ_FLAG_HIDDEN);
}

/* the static layout (square panel): the 240 px thumbnail is on the page, stretched; the same source is decoded again at the size of the cover box (nowfp_cover_size: 296 in style A, 460 in B) and
 * swapped in when it is ready (a plain swap, no fade). The card's thumbnail pack keeps 240 px, so this is one more decode per track change, on the thumbnail task like the first one.
 * A refused or failed decode (a source above 48 kHz, low memory, a huge picture) leaves the stretched thumbnail. */
static void big_ready(uint32_t req, uint16_t *px, void *u)
{
    (void)u;
    if (req != s_req || !alive() || !s_stat || !px) { if (px) heap_caps_free(px); return; }   /* superseded, or nothing made: the thumbnail stays */
    set_pixels_n(px, nowfp_cover_size());
}
static void ask_big(void)
{
    const int n = nowfp_cover_size();
    const now_ops_t *o = player_ctl_now();
    if (!s_stat || n <= S || !o || !s_px) return;
    char path[300];
    uint32_t off = 0, len = 0, r = 0;
    const uint8_t *jpg;
    if (!s_cv.req_jpeg && o->cover && o->cover(path, sizeof path, &off, &len)) r = thumb_svc_request_big(path, off, len, n, big_ready, NULL);
    else if (s_cv.req_jpeg && o->cover_jpeg && o->cover_jpeg(&jpg, &len)) r = thumb_svc_request_jpeg_big(jpg, len, n, big_ready, NULL);   /* the phone's cover art */
    if (r) s_req = r;
}

static void thumb_ready(uint32_t req, uint16_t *px, void *u)
{
    (void)u;
    if (req != s_req || !alive()) { if (px) heap_caps_free(px); return; }
    const now_ops_t *o = player_ctl_now();
    if (s_cv.req_jpeg && o && o->cover_done) o->cover_done(px != NULL, !px && thumb_svc_low_mem(req));   /* the owner keeps the honest state */
    note_refresh();
    if (px) { set_pixels(px); ask_big(); }
    else show_default();                       /* the cover could not be read */
}

/* G13 / ruling R40: this page (or the spectrum page over it) has seen playback and nothing plays while it is on top: the page leaves, as a tap would.
 * Checked when the source changes (Stop, a scan) and when the page comes back on top (the message after a failed stream or file is dismissed, a page
 * that covered it while playback stopped is popped). Pause is not stop; a page that never saw playback stays (R5). Async: never from inside the owner's callback. */
static bool s_seen;                                   /* playback was seen since this page was created */
static void leave_stopped(void *arg)
{
    (void)arg;
    const now_ops_t *o = player_ctl_now();
    if (o && o->playing()) return;                    /* something started meanwhile */
    if (ui_top() == &scr_viz) ui_pop();               /* both pages go: the spectrum page is always over this one */
    if (ui_top() == &scr_now) ui_pop();
}

void now_leave_if_stopped(void)
{
    const now_ops_t *o = player_ctl_now();
    if (!alive() || !s_seen || (o && o->playing()) || (ui_top() != &scr_now && ui_top() != &scr_viz)) return;
    lv_async_call_cancel(leave_stopped, NULL);
    lv_async_call(leave_stopped, NULL);
}

static void on_change(void)
{
    if (!alive()) return;
    now_menu_owner_check();                            /* cells built for another owner: the ring goes (also for the spectrum page's ring: its hook chains here) */
    const now_ops_t *o = player_ctl_now();
    const bool playing = o && o->playing();
    if (playing) s_seen = true;
    now_leave_if_stopped();
    s_cv.req_jpeg = false;
    note_refresh();
    if (!playing) {
        if (s_stat) nowfp_set_title(T(S_STOPPED_2) /* Stopped */); else lv_label_set_text(s_title, T(S_STOPPED_2) /* Stopped */);
        set_pixels(NULL);
        s_req = 0;
        return;
    }
    char t[300];
    o->title(t, sizeof t);
    if (s_stat) nowfp_set_title(t); else lv_label_set_text(s_title, t);
    set_pixels(NULL);
    if (s_stat) {                                      /* the static layout: the cover box always shows something (white noise: the record picture, like a track without a cover) */
        if (o->no_art) { s_req = 0; show_default(); return; }
    } else if (o->no_art) {                           /* white noise: the name in the middle, no disc */
        lv_obj_add_flag(s_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_name, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_name, t);
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
        s_req = 0;
        return;
    }
    if (!s_stat) {
        lv_obj_remove_flag(s_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (album_on()) { s_req = 0; return; }     /* photos on top: no cover to decode (album_check loads it back) */
    char path[300];
    uint32_t off = 0, len = 0;
    const uint8_t *jpg;
    if (o->cover && o->cover(path, sizeof path, &off, &len)) s_req = thumb_svc_request_now(path, off, len, thumb_ready, NULL);
    else if (o->cover_jpeg && o->cover_jpeg(&jpg, &len)) { s_req = thumb_svc_request_jpeg(jpg, len, thumb_ready, NULL); s_cv.req_jpeg = s_req != 0; }   /* the phone's cover art */
    else s_req = 0;
    if (!s_req || !len) show_default();        /* chiptune, mp3 without an embedded picture (an image file next to it replaces the record when found), a phone track without cover art */
    s_rot = 0;
}

static void rot_cb(lv_timer_t *t)
{
    (void)t;
    const now_ops_t *o = player_ctl_now();
    if (ui_visual_paused(s_root) || !pc_power_allows_heavy(power_svc_state()) || !o || !o->playing() || !s_px || now_menu_paused()) return;   /* dark, a popup ring open (the cover freezes on its frame, no rotate, no invalidate), battery or paused: still cover */
    if (s_stat) return;                                /* the static layout does not turn */
    if (lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) || album_on()) return;   /* covered (another screen, the photos) */
    if (lowmem_level() >= PC_LOWMEM_STILL) return;      /* low internal RAM: looks go first */
    s_rot = (s_rot + s_rot_step) % 3600;               /* deci-degrees per tick (console 'rot') */
    show_rotated();                                    /* rotate once here (~ms); frames then only copy pixels */
}

/* ---- the playback menu (spec 5.8, now_menu.c): nothing on the picture until it is tapped ---- */
static void open_viz(void) { ui_push(&scr_viz, NULL); }   /* the setting now_view stays as it is */
void scr_now_tap(void)
{
    if (!now_menu_open(s_root, NOW_MENU_COVER, open_viz, ui_pop)) ui_pop();   /* nothing playing: nothing to control */
}
static void open_next_page(void) { ui_push(&scr_viz, NULL); }   /* the nine-grid's 下一頁: cover -> spectrum (-> lyrics, scr_viz.c -> back here) */
static bool ng_passthru(void) { return album_on(); }
static const ui_ng_set_t *const NG_SETS[] = { &UI_NG_PLAYMODE };   /* no seek op exists in the player (nothing seeks inside a track): the 快轉倒轉 set is left out */
static const ui_ng_def_t NG_COVER = { .id = 0, .sets = NG_SETS, .n_sets = 1, .menu = scr_now_tap, .next_page = open_next_page, .passthru = ng_passthru };
static void root_click(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) != s_root) return;  /* only taps on the picture itself */
    if (s_stat) return;                                /* the static layout: the nine-grid takes the tap (its menu zone opens the ring; with the photos on it goes straight to it) */
    scr_now_tap();
}

static void album_check(lv_timer_t *t)
{
    (void)t;
    bool want = album_now_mode();
    if (want && !s_album && album_svc_ready() > 0) {
        s_album = album_view_create(s_root);
        now_menu_raise();                              /* a ring that is open stays above the photos */
    }
    if (!want && s_album) { lv_obj_delete(s_album); s_album = NULL; on_change(); }   /* back to the cover */
    bool on = album_on();                               /* the scrolling title kept redrawing under the photo */
    if (on != lv_obj_has_flag(s_under, LV_OBJ_FLAG_HIDDEN)) { if (on) lv_obj_add_flag(s_under, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_under, LV_OBJ_FLAG_HIDDEN); }
}

static void create_disc(void)                      /* the rotating cover disc, the title band, the scene name and the note: the page as it was (the round board's, the square panel's grid style) */
{
    s_cover = lv_obj_create(s_under);
    lv_obj_remove_style_all(s_cover);
    lv_obj_set_size(s_cover, SD, SD);
    lv_obj_align(s_cover, LV_ALIGN_CENTER, 0, ui_cover_dy());
    lv_obj_set_style_radius(s_cover, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_cover, UI_PANEL2, 0);
    lv_obj_set_style_bg_opa(s_cover, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_cover, LV_OBJ_FLAG_CLICKABLE);
    s_img = lv_image_create(s_cover);
    lv_obj_center(s_img);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_img, LV_OBJ_FLAG_CLICKABLE);

    s_title = lv_label_create(s_under);
    lv_obj_set_width(s_title, 170);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, ui_scale(-158));

    s_name = lv_label_create(s_under);                       /* white noise: the scene's name where a cover would be */
    lv_obj_set_width(s_name, 200);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_name);
    lv_obj_add_flag(s_name, LV_OBJ_FLAG_HIDDEN);
    s_cv.note = ui_text_dim(s_under, UI_SLOT_LOWER, font_main());   /* the shared dim text in the lower slot, over the cover */
    lv_obj_add_flag(s_cv.note, LV_OBJ_FLAG_HIDDEN);
    s_cv.note_txt = NULL;
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_under = lv_obj_create(p);                        /* cover, title: hidden as a whole under the photos */
    lv_obj_remove_style_all(s_under);
    ui_content_box(s_under);                          /* the content box: the larger square cover (SQ2) must not be clipped to the 360 design box */
    lv_obj_remove_flag(s_under, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_under, LV_OBJ_FLAG_SCROLLABLE);
    s_stat = nowfp_style() != NOWFP_GRID && nowfp_create(s_under, nowfp_style()) != NULL;   /* the square panel's static layout; the grid style (and the round board) keep the disc */
    s_cv.note_txt = NULL;
    if (!s_stat) create_disc();
    s_root = p;
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p, root_click, LV_EVENT_CLICKED, NULL);
    if (nowfp_available()) ui_ninegrid_attach(p, &NG_COVER, nowfp_style() == NOWFP_FULL);   /* every style of the square panel, the disc style too (its tap still opened the old menu and had no 下一頁); 封面＋資訊 and the disc show their own title: no bands */

    player_ctl_on_change = on_change;
    s_seen = false;                                    /* the first on_change sets it when something plays */
    on_change();
    s_t_rot = s_stat ? NULL : ui_timer_bind(p, rot_cb, s_rot_ms, NULL);
    if (!s_stat) {                                     /* the static layout has its own bar; the rim arcs would cross the cover */
        ui_progress_arc(p);                            /* under the photos in album mode: out of sight */
        ui_sleep_arc(p);
    }
    s_album = NULL;
    if (album_now_mode()) album_svc_wake();
    s_t_album = ui_timer_bind(p, album_check, 1000, NULL);
    album_check(NULL);
}

static void destroy(void)
{
    lv_async_call_cancel(leave_stopped, NULL);         /* the page went another way first */
    now_menu_close();
    player_ctl_on_change = NULL;
    s_stat = false;
    s_t_rot = s_t_album = NULL;             /* bound to the root: they go with it */
    s_album = NULL;
    s_img = NULL;
    s_title = NULL;
    s_cv.note = NULL;
    s_cv.note_txt = NULL;
    s_req = 0;
    s_cv.req_jpeg = false;
    if (s_px) { heap_caps_free(s_px); s_px = NULL; }
    if (s_rpx) { heap_caps_free(s_rpx); s_rpx = NULL; }
}

/* test hook: hide parts to find what the frame time goes to (1 cover, 2 title) */
void scr_now_set_rotation(int step_deci, int period_ms)   /* console 'rot <step> <ms>' (trying smoother turns) */
{
    s_rot_step = step_deci;
    s_rot_ms = period_ms < 20 ? 20 : period_ms;
    if (s_t_rot && s_title) lv_timer_set_period(s_t_rot, s_rot_ms);
    printf("APP rot set step=%d ms=%d (%.1f deg/s)\n", s_rot_step, s_rot_ms, s_rot_step * 100.0 / s_rot_ms);
}

void scr_now_debug_hide(int mask)
{
    if (!s_title) return;
    lv_obj_t *o[] = { s_cover, s_title };
    for (int i = 0; i < 2; i++) if (o[i]) { if (mask & (1 << i)) lv_obj_add_flag(o[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o[i], LV_OBJ_FLAG_HIDDEN); }
    printf("APP now hide=%d\n", mask);
}

static void on_suspend(void) { now_menu_close(); }   /* covered (a message, the alarm) or dark: the playing ring goes now, not when its 4 s run out */
const ui_screen_t scr_now = { .name = "now", .create = create, .destroy = destroy, .on_knob = ui_axis_knob, .on_suspend = on_suspend, .on_resume = now_leave_if_stopped };   /* the playing ring's play order borrows the knob for 4 s after its tap */

/* The playing page is what the user sees, exactly once. Its state is file statics, so a second instance would share them
 * (popping the upper one freed what the lower one's timers still used). The spectrum page or the cover page already on top: nothing
 * to do (the page's own on_change hook shows the new track). Otherwise one already on the stack is popped together with the
 * pages above it (the list or picker the pick came from) and a new one is pushed: a kept page was not identical to a fresh one
 * (screenshots in an internal note). Only when this push really happened and now_view=viz, the spectrum page goes
 * over it (a full stack: ui_push printed its message, nothing more happens). Never called from a create.
 * The root (main menu) is never the playing page, so the loop ends. */
void now_open(void)
{
    if (ui_top() == &scr_viz || ui_top() == &scr_now) return;
    while (ui_on_stack(&scr_now) && ui_layer(1)) ui_pop();
    ui_push(&scr_now, NULL);
    if (ui_top() == &scr_now && now_view_viz()) ui_push(&scr_viz, NULL);
}
