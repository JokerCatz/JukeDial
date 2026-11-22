// SPDX-License-Identifier: LGPL-2.1-or-later
/* BTVOL: volumes per output (vol_out.h). Storage and the one place where the output changes.
 *
 * The order in vol_out_switch (the rule: no block reaches an output louder than that output's own value):
 *   1. next   = what the new output gets (its own value; lower while an alarm ramp or a duck holds the volume down: pc_volsel_next)
 *      across = min(what plays now, next)
 *   2. next <= what plays now: audio_svc_set_volume(across) FIRST, then the sink. Whatever sink a block in between sees, its target is at or
 *      below both outputs' values.
 *   3. next > what plays now: the volume is NOT raised yet. audio_svc_volume_hold(what plays now) puts a ceiling on every audio_svc_set_volume
 *      (the knob, an alarm, the web tool: whoever calls), then the sink is stored, and the raise waits until the audio task has begun a block
 *      after that store (audio_svc_block_mark changed): from then on no block can still carry the old sink snapshot. Only then
 *      audio_svc_volume_release() applies the newest value asked for. The wait is an LVGL timer (10 ms); if the audio task makes no block the
 *      hold simply stays (quieter, never louder).
 *   The headset's link is handed values only while the headset is the current output (vol_out_is_bt, read by bt_remote.c): a wired value never
 *   becomes a headset's absolute volume.
 * State: PSRAM only (EXT_RAM_BSS_ATTR), 0 B internal. LVGL task, except vol_out_is_bt (one byte, any task). */
#include "vol_out.h"
#include <stdio.h>
#include <string.h>
#include "bt_svc.h"
#include "cfg.h"
#include "esp_attr.h"
#include "lvgl.h"
#include "pc/pc_volume.h"

static EXT_RAM_BSS_ATTR volatile uint8_t s_out;         /* pc_vout_t; 0 = PC_VOUT_WIRED; PC_VOUT_SPEAKER = the jack while the device is in speaker / Auracast-listening mode */
static EXT_RAM_BSS_ATTR uint8_t s_bda[6];               /* the headset, while s_out is PC_VOUT_BT */
static EXT_RAM_BSS_ATTR lv_timer_t *s_rel;              /* the wait of step 3 */
static EXT_RAM_BSS_ATTR uint32_t s_mark;

static int vmax(void) { return cfg_int("volume_max", 100); }
pc_vout_t vol_out_current(void) { return (pc_vout_t)s_out; }
bool vol_out_is_bt(void) { return s_out == PC_VOUT_BT; }
int vol_out_wired(void) { return pc_volsel_wired(cfg_int("volume", -1), vmax()); }
int vol_out_spk(void) { return pc_volsel_spk(cfg_int("volume_spk", -1), vmax()); }
/* What the jack is: decided by the MODE, not by a phone stream (the owner sets the volume in speaker mode before the phone plays). Speaker mode (1) and Auracast listening (4: a broadcast is received and
 * played on the jack, also with a source volume in front of ours, and its own unknown level) share the stored speaker value; every other mode (off, headphones, broadcast) is the wired jack. */
static pc_vout_t jack_kind(void) { const bt_mode_t m = bt_svc_mode(); return m == BT_MODE_SPEAKER || m == BT_MODE_LISTEN ? PC_VOUT_SPEAKER : PC_VOUT_WIRED; }
int vol_out_headset(const uint8_t bda[6]) { return pc_volsel_bt(cfg_str("bt_vols", ""), bda, vmax()); }

int vol_out_set_wired(int v)
{
    v = pc_volsel_clamp(v, vmax());
    cfg_set_int("volume", v);
    return v;
}

int vol_out_set_spk(int v)
{
    v = pc_volsel_clamp(v, vmax());
    cfg_set_int("volume_spk", v);
    return v;
}

static void list_get(char l[PC_VOLSEL_TEXT]) { snprintf(l, PC_VOLSEL_TEXT, "%s", cfg_str("bt_vols", "")); }

int vol_out_set_headset(const uint8_t bda[6], int v)
{
    char l[PC_VOLSEL_TEXT];
    v = pc_volsel_clamp(v, vmax());
    list_get(l);
    if (pc_volsel_bt_put(l, sizeof l, bda, v)) cfg_set_str("bt_vols", l);
    return v;
}

void vol_out_forget(const uint8_t bda[6])
{
    char l[PC_VOLSEL_TEXT];
    list_get(l);
    if (pc_volsel_bt_del(l, bda)) cfg_set_str("bt_vols", l);
}

bool vol_out_limit(int max)
{
    const int w = pc_volsel_wired(cfg_int("volume", -1), 100), sp = pc_volsel_spk(cfg_int("volume_spk", -1), 100);
    if (w > max) cfg_set_int("volume", max);
    if (sp > max) cfg_set_int("volume_spk", max);
    if (!vol_out_is_bt()) return (s_out == PC_VOUT_SPEAKER ? sp : w) > max;
    const bool above = pc_volsel_bt(cfg_str("bt_vols", ""), s_bda, 100) > max;
    if (above) vol_out_set_headset(s_bda, max);          /* the other headsets' values are clamped when they are read */
    return above;
}

/* cfg.h: the CURRENT output's volume (the knob, the keys, quick settings, the web tool, an alarm's ramp) */
int app_volume(void) { return vol_out_is_bt() ? vol_out_headset(s_bda) : s_out == PC_VOUT_SPEAKER ? vol_out_spk() : vol_out_wired(); }
void app_set_volume(int v)
{
    if (vol_out_is_bt()) vol_out_set_headset(s_bda, v);
    else if (s_out == PC_VOUT_SPEAKER) vol_out_set_spk(v);
    else vol_out_set_wired(v);
}

static void rel_cb(lv_timer_t *t)
{
    if (audio_svc_block_mark() == s_mark) return;        /* no block began since the sink was stored: stay held */
    lv_timer_delete(t);
    s_rel = NULL;
    audio_svc_volume_release();
    printf("APP vol switch released value=%d\n", audio_svc_volume_last());
}

void vol_out_switch(pc_vout_t out, const uint8_t bda[6], aud_sink_fn fn)
{
    static const uint8_t none[6];
    if (out == PC_VOUT_BT && !bda) bda = none;
    if (out == PC_VOUT_WIRED) out = jack_kind();         /* "back to the jack": which value the jack has depends on the mode */
    const int old_full = app_volume();
    const int applied = audio_svc_volume_last(), req = audio_svc_volume_req();
    if (out == PC_VOUT_BT) {                              /* used now: it moves to the front of the list */
        char l[PC_VOLSEL_TEXT];
        list_get(l);
        if (pc_volsel_bt_touch(l, sizeof l, bda)) cfg_set_str("bt_vols", l);
    }
    const int new_full = out == PC_VOUT_BT ? vol_out_headset(bda) : out == PC_VOUT_SPEAKER ? vol_out_spk() : vol_out_wired();
    const int next = pc_volsel_next(req, old_full, new_full);
    const int across = pc_volsel_across(applied, next);
    const bool raise = next > across;
    /* the current output first: from here app_volume() is the new output's, and a value below goes to the headset's link only if the new output is
     * that headset (then it is at or below the headset's own value) */
    if (out == PC_VOUT_BT) memcpy(s_bda, bda, 6);
    s_out = (uint8_t)out;
    if (raise) audio_svc_volume_hold(across);            /* step 3: nothing above what plays now until a block has the new sink */
    else if (across != applied || across != req) audio_svc_set_volume(across);   /* step 2: down first */
    audio_svc_set_sink(fn);
    if (raise) audio_svc_set_volume(next);               /* kept by the hold; the release applies it (or a newer request) */
    if (audio_svc_volume_held()) {
        s_mark = audio_svc_block_mark();
        if (!s_rel) s_rel = lv_timer_create(rel_cb, 10, NULL);
        if (!s_rel) printf("APP vol switch: no timer, volume stays at %d until the next change of output\n", across);
    }
    char nm[20];
    printf("APP vol switch out=%s across=%d value=%d full=%d hold=%d\n", pc_volsel_name(out, s_bda, nm, sizeof nm), across, next, new_full, raise);
}

/* The bt mode became known or changed (bt_svc.c ui_saved on LVGL; the sim's simbt mode): the jack's own value follows, through the same switch (nothing else changes: the sink stays the jack's NULL) */
void vol_out_mode_changed(void)
{
    const pc_vout_t j = jack_kind();
    if ((s_out == PC_VOUT_WIRED || s_out == PC_VOUT_SPEAKER) && s_out != j) vol_out_switch(j, NULL, NULL);
}

void vol_out_print(void)
{
    char nm[20];
    printf("APP vol out=%s value=%d wired=%d\n", pc_volsel_name(vol_out_current(), s_bda, nm, sizeof nm), app_volume(), vol_out_wired());
    printf("APP vol spk=%d\n", vol_out_spk());
}
