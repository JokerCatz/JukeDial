// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes radio_svc (network radio: HTTP stream, ICY, HLS). */
/* Behaviour: the station list is read from /sdcard/_player/config/radio.txt (real pc_radio_parse; a missing file is an empty list); playing a station takes the
 * player (now_ops like the board's: title, stop, list, next) and the fake audio (a mixer source: "playing", no sound); refused in Bluetooth broadcast mode,
 * as on the board. "simradio giveup" runs the board's give-up after three failed connects (radio_svc.c on_event): stop, then the message page. */
/* Empty: the stream, reconnects, probe, the default list a fresh card gets on the board; nothing is written. */
#include <stdio.h>
#include <string.h>
#include "bt_svc.h"
#include "cfg.h"
#include "radio_svc.h"
#include "audio_svc.h"
#include "font.h"
#include "i18n.h"
#include "player_ctl.h"
#include "screens.h"
#include "sim.h"

static pc_station_t s_list[RADIO_MAX];
static int s_n, s_cur = -1;

/* the list files as radio_svc.c: radio.txt (stem "") and radio_<stem>.txt; the one in use is cfg "radio_list" ("radio" = the main one), else the language's default.
 * Nothing is written (a missing file is an empty list). */
static bool list_exists(const char *stem)
{
    char path[64];
    FILE *f = pc_radio_list_path(stem, path, sizeof path) < 0 ? NULL : fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}
const char *radio_list_stem(void)
{
    const char *v = cfg_str("radio_list", NULL);
    if (v && !strcmp(v, "radio")) return "";
    if (v && v[0] && pc_radio_stem_ok(v) && list_exists(v)) return v;
    return pc_radio_default_stem((int)i18n_lang());
}
void radio_list_path(char *out, size_t cap)
{
    if (pc_radio_list_path(radio_list_stem(), out, cap) < 0) snprintf(out, cap, "%s", RADIO_LIST);
}
int radio_lists(char (*stems)[PC_RADIO_STEM_MAX], int max) { return pc_radio_scan_lists(PC_RADIO_DIR, stems, max); }
int radio_load(void);
int radio_list_select(const char *stem)
{
    if (!stem || !pc_radio_stem_ok(stem) || !list_exists(stem)) return -1;
    cfg_set_str("radio_list", stem[0] ? stem : "radio");
    return radio_load();
}
int radio_load(void)
{
    static char text[32 * 1024];
    size_t len = 0;
    char path[64];
    radio_list_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) { len = fread(text, 1, sizeof text - 1, f); fclose(f); }
    text[len] = 0;
    char playing[256] = "";
    if (s_cur >= 0 && s_cur < s_n) snprintf(playing, sizeof playing, "%s", s_list[s_cur].url);
    s_n = len ? pc_radio_parse(text, s_list, RADIO_MAX) : 0;
    if (s_cur >= 0) {                                      /* follow the playing station through a list change */
        int at = -1;
        for (int i = 0; i < s_n && at < 0; i++) if (!strcmp(s_list[i].url, playing)) at = i;
        s_cur = at;
    }
    printf("APP radio stations=%d list=%s\n", s_n, radio_list_stem()[0] ? radio_list_stem() : "radio");
    return s_n;
}
int radio_count(void) { return s_n; }
const pc_station_t *radio_station(int i) { return i >= 0 && i < s_n ? &s_list[i] : NULL; }
static const now_ops_t s_ops;
static void forget(void) { s_cur = -1; }
static void redo_play(int a, int b) { (void)b; radio_play(a); }
int radio_play(int i)
{
    if (i < 0 || i >= s_n || bt_svc_mode() == BT_MODE_BROADCAST) return -1;
    if (spk_excl_gate(redo_play, i, 0)) return -5;          /* as radio_svc.c: speaker mode asks first */
    player_ctl_take(&s_ops, forget, NULL);
    s_cur = i;
    audio_svc_play_mixer(NULL, NULL, 44100, NULL, NULL);   /* the stream: the fake audio is "playing" */
    player_ctl_changed();
    return 0;
}
void radio_stop(void)
{
    if (s_cur < 0) return;
    s_cur = -1;
    audio_svc_stop(false);
    player_ctl_release();
    player_ctl_changed();
}
static void title(char *out, size_t cap) { snprintf(out, cap, "%s\n", s_cur >= 0 ? s_list[s_cur].name : ""); }
static void next(void) { if (s_n) radio_play((s_cur + 1) % s_n); }
static bool playing(void) { return s_cur >= 0; }
static void open_list(void) { ui_open_once(&scr_radio, NULL); }   /* as radio_svc.c */
static const char *list_label(void) { return T(S_STATIONS) /* 電台清單 */; }
static const now_ops_t s_ops = { .src = NOW_SRC_RADIO, .title = title, .next = next, .stop = radio_stop, .playing = playing, .open_list = open_list, .list_label = list_label };
/* simradio giveup: what radio_svc.c does after the third failed connect (same order: stop, then the message) */
bool fake_radio_cmd(const char *arg)
{
    if (strcmp(arg, "giveup") || s_cur < 0) return false;
    int cur = s_cur;
    radio_stop();
    ui_push_message(T(S_CANNOT_PLAY_THIS_STATION_FORMAT) /* 這台無法播放（格式不支援） */);
    printf("APP radio give up station=%d\n", cur);
    return true;
}
int radio_current(void) { return s_cur; }
int radio_probe(const char *url, int id, void (*done)(int id, bool ok, const char *fmt, const char *url, const char *why)) { (void)url; (void)id; (void)done; return -1; }
int radio_add_temp(const char *name, const char *url)
{
    if (s_n >= RADIO_MAX) return -1;
    snprintf(s_list[s_n].name, sizeof s_list[s_n].name, "%s", name);
    snprintf(s_list[s_n].url, sizeof s_list[s_n].url, "%s", url);
    return s_n++;
}

void radio_stats(uint32_t *bytes, uint32_t *reconnects, uint32_t *underruns) { *bytes = s_cur >= 0 ? 1048576u : 0; *reconnects = 0; *underruns = 0; }   /* the fake streams nothing: a fixed count while a station "plays" */
