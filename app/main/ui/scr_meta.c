// SPDX-License-Identifier: LGPL-2.1-or-later
/* Track details (spec 4.4): title, artist, album, date, track, copyright, description, purl. Knob scrolls.
 * arg -1: the phone's track in speaker mode (AVRCP, bt_meta.h): title, artist, album, track n / m, length - what the phone sent. */
#include "esp_attr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bt_meta.h"
#include "font.h"
#include "library_svc.h"
#include "out_status.h"
#include "pc/pc_id3.h"
#include "screens.h"
#include "ui_kit.h"

static lv_obj_t *s_col;
static EXT_RAM_BSS_ATTR lv_obj_t *s_blk;   /* s_blk: the live output block (FIX4 M11), refreshed once a second */

static lv_obj_t *add(lv_obj_t *col, const char *ic, const char *k, const char *v, bool big)   /* ic: the LINE icon in front (ICON-B; NULL: none) */
{
    if (!v || !*v) return NULL;
    lv_obj_t *l = lv_label_create(col);
    lv_obj_set_width(l, 240);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    if (big) lv_obj_set_style_text_font(l, font_large(), 0);
    const char *i = ic && font_has_icons() ? ic : "", *sp = *i ? " " : "";
    if (k) lv_label_set_text_fmt(l, "%s%s%s  %s", i, sp, k, v);
    else lv_label_set_text_fmt(l, "%s%s%s", i, sp, v);
    return l;
}

static void blk_tick(lv_timer_t *t)   /* FIX4 M11: the block is the LIVE chain (an ABR step, a re-plan, a codec step show without reopening the page) */
{
    (void)t;
    if (!s_blk) return;
    char blk[512];
    out_status_text_block(blk, sizeof blk);
    if (strcmp(lv_label_get_text(s_blk), blk)) lv_label_set_text(s_blk, blk);
}

static lv_obj_t *column(lv_obj_t *p)
{
    ui_obj_guard(lv_obj_create(p), &s_col);
    lv_obj_remove_style_all(s_col);
    lv_obj_set_size(s_col, 260, 250);
    lv_obj_align(s_col, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_flex_flow(s_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_col, 6, 0);
    lv_obj_set_style_pad_top(s_col, 30, 0);
    lv_obj_set_scroll_dir(s_col, LV_DIR_VER);
    return s_col;
}

static void create_phone(lv_obj_t *p)          /* the same rows, from what the phone sent (a field it did not send is left out) */
{
    const pc_btmeta_t *m = bt_meta_get();
    column(p);
    if (m) {
        add(s_col, UI_ICON_L_MUSIC, NULL, m->title, true);
        add(s_col, UI_ICON_L_ARTIST, NULL, m->artist, false);
        add(s_col, UI_ICON_L_DISC, T(S_ALBUM) /* Album */, m->album, false);
        char buf[32];
        if (m->track && m->total) { snprintf(buf, sizeof buf, "%u / %u", m->track, m->total); add(s_col, UI_ICON_L_TRACKNO, T(S_TRACK_2) /* Track */, buf, false); }
        else if (m->track) { snprintf(buf, sizeof buf, "%u", m->track); add(s_col, UI_ICON_L_TRACKNO, T(S_TRACK_2) /* Track */, buf, false); }
        if (m->play_ms) { unsigned s = (unsigned)(m->play_ms / 1000); snprintf(buf, sizeof buf, "%u:%02u", s / 60, s % 60); add(s_col, UI_ICON_L_CLOCK, T(S_LENGTH) /* Length */, buf, false); }
    }
    ui_back_button(p);
}

static void create(lv_obj_t *p, void *arg)
{
    int t = (int)(intptr_t)arg;
    if (t < 0) { create_phone(p); return; }
    const pc_track_t *tr = &g_lib.tracks[t];
    pc_id3_t tag;
    memset(&tag, 0, sizeof tag);
    FILE *f = fopen(pc_str(&g_lib, tr->path), "rb");
    pc_reader_t r;
    if (f && !pc_reader_file(&r, f)) pc_id3_parse(&r, &tag);
    if (f) fclose(f);
    column(p);
    add(s_col, UI_ICON_L_MUSIC, NULL, pc_str(&g_lib, tr->title), true);
    add(s_col, UI_ICON_L_ARTIST, NULL, pc_str(&g_lib, tr->artist), false);
    add(s_col, UI_ICON_L_DISC, T(S_ALBUM) /* Album */, pc_str(&g_lib, tr->album), false);
    char buf[32];
    if (tr->date) { snprintf(buf, sizeof buf, "%04lu-%02lu-%02lu", (unsigned long)(tr->date / 10000), (unsigned long)(tr->date / 100 % 100), (unsigned long)(tr->date % 100)); add(s_col, UI_ICON_L_CALENDAR, T(S_DATE) /* Date */, buf, false); }
    if (tr->track) { snprintf(buf, sizeof buf, "%u", tr->track); add(s_col, UI_ICON_L_TRACKNO, T(S_TRACK_2) /* Track */, buf, false); }
    add(s_col, UI_ICON_L_COPYRIGHT, T(S_COPYRIGHT) /* Copyright */, tag.copyright, false);
    add(s_col, UI_ICON_L_LIST, NULL, tag.desc, false);
    add(s_col, UI_ICON_L_LIST, NULL, tag.purl, false);
    pc_id3_free(&tag);
    {                                              /* BTAUDIO: the live output chain of this track at the bottom, wired or wireless; one formatter (pc_outstat.h) */
        char blk[512];                             /* the label copies it: nothing kept (0 B static) */
        out_status_text_block(blk, sizeof blk);
        lv_obj_t *bl = add(s_col, NULL, NULL, blk, false);
        if (bl) ui_obj_guard(bl, &s_blk);   /* no block (nothing plays): none is added later, as before */
    }
    ui_timer_bind(p, blk_tick, 1000, NULL);
    ui_back_button(p);
}

static bool on_knob(int d)
{
    lv_obj_scroll_by_bounded(s_col, 0, -40 * d, LV_ANIM_OFF);
    return true;
}

const ui_screen_t scr_meta = { .name = "meta", .create = create, .on_knob = on_knob };
