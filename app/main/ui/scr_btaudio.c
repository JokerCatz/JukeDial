// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth audio (BTAUDIO): the headphones link like Android's codec developer options. Every option is a PREFERENCE / CEILING and stays selectable: the actual value is
 * the best the preference, the connected headset and this build all allow, said by two info lines (the headset's best, the actual) and a live line on top (pc_outstat.h: one formatter).
 * A change is stored at once and the sink is reconnected ONCE when the page is left (bt_svc_apply_prefs: the endpoints are offered again), never a reboot.
 * Composed of shared pieces only (list, rows, notes, the borrowed knob). Console: `btaudio [open | rows | key <codec|rate|bits|chan|ldac>]`. */
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include "bt_svc.h"
#include "cfg.h"
#include "font.h"
#include "i18n.h"
#include "out_status.h"
#include "pc/pc_outstat.h"
#include "screens.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "pc/pc_dial.h"

enum { R_CODEC, R_RATE, R_BITS, R_CHAN, R_LDAC, R_N };
static EXT_RAM_BSS_ATTR lv_obj_t *s_list, *s_live, *s_best, *s_act, *s_next, *s_build, *s_apply, *s_row[R_N];
static EXT_RAM_BSS_ATTR bool s_dirty;                                    /* a preference changed: the sink reconnects when the page is left */

static const int CODECS[] = { PC_BT_PREF_SBC, PC_BT_PREF_AAC, PC_BT_PREF_APTX, PC_BT_PREF_LDAC };
static const int RATES[] = { 44100, 48000, 96000 };
static const int LDACS[] = { 330, 492, 660, 990 };
static const char *const CODEC_NAME[] = { "SBC", "AAC", "aptX", "LDAC" };

static int idx_of(const int *v, int n, int x) { int i = 0; while (i < n && v[i] != x) i++; return i < n ? i : n - 1; }

static void set_text(lv_obj_t *label, const char *text) { if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text); }

static void refresh(void)
{
    if (!s_list) return;
    char b[160];
    pc_outstat_t os;
    out_status_get_bt(&os);
    const pc_lang_t l = i18n_lang();
    const bool cjk = font_has_cjk();
    pc_os_live_bt(b, sizeof b, &os, l, cjk); set_text(s_live, b);
    pc_os_info_best(b, sizeof b, &os, l, cjk); set_text(s_best, b);
    pc_os_info_actual(b, sizeof b, &os, l, cjk); set_text(s_act, b);
    pc_outstat_t nx;
    if (out_status_get_bt_next(&nx)) { pc_os_info_next(b, sizeof b, &nx, l, cjk); set_text(s_next, b); }   /* FIX4 I3: a stored preference not applied yet is the NEXT connection, never the actual one */
    else set_text(s_next, "");
    pc_os_build_note(b, sizeof b, cfg_int("bt_codec", PC_BT_PREF_LDAC), out_status_bt_build_ldac(), l, cjk); set_text(s_build, b);
    set_text(s_apply, bt_svc_prefs_pending() && os.conn ? T(S_BTA_APPLY) /* Reconnect to apply */ : "");
    for (int i = 0; i < R_N; i++) {
        switch (i) {
        case R_CODEC: snprintf(b, sizeof b, "%s%s", T(S_BTA_CODEC) /* Codec limit: */, CODEC_NAME[idx_of(CODECS, 4, cfg_int("bt_codec", PC_BT_PREF_LDAC))]); break;
        case R_RATE: { char r[16]; pc_os_rate_text(r, sizeof r, (uint32_t)RATES[idx_of(RATES, 3, cfg_int("bt_rate", 96000))]); snprintf(b, sizeof b, "%s%s", T(S_BTA_RATE) /* Rate limit: */, r); break; }
        case R_BITS: snprintf(b, sizeof b, "%s%d-bit", T(S_BTA_BITS) /* Bits limit: */, cfg_int("bt_bits", 24) == 16 ? 16 : 24); break;
        case R_CHAN: snprintf(b, sizeof b, "%s%s", T(S_BTA_CHAN) /* Channel mode: */, cfg_int("bt_chan", 0) == 1 ? T(S_JOINT_STEREO) /* Joint stereo */ : T(S_STEREO) /* Stereo */); break;
        default: snprintf(b, sizeof b, "%s%d kbps", T(S_BTA_LDAC) /* LDAC quality: */, LDACS[idx_of(LDACS, 4, cfg_int("ldac_kbps", 990))]); break;
        }
        { char b2[176]; ui_list_row_set_text(s_row[i], ui_icon_text(b2, sizeof b2, i == R_CODEC ? UI_ICON_L_CODEC : i == R_RATE ? UI_ICON_L_RATE : i == R_CHAN ? UI_ICON_L_SPEAKER : UI_ICON_L_GAUGE, b)); }   /* ICON-B */
        ui_list_row_look(s_row[i], i == ui_axis_current_of(s_list) ? UI_LOOK_SEL : UI_LOOK_NORMAL);
    }
}

static void row_end(void) { refresh(); }
static bool row_step(int r, int d)
{
    switch (r) {
    case R_CODEC: cfg_set_int("bt_codec", CODECS[ui_axis_idx(idx_of(CODECS, 4, cfg_int("bt_codec", PC_BT_PREF_LDAC)), d, 4)]); break;
    case R_RATE: cfg_set_int("bt_rate", RATES[ui_axis_idx(idx_of(RATES, 3, cfg_int("bt_rate", 96000)), d, 3)]); break;
    case R_BITS: cfg_set_int("bt_bits", cfg_int("bt_bits", 24) == 16 ? 24 : 16); break;
    case R_CHAN: cfg_set_int("bt_chan", cfg_int("bt_chan", 0) == 1 ? 0 : 1); break;
    default: {
        const int v = LDACS[ui_axis_idx(idx_of(LDACS, 4, cfg_int("ldac_kbps", 990)), d, 4)];
        cfg_set_int("ldac_kbps", v);
        bt_svc_ldac_kbps(v);                            /* the running encoder follows at once (vendor builds) */
        break;
    }
    }
    s_dirty = true;
    refresh();
    return true;
}
static bool row_is_axis(int r) { return r == R_CODEC || r == R_RATE || r == R_LDAC; }   /* three or more values: the borrowed knob (R21); two values: a plain tap */

static void row_cb(lv_event_t *e)
{
    const int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (row_is_axis(r)) { ui_list_row_axis_tap(e, s_list, r, row_step, row_end); return; }
    if (ui_axis_current_of(s_list) >= 0) ui_axis_end();
    row_step(r, 1);
}

static void tick(lv_timer_t *t) { (void)t; refresh(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_BT_AUDIO) /* Bluetooth audio */), &s_list);
    s_live = ui_list_note(s_list, "");
    for (int i = 0; i < R_N; i++) { s_row[i] = ui_list_row(s_list, "", UI_PANEL, row_cb, (void *)(intptr_t)i); if (row_is_axis(i)) ui_list_row_axis(s_row[i]); }
    s_best = ui_list_note(s_list, "");
    s_act = ui_list_note(s_list, "");
    s_next = ui_list_note(s_list, "");
    s_build = ui_list_note(s_list, "");
    s_apply = ui_list_note(s_list, "");
    ui_back_button(p);
    ui_timer_bind(p, tick, 1000, NULL);
    refresh();
}

static void destroy(void)
{
    s_list = s_live = s_best = s_act = s_next = s_build = s_apply = NULL;
    memset(s_row, 0, sizeof s_row);
    if (s_dirty) { s_dirty = false; bt_svc_apply_prefs(); }   /* one reconnect for all the changes of this visit */
}
static bool on_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    return ui_list_knob(s_list, d);
}

const ui_screen_t scr_btaudio = { .name = "btaudio", .create = create, .destroy = destroy, .on_knob = on_knob };

void btaudio_cmd(const char *a)
{
    if (!strcmp(a, "open")) { ui_push(&scr_btaudio, NULL); return; }
    if (!strncmp(a, "key ", 4)) {
        static const struct { const char *k; int r; } K[] = { { "codec", R_CODEC }, { "rate", R_RATE }, { "bits", R_BITS }, { "chan", R_CHAN }, { "ldac", R_LDAC } };
        for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++)
            if (!strcmp(a + 4, K[i].k) && ui_top() == &scr_btaudio) { lv_obj_send_event(s_row[K[i].r], LV_EVENT_CLICKED, NULL); return; }
        printf("APP btaudio key none\n");
        return;
    }
    if (ui_top() != &scr_btaudio) { printf("APP btaudio rows=0\n"); return; }
    printf("APP btaudio rows=%d dirty=%d\n", R_N, s_dirty);
    printf("APP btaudio live=%s\n", lv_label_get_text(s_live));
    for (int i = 0; i < R_N; i++) printf("APP btaudio row=%s\n", lv_label_get_text(lv_obj_get_child(s_row[i], 0)));
    printf("APP btaudio next=%s\nAPP btaudio best=%s\nAPP btaudio actual=%s\nAPP btaudio build=%s\nAPP btaudio apply=%s\n", lv_label_get_text(s_next), lv_label_get_text(s_best), lv_label_get_text(s_act), lv_label_get_text(s_build), lv_label_get_text(s_apply));
}
