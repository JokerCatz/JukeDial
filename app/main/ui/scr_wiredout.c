// SPDX-License-Identifier: LGPL-2.1-or-later
/* Wired output (BTAUDIO): the wired path's limits in one place, with a live line on top (rates in / out, passthrough / halved / upsampled and why, the DAC volume on the FakePod).
 * Rows: the halve threshold / wired ceiling (dac_max_rate, both boards), and on the FakePod the hard limit (dac_hard_rate), the 24-bit slot threshold (dac_slot24_from), the DAC's high-pass
 * (dac_hpf), output drive (dac_fs) and auto-mute (dac_amute). A change is stored and the audio task re-plans and re-opens the output at once with a short mute (audio_svc_wired_changed), never a reboot.
 * Composed of shared pieces only. Console: `wired [open | rows | key <dacmax|dachard|slot|hpf|fs|amute>]`. */
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "bsp/bsp_audio.h"
#include "cfg.h"
#include "vol_out.h"
#include "font.h"
#include "i18n.h"
#include "out_status.h"
#include "pc/pc_dacmax.h"
#include "pc/pc_outstat.h"
#include "screens.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "pc/pc_dial.h"

#if defined(BSP_DAC_CS43131) || defined(SIM_FP)
#define WO_FAKEPOD 1
#endif

enum { R_DACMAX,
#ifdef WO_FAKEPOD
       R_HARD, R_SLOT, R_HPF, R_FS, R_AMUTE,
#endif
       R_N };
static EXT_RAM_BSS_ATTR lv_obj_t *s_list, *s_live, *s_help, *s_row[R_N];
#ifdef WO_FAKEPOD
static EXT_RAM_BSS_ATTR uint32_t s_fs_armed_at;   /* tick of the 1.73 V disclaimer tap (0: none): the 1 s refresh leaves the row's text alone while the disclaimer shows */
#define FS_LABEL(v) ((v) >= 2 ? T(S_DAC_FS_HI) /* 1.73 V · high voltage (experimental) */ : (v) == 1 ? T(S_DAC_FS_MID) : T(S_DAC_FS_BEST))
static void fs_help_text(char *b, size_t n)   /* the honest state: the drive the chip's registers show (not the setting), what differs, then the help */
{
    const int set = cfg_int("dac_fs", 0), rb = bsp_audio_dac_drive();
    const char *v = rb == 0 ? "1.0 V" : rb == 1 ? "1.41 V" : rb == 2 ? "1.73 V" : rb == 3 ? T(S_DAC_FS_ODD) /* unexpected registers */ : T(S_DAC_FS_UNK) /* unknown */;
    const char *d = rb < 0 || rb == set ? "" : bsp_audio_dac_cfg_pending() ? T(S_DAC_FS_PEND) /* (applies at the next play) */ : T(S_DAC_FS_DIFF) /* (differs from the setting) */;
    const char *act = T(S_DAC_FS_ACTIVE) /* Chip drive now:  */, *help = T(S_DAC_FS_HELP) /* 1.0 V has the lowest noise and power ... */;
    snprintf(b, n, "%s%s%s%s\n%s", act, v, *d ? " " : "", d, help);
}
#endif

static uint32_t dacmax(void) { return pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT); }
#ifdef WO_FAKEPOD
static uint32_t dachard(void) { return pc_dacmax_norm(cfg_int("dac_hard_rate", 0), 384000); }
#endif
#ifdef WO_FAKEPOD
#endif
static void set_text(lv_obj_t *label, const char *text) { if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text); }

static void refresh(void)
{
    if (!s_list) return;
    char b[160];
    pc_outstat_t os;
    aud_info_t ai;
    audio_svc_info(&ai);
#ifdef WO_FAKEPOD
    if (bsp_audio_dac_absent()) set_text(s_live, T(S_DAC_ABSENT_LIVE) /* No CS43131 DAC answered (PCM5102 variant?): no wired sound, Bluetooth only */);
    else
#endif
    if (ai.sink) set_text(s_live, T(S_WIRED_NOW_BT) /* Current output: Bluetooth (the wired chain is not in use) */);   /* FIX4 M1: never the Bluetooth plan under the wired title */
    else {
        out_status_get_wired(&os);
        pc_os_live_wired(b, sizeof b, &os, i18n_lang(), font_has_cjk());
        set_text(s_live, b);
    }
    for (int i = 0; i < R_N; i++) {
        switch (i) {
        case R_DACMAX: snprintf(b, sizeof b, "%s%s", T(S_WIRED_MAX_RATE) /* Wired max sample rate: */, pc_dacmax_label(dacmax())); break;
#ifdef WO_FAKEPOD
        case R_HARD: snprintf(b, sizeof b, "%s%s", T(S_WIRED_HARD_RATE) /* Wired refuse-above rate: */, pc_dacmax_label(dachard())); break;
        case R_SLOT: {
            const int m = pc_slot24_mode(cfg_int("dac_slot24_from", 0));
            snprintf(b, sizeof b, "%s%s", T(S_SLOT24_FROM) /* 24-bit slot from: */, m == PC_SLOT24_OFF ? T(S_OFF) /* off */ : m == PC_SLOT24_352 ? T(S_SLOT24_352) /* 352.8k and up */ : m == PC_SLOT24_192 ? T(S_SLOT24_192) /* 192k and up */ : T(S_ALL) /* All */);
            break;
        }
        case R_HPF: snprintf(b, sizeof b, "%s%s", T(S_DAC_HPF) /* DAC high-pass: */, cfg_int("dac_hpf", 1) ? T(S_ON) /* on */ : T(S_OFF) /* off */); break;
        case R_FS: snprintf(b, sizeof b, "%s%s", T(S_DAC_FS) /* Output drive: */, FS_LABEL(cfg_int("dac_fs", 0))); break;   /* 1.0 V · best quality (default) / 1.41 V · more drive / 1.73 V · high voltage */
        case R_AMUTE: snprintf(b, sizeof b, "%s%s", T(S_DAC_AMUTE) /* DAC auto-mute: */, cfg_int("dac_amute", 1) ? T(S_ON) /* on */ : T(S_OFF) /* off */); break;
#endif
        default: b[0] = 0; break;
        }
#ifdef WO_FAKEPOD
        if (i == R_FS && s_fs_armed_at && lv_tick_elaps(s_fs_armed_at) < UI_HOLD_MS + 500) continue;   /* the disclaimer is showing */
        if (i == R_FS) { char hb[400]; fs_help_text(hb, sizeof hb); set_text(s_help, hb); }
#endif
        { char b2[176]; ui_list_row_set_text(s_row[i], ui_icon_text(b2, sizeof b2, b[0] ? (i == R_DACMAX ? UI_ICON_L_RATE : UI_ICON_L_WIRED) : NULL, b)); }   /* ICON-B */
        ui_list_row_look(s_row[i], i == ui_axis_current_of(s_list) ? UI_LOOK_SEL : UI_LOOK_NORMAL);
    }
}

static bool row_is_axis(int r);
/* FIX4 I5: the knob edits a DRAFT (the stored value, shown at once); it is applied when the axis is released (4 s, another control, a plain tap) or the page is left, never per detent:
 * a turn through a lower hard limit never reaches the audio task, and the FakePod's I2S is re-opened once, not once per step. A hard limit below the playing file's rate applies
 * from the next track (the playing one is never stopped by a settings change). */
static EXT_RAM_BSS_ATTR bool s_pend;
static void commit(void)
{
    if (!s_pend) return;
    s_pend = false;
    audio_svc_set_cap(dacmax());
    audio_svc_wired_changed();                          /* the audio task re-plans and re-opens the output: a short mute, never a reboot */
}
static void row_end(void) { commit(); refresh(); }
static bool row_step(int r, int d)
{
    switch (r) {
    case R_DACMAX: { uint32_t v = pc_dacmax_step(dacmax(), d); if (ui_axis_would_wrap((int)dacmax(), (int)v, d)) v = dacmax(); cfg_set_int("dac_max_rate", (int)v); s_pend = true; if (!row_is_axis(r)) commit(); refresh(); return true; }
#ifdef WO_FAKEPOD
    case R_HARD: { const uint32_t v = pc_dacmax_step(dachard(), d); if (!ui_axis_would_wrap((int)dachard(), (int)v, d)) cfg_set_int("dac_hard_rate", (int)v); break; }
    case R_SLOT: cfg_set_int("dac_slot24_from", ui_axis_idx(pc_slot24_mode(cfg_int("dac_slot24_from", 0)), d, PC_SLOT24_N)); break;
    case R_HPF: cfg_set_int("dac_hpf", !cfg_int("dac_hpf", 1)); break;
    case R_FS: {   /* 1.0 -> 1.41 -> 1.73 -> 1.0 V. Raising the drive caps the volume at 20 first (owner's hearing); lowering never touches it */
        const int cur = cfg_int("dac_fs", 0), nv = (cur + 1) % 3;
        if (nv > cur && (vol_out_wired() > 20 || vol_out_spk() > 20)) {   /* BTVOL: a rule of the WIRED JACK: its own value and the jack's speaker-mode value (PMVOL), also while a headset is the output */
            if (vol_out_wired() > 20) vol_out_set_wired(20);
            if (vol_out_spk() > 20) vol_out_set_spk(20);
            if (!vol_out_is_bt() && ui_volume_hook) ui_volume_hook(app_volume());   /* the normal volume path: the DAC's volume registers are read back before any unmute (a headset is the output: the wired value is applied, lower first, when the output comes back: vol_out_switch) */
            ui_push_message(T(S_DAC_FS_CLAMP) /* Drive raised: volume set to 20 */);
        }
        cfg_set_int("dac_fs", nv);
        break;
    }
    case R_AMUTE: cfg_set_int("dac_amute", !cfg_int("dac_amute", 1)); break;
#endif
    default: return false;
    }
    s_pend = true;
    if (!row_is_axis(r)) commit();                      /* a two-value row: a tap, applied at once */
    refresh();
    return true;
}
static bool row_is_axis(int r)
{
    if (r == R_DACMAX) return pc_dacmax_count() >= 3;
#ifdef WO_FAKEPOD
    return r == R_HARD || r == R_SLOT;
#else
    return false;
#endif
}

static void row_cb(lv_event_t *e)
{
    const int r = (int)(intptr_t)lv_event_get_user_data(e);
#ifdef WO_FAKEPOD
    if (r != R_FS) ui_arm_cancel();
    if (r == R_FS && cfg_int("dac_fs", 0) == 1) {   /* experimental: the step to 1.73 V is taken only on the second tap, after the disclaimer (as the CPU clock row) */
        s_fs_armed_at = lv_tick_get();
        if (!ui_arm_tap(s_row[R_FS], T(S_DAC_FS_ARM) /* Experimental: 1.73 V high-voltage mode ... Tap again to turn on */)) return;
        s_fs_armed_at = 0;
    }
#endif
    if (row_is_axis(r)) { ui_list_row_axis_tap(e, s_list, r, row_step, row_end); return; }
    if (ui_axis_current_of(s_list) >= 0) ui_axis_end();   /* releases the axis: its draft is applied */
    row_step(r, 1);
}

static void tick(lv_timer_t *t) { (void)t; refresh(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_WIRED_OUTPUT) /* Wired output */), &s_list);
    s_live = ui_list_note(s_list, "");
    for (int i = 0; i < R_N; i++) {
#ifdef WO_FAKEPOD
        if (i == R_FS) s_row[i] = ui_list_row_wrap(s_list, "", UI_PANEL, row_cb, (void *)(intptr_t)i); else   /* its armed text is long: the row wraps */
#endif
        s_row[i] = ui_list_row(s_list, "", UI_PANEL, row_cb, (void *)(intptr_t)i);
        if (row_is_axis(i)) ui_list_row_axis(s_row[i]);
#ifdef WO_FAKEPOD
        if (i == R_FS) s_help = ui_list_note(s_list, "");   /* the chip's drive read-back and the help: refresh() */
#endif
    }
    set_text(ui_list_note(s_list, ""), T(S_WIRED_APPLY_NOTE) /* Changes apply at once, with a brief mute */);
    ui_back_button(p);
    ui_timer_bind(p, tick, 1000, NULL);
    refresh();
}

static void destroy(void) { ui_arm_cancel(); commit(); s_list = s_live = s_help = NULL; memset(s_row, 0, sizeof s_row); }
static bool on_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    return ui_list_knob(s_list, d);
}

const ui_screen_t scr_wiredout = { .name = "wired", .create = create, .destroy = destroy, .on_knob = on_knob };

void wired_cmd(const char *a)
{
    if (!strcmp(a, "open")) { ui_push(&scr_wiredout, NULL); return; }
    if (!strncmp(a, "key ", 4)) {
        static const struct { const char *k; int r; } K[] = { { "dacmax", R_DACMAX },
#ifdef WO_FAKEPOD
            { "dachard", R_HARD }, { "slot", R_SLOT }, { "hpf", R_HPF }, { "fs", R_FS }, { "amute", R_AMUTE },
#endif
        };
        for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++)
            if (!strcmp(a + 4, K[i].k) && ui_top() == &scr_wiredout) { lv_obj_send_event(s_row[K[i].r], LV_EVENT_CLICKED, NULL); return; }
        printf("APP wired key none\n");
        return;
    }
    if (ui_top() != &scr_wiredout) { printf("APP wired rows=0\n"); return; }
    printf("APP wired rows=%d\nAPP wired live=%s\n", R_N, lv_label_get_text(s_live));
    for (int i = 0; i < R_N; i++) printf("APP wired row=%s\n", lv_label_get_text(lv_obj_get_child(s_row[i], 0)));
#ifdef WO_FAKEPOD
    printf("APP wired help=%s\n", lv_label_get_text(s_help));
#endif
}
