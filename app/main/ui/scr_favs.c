// SPDX-License-Identifier: LGPL-2.1-or-later
/* Favourites (spec 5.5): MP3, chiptune and white-noise favourites, grouped; tap plays. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"
#include "chip_ctl.h"
#include "font.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "noise_engine.h"
#include "screens.h"
#include "radio_svc.h"
#include "stats_svc.h"
#include "ui_kit.h"

#define MAX_FAVS 64
static lv_obj_t *s_list;
static char *s_ids[4 * MAX_FAVS];
static int s_nids;

static void play(const char *id)
{
    if (!strncmp(id, "mp3:", 4)) {
        for (int i = 0; i < g_lib.n; i++)
            if (!strcmp(pc_str(&g_lib, g_lib.tracks[i].path), id + 4)) { mp3_ctl_play_track(i); now_open(); return; }
    } else if (!strncmp(id, "chip:", 5)) {                 /* chip:<system>.zip|<entry>|<sub> */
        char sys[64], entry[160];
        int sub = 0;
        const char *a = id + 5, *z = strstr(a, ".zip|"), *bar;
        if (!z || !(bar = strrchr(z + 5, '|'))) return;
        snprintf(sys, sizeof sys, "%.*s", (int)(z - a), a);
        snprintf(entry, sizeof entry, "%.*s", (int)(bar - (z + 5)), z + 5);
        sub = atoi(bar + 1);
        int n = chip_open_system(sys);
        for (int f = 0; f < n; f++) {
            char lbl[96], want[96];
            chip_file_label(f, lbl, sizeof lbl);
            const char *slash = strrchr(entry, '/'), *base = slash ? slash + 1 : entry, *dot = strrchr(base, '.');
            snprintf(want, sizeof want, "%.*s", (int)(dot ? dot - base : (long)strlen(base)), base);
            if (!strcmp(lbl, want)) { chip_play(f, sub); now_open(); return; }
        }
    } else if (!strncmp(id, "radio:", 6)) {               /* radio:<url> - in the list or not */
        int n = radio_count() ? radio_count() : radio_load(), at = -1;
        for (int i = 0; i < n && at < 0; i++) if (!strcmp(radio_station(i)->url, id + 6)) at = i;
        if (at < 0) at = radio_add_temp(id + 6, id + 6);
        if (at >= 0 && !radio_play(at)) { now_open(); return; }
    } else if (!strncmp(id, "scene:", 6)) {
        int n = noise_load();
        for (int i = 0; i < n; i++)
            if (!strcmp(noise_scene(i)->id, id + 6)) { if (!noise_engine_start(i)) { now_open(); return; } break; }
    }
    ui_push_message(T(S_FAVOURITE_NOT_FOUND) /* This favourite cannot be found */);
}

static void row_cb(lv_event_t *e) { play(s_ids[(int)(intptr_t)lv_event_get_user_data(e)]); }

/* the text of one favourite, as the list it comes from shows it; `buf` holds it when it is built */
static const char *shown_name(const char *prefix, const char *id, char *buf, size_t cap)
{
    const char *rest = id + strlen(prefix);
    if (!strcmp(prefix, "mp3:")) {                         /* the library's title; the file name when the track is not in it */
        for (int i = 0; i < g_lib.n; i++)
            if (!strcmp(pc_str(&g_lib, g_lib.tracks[i].path), rest)) return pc_str(&g_lib, g_lib.tracks[i].title);
        const char *slash = strrchr(rest, '/');
        return slash ? slash + 1 : rest;
    }
    if (!strcmp(prefix, "chip:")) {                        /* <file base name without extension> #<sub+1> */
        const char *z = strstr(rest, ".zip|"), *bar = z ? strrchr(z + 5, '|') : NULL;
        if (!bar) return rest;
        const char *entry = z + 5, *slash = NULL;
        for (const char *q = entry; q < bar; q++) if (*q == '/') slash = q;
        const char *base = slash ? slash + 1 : entry, *dot = NULL;
        for (const char *q = base; q < bar; q++) if (*q == '.') dot = q;
        snprintf(buf, cap, "%.*s #%d", (int)((dot ? dot : bar) - base), base, atoi(bar + 1) + 1);
        return buf;
    }
    if (!strcmp(prefix, "scene:")) {
        int n = noise_load();
        for (int i = 0; i < n; i++) if (!strcmp(noise_scene(i)->id, rest)) return noise_scene(i)->name;
        return rest;
    }
    if (!strcmp(prefix, "radio:")) {                       /* the station's name when it is in the list */
        int n = radio_count() ? radio_count() : radio_load();
        for (int k = 0; k < n; k++) if (!strcmp(radio_station(k)->url, rest)) return radio_station(k)->name;
    }
    return rest;
}

static void group(const char *prefix, const char *title)
{
    const char *ids[MAX_FAVS];
    int n = pc_stats_list_favs(g_stats, prefix, ids, MAX_FAVS);
    if (!n) return;
    ui_list_header(s_list, title);
    for (int i = 0; i < n; i++) {
        if (!(s_ids[s_nids] = strdup(ids[i]))) continue;     /* out of memory: leave this row out */
        char buf[160];
        ui_list_row(s_list, shown_name(prefix, ids[i], buf, sizeof buf), UI_PANEL, row_cb, (void *)(intptr_t)s_nids);
        s_nids++;
    }
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list(p), &s_list);
    s_nids = 0;
    group("mp3:", T(S_SONGS_2) /* Songs */);
    group("chip:", "Chiptune");
    group("scene:", T(S_NOISE_2) /* White noise */);
    group("radio:", T(S_RADIO) /* Radio */);
    if (!s_nids) ui_list_note(s_list, T(S_NO_FAVOURITES_YET) /* No favourites yet */);
    ui_back_button(p);
}

static void destroy(void)
{
    for (int i = 0; i < s_nids; i++) free(s_ids[i]);
    s_nids = 0;
}

static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_favs = { .name = "favs", .create = create, .destroy = destroy, .on_knob = on_knob };
