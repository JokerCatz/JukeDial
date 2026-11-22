// SPDX-License-Identifier: LGPL-2.1-or-later
/* Spectrum page (spec 2026-11-11 section 3): the picture comes from pc_vizdraw (software renderer, RGB565) and is shown by one
 * lv_image; each frame invalidates only the rectangle that changed. All six kinds are wired (voices: one bar per chip or tracker voice, only while audio_svc_voices has some; else plain bars). The stereo tap of the audio task
 * (phase) runs only while this page is on top, the screen is on and the kind is phase. The page opens over the cover page (scr_now), so "back" returns to it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "audio_svc.h"
#include "lowmem.h"
#include "now_menu.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_lrc.h"
#include "pc/pc_vmute.h"
#include "pc/pc_vizdraw.h"
#include "player_ctl.h"
#include "power_svc.h"
#include "screens.h"
#include "thumb_svc.h"
#include "ui_help.h"
#include "ui_lyrics.h"
#include "ui_ninegrid.h"
#include "ui_axis.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "font.h"
#include "pc/pc_dial.h"

/* VIZ_STEREO: 1 = phase reads the stereo tap. The board judgement (Task 6 step 5) may turn it to 0: the places that depend on
 * it are marked VIZ_STEREO (drawn_kind, stereo_kind, the phase case in tick). */
#define VIZ_STEREO 1
#define TICK_MS 33

/* big or rarely-touched state lives in PSRAM: the renderer's memory (1.3 KB) and, per page, the picture (340 x 340 x 2 = 231200 bytes) and the ring map (254126 bytes) */
EXT_RAM_BSS_ATTR static pc_vd_t s_d;
#define LR_FRAMES 1024
EXT_RAM_BSS_ATTR static int16_t s_lr[2 * LR_FRAMES];     /* the stereo frames of one picture */
static bool s_st;                        /* the stereo tap is really on (audio_svc_want_stereo said so) */
static bool s_st_fail;                   /* it could not be switched on (PSRAM): not tried again until the next apply() */
static float s_level[64], s_peak[64], s_hold[64];
#define WAVE_N 600                       /* 2 * 300 columns: what pc_vd_scope looks at for the trigger */
EXT_RAM_BSS_ATTR static float s_wave[WAVE_N];   /* the scope's samples, per frame, off the LVGL task's stack and out of internal RAM */
/* lyrics (kind 歌詞): the parsed .lrc of the track in PSRAM (pc_lrc.h: 28 KB, bounded), the track it was read for, and the view; voice rows / piano roll: their state (PSRAM, about 10 KB) */
EXT_RAM_BSS_ATTR static pc_lrc_t s_lrc;
EXT_RAM_BSS_ATTR static pc_vd_vstate_t s_vs;
#define LRC_PATH_MAX 288   /* a FAT path is at most 260: the whole path is kept (160 cut long names, and two with one prefix shared lyrics) */
EXT_RAM_BSS_ATTR static char s_lrc_for[LRC_PATH_MAX];   /* the audio path the lyrics belong to ("" none loaded) */
EXT_RAM_BSS_ATTR static char s_lrc_cur[LRC_PATH_MAX];   /* lyrics_tick's copy of the playing path (off the LVGL task's stack) */
EXT_RAM_BSS_ATTR static char s_lrc_file[LRC_PATH_MAX + 8];
static int s_lrc_err;                    /* PC_LRC_OK, or why there are no lyrics (a missing file is PC_LRC_E_ARG) */
static int s_lrc_idx = -2;               /* the line shown (-1 before the first, -2 nothing set yet) */
EXT_RAM_BSS_ATTR static uint8_t s_mstate[PC_VOICES_MAX];   /* the mute / solo state the picture is drawn with (pc_vmute_get, copied once per frame) */
static int s_sel;                        /* the voice the style ring's cursor is on (axis 4 of the voice kinds) */
static lv_obj_t *s_root, *s_img, *s_title, *s_lyr;
static lv_image_dsc_t s_dsc;
static int s_zoom = 256;                 /* the picture's display scale (256 = 1:1) */
static bool cur_zoom(void);
static int img_zoom(pc_vd_kind_t k, int w)
{
    if (k == PC_VD_FALL || k == PC_VD_PHASE) return 256;   /* the waterfall is dear to draw: not enlarged; the phase picture keeps its size */
    if (!cur_zoom()) return 256;                           /* the square panel's size switch "Original": no scaling at all */
    return ui_picture_zoom(w);                             /* the square panel: bars, ring, scope and the voice pictures fill the width (the same drawer, shown larger); round: 1:1 */
}
static uint16_t *s_px;                   /* PC_VD_MAX_PX pixels */
static void *s_map;                      /* ring map, built when the ring is first wanted */
static bool s_live;                      /* the picture exists and can be drawn */
static uint8_t s_vn;                     /* voices the picture was applied for: > 0 only while the kind is voices and the source has some (the voices' follow state is s_level/s_peak/s_hold) */
static pc_vd_kind_t s_kind;              /* what is drawn (bars stands in for phase when VIZ_STEREO is off) */
static int s_var, s_barw;
static float s_floor;
static int64_t s_last_us, s_changed_ms;
static uint32_t s_tick;
static void (*s_prev_hook)(void);
/* the style ring is a ring of axes (ui_axis.h): the ring, its lines, the borrowed knob and the 4 s live there; the page keeps the working set below */
static ui_axis_ring_t RING;                 /* defined below; n is set when the ring opens (the square board has the size cell too) */
static int64_t s_title_until_ms;        /* the top title line hides at this time (TITLE_MS after the last of: entering the page, a track or state change, the end of the hint) */
static bool s_was_on = true;            /* the screen was on at the last tick: coming back on is an "enter" too */
static bool s_was_top;                  /* the last tick found this page on top: a covered page that comes back on top is an "enter" */
static bool s_notrack;                 /* the hint after a centre tap says "No track" (MP3 owns playback but nothing is playing: no details to open) */
/* While the ring is open every axis works on these copies (the ring's "working set"); they are written to the settings when it closes (ring_flush),
 * so a value that ends where it started causes no write, and everything that changed is written once, at close (settings are already debounced by cfg_save_soon). The page reads kind, variant, ramp and map through cur_* / var_of: the copies while the
 * ring is open (OPEN), the settings otherwise. */
static pc_vd_kind_t s_wk;               /* the kind as chosen (may be phase even when drawn_kind shows bars) */
static int8_t s_wv[PC_VD_KINDS];        /* the variant of every kind */
static pc_ramp_t s_wr;
static pc_vd_map_t s_wm;
static pc_vd_order_t s_wo;               /* the ring kind's band order and wedge gap (px): slot 4 on the ring kind steps the 9 combinations */
static int s_wgap;
static bool s_wpk;                      /* the falling peak marks (rides the map axis: eight values, the four maps with peaks then without) */
static bool s_wfs;                      /* the waterfall is the full-size picture (cfg viz_fall) */
static bool s_wz;                       /* the square board's size switch: zoomed (cfg viz_zoom) */
static int s_wbw, s_wfl;                /* bar width (5|7|9) and sensitivity floor (-60|-48|-36 dB) */
static bool s_from_menu;                /* the style ring was opened from the playing ring: its Back cell returns there */
#define OPEN ui_axis_ring_up(&RING)     /* the style ring is open: the knob steps its current axis (0 kind, 1 variant, 2 colour, 3 map, 4 the kind's own, 5 sensitivity [the voice kinds: the voice's state], 6 size [the square board only], then the help cell: the cells, in this order) */
#define TITLE_MS 10000
#define AX_SIZE 6                       /* the square board's size switch (原尺寸 / 放大) */
/* the panel decides at run time (ui_by_panel): PC_VD_ROUND_SCREEN is a drawer switch no build turns off, the square board shows the round pictures too */
#define SQUARE ui_by_panel(0, 1)
#define AX_HELP ui_by_panel(6, 7)       /* the last cell opens HELP_VIZ */
#define NAXIS (AX_HELP + 1)
static uint32_t s_dn, s_dsum, s_dmax;   /* draw time of the page's own render call: frames, total and worst (us) since open / last "viz stat" */

/* the kinds that draw the voice snapshot: they exist only while the source has voices (else the bars stand in, the stored setting stays) */
static bool is_vk(pc_vd_kind_t k) { return k == PC_VD_VOICES || k == PC_VD_ROWS || k == PC_VD_ROLL; }
static bool rows_like(pc_vd_kind_t k) { return k == PC_VD_ROWS || k == PC_VD_ROLL; }   /* these two have the mute / solo axes */

/* VIZ_STEREO */
static pc_vd_kind_t drawn_kind(pc_vd_kind_t k) { return VIZ_STEREO || k != PC_VD_PHASE ? k : PC_VD_BARS; }
/* VIZ_STEREO: does this kind need the stereo tap? */
static bool stereo_kind(pc_vd_kind_t k) { return VIZ_STEREO && k == PC_VD_PHASE; }

/* the kinds the kind axis walks, in order: phase only with the stereo tap (VIZ_STEREO), voices only while the source has voices */
static int kind_list(pc_vd_kind_t *out)
{
    int c = 0;
    out[c++] = PC_VD_BARS; out[c++] = PC_VD_RING; out[c++] = PC_VD_SCOPE; out[c++] = PC_VD_FALL;
    if (VIZ_STEREO) out[c++] = PC_VD_PHASE;
    pc_voices_t v;
    if (audio_svc_voices(&v) > 0) { out[c++] = PC_VD_VOICES; out[c++] = PC_VD_ROWS; out[c++] = PC_VD_ROLL; }
    out[c++] = PC_VD_LYRICS;
    return c;
}

static pc_vd_kind_t cur_kind(void) { return drawn_kind(OPEN ? s_wk : ui_viz_kind()); }
static int var_of(pc_vd_kind_t k) { return OPEN ? s_wv[k] : ui_viz_var(k); }
static pc_ramp_t cur_ramp(void) { return OPEN ? s_wr : ui_viz_ramp(); }
static pc_vd_map_t cur_map(void) { return OPEN ? s_wm : ui_viz_map(); }
static int cur_bw(void) { return OPEN ? s_wbw : ui_viz_bar_w(); }
static bool cur_peaks(void) { return OPEN ? s_wpk : ui_viz_peaks(); }
static pc_vd_order_t cur_order(void) { return OPEN ? s_wo : ui_viz_order(); }
static int cur_gap(void) { return OPEN ? s_wgap : ui_viz_ring_gap(); }
static bool cur_fs(void) { return OPEN ? s_wfs : ui_viz_fall_full(); }
static bool cur_zoom(void) { return SQUARE && (OPEN ? s_wz : ui_viz_zoom()); }   /* the round board never zooms */
static int cur_fl(void) { return OPEN ? s_wfl : (int)ui_viz_floor_db(); }
static void ring_flush(void)                          /* RING.store: settings get only what ended up different from what they hold */
{
    if (ui_viz_kind() != s_wk) ui_viz_kind_set(s_wk);
    for (int k = 0; k < PC_VD_KINDS; k++) if (ui_viz_var((pc_vd_kind_t)k) != s_wv[k]) ui_viz_var_set((pc_vd_kind_t)k, s_wv[k]);
    if (ui_viz_ramp() != s_wr) ui_viz_ramp_set(s_wr);
    if (ui_viz_map() != s_wm) ui_viz_map_set(s_wm);
    if (ui_viz_bar_w() != s_wbw) ui_viz_bar_w_set(s_wbw);
    if (ui_viz_peaks() != s_wpk) ui_viz_peaks_set(s_wpk);
    if (ui_viz_order() != s_wo) ui_viz_order_set(s_wo);
    if (ui_viz_ring_gap() != s_wgap) ui_viz_ring_gap_set(s_wgap);
    if (ui_viz_fall_full() != s_wfs) ui_viz_fall_full_set(s_wfs);
    if (ui_viz_zoom() != s_wz) ui_viz_zoom_set(s_wz);
    if ((int)ui_viz_floor_db() != s_wfl) ui_viz_floor_set(s_wfl);
}

/* the one place that talks to the audio task about the tap; only on a change */
static void set_stereo(bool on)
{
    if (on == s_st) return;
    s_st = audio_svc_want_stereo(on);
    if (on && !s_st) s_st_fail = true;
}

static void set_title(void)                            /* the one text of the top line */
{
    if (!s_title) return;
    const now_ops_t *o = player_ctl_now();
    char t[300];
    if (!o || !o->playing()) snprintf(t, sizeof t, "%s", T(S_STOPPED_2) /* Stopped */);   /* the cover page's text */
    else o->title(t, sizeof t);
    lv_label_set_text(s_title, t);
}

/* the top title line shows for TITLE_MS from now (entering, a track or state change, the end of the hint), then tick hides it; not while the ring or its hint use the room */
static void title_show(void)
{
    s_title_until_ms = esp_timer_get_time() / 1000 + TITLE_MS;
    if (s_title && !OPEN && !ui_axis_hinting(&RING)) lv_obj_remove_flag(s_title, LV_OBJ_FLAG_HIDDEN);
}

static void on_change(void)
{
    const now_ops_t *o = player_ctl_now();
    if (o && o->playing()) s_changed_ms = esp_timer_get_time() / 1000;   /* the bars rest a moment after a new track (stop: they fall at once) */
    set_title();
    title_show();
    if (s_prev_hook) s_prev_hook();                    /* the cover page underneath still keeps its own state */
}

/* ---- lyrics (kind 歌詞) ---- */
/* the lyrics view: `side` lines each way (1 or 2), 0 removes it. A different side count rebuilds it. */
static void lyrics_view(int side)
{
    if (s_lyr && (side == 0 || ui_lyrics_side(s_lyr) != side)) { lv_obj_delete(s_lyr); s_lyr = NULL; }
    if (side && !s_lyr && s_root) s_lyr = ui_lyrics(s_root, side);
}

/* the lyrics of the track playing now: the .lrc next to the audio file (library songs only), parsed once per track; the file is read here, in the LVGL task
 * (at most 64 KB, one SD read when the lyrics kind is on screen and the track changed) */
static void lyrics_load(const char *audio)
{
    snprintf(s_lrc_for, sizeof s_lrc_for, "%s", audio);
    s_lrc_err = PC_LRC_E_ARG;
    s_lrc.n = 0;
    s_lrc_idx = -2;
    char *const path = s_lrc_file;
    if (!audio[0] || !pc_lrc_path(audio, path, sizeof s_lrc_file)) return;
    FILE *f = fopen(path, "rb");
    if (!f) { size_t n = strlen(path); if (n > 4) { memcpy(path + n - 4, ".LRC", 4); f = fopen(path, "rb"); } }   /* the card's names are case-insensitive on FAT but the simulator's are not */
    if (!f) return;
    char *buf = heap_caps_malloc(PC_LRC_MAX_FILE, MALLOC_CAP_SPIRAM);
    if (buf) {
        const size_t n = fread(buf, 1, PC_LRC_MAX_FILE, f);
        s_lrc_err = pc_lrc_parse(&s_lrc, buf, n);
        heap_caps_free(buf);
    }
    fclose(f);
}

/* the audio file of the playing library song ("" for anything else: chips, radio, noise, Bluetooth have no lyrics) */
static const char *lyrics_audio(char *b, size_t n)
{
    b[0] = 0;
    const now_ops_t *o = player_ctl_now();
    uint32_t off, len;
    if (o && o->src == NOW_SRC_MP3 && o->cover && !o->cover(b, n, &off, &len)) b[0] = 0;
    return o && o->src == NOW_SRC_MP3 ? b : "";
}

static void lyrics_tick(void)
{
    const char *audio = lyrics_audio(s_lrc_cur, sizeof s_lrc_cur);
    if (strcmp(audio, s_lrc_for)) lyrics_load(audio);
    if (!s_lyr) return;
    if (s_lrc_err != PC_LRC_OK) {
        if (s_lrc_idx != -3) { ui_lyrics_note(s_lyr, s_lrc_err == PC_LRC_E_ENCODING ? T(S_LYRICS_NOT_UTF8) : T(S_NO_LYRICS)); s_lrc_idx = -3; }
        return;
    }
    aud_status_t st;
    audio_svc_status(&st);
    const int idx = pc_lrc_find(&s_lrc, st.pos_ms);
    if (idx == s_lrc_idx) return;
    s_lrc_idx = idx;
    const char *t[5];
    for (int i = 0; i < 5; i++) { const int j = idx + i - 2; t[i] = j >= 0 && j < s_lrc.n ? pc_lrc_text(&s_lrc, j) : NULL; }
    if (idx < 0) { t[2] = NULL; }                      /* before the first line: only what comes */
    ui_lyrics_set(s_lyr, t);
}

/* (re)size the picture for the configured kind and variation; everything is redrawn on the next frame */
static void apply(void)
{
    s_live = false;
    s_st_fail = false;                                 /* a failed switch-on is tried again here */
    if (!s_px || !s_img) { set_stereo(false); return; }   /* every way out of here that is not a live picture leaves the tap off */
    pc_vd_kind_t k = cur_kind();
    int w, h;
    const uint8_t vn_before = s_vn;
    s_vn = 0;
    if (k == PC_VD_LYRICS) {                           /* no picture: the lyrics view (LVGL labels) replaces it */
        s_kind = k;
        s_var = var_of(k);
        lyrics_view(s_var + 1);
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        s_zoom = 256;
        s_live = true;
        s_lrc_idx = -2;                                /* the next tick sets the text */
        set_stereo(false);
        return;
    }
    lyrics_view(0);                                    /* any other kind: the lyrics view goes */
    if (is_vk(k)) {                                    /* voices, rows and roll only while the source has voices; else the bars stand in (the stored setting stays) */
        pc_voices_t v;
        const int n = audio_svc_voices(&v);
        if (n > 0) s_vn = (uint8_t)n; else k = PC_VD_BARS;
    }
    int var = var_of(k);
    const bool ring_pic = k == PC_VD_RING || (k == PC_VD_VOICES && var == 2);   /* the pictures that need the ring map */
    const int want_gap = k == PC_VD_VOICES ? 0 : cur_gap();    /* the voices' ring keeps its plain look; the ring kind wants the stored gap */
    if (ring_pic && (!s_map || pc_vd_ringmap_gap(s_map) != want_gap)) {   /* built the first time the ring is wanted and again (in place) only when the wanted gap differs */
        size_t cap = pc_vd_ringmap_bytes(PC_VD_RING_SZ, PC_VD_RING_SZ);
        if (!s_map) s_map = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
        if (s_map && !pc_vd_ringmap_build(s_map, cap, PC_VD_RING_SZ, PC_VD_RING_SZ, PC_VD_RING_IN, PC_VD_RING_OUT, want_gap)) { heap_caps_free(s_map); s_map = NULL; }
    }
    if (ring_pic && !s_map) {                           /* no ring map: bars for this visit, the stored setting stays ring; voices fall back to their own bars variant 0 */
        if (k == PC_VD_RING) { k = PC_VD_BARS; var = var_of(k); }
        else var = 0;
    }
    if (k != s_kind || s_vn != vn_before) { memset(s_level, 0, sizeof s_level); memset(s_peak, 0, sizeof s_peak); memset(s_hold, 0, sizeof s_hold); pc_vd_vstate_reset(&s_vs); }
    s_kind = k;
    s_var = var;
    s_barw = cur_bw();
    s_floor = (float)cur_fl();
    if (k == PC_VD_FALL) pc_vd_size_fall(cur_fs(), &w, &h); else pc_vd_size(k, var, &w, &h);
    pc_vd_init(&s_d, s_px, w, h);
    pc_vd_set_colors(&s_d, cur_ramp(), ui_theme());
    pc_vd_set_map(&s_d, cur_map());
    pc_vd_set_peaks(&s_d, cur_peaks());
    pc_vd_set_order(&s_d, cur_order());
    pc_vd_blank(&s_d, k);                              /* LVGL shows the buffer before the first tick has drawn anything: it must hold the page background (pc_vd_blank paints the current theme's bg), not pc_vd_init's default-theme one */
    s_dsc = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = (uint32_t)w, .h = (uint32_t)h, .stride = (uint32_t)w * 2 },
                              .data_size = (uint32_t)w * (uint32_t)h * 2, .data = (const uint8_t *)s_px };
    lv_image_cache_drop(&s_dsc);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_set_size(s_img, w, h);
    lv_obj_center(s_img);
    s_zoom = img_zoom(k, w);
    lv_image_set_scale(s_img, s_zoom);                 /* the square panel: bars, ring and scope are drawn at the round size and shown larger (same drawer, no extra drawing cost) */
    lv_obj_remove_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_img);
    if (s_title) lv_obj_move_foreground(s_title);     /* the square panel shows the picture enlarged: its blank top rows covered all but the top 8 px of the title */
    s_live = true;
    set_stereo(stereo_kind(k));                        /* only a change of the wanted state touches the tap (a variant turn keeps it on); the tick turns it off again when the page is covered or the screen is off */
}

/* ---- style ring (spec 3.4, redone 2026-11-12 as axes; the mechanics are ui_axis since Task 7): the cells are the axes kind, variant, colour, map, bar width, sensitivity.
 * The ring closes 4 s after the last tap or turn and the lines say the knob is volume again for 1.5 s. It ends from: the centre, the back cell (opened from the
 * playing ring: back to it, else the page is left), the timeout, on_suspend (another page over this one, screen dark), destroy (back, theme change). ---- */
static pc_vd_kind_t var_kind(void);
/* slot 4 is the drawn kind's own extra axis: bars their width, the ring kind its order x gap, the waterfall its size; the other kinds have none (a dash) */
EXT_RAM_BSS_ATTR static pc_voices_t s_axv;   /* the voice snapshot the voice axes read (the ring is a LVGL-task thing, this is its working copy; PSRAM) */
static int axis_vn(void) { return audio_svc_voices(&s_axv); }
static const char *own_name(void)
{
    const pc_vd_kind_t k = var_kind();
    if (rows_like(k)) return T(S_VOICES) /* Voices */;
    return k == PC_VD_RING ? T(S_ORDER_2) /* Layout */ : k == PC_VD_FALL ? T(S_SIZE_2) /* Size */ : T(S_BAR_WIDTH) /* Bar width */;
}
static const char *own_hint(void)
{
    const pc_vd_kind_t k = var_kind();
    if (rows_like(k)) return NULL;                       /* "Knob = Voices" */
    return k == PC_VD_RING ? T(S_KNOB_ORDER) /* Knob = change layout */ : k == PC_VD_FALL ? T(S_KNOB_SIZE) /* Knob = change size */ : T(S_KNOB_WIDTH) /* Knob = change bar width */;
}
static const char *axis_name(int a)
{
    if (a == AX_HELP) return ui_help_label();
    if (SQUARE && a == AX_SIZE) return T(S_SIZE_2) /* Size */;
    if (a == 5 && rows_like(var_kind())) return T(S_STATE) /* State */;
    return a == 0 ? T(S_KIND) /* Type */ : a == 1 ? T(S_VARIANT) /* Variation */ : a == 2 ? T(S_COLOUR_2) /* Colour band */ : a == 3 ? T(S_MAP) /* Colouring */ : a == 4 ? own_name() : T(S_SENSITIVITY) /* Sensitivity */;
}
static const char *axis_hint(int a)
{
    if (a == AX_HELP || (SQUARE && a == AX_SIZE) || (a == 5 && rows_like(var_kind()))) return NULL;   /* the ring's own line: "Knob = <the cell's name>" */
    return a == 0 ? T(S_KNOB_KIND) /* Knob = change type */ : a == 1 ? T(S_KNOB_VARIANT) /* Knob = change variation */ : a == 2 ? T(S_KNOB_COLOUR) /* Knob = change colour band */ : a == 3 ? T(S_KNOB_MAP) /* Knob = change colouring */
         : a == 4 ? own_hint() : T(S_KNOB_SENSITIVITY) /* Knob = change sensitivity */;
}
static const int BARW[3] = { 5, 7, 9 };               /* Thin / Medium / Thick */
static int index_of(const int *v, int x) { for (int i = 0; i < 3; i++) if (v[i] == x) return i; return 1; }
static const char *const MAPPK_ZH[4] = { "高度・無頂點", "火苗・無頂點", "頻段・無頂點", "音量・無頂點" };   /* the map axis' values 4..7: the four maps without the peak marks */
static const char *const MAPPK_EN[4] = { "Height, no caps", "Tip, no caps", "Band, no caps", "Level, no caps" };
static const int GAPS[3] = { 2, 0, 4 };               /* the ring kind's wedge gaps in the order the axis walks them: thin (the default), none, wide */
static const char *const ORDGAP_ZH[9] = { "順序・細縫", "交錯・細縫", "打散・細縫", "順序・無縫", "交錯・無縫", "打散・無縫", "順序・寬縫", "交錯・寬縫", "打散・寬縫" };
static const char *const ORDGAP_EN[9] = { "Seq, thin", "Alt, thin", "Scatter, thin", "Seq, none", "Alt, none", "Scatter, none", "Seq, wide", "Alt, wide", "Scatter, wide" };   /* shortened so the value fits its 160 px line (R55) */
static int ordgap_index(pc_vd_order_t o, int gap) { return index_of(GAPS, gap) * 3 + (int)o; }   /* 0..8: gap_index * 3 + order */
static const int FLOOR_DB[3] = { -60, -48, -36 };     /* Low / Medium / High */

/* the kind whose variants the variant axis steps and names: voices stored while nothing voiced plays are drawn as bars, so the bars' variants (no dead control) */
static pc_vd_kind_t var_kind(void)
{
    const pc_vd_kind_t k = cur_kind();
    pc_voices_t v;
    return is_vk(k) && audio_svc_voices(&v) <= 0 ? PC_VD_BARS : k;
}

static const char *axis_value(int a)                  /* the current axis' value, inside the ring */
{
    static const char *const NONE = "\xE2\x80\x94";   /* an em dash: this kind has no such axis */
    const pc_vd_kind_t k = cur_kind();
    const bool en = !i18n_zh();
    const int bi = index_of(BARW, cur_bw()), fi = index_of(FLOOR_DB, cur_fl());
    const pc_vd_kind_t vk = var_kind();
    if (a == AX_HELP) return NONE;
    if (SQUARE && a == AX_SIZE) return cur_zoom() ? T(S_ZOOMED) /* Zoomed */ : T(S_ORIGINAL_SIZE) /* Original */;
    if (rows_like(vk) && (a == 4 || a == 5)) {
        EXT_RAM_BSS_ATTR static char b[24];
        const int vn = axis_vn();
        if (vn <= 0) return NONE;
        if (s_sel >= vn) s_sel = vn - 1;
        if (a == 4) { snprintf(b, sizeof b, "%d/%d %s", s_sel + 1, vn, s_axv.name[s_sel]); return b; }
        const pc_vm_state_t st = pc_vmute_get(s_sel);
        return st == PC_VM_MUTED ? T(S_MUTE) /* Mute */ : st == PC_VM_SOLO ? T(S_SOLO_VOICE) /* Solo */ : T(S_ON_2) /* On */;
    }
    return a == 0 ? ui_viz_kind_label(s_wk) : a == 1 ? pc_vd_var_label(vk, var_of(vk), en)
         : a == 2 ? ui_ramp_label(cur_ramp()) : a == 3 ? (!pc_vd_kind_has_map(k) ? NONE : cur_peaks() ? pc_vd_map_label(cur_map(), en) : T_RAW(MAPPK_ZH[cur_map()], MAPPK_EN[cur_map()]))
         : a == 4 ? (vk == PC_VD_BARS ? (bi == 0 ? T(S_THIN) /* Thin */ : bi == 2 ? T(S_WIDE_3) /* Thick */ : T(S_MEDIUM) /* Medium */)
                   : vk == PC_VD_RING ? T_RAW(ORDGAP_ZH[ordgap_index(cur_order(), cur_gap())], ORDGAP_EN[ordgap_index(cur_order(), cur_gap())])
                   : vk == PC_VD_FALL && PC_VD_ROUND_SCREEN ? (cur_fs() ? T(S_FULL_2) /* Full */ : T(S_BOX_2) /* Boxed */) : NONE)
         : s_kind == PC_VD_VOICES || s_kind == PC_VD_LYRICS ? NONE : fi == 0 ? T(S_LOW) /* Low */ : fi == 2 ? T(S_HIGH) /* High */ : T(S_MID) /* Medium */;
}
static const char *ring_after(char *b, size_t n)      /* the ring is gone: the whole description, then what the knob does now */
{
    const pc_vd_kind_t k = cur_kind();
    const char *dot = T(S_STR_6) /* ・ */;
    snprintf(b, n, "%s%s%s%s%s", ui_viz_kind_label(k), dot, pc_vd_var_label(k, var_of(k), !i18n_zh()), dot, ui_ramp_label(cur_ramp()));
    return s_notrack ? T(S_NO_TRACK) /* No songs */ : T(S_KNOB_VOLUME) /* Knob = volume */;
}
static void ring_gone(void) { s_notrack = false; title_show(); }   /* the lines went: the song title comes back, its 10 s start again */

static void leave_page(void) { ui_pop(); }
static void style_from_menu(void);
static bool playing_ring(void) { return now_menu_open(s_root, NOW_MENU_VIZ, style_from_menu, leave_page); }   /* false: nothing is playing */
/* the Back cell: opened from the playing ring -> back to it (its 4 s start again); opened directly -> leave the page */
static void ring_back(void)
{
    const bool from_menu = s_from_menu;
    ui_axis_ring_close(false);
    if (!from_menu) ui_pop();
    else playing_ring();
}

/* the centre tap opens the source's details page (MP3) instead of the hint, while the ring is open */
static bool detail_now(void) { const now_ops_t *o = player_ctl_now(); return OPEN && o && o->open_info; }
static void ring_centre(void)
{
    const now_ops_t *o = player_ctl_now();
    if (detail_now() && o->playing()) { ui_axis_ring_close(false); o->open_info(); }   /* Details: no hint, this page is covered at once */
    else if (detail_now()) { s_notrack = true; ui_axis_ring_close(true); }          /* MP3 with no current track: the hint says so, no details page */
    else ui_axis_ring_close(true);
}


/* RING.step: step axis a by n (signed, any size) and show it once; false: nothing changed (the map axis on a kind without a mapping).
 * Several steps are added up first (cyclic), so a kind passed on the way are never switched to (no tap switch, no ring map build). */
static bool axis_step(int a, int n)
{
    const pc_vd_kind_t k = cur_kind();
    if (a == AX_HELP) return false;                      /* the help cell is an action (axis_action); the knob does nothing on it */
    if (SQUARE && a == AX_SIZE) {                     /* the square board: zoomed / original, a two-value switch */
        if (n % 2 == 0) return true;
        s_wz = !s_wz;
        if (s_root) apply();
        return true;
    }
    switch (a) {
    case 0:
    {   pc_vd_kind_t list[PC_VD_KINDS];
        const int cnt = kind_list(list);
        int cur = -1;
        for (int i = 0; i < cnt; i++) if (list[i] == s_wk) cur = i;
        if (cur < 0) cur = n > 0 ? -1 : cnt;            /* the chosen kind is not on the axis now (voices while nothing has voices): the first step enters the list at its start or its end */
        pc_vd_kind_t nk = list[pc_wrapi(cur + n % cnt, cnt)];
        if (nk == s_wk) break;                          /* landed on the same value: nothing to apply */
        s_wk = nk;
        if (s_root) apply(); }                            /* the ring map is built here the first time the ring kind is reached */
        break;
    case 1:
    {   const pc_vd_kind_t vk = var_kind();
        int nv = pc_wrapi(s_wv[vk] + n % pc_vd_variants(vk), pc_vd_variants(vk));
        if (nv == s_wv[vk]) break;
        s_wv[vk] = (int8_t)nv;
        if (s_root) apply(); }
        break;
    case 2:
    {   pc_ramp_t nr = (pc_ramp_t)pc_wrapi((int)s_wr + n % PC_RAMP_COUNT, PC_RAMP_COUNT);
        if (nr == s_wr) break;
        s_wr = nr;
        if (s_live) pc_vd_set_colors(&s_d, s_wr, ui_theme()); }    /* sets fresh; the map is untouched by it */
        break;
    case 3:
        if (!pc_vd_kind_has_map(k)) return false;
    {   const int ni = pc_wrapi((int)s_wm + (s_wpk ? 0 : PC_VD_MAPS) + n % (2 * PC_VD_MAPS), 2 * PC_VD_MAPS);   /* 0..3 the maps with peaks, 4..7 without */
        const pc_vd_map_t nm = (pc_vd_map_t)(ni % PC_VD_MAPS);
        const bool npk = ni < PC_VD_MAPS;
        if (nm == s_wm && npk == s_wpk) break;
        s_wm = nm; s_wpk = npk;
        if (s_live) { pc_vd_set_map(&s_d, s_wm); pc_vd_set_peaks(&s_d, s_wpk); } }
        break;
    case 4:                                             /* the drawn kind's own axis (voices with nothing voiced are drawn as bars, as the variant axis does) */
        if (rows_like(var_kind())) {                    /* the voice rows / piano roll: the voice the cursor is on */
            const int vn = axis_vn();
            if (vn <= 0) return false;
            s_sel = pc_wrapi(s_sel + n % vn, vn);
        } else if (var_kind() == PC_VD_RING) {                 /* order x gap: index = gap_index * 3 + order */
            const int ni = pc_wrapi(ordgap_index(s_wo, s_wgap) + n % 9, 9), ng = GAPS[ni / 3];
            const pc_vd_order_t no = (pc_vd_order_t)(ni % 3);
            if (no == s_wo && ng == s_wgap) break;
            s_wo = no;
            if (s_live) pc_vd_set_order(&s_d, no);
            if (ng != s_wgap) { s_wgap = ng; if (s_root) apply(); }    /* a new gap is a new ring map */
        } else if (var_kind() == PC_VD_FALL && PC_VD_ROUND_SCREEN) {
            if (n % 2 == 0) break;                       /* two values: an even number of detents is no change */
            s_wfs = !s_wfs;
            if (s_root) apply();                         /* the other picture size */
        } else if (var_kind() == PC_VD_BARS) {
            int nw = BARW[pc_wrapi(index_of(BARW, s_wbw) + n % 3, 3)];
            if (nw == s_wbw) break;
            s_wbw = nw;
            s_barw = nw;                                /* read by every frame; the bars redraw whole on the next one */
        } else return false;
        break;
    default:
        if (rows_like(var_kind())) {                    /* the voice rows / piano roll: the cursor voice's state, on -> mute -> solo (the mute is applied to the decoder at once) */
            const int vn = axis_vn();
            if (vn <= 0) return false;
            if (s_sel >= vn) s_sel = vn - 1;
            pc_vmute_set(s_sel, (pc_vm_state_t)pc_wrapi((int)pc_vmute_get(s_sel) + n % 3, 3), vn);
            return true;
        }
        if (s_kind == PC_VD_VOICES || s_kind == PC_VD_LYRICS) return false;   /* the voices' levels are not an FFT, the lyrics are text: no sensitivity */
    {   int nf = FLOOR_DB[pc_wrapi(index_of(FLOOR_DB, s_wfl) + n % 3, 3)];
        if (nf == s_wfl) break;
        s_wfl = nf;
        s_floor = (float)nf; }
        break;
    }
    return true;
}

/* the last cell is the help: one tap opens the page (an action cell: it does not become the current axis) */
static bool axis_action(int a)
{
    if (a != AX_HELP) return false;
    ui_axis_ring_close(false);
    ui_help_open(&HELP_VIZ);
    return true;
}

static ui_axis_ring_t RING = { .n = 6, .action = axis_action, .name = axis_name, .value = axis_value, .hint = axis_hint, .step = axis_step, .store = ring_flush,
                                     .centre = ring_centre, .back = ring_back, .after = ring_after, .gone = ring_gone };

static bool ring_open(bool from_menu)
{
    if (!s_root) return false;
    if (OPEN) { ui_axis_touch(); return true; }
    now_menu_close();                                   /* the console can ask while the playing ring is up */
    s_wk = ui_viz_kind();                               /* the working set starts as the settings */
    for (int k = 0; k < PC_VD_KINDS; k++) s_wv[k] = (int8_t)ui_viz_var((pc_vd_kind_t)k);
    s_wr = ui_viz_ramp();
    s_wm = ui_viz_map();
    s_wbw = ui_viz_bar_w();
    s_wfs = ui_viz_fall_full();
    s_wz = ui_viz_zoom();
    s_sel = 0;
    s_wpk = ui_viz_peaks();
    s_wo = ui_viz_order();
    s_wgap = ui_viz_ring_gap();
    s_wfl = (int)ui_viz_floor_db();
    RING.n = NAXIS;                                     /* 6 axes + the help cell, + the size cell on the square board */
    if (!ui_axis_ring_open(s_root, &RING, 0, false)) return false;   /* no memory for it: the knob stays volume, the caller sees to it that the tap is not a dead end */
    s_from_menu = from_menu;
    if (s_title) lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void on_suspend(void)
{
    now_menu_close();
    ui_axis_stop();
}

/* a tap on the page: a source is playing -> the playing ring (its style cell opens the style ring); nothing playing -> the style ring directly.
 * A ring that cannot be created must not leave a dead end: the style ring failing on a direct tap leaves the page. */
static void style_from_menu(void)
{
    if (!ring_open(true) && !playing_ring()) ui_pop();    /* no style ring: the playing ring again (its bare Back button at worst), else the page goes */
}
static void tap(void)
{
    if (OPEN) { ui_axis_touch(); return; }
    if (playing_ring()) { if (ui_axis_hinting(&RING)) ui_axis_stop(); return; }
    if (!ring_open(false)) ui_pop();
}
void scr_viz_tap(void) { if (ui_top() == &scr_viz) tap(); }

static void tick(lv_timer_t *t)
{
    (void)t;
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool top = ui_top() == &scr_viz;
    if (top && !s_was_top) title_show();               /* back from a page that covered this one: the title again */
    s_was_top = top;
    bool on = ui_screen_on();
    if (on && !s_was_on && top) title_show();         /* the screen woke up with this page on top */
    s_was_on = on;
    if (s_title && !OPEN && !ui_axis_hinting(&RING) && now_ms >= s_title_until_ms && s_img && lowmem_level() < PC_LOWMEM_STILL)
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);   /* the low-memory still page (title and back only) keeps it */
    if (!s_live) return;
    /* the tap runs only while the picture is seen: on top, screen on, enough RAM (covered page: tap off, timer keeps running and does nothing) */
    bool seen = ui_screen_on() && lowmem_level() < PC_LOWMEM_STILL && ui_top() == &scr_viz;
    set_stereo(seen && stereo_kind(s_kind) && !s_st_fail);
    if (!seen || ui_visual_paused(s_root)) return;       /* a popup ring open over the picture (the page stays on top): freeze on the last frame, no FFT, no draw; the audio tap above is not touched */
    if (s_kind == PC_VD_LYRICS) { lyrics_tick(); return; }   /* text, set when the line changes: no picture, no pacing */
    pc_voices_t v;
    int vn = 0;
    if (is_vk(cur_kind())) {                  /* a chip track starting or stopping, or a changed voice count, re-applies the picture (voices <-> bars, no black frame) */
        vn = audio_svc_voices(&v);
        if (vn != (int)s_vn) {
            apply();
            if (!s_live) return;
            vn = audio_svc_voices(&v);
        }
    }
    if (!pc_viz_should_draw(thumb_svc_busy(), esp_timer_get_time() / 1000, s_changed_ms, power_svc_state() == PC_PWR_BATT, s_tick++)) return;
    float bars[64];
    int64_t now = esp_timer_get_time();
    float dt = s_last_us ? (float)(now - s_last_us) / 1e6f : TICK_MS / 1000.0f;
    s_last_us = now;
    int wn = 0, lrn = 0;
    if (s_kind == PC_VD_PHASE) {                         /* VIZ_STEREO */
        lrn = audio_svc_stereo(s_lr, LR_FRAMES);         /* 0: tap off or nothing yet: silence */
    } else if (is_vk(s_kind)) {                          /* no FFT: the levels come from the voice snapshot */
        if (vn <= 0) return;                             /* the voices went between the check and here: the next tick switches */
        float in[PC_VOICES_MAX];
        for (int i = 0; i < vn; i++) in[i] = (float)v.level[i] / 255.0f;
        pc_viz_follow(in, vn, dt, s_level, s_peak, s_hold);
        if (rows_like(s_kind)) {
            pc_vmute_sync();                             /* a new track (the audio side reset the mask): the UI copy goes too */
            for (int i = 0; i < vn; i++) s_mstate[i] = (uint8_t)pc_vmute_get(i);
            pc_vd_vstate_feed(&s_vs, &v, (uint32_t)(now / 1000), (uint32_t)(dt * 1000.0f));
        }
    } else if (s_kind == PC_VD_SCOPE) {
        wn = audio_svc_wave(s_wave, WAVE_N);
        if (wn <= 0) { memset(s_wave, 0, sizeof s_wave); wn = WAVE_N; }   /* no tap ring: the line settles flat all the same */
    } else {
        audio_svc_spectrum_db(s_floor, bars);
        if (s_kind != PC_VD_FALL) pc_viz_follow(bars, 64, dt, s_level, s_peak, s_hold);   /* the waterfall shows the raw bars: it is its own memory */
    }
    int64_t t0 = esp_timer_get_time();
    pc_vd_rect_t r;
    switch (s_kind) {
    case PC_VD_RING: r = pc_vd_ring(&s_d, s_var, s_map, s_level, s_peak); break;
    case PC_VD_SCOPE: r = pc_vd_scope(&s_d, s_var, s_wave, wn); break;   /* nothing playing: silence -> the flat line */
    case PC_VD_FALL: r = pc_vd_fall(&s_d, s_var, bars); break;           /* nothing playing: empty rows keep scrolling */
    case PC_VD_VOICES: r = pc_vd_voices(&s_d, s_var, s_map, vn, s_level, s_peak, (const char (*)[4])v.name); break;
    case PC_VD_ROWS: r = pc_vd_rows(&s_d, &s_vs, &v, s_level, s_peak, s_mstate, OPEN && ui_axis_ring_axis(&RING) >= 4 && ui_axis_ring_axis(&RING) <= 5 ? s_sel : -1); break;
    case PC_VD_ROLL: r = pc_vd_roll(&s_d, s_var, &s_vs, &v, s_mstate, OPEN && ui_axis_ring_axis(&RING) >= 4 && ui_axis_ring_axis(&RING) <= 5 ? s_sel : -1); break;
    case PC_VD_PHASE: r = pc_vd_phase(&s_d, s_var, s_lr, lrn); break;              /* VIZ_STEREO; lrn 0: the picture just fades */
    default: r = pc_vd_bars(&s_d, s_var, s_level, s_peak, s_barw); break;
    }
    uint32_t du = (uint32_t)(esp_timer_get_time() - t0);
    s_dn++; s_dsum += du; if (du > s_dmax) s_dmax = du;
    if (r.w <= 0) return;
    lv_area_t o, a;
    lv_obj_get_coords(s_img, &o);                      /* screen coordinates */
    a.x1 = o.x1 + r.x; a.y1 = o.y1 + r.y; a.x2 = a.x1 + r.w - 1; a.y2 = a.y1 + r.h - 1;
    if (s_zoom != 256) {                               /* a scaled picture: the changed rectangle is scaled about the picture's centre (plus a pixel for the filter) */
        const int cx2 = o.x1 + o.x2 + 1, cy2 = o.y1 + o.y2 + 1;      /* twice the centre */
        a.x1 = (cx2 + (2 * a.x1 - cx2) * s_zoom / 256) / 2 - 2; a.x2 = (cx2 + (2 * (a.x2 + 1) - cx2) * s_zoom / 256) / 2 + 2;
        a.y1 = (cy2 + (2 * a.y1 - cy2) * s_zoom / 256) / 2 - 2; a.y2 = (cy2 + (2 * (a.y2 + 1) - cy2) * s_zoom / 256) / 2 + 2;
    }
    lv_obj_invalidate_area(s_img, &a);
}

/* ---- the square panel's nine-grid (ui_ninegrid.c): its sets and 下一頁 use the page's own state and setters; nothing new in the picture code ---- */
EXT_RAM_BSS_ATTR static int s_ng_saved1;   /* (stored +1: 0 = none, PSRAM, no internal .data) */
#define s_ng_saved (s_ng_saved1 - 1)
//                          /* the kind that was on before 下一頁 switched to the lyrics (given back when the page leaves them), -1 none */
static bool lyrics_ok(void)                            /* the playing track has a readable .lrc */
{
    const char *audio = lyrics_audio(s_lrc_cur, sizeof s_lrc_cur);
    if (!audio[0]) return false;
    if (strcmp(audio, s_lrc_for)) lyrics_load(audio);
    return s_lrc_err == PC_LRC_OK;
}
static void ng_kind(int d)                             /* the kind axis by one, the way the style ring walks it */
{
    pc_vd_kind_t list[PC_VD_KINDS];
    const int cnt = kind_list(list);
    int cur = -1;
    for (int i = 0; i < cnt; i++) if (list[i] == ui_viz_kind()) cur = i;
    if (cur < 0) cur = d > 0 ? -1 : cnt;
    s_ng_saved1 = 0;
    scr_viz_set_kind(list[pc_wrapi(cur + d, cnt)]);
}
static void ng_kind_prev(void) { ng_kind(-1); }
static void ng_kind_next(void) { ng_kind(1); }
static void ng_ramp(int d)
{
    ui_viz_ramp_set((pc_ramp_t)pc_wrapi((int)ui_viz_ramp() + d, PC_RAMP_COUNT));
    if (OPEN) s_wr = ui_viz_ramp();
    if (s_live && s_kind != PC_VD_LYRICS) pc_vd_set_colors(&s_d, cur_ramp(), ui_theme());
}
static void ng_ramp_prev(void) { ng_ramp(-1); }
static void ng_ramp_next(void) { ng_ramp(1); }
static const char *ng_name_style(void) { return T(S_STYLE_2) /* Style */; }
static const char *ng_prev_style(void) { return T(S_NG_PREV_STYLE) /* Prev style */; }
static const char *ng_next_style(void) { return T(S_NG_NEXT_STYLE) /* Next style */; }
static const char *ng_name_colour(void) { return T(S_NG_COLOURS) /* Colours */; }
static const char *ng_prev_colour(void) { return T(S_NG_PREV_COLOUR) /* Prev colours */; }
static const char *ng_next_colour(void) { return T(S_NG_NEXT_COLOUR) /* Next colours */; }
static const ui_ng_set_t NG_STYLE = { .name = ng_name_style, .left = { ng_prev_style, ng_kind_prev, UI_ICON_L_MINUS }, .right = { ng_next_style, ng_kind_next, UI_ICON_L_PLUS } };
static const ui_ng_set_t NG_COLOUR = { .name = ng_name_colour, .left = { ng_prev_colour, ng_ramp_prev, UI_ICON_L_MINUS }, .right = { ng_next_colour, ng_ramp_next, UI_ICON_L_PLUS } };
static const ui_ng_set_t *const NG_SETS[] = { &NG_STYLE, &NG_COLOUR, &UI_NG_PLAYMODE };
static void ng_next_page(void)                         /* spectrum -> lyrics (when the track has them) -> cover (the page below) */
{
    if (ui_viz_kind() != PC_VD_LYRICS && lyrics_ok()) { s_ng_saved1 = (int)ui_viz_kind() + 1; scr_viz_set_kind(PC_VD_LYRICS); return; }
    if (ui_viz_kind() == PC_VD_LYRICS && s_ng_saved >= 0) { const pc_vd_kind_t k = (pc_vd_kind_t)s_ng_saved; s_ng_saved1 = 0; ui_viz_kind_set(k); }   /* the stored kind is the one the owner chose */
    ui_pop();
}
static bool ng_open(void) { return OPEN; }             /* the style ring is open: a tap is the ring's own */
static const ui_ng_def_t NG_VIZ = { .id = 1, .sets = NG_SETS, .n_sets = 3, .menu = tap, .next_page = ng_next_page, .passthru = ng_open };

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_root = p;
    s_live = false;
    s_img = NULL;
    s_lyr = NULL;
    s_lrc_for[0] = 0;
    s_lrc_idx = -2;
    s_lrc_err = PC_LRC_E_ARG;
    s_lrc.n = 0;
    s_notrack = false;                        /* a rebuilt page starts with the ring closed and the knob volume (the old ring went with the old root) */
    s_from_menu = false;
    s_was_top = true;
    s_was_on = true;
    if (SQUARE) { s_ng_saved1 = 0; ui_ninegrid_attach(p, &NG_VIZ, true); }   /* the square panel: a tap shows the nine-grid, its 選單 zone is the tap below */
    else ui_page_tap(p, tap);
    memset(s_level, 0, sizeof s_level);
    memset(s_peak, 0, sizeof s_peak);
    memset(s_hold, 0, sizeof s_hold);
    s_last_us = 0;
    s_tick = 0;
    s_dn = s_dsum = s_dmax = 0;
    s_changed_ms = -PC_VIZ_SWITCH_PAUSE_MS;
    s_title = ui_label(p, NULL, -146, BSP_LCD_ROUND ? 160 : 320);   /* square: the 24 px font and the wider top */
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_title, UI_DIM, 0);
    if (lowmem_level() < PC_LOWMEM_STILL) {            /* low RAM: title only (a tap still opens the ring, whose Back cell leaves) */
        s_px = heap_caps_malloc((size_t)PC_VD_MAX_PX * 2, MALLOC_CAP_SPIRAM);
        if (s_px) {
            s_img = lv_image_create(p);
            lv_obj_remove_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        }
    }
    set_title();
    title_show();
    s_prev_hook = player_ctl_on_change;
    player_ctl_on_change = on_change;
    apply();
    ui_timer_bind(p, tick, TICK_MS, NULL);
}

static void destroy(void)
{
    if (player_ctl_on_change == on_change) player_ctl_on_change = s_prev_hook;
    s_prev_hook = NULL;
    s_live = false;
    set_stereo(false);
    now_menu_close();                                  /* the style ring, if open, stores what its axes changed when the root deletes it (RING.store) */
    s_from_menu = false;
    s_root = s_img = s_title = s_lyr = NULL;           /* the objects and the timer go with the root */
    if (s_px) { heap_caps_free(s_px); s_px = NULL; }
    if (s_map) { heap_caps_free(s_map); s_map = NULL; }
}

/* the knob: the style ring's current axis or the playing ring's play order while borrowed, else volume; back on top with playback gone: this page and the cover page go (scr_now.c, R40) */
const ui_screen_t scr_viz = { .name = "viz", .create = create, .destroy = destroy, .on_knob = ui_axis_knob, .on_suspend = on_suspend, .on_resume = now_leave_if_stopped };

void scr_viz_open(void)
{
    if (ui_top() == &scr_viz) return;
    now_open();                                        /* may push this page itself (now_view=viz), fail (full stack) or end somewhere else: look again */
    if (ui_top() == &scr_viz) return;
    if (ui_top() == &scr_now) ui_push(&scr_viz, NULL); /* ui_push itself refuses a full stack */
}

void scr_viz_set_kind(pc_vd_kind_t k)
{
    ui_viz_kind_set(k);                                /* the console stores at once; the ring's working copy follows */
    if (OPEN) s_wk = k;
    if (s_root) apply();
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_map(pc_vd_map_t m)                    /* stored; applied at once when the page is up */
{
    ui_viz_map_set(m);
    if (OPEN) s_wm = ui_viz_map();
    if (s_live) pc_vd_set_map(&s_d, cur_map());
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_peaks(bool on)                        /* console: stored at once, applied now when the page is up */
{
    ui_viz_peaks_set(on);
    if (OPEN) s_wpk = ui_viz_peaks();
    if (s_live) pc_vd_set_peaks(&s_d, cur_peaks());
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_var(int v)
{
    const pc_vd_kind_t k = OPEN ? s_wk : ui_viz_kind();    /* the ring's working kind while it is open */
    ui_viz_var_set(k, v);
    if (OPEN) s_wv[k] = (int8_t)ui_viz_var(k);
    if (s_root) apply();
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_fall_full(bool full)                  /* console: stored at once; the ring's working copy follows; the picture is rebuilt when the page is up */
{
    ui_viz_fall_full_set(full);
    if (OPEN) s_wfs = ui_viz_fall_full();
    if (s_root) apply();
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_order(pc_vd_order_t o)                 /* console: stored at once, applied now when the page is up */
{
    ui_viz_order_set(o);
    if (OPEN) s_wo = ui_viz_order();
    if (s_live) pc_vd_set_order(&s_d, cur_order());
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_ring_gap(int px)                      /* console: 0, 2 or 4; the ring map is rebuilt when the page is up and wants the ring */
{
    ui_viz_ring_gap_set(px);
    if (OPEN) s_wgap = ui_viz_ring_gap();
    if (s_root) apply();
    if (OPEN) ui_axis_touch();
}

void scr_viz_set_zoom(bool on)                         /* console: stored at once; the ring's working copy follows; the picture is shown again at its size when the page is up */
{
    ui_viz_zoom_set(on);
    if (OPEN) s_wz = ui_viz_zoom();
    if (s_root) apply();
    if (OPEN) ui_axis_touch();
}

void scr_viz_pick_voice(int i) { s_sel = i < 0 ? 0 : i >= PC_VOICES_MAX ? PC_VOICES_MAX - 1 : i; }

void scr_viz_ring(int pick)                            /* console: -2 opens the ring; open: 0..n-1 an axis cell (kind, variant, colour, map), -1 the centre, -3 the back cell */
{
    if (pick == -2) { if (ui_top() == &scr_viz) ring_open(false); return; }
    if (!OPEN) return;
    if (pick == -3) ring_back();
    else if (pick < NAXIS) ui_axis_ring_pick(pick);
}

static const char *axis_key(int ax)                    /* the axis name "viz stat" prints; slot 4 follows the kind */
{
    if (ax == AX_HELP) return "help";
    if (SQUARE && ax == AX_SIZE) return "zoom";
    if (rows_like(var_kind()) && ax == 4) return "voice";
    if (rows_like(var_kind()) && ax == 5) return "state";
    if (ax == 4) return var_kind() == PC_VD_RING ? "order" : var_kind() == PC_VD_FALL ? "size" : "barw";
    return (const char *[]){ "kind", "var", "col", "map", "barw", "floor" }[ax];
}

void scr_viz_stat(void)
{
    const bool open = OPEN;
    pc_vd_kind_t k = open ? s_wk : ui_viz_kind();
    const int ax = ui_axis_ring_axis(&RING);
    printf("APP viz open=%d kind=%s var=%d ramp=%s ring=%d knob=%s period_ms=%d stereo=%d map=%s\n", ui_top() == &scr_viz, pc_vd_kind_name(k),
           var_of(k), pc_ramp_name(cur_ramp()), open, ax >= 0 && ax < NAXIS ? axis_key(ax) : "vol", TICK_MS, s_st, pc_vd_map_name(cur_map()));
    printf("APP viz axes barw=%d floor=%d order=%s gap=%d size=%s peaks=%d\n", cur_bw(), cur_fl(), pc_vd_order_name(cur_order()), cur_gap(), cur_fs() ? "full" : "box", cur_peaks());
    printf("APP viz draw live=%d frames=%lu avg_us=%lu max_us=%lu\n", s_live, (unsigned long)s_dn, (unsigned long)(s_dn ? s_dsum / s_dn : 0), (unsigned long)s_dmax);
    s_dn = s_dsum = s_dmax = 0;
    printf("APP viz extra zoom=%d size=%s sel=%d mask=0x%lx solo=%d lyr=%s lines=%d idx=%d for=%s\n", s_zoom, cur_zoom() ? "zoom" : "orig", s_sel, (unsigned long)pc_vmute_mask(), pc_vmute_solo_voice(),
           s_lrc_err == PC_LRC_OK ? "ok" : s_lrc_err == PC_LRC_E_ENCODING ? "encoding" : s_lrc_err == PC_LRC_E_EMPTY ? "empty" : "none", s_lrc.n, s_lrc_idx, s_lrc_for[0] ? "set" : "-");
    printf("APP viz shown=%s\n", s_live ? pc_vd_kind_name(s_kind) : "-");   /* the kind really on screen (bars while the ring map is missing or no voices) */
    printf("APP viz voices n=%d var=%d\n", s_vn, s_live ? s_var : -1);       /* the voice count the picture was applied for and the variant really drawn (0 when voices fell back from the ring) */
    const char *l1 = ui_axis_line(&RING, 0), *l2 = ui_axis_line(&RING, 1);
    printf("APP viz hinttext=%s\n", l2 ? l2 : "-");
    printf("APP viz ring hint=%d title=%d line=%s cells=%d detail=%d\n", ui_axis_hinting(&RING), s_title && !lv_obj_has_flag(s_title, LV_OBJ_FLAG_HIDDEN), l1 ? l1 : "-",
           open ? NAXIS : 0, detail_now());
}
