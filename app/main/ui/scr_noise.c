// SPDX-License-Identifier: LGPL-2.1-or-later
/* White noise (spec 4.7): scene list (ruling R23; favourites first) -> scene screen with per-layer volume. No animation. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui_theme.h"
#include "cfg.h"
#include "font.h"
#include "noise_engine.h"
#include "pc/pc_layerset.h"
#include "screens.h"
#include "ui_kit.h"
#include "color_noise.h"
#include "pc/pc_noisegen.h"
#include "stats_svc.h"
#include "ui_axis.h"
#include "ui_popring.h"
#include "ui_ring.h"

static lv_obj_t *s_list;                      /* the scene list */

static const ui_screen_t scr_sound_cats;
extern const ui_screen_t scr_scene;
extern const ui_screen_t scr_noise;

/* ---- scene screen (UI unify T6): one list, a row per layer ("● name 60%" / "・ name"), then add a sound, play, favourite, save, delete (custom only).
 * A tap on a layer row opens that layer's ring: on/off (tap), volume (a bounded axis, the knob steps it), Back = close, the centre = the layer name. ---- */
static lv_obj_t *s_root, *s_rows[PC_SCENE_MAX_LAYERS], *s_play, *s_fav, *s_lcol;   /* s_lcol: the list (cleared with it) */
static int s_scene, s_ring_layer = -1;
static bool s_full;                           /* the last on/off tap found three layers on: the ring's centre says so until the next tap */
enum { C_ONOFF, C_VOL, C_N };
static const ui_popring_t s_ring_def;

static void row_text(int i)
{
    noise_layer_state_t st = noise_engine_layer_state(s_scene, i);
    const char *n = noise_layer_name(s_scene, i);
    char nb[48], buf[96];
    if (!n[0]) { snprintf(nb, sizeof nb, "%s %d", T(S_LAYER_2) /* layers */, i + 1); n = nb; }
    if (st == NOISE_LAYER_FAILED) snprintf(buf, sizeof buf, TZ("・ %s  %s"), n, T(S_FAILED) /* Cannot play */);
    else if (st == NOISE_LAYER_OFF) snprintf(buf, sizeof buf, TZ("・ %s"), n);
    else snprintf(buf, sizeof buf, "● %s %d%%", n, (int)(noise_engine_layer_volume(i) * 100 + 0.5f));
    lv_label_set_text(lv_obj_get_child(s_rows[i], 0), buf);
    lv_obj_set_style_bg_color(s_rows[i], i == s_ring_layer && ui_popring_up(&s_ring_def) ? UI_SEL : UI_PANEL2, 0);
}
static void play_text(void)
{
    const bool on = noise_current() == s_scene;
    lv_label_set_text(lv_obj_get_child(s_play, 0), on ? T(S_STOP_2) /* ■ Stop */ : T(S_PLAY_2) /* ▶ Play */);
    lv_obj_set_style_bg_color(s_play, on ? UI_SEL : UI_PANEL2, 0);
}
static bool is_fav(void)
{
    char id[96];
    snprintf(id, sizeof id, "scene:%s", noise_scene(s_scene)->id);
    return pc_stats_is_fav(g_stats, id);
}
static void fav_text(void)
{
    lv_label_set_text(lv_obj_get_child(s_fav, 0), is_fav() ? T(S_UNFAVOURITE) /* Unfavourite */ : T(S_FAVOURITE) /* Favourite */);
    lv_obj_set_style_bg_color(s_fav, is_fav() ? UI_SEL : UI_PANEL2, 0);
}
static void rows_refresh(void)
{
    for (int i = 0; i < PC_SCENE_MAX_LAYERS && s_rows[i]; i++) row_text(i);
    if (s_play) play_text();
}
static void poll_cb(lv_timer_t *t) { (void)t; rows_refresh(); }   /* loading -> on, failures, a closed ring's row colour */

/* ---- the layer ring ---- */
static const char *ring_label(int i)
{
    static char onoff[40], vol[40];                       /* the ring asks for every cell before it copies any: one buffer each */
    if (i == C_ONOFF) {
        snprintf(onoff, sizeof onoff, "%s\n%s", T(S_ON_OFF) /* On/Off */, noise_engine_layer_state(s_scene, s_ring_layer) == NOISE_LAYER_OFF ? T(S_OFF) /* Off */ : T(S_ON) /* On */);
        return onoff;
    }
    snprintf(vol, sizeof vol, "%s\n%d%%", T(S_VOLUME) /* Volume */, (int)(noise_engine_layer_volume(s_ring_layer) * 100 + 0.5f));
    return vol;
}
static const char *ring_centre(void)
{
    static char b[48];
    if (s_full) return T(S_3_LAYERS_AT_MOST) /* At most 3 layers */;
    const char *n = noise_layer_name(s_scene, s_ring_layer);
    if (n[0]) return n;
    snprintf(b, sizeof b, "%s %d", T(S_LAYER_2) /* layers */, s_ring_layer + 1);
    return b;
}
static void ring_pick(int i)
{
    if (i == C_ONOFF) {
        s_full = noise_engine_toggle_layer(s_scene, s_ring_layer) == PC_LS_FULL;
        if (s_full) ui_haptic_tap();
        rows_refresh();
        ui_popring_relabel();
    } else if (i < 0) ui_popring_close();                 /* the centre has the layer's name: pick decides */
}
static bool ring_step(int axis, int d)                    /* the volume: 0..100 % in steps of 5, bounded (ui_axis_step) */
{
    (void)axis;
    if (s_ring_layer < 0) return false;
    const int v = (int)(noise_engine_layer_volume(s_ring_layer) * 100 + 0.5f);
    noise_engine_set_layer_volume(s_ring_layer, (float)ui_axis_step(v, 0, 100, 5, d, ui_axis_from_tap()) / 100.0f);
    s_full = false;
    row_text(s_ring_layer);
    ui_popring_relabel();                                 /* the cell shows the new value; a fresh 4 s */
    return true;
}
static const ui_popring_t s_ring_def = { .n = C_N, .label = ring_label, .pick = ring_pick, .centre = ring_centre,
                                         .axis_mask = 1u << C_VOL, .step = ring_step };
static void layer_cb(lv_event_t *e)
{
    ui_arm_cancel();                                      /* a tap on a layer row is a tap anywhere else: an armed delete row goes back */
    s_ring_layer = (int)(intptr_t)lv_event_get_user_data(e);
    s_full = false;
    ui_popring_open(s_root, &s_ring_def, 0);
    rows_refresh();
}

/* ---- the rows below the layers ---- */
static void add_cb(lv_event_t *e) { (void)e; ui_push(&scr_sound_cats, NULL); }
static void play_cb(lv_event_t *e)
{
    (void)e;
    if (noise_current() == s_scene) noise_engine_stop(false);
    else noise_engine_start(s_scene);
    rows_refresh();
}
static void fav_cb(lv_event_t *e)
{
    (void)e;
    char id[96];
    snprintf(id, sizeof id, "scene:%s", noise_scene(s_scene)->id);
    pc_stats_toggle_fav(g_stats, id);
    fav_text();
}
/* after a save or a delete the indices moved: the scene list again, once, rebuilt, on top. It is popped to when it is on the stack (the pages over it go:
 * this page, the cover page when the scene page came from the playing ring; the noise may still play: the main menu's noise entry opens the cover page, R17);
 * else (the scene page came from the playing ring of a favourite) this page is replaced by it */
static void scene_list_again(void)
{
    ui_pop_to(&scr_noise);
    ui_replace(&scr_noise, NULL);
}
static void save_cb(lv_event_t *e)
{
    (void)e;
    if (noise_save_custom(s_scene) < 0) { ui_push_message(T(S_NOTHING_TO_SAVE_2) /* No sound to save */); return; }
    scene_list_again();                            /* the saved scene is a row now, marked and in view when it plays */
}
static void del_cb(lv_event_t *e)              /* custom scenes: tap once to arm (the row says so, 4 s), again to delete */
{
    if (!ui_arm_tap(lv_event_get_target_obj(e), T(S_TAP_AGAIN_TO_DELETE) /* Tap again to delete */)) return;
    if (noise_delete_custom(s_scene) == 0) scene_list_again();
}

static void scene_create(lv_obj_t *p, void *arg)
{
    s_scene = (int)(intptr_t)arg;
    s_root = p;
    s_ring_layer = -1;
    s_full = false;
    s_lcol = NULL;
    s_play = s_fav = NULL;
    for (int i = 0; i < PC_SCENE_MAX_LAYERS; i++) s_rows[i] = NULL;
    const pc_scene_t *sc = noise_scene(s_scene);
    if (!sc) {                                     /* a bad index from whoever pushed us: an empty page with a way out */
        lv_label_set_text(ui_text(p, UI_SLOT_CENTER, NULL), "?");
        ui_back_button(p);
        return;
    }
    char title[160];
    if (sc->missing) snprintf(title, sizeof title, "%s (%s %d)", pc_pick_name(sc->name, sc->en), T(S_MISSING) /* missing */, sc->missing);
    else snprintf(title, sizeof title, "%s%s", pc_pick_name(sc->name, sc->en), noise_scene_modified(s_scene) ? T(S_EDITED) /* (modified) */ : "");
    const uint32_t c0 = lv_obj_get_child_count(p);
    lv_obj_t *list = ui_obj_guard(ui_list_titled(p, title), &s_lcol);
    lv_obj_t *tl = lv_obj_get_child(p, c0);        /* the title the kit just made: a long name scrolls */
    if (tl) lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    for (int i = 0; i < sc->nlayers; i++) s_rows[i] = ui_list_row(list, "", UI_PANEL2, layer_cb, (void *)(intptr_t)i);
    if (sc->nlayers < PC_SCENE_MAX_LAYERS) ui_list_row(list, T(S_ADD_A_SOUND) /* + Add sound */, UI_PANEL2, add_cb, NULL);   /* custom scenes, spec 5.6 B */
    s_play = ui_list_row(list, "", UI_PANEL2, play_cb, NULL);
    s_fav = ui_list_row(list, "", UI_PANEL2, fav_cb, NULL);
    ui_list_row(list, T(S_SAVE_AS_NEW) /* Save as new scene */, UI_PANEL2, save_cb, NULL);
    if (sc->custom) ui_list_row(list, T(S_DELETE_SCENE) /* Delete scene */, UI_SYS, del_cb, NULL);
    fav_text();
    rows_refresh();
    ui_back_button(p);
    ui_timer_bind(p, poll_cb, 300, NULL);
    ui_sleep_arc(p);
}
static void scene_destroy(void)
{
    if (ui_popring_up(&s_ring_def)) ui_popring_close();   /* a borrowed volume ends with the page */
    for (int i = 0; i < PC_SCENE_MAX_LAYERS; i++) s_rows[i] = NULL;
    s_play = s_fav = s_root = NULL;                    /* the poll is bound to the root; s_lcol is cleared with its list (ui_obj_guard) */
}
static bool scene_knob(int d)
{
    if (ui_axis_knob(d)) return true;                  /* a borrowed layer volume */
    ui_arm_cancel();                                   /* scrolling cancels an armed delete row (as the mp3 page and settings do) */
    return ui_list_knob(s_lcol, d);                    /* else the knob scrolls the rows */
}
const ui_screen_t scr_scene = { .name = "scene", .create = scene_create, .destroy = scene_destroy, .on_knob = scene_knob };

/* ---- add a sound: category -> sound (spec 5.6 B) ---- */
static lv_obj_t *s_catlist, *s_slist;         /* one list per page: the category page under the sound page keeps its own */
static int s_cat;
static bool slist_knob(int d) { return ui_list_knob(s_slist, d); }
static bool catlist_knob(int d) { return ui_list_knob(s_catlist, d); }
static lv_obj_t *slist_make(lv_obj_t *p, const char *title)   /* the shared titled list */
{
    lv_obj_t *l = ui_list_titled(p, title);
    ui_back_button(p);
    return l;
}
static void slist_row(lv_obj_t *list, const char *text, lv_event_cb_t cb, int user) { ui_list_row(list, text, UI_PANEL, cb, (void *)(intptr_t)user); }

static void sound_cb(lv_event_t *e)
{
    int r = noise_engine_add_sound(s_scene, (int)(intptr_t)lv_event_get_user_data(e));
    if (r == -EEXIST) { ui_haptic_tap(); return; }   /* already in the scene: stay on the list */
    if (r < 0) { ui_push_message(r == -ENOSPC ? T(S_16_SOUNDS_AT_MOST) /* 16 sounds already */ : T(S_COULD_NOT_ADD) /* Cannot add */); return; }
    ui_pop_to(&scr_scene);                           /* the sound and category lists go */
    ui_replace(&scr_scene, (void *)(intptr_t)s_scene);   /* the scene page rebuilt in place: the new layer is a row (off when three were on: its mark says so) */
}
static void sounds_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(slist_make(p, noise_cat_name(s_cat)), &s_slist);
    static int idx[800];
    int n = noise_cat_sounds(s_cat, idx, 800);
    for (int i = 0; i < n; i++) slist_row(s_slist, noise_sound_name(idx[i]), sound_cb, idx[i]);
}
static const ui_screen_t scr_sounds = { .name = "sounds", .create = sounds_create, .on_knob = slist_knob };

static void cat_cb(lv_event_t *e) { s_cat = (int)(intptr_t)lv_event_get_user_data(e); ui_push(&scr_sounds, NULL); }
static void cats_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(slist_make(p, T(S_CATEGORY) /* Pick a category */), &s_catlist);
    static int idx[800];
    for (int c = 0; c < noise_cat_count(); c++) {
        char b[64];
        snprintf(b, sizeof b, "%s  (%d)", noise_cat_name(c), noise_cat_sounds(c, idx, 800));
        slist_row(s_catlist, b, cat_cb, c);
    }
}
static const ui_screen_t scr_sound_cats = { .name = "sound_cats", .create = cats_create, .on_knob = catlist_knob };

/* ---- the scene list (ruling R23): Noise colours first (system row), favourites, then the other scenes; the playing scene is marked and in view; a tap plays ---- */
static void colors_cb(lv_event_t *e) { (void)e; ui_push(&scr_colors, NULL); }   /* the generated colours */
static void breath_cb(lv_event_t *e) { (void)e; ui_push(&scr_breath, NULL); }   /* ideas G7: the breathing coach */
static void scene_cb(lv_event_t *e)
{
    int si = (int)(intptr_t)lv_event_get_user_data(e);
    printf("APP pick noise %d\n", si);
    if (noise_current() != si) noise_engine_start(si);
    now_open();                                  /* spec 5.8: the playing page; its menu has "sounds" */
}
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    cfg_set_str("last_screen", "noise");
    int n = noise_load();
    ui_obj_guard(ui_list(p), &s_list);
    ui_list_row(s_list, T(S_NOISE_COLOURS) /* Noise colours */, UI_SYS, colors_cb, NULL);
    ui_list_row(s_list, T(S_BREATHING_COACH) /* Breathing coach */, UI_SYS, breath_cb, NULL);   /* ideas G7 (owner): the second row, right under Noise colours */
    lv_obj_t *cur = NULL;
    for (int pass = 0; pass < 2; pass++)          /* favourites first */
        for (int i = 0; i < n; i++) {
            char id[96];
            snprintf(id, sizeof id, "scene:%s", noise_scene(i)->id);
            if (pc_stats_is_fav(g_stats, id) != (pass == 0)) continue;
            lv_obj_t *r = ui_list_row(s_list, noise_scene_name(i), i == noise_current() ? UI_SEL : UI_PANEL, scene_cb, (void *)(intptr_t)i);
            if (i == noise_current()) cur = r;
        }
    if (!n) ui_list_note(s_list, T(S_NO_LIBRARY_MUK) /* No library.muk */);
    ui_back_button(p);
    lv_obj_update_layout(s_list);
    ui_list_show(s_list, cur);
}
static bool on_knob(int d) { return ui_list_knob(s_list, d); }
const ui_screen_t scr_noise = { .name = "noise", .create = create, .on_knob = on_knob };

/* ---- coloured noise ring ---- */
static void back(void) { ui_pop(); }
static void color_pick(int idx)
{
    if (idx < 0 || idx >= PC_NOISE_COLORS) return;
    if (color_noise_current() != idx && color_noise_play(idx)) { ui_push_message(T(S_CANNOT_PLAY) /* Cannot play */); return; }
    now_open();
}
static void colors_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[PC_NOISE_COLORS];
    for (int i = 0; i < PC_NOISE_COLORS; i++) labels[i] = pc_noise_name(i, i18n_zh());
    lv_obj_t *r = ui_ring_create(p, labels, PC_NOISE_COLORS, color_pick, back);
    if (color_noise_current() >= 0) ui_ring_set_center_text(r, pc_noise_name(color_noise_current(), i18n_zh()));
}
const ui_screen_t scr_colors = { .name = "colors", .create = colors_create };
