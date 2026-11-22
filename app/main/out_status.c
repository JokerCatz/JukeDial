// SPDX-License-Identifier: LGPL-2.1-or-later
#include "out_status.h"
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "bsp/bsp_audio.h"
#include "bt_svc.h"
#include "cfg.h"
#include "esp_attr.h"
#include "fx_state.h"
#include "i18n.h"
#include "font.h"
#include "ui/icons_gen.h"
#include "pc/pc_castq.h"
#include "pc/pc_cs43131.h"
#include "pc/pc_dacmax.h"
#include "speed_svc.h"

#if defined(BSP_DAC_CS43131) || defined(SIM_FP)
#define OS_FAKEPOD 1
#endif

EXT_RAM_BSS_ATTR static struct { volatile uint32_t seq; pc_os_wired_in_t w; bool valid; } S;

void out_status_wired_set(const pc_os_wired_in_t *d)
{
    __atomic_store_n(&S.seq, S.seq + 1, __ATOMIC_RELAXED);     /* odd: being written */
    __atomic_thread_fence(__ATOMIC_RELEASE);                   /* FIX4 M5: the odd count is seen before any of the data stores */
    S.w = *d;
    S.valid = true;
    __atomic_store_n(&S.seq, S.seq + 1, __ATOMIC_RELEASE);
}
void out_status_wired_clear(void)
{
    __atomic_store_n(&S.seq, S.seq + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    S.valid = false;
    __atomic_store_n(&S.seq, S.seq + 1, __ATOMIC_RELEASE);
}

static bool wired_read(pc_os_wired_in_t *w)
{
    for (int i = 0; i < 8; i++) {
        uint32_t a = __atomic_load_n(&S.seq, __ATOMIC_ACQUIRE);
        if (a & 1) continue;
        *w = S.w;
        bool v = S.valid;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);               /* FIX4 M5: the copy is read before the second count */
        if (__atomic_load_n(&S.seq, __ATOMIC_RELAXED) == a) return v;
    }
    return false;
}

static pc_lang_t lang(void) { return i18n_lang(); }

static void fill_stages(pc_outstat_t *os);
void out_status_get_wired(pc_outstat_t *os)
{
    memset(os, 0, sizeof *os);
    pc_os_wired_in_t w;
    aud_info_t a;
    audio_svc_info(&a);
    os->kind = PC_OS_WIRED;
    if (!wired_read(&w)) return;
    w.src_bits = a.src_bits;
    const uint8_t rb = bsp_audio_bits();                         /* FIX4 I2: what the BSP channel is open with, not what the settings say */
    w.dac_bits = rb ? (rb == 16 ? 16 : 32) : (cfg_int("dac_bits", 32) == 16 ? 16 : 32);
    w.slot_mode = cfg_int("dac_slot24_from", 0);
    w.slot_real = rb;
    pc_os_wired_plan(&w, os);
    os->fmt = a.fmt;
    if (os->src_rate) fill_stages(os);
#ifdef OS_FAKEPOD
    const int db = pc_cs_vol_db_x10(audio_svc_volume_last());   /* the DAC's own volume is the wired path's volume */
    os->dac_db_valid = db != -9999;
    os->dac_db_mute = !os->dac_db_valid;
    os->dac_db_x10 = os->dac_db_valid ? db : 0;
#endif
}

void out_status_bt_prefs(pc_os_bt_in_t *in)
{
    in->pref_codec = cfg_int("bt_codec", PC_BT_PREF_LDAC);
    if (in->pref_codec < 0 || in->pref_codec > PC_BT_PREF_LDAC) in->pref_codec = PC_BT_PREF_LDAC;
    in->pref_rate = (uint32_t)cfg_int("bt_rate", 96000);
    in->pref_bits = cfg_int("bt_bits", 24);
    in->pref_chan = cfg_int("bt_chan", 0);
    in->ldac_kbps = cfg_int("ldac_kbps", 990);
}

static void bt_with_source(pc_outstat_t *os, uint32_t src_rate, uint8_t src_bits)
{
    memset(os, 0, sizeof *os);
    pc_os_bt_in_t in;
    memset(&in, 0, sizeof in);
    in.step_codec = PC_BTC_NONE;
    in.src_rate = src_rate; in.src_bits = src_bits;
    bt_svc_out_in(&in);                                         /* the live state of the link: connected, the sink's caps, the adaptive rate, the NEGOTIATED codec / rate / channel mode, the codecs that failed (FIX4 I3) */
    out_status_bt_prefs(&in);
    if (in.connected) bt_svc_applied_prefs(&in);                /* a connected link is what the registered endpoints made: the stored values not applied yet are "next connection" */
    pc_os_bt_plan(&in, os);
}
bool out_status_get_bt_next(pc_outstat_t *os)                   /* FIX4 I3: a stored preference waits for the next connection: the chain it will give (the sink's caps as now); false: nothing waits */
{
    memset(os, 0, sizeof *os);
    if (!bt_svc_prefs_pending()) return false;
    pc_os_bt_in_t in;
    memset(&in, 0, sizeof in);
    in.step_codec = PC_BTC_NONE;
    bt_svc_out_in(&in);
    if (!in.connected) return false;
    in.neg = false;
    out_status_bt_prefs(&in);
    pc_os_bt_plan(&in, os);
    return true;
}
void out_status_get_bt(pc_outstat_t *os) { bt_with_source(os, 0, 0); }

bool out_status_bt_build_ldac(void)
{
    pc_os_bt_in_t in;
    memset(&in, 0, sizeof in);
    bt_svc_out_in(&in);
    return in.build_ldac;
}

/* the processing stages that change the samples (speed, effects): read when asked, the same for the wired and the headphones chain (FIX4 I4: the wired live line names them too) */
static void fill_stages(pc_outstat_t *os)
{
    const int pct = speed_svc_pct();
    const char *sw = speed_svc_why();
    if ((pct != 100 || speed_svc_xfade_s()) && sw) {            /* asked for, off for the mode: the speed gate's own reason, in the screen's language */
        os->speed.value = (uint32_t)pct; os->speed.why = PC_WHY_MODE; os->speed.lim = PC_LIM_TEXT;
        snprintf(os->speed_note, sizeof os->speed_note, "%s", sw);
    } else {
        os->speed.value = (uint32_t)pct;
    }
    int run = 0, paused = 0;
    const char *fw = NULL;
    fx_state_running(&run, &paused, &fw);
    os->fx.value = (uint32_t)run;
    if (paused > 0) {
        os->fx.why = fx_mode_bits() ? PC_WHY_MODE : PC_WHY_BUSY;
        os->fx.lim = fw ? PC_LIM_TEXT : PC_LIM_NONE;
        if (fw) snprintf(os->fx_note, sizeof os->fx_note, "%s", fw);
    }
}

void out_status_get(pc_outstat_t *os)
{
    aud_info_t a;
    audio_svc_info(&a);
    const bt_mode_t m = bt_svc_mode();
    if (a.sink && m == BT_MODE_HEADPHONES) {
        bt_with_source(os, a.src_rate, a.src_bits);
        os->fmt = a.fmt; os->src_rate = a.src_rate; os->src_bits = a.src_bits;
    } else if (a.sink && m == BT_MODE_BROADCAST) {
        memset(os, 0, sizeof *os);
        os->kind = PC_OS_CAST;
        const int q = pc_cast_quality(cfg_int("cast_q", PC_CAST_Q_DEFAULT));
        os->lc3_rate = (uint32_t)pc_cast_q_rate(q);
        os->lc3_kbps = (uint32_t)pc_cast_q_nbyte(q) * 8u * 100u * 2u / 1000u;   /* two channels, 100 frames a second */
        os->fmt = a.fmt; os->src_rate = a.src_rate; os->src_bits = a.src_bits;
    } else {
        out_status_get_wired(os);
        if (!os->src_rate) { os->kind = PC_OS_NONE; return; }
    }
    if (!os->fmt && !os->src_rate) { os->kind = PC_OS_NONE; return; }
    fill_stages(os);
}

void out_status_text_block(char *b, unsigned n)
{
    pc_outstat_t os;
    out_status_get(&os);
    static const char *const IC[PC_OSI_N] = { [PC_OSI_OUTPUT] = UI_ICON_L_HEADPHONES, [PC_OSI_SOURCE] = UI_ICON_L_FORMAT, [PC_OSI_PROC] = UI_ICON_L_RATE, [PC_OSI_BITS] = UI_ICON_L_RATE,
        [PC_OSI_SLOT] = UI_ICON_L_RATE, [PC_OSI_VOLUME] = UI_ICON_L_VOLUME, [PC_OSI_STATE] = UI_ICON_L_INFO, [PC_OSI_CODEC] = UI_ICON_L_CODEC, [PC_OSI_RATE] = UI_ICON_L_RATE,
        [PC_OSI_CHAN] = UI_ICON_L_SPEAKER, [PC_OSI_BITRATE] = UI_ICON_L_GAUGE, [PC_OSI_SPEED] = UI_ICON_L_SPEED, [PC_OSI_FX] = UI_ICON_L_FX };
    if (!pc_os_block_icons(b, n, &os, lang(), font_has_cjk(), font_has_icons() ? IC : NULL) && n) b[0] = 0;
}

void out_status_console(const char *lg)
{
    pc_lang_t l = lang();
    bool cjk = font_has_cjk();
    if (*lg) { const int v = pc_lang_parse(lg); if (v >= 0) { l = (pc_lang_t)v; cjk = true; } }   /* a named language is printed with its own characters */
    char b[1024];
    pc_outstat_t os;
    out_status_get(&os);
    printf("APP outstat kind=%d proc=%d\n", os.kind, os.proc);
    out_status_get_wired(&os);
    pc_os_live_wired(b, sizeof b, &os, l, cjk);
    printf("APP outstat wired=%s\n", b);
    out_status_get_bt(&os);
    pc_os_live_bt(b, sizeof b, &os, l, cjk); printf("APP outstat bt=%s\n", b);
    pc_os_info_best(b, sizeof b, &os, l, cjk); printf("APP outstat best=%s\n", b);
    pc_os_info_actual(b, sizeof b, &os, l, cjk); printf("APP outstat actual=%s\n", b);
    out_status_get(&os);
    if (!pc_os_block(b, sizeof b, &os, l, cjk)) { printf("APP outstat block=none\n"); return; }
    for (char *p = b, *e; p && *p; p = e ? e + 1 : NULL) { e = strchr(p, '\n'); if (e) *e = 0; printf("APP outstat block %s\n", p); if (!e) break; }
}
