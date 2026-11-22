// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_outstat.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* three languages in one place: 繁中 | 简中 | English (the language's text, English without a CJK font) */
#define L3(tw, cn, en) pc_i18n_text(l, cjk, tw, cn, en)

typedef struct { char *b; size_t n, len; } sb_t;
static void put(sb_t *s, const char *fmt, ...)
{
    if (!s->n || s->len >= s->n - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(s->b + s->len, s->n - s->len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    s->len += (size_t)w;
    if (s->len >= s->n) s->len = s->n - 1;
}
static sb_t sb_new(char *b, size_t n) { if (n) b[0] = 0; return (sb_t){ b, n, 0 }; }

const char *pc_why_tag(int why, pc_lang_t l, bool cjk)
{
    switch (why) {
    case PC_WHY_NONE: return L3("原始", "原始", "source");
    case PC_WHY_FILE: return L3("檔案", "文件", "file");
    case PC_WHY_SETTING: return L3("設定", "设置", "setting");
    case PC_WHY_SINK: return L3("耳機", "耳机", "headset");
    case PC_WHY_DAC: return "DAC";
    case PC_WHY_BUILD: return L3("韌體", "固件", "firmware");
    case PC_WHY_SIGNAL: return L3("訊號", "信号", "signal");
    case PC_WHY_BUSY: return L3("忙碌", "忙碌", "busy");
    case PC_WHY_MODE: return L3("模式", "模式", "mode");
    case PC_WHY_BATTERY: return L3("電量", "电量", "battery");
    default: return "?";
    }
}

void pc_os_rate_text(char *b, size_t n, uint32_t hz)
{
    unsigned k = hz / 1000, f = hz % 1000;
    if (!f) snprintf(b, n, "%uk", k);
    else if (f % 100 == 0) snprintf(b, n, "%u.%uk", k, f / 100);
    else if (f % 10 == 0) snprintf(b, n, "%u.%02uk", k, f / 10);
    else snprintf(b, n, "%u.%03uk", k, f);
}

/* ---------- slot width ---------- */
int pc_slot24_mode(int v) { return v >= PC_SLOT24_OFF && v < PC_SLOT24_N ? v : PC_SLOT24_OFF; }
uint32_t pc_slot24_from(int mode)
{
    switch (pc_slot24_mode(mode)) {
    case PC_SLOT24_352: return 352800;
    case PC_SLOT24_192: return 192000;
    case PC_SLOT24_ALL: return 1;
    default: return 0;
    }
}
uint8_t pc_slot_bits(int mode, uint32_t rate, int dac_bits)
{
    if (dac_bits == 16) return 16;
    uint32_t from = pc_slot24_from(mode);
    return from && rate >= from ? 24 : 32;
}
uint32_t pc_slot_bclk(uint32_t rate, uint8_t slot_bits) { return 2u * slot_bits * rate; }

void pc_pack_s24(const int32_t *s32, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {                       /* forward: the 3-byte write position never passes the 4-byte read position */
        const int32_t v = s32[i];
        out[3 * i] = (uint8_t)(v >> 8); out[3 * i + 1] = (uint8_t)(v >> 16); out[3 * i + 2] = (uint8_t)(v >> 24);
    }
}

/* ---------- the wired decision ---------- */
static void st(pc_stage_t *s, uint32_t value, uint32_t limit, int why, int lim)
{
    s->value = value; s->limit = limit; s->why = (uint8_t)why; s->lim = (uint8_t)lim; s->also = 0;
}

void pc_os_wired_plan(const pc_os_wired_in_t *in, pc_outstat_t *os)
{
    const bool fp = in->caps && in->caps->native_from;
    os->kind = PC_OS_WIRED;
    os->src_rate = in->src_rate;
    os->src_bits = in->src_bits;
    pc_stage_t *r = &os->rate;
    if (!in->plan.pipe) {                                   /* refused: the reason is the binding constraint */
        os->proc = PC_PROC_REFUSED;
        if (in->refused_hard) st(r, 0, in->hard, PC_WHY_SETTING, PC_LIM_HARD);
        else if (fp && in->src_rate < in->caps->resample_floor) st(r, 0, in->caps->resample_floor, PC_WHY_BUILD, PC_LIM_BELOW);
        else st(r, 0, in->caps ? in->caps->max : 0, PC_WHY_DAC, PC_LIM_MAXRATE);
    } else if (in->plan.pipe < in->src_rate && !(in->up_from && !in->plan.halvings)) {   /* halved: the reason of the last halving */
        os->proc = PC_PROC_HALVED;
        switch (in->plan.why) {
        case PC_PIPE_CAP: st(r, in->plan.pipe, in->cap, PC_WHY_SETTING, fp ? PC_LIM_HALVE : PC_LIM_CEIL); break;
        case PC_PIPE_BURST: st(r, in->plan.pipe, in->plan.pipe, PC_WHY_FILE, PC_LIM_BURST); break;
        case PC_PIPE_CLOCK: st(r, in->plan.pipe, in->hw_cap, PC_WHY_DAC, PC_LIM_CLOCK); break;
        default: st(r, in->plan.pipe, in->plan.pipe, PC_WHY_SINK, PC_LIM_MAXRATE); break;
        }
    } else if (in->plan.pipe != in->src_rate || in->up_from) {   /* the resampling stage: the rate is not one the DAC takes */
        if (in->plan.pipe > in->src_rate) {                 /* below the DAC's lowest native rate: up to the same family */
            os->proc = PC_PROC_UPSAMPLED;
            st(r, in->plan.pipe, fp ? in->caps->native_from : in->plan.pipe, PC_WHY_DAC, PC_LIM_MINRATE);
            if (!fp) r->lim = PC_LIM_MAXRATE;
        } else {                                            /* an odd rate: the nearest rate the DAC takes below it */
            os->proc = PC_PROC_RESAMPLED;
            st(r, in->plan.pipe, in->plan.pipe, PC_WHY_DAC, PC_LIM_MAXRATE);
        }
    } else {
        os->proc = PC_PROC_PASS;
        st(r, in->plan.pipe, in->plan.pipe, PC_WHY_NONE, PC_LIM_NONE);
    }
    /* the word: 24 valid bits (the DAC's 32-bit slot), or 16 by dac_bits */
    const uint32_t want = in->src_bits && in->src_bits < 24 ? in->src_bits : 24;
    if (in->dac_bits == 16) {
        st(&os->bits, 16, 16, want > 16 ? PC_WHY_SETTING : PC_WHY_NONE, want > 16 ? PC_LIM_MAXBITS : PC_LIM_NONE);
    } else {
        st(&os->bits, 24, 24, PC_WHY_NONE, PC_LIM_NONE);
    }
    if (fp) {
        const uint8_t sb = in->slot_real ? in->slot_real : pc_slot_bits(in->slot_mode, in->plan.pipe ? in->plan.pipe : in->src_rate, in->dac_bits);   /* the real width, never the setting's */
        st(&os->slot, sb, sb, sb == 24 ? PC_WHY_SETTING : PC_WHY_NONE, PC_LIM_NONE);
    } else {
        memset(&os->slot, 0, sizeof os->slot);
    }
}

/* ---------- the Bluetooth decision ---------- */
unsigned pc_bt_pref_mask(int pref)
{
    const unsigned sbc = 1u << PC_BTC_SBC, aac = 1u << PC_BTC_AAC, apx = (1u << PC_BTC_APTX) | (1u << PC_BTC_APTX_HD), ldac = 1u << PC_BTC_LDAC;
    switch (pref) {
    case PC_BT_PREF_SBC: return sbc;
    case PC_BT_PREF_AAC: return sbc | aac;
    case PC_BT_PREF_APTX: return sbc | aac | apx;
    default: return sbc | aac | apx | ldac;
    }
}
unsigned pc_bt_build_mask(bool ldac)
{
    unsigned m = (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC);
    return ldac ? m | (1u << PC_BTC_LDAC) | (1u << PC_BTC_APTX_HD) | (1u << PC_BTC_APTX) : m;
}
uint32_t pc_bt_rate_pick(uint32_t limit)
{
    static const uint32_t R[] = { 96000, 88200, 48000, 44100 };
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) if (R[i] <= limit) return R[i];
    return 44100;
}
static const uint8_t CODEC_BITS[PC_BTC_N] = { 24, 24, 16, 16, 16 };   /* what each codec carries by itself */

/* the constraint that binds: the one whose own limit is the lowest (ties: the sink, then the build, then the setting); the others that bind go in also */
typedef struct { uint32_t lim[3]; bool binds[3]; } cons_t;   /* index 0 sink, 1 build, 2 setting */
static int why_of(int i) { return i == 0 ? PC_WHY_SINK : i == 1 ? PC_WHY_BUILD : PC_WHY_SETTING; }
static int primary(const cons_t *c, uint16_t *also)
{
    int best = -1;
    *also = 0;
    for (int i = 0; i < 3; i++)
        if (c->binds[i] && (best < 0 || c->lim[i] < c->lim[best])) best = i;
    for (int i = 0; i < 3; i++) if (c->binds[i] && i != best) *also |= (uint16_t)(1u << why_of(i));
    return best;
}

static pc_btc_t pick_in(unsigned m) { return pc_btc_pick(m); }

void pc_os_bt_plan(const pc_os_bt_in_t *in, pc_outstat_t *os)
{
    os->kind = PC_OS_A2DP;
    os->conn = in->connected;
    os->best_codec = (uint8_t)PC_BTC_NONE; os->best_rate = 0; os->best_bits = 0;
    if (!in->connected) {
        memset(&os->codec, 0, sizeof os->codec); memset(&os->lrate, 0, sizeof os->lrate);
        memset(&os->lbits, 0, sizeof os->lbits); memset(&os->kbps, 0, sizeof os->kbps);
        os->chan = 0;
        return;
    }
    const unsigned all = pc_bt_pref_mask(PC_BT_PREF_LDAC);
    const unsigned pref = pc_bt_pref_mask(in->pref_codec), build = pc_bt_build_mask(in->build_ldac);
    unsigned sink = (in->sink_codecs ? in->sink_codecs : all) & ~in->failed;   /* nothing reported: not a limit; a codec that could not start this boot is not on offer */
    if (!sink) sink = 1u << PC_BTC_SBC;
    pc_btc_t actual = pick_in(pref & build & sink);
    if (actual == PC_BTC_NONE) actual = PC_BTC_SBC;                  /* every sink has SBC */

    /* the sink's best */
    pc_btc_t sb = pick_in(in->sink_codecs ? in->sink_codecs : (1u << actual));
    os->best_codec = (uint8_t)sb;
    os->best_rate = sb != PC_BTC_NONE && in->sink_rate[sb] ? in->sink_rate[sb] : 0;
    os->best_bits = sb == PC_BTC_NONE ? 0 : in->sink_bits[sb] ? in->sink_bits[sb] : CODEC_BITS[sb];   /* not reported: what the codec itself carries */

    /* codec */
    {
        cons_t c;                                                  /* quality = PC_BTC_N - enum: the lowest one binds */
        const pc_btc_t ps = pick_in(sink), pb = pick_in(build), pp = pick_in(pref);
        c.lim[0] = (uint32_t)(PC_BTC_N - (int)ps); c.lim[1] = (uint32_t)(PC_BTC_N - (int)pb); c.lim[2] = (uint32_t)(PC_BTC_N - (int)pp);
        c.binds[0] = ps > PC_BTC_LDAC; c.binds[1] = pb > PC_BTC_LDAC; c.binds[2] = pp > PC_BTC_LDAC;   /* a constraint binds when it keeps LDAC out of reach */
        uint16_t also;
        int p = primary(&c, &also);
        pc_stage_t *s = &os->codec;
        s->value = (uint32_t)actual; s->also = also;
        if (in->step_rung > 0 && in->step_codec >= 0 && in->step_codec < PC_BTC_N) {   /* stepped down for a busy link */
            s->value = (uint32_t)in->step_codec; s->why = PC_WHY_SIGNAL; s->lim = PC_LIM_MAXCODEC; s->limit = (uint32_t)actual;
        } else if (p < 0) { s->why = PC_WHY_NONE; s->lim = PC_LIM_NONE; s->limit = (uint32_t)actual; }
        else {
            s->why = (uint8_t)why_of(p);
            s->limit = (uint32_t)(PC_BTC_N - (int)c.lim[p]);
            s->lim = p == 1 ? PC_LIM_NOCODEC : p == 2 ? PC_LIM_PREFCODEC : PC_LIM_MAXCODEC;
            if (p == 1) {                                          /* the codec the build lacks: the best the preference and the sink would have used */
                pc_btc_t want = pick_in(pref & sink);
                s->limit = (uint32_t)(want == PC_BTC_NONE ? PC_BTC_LDAC : want);
            }
        }
    }
    if (in->neg && in->neg_codec < PC_BTC_N && in->neg_codec != os->codec.value) {   /* the link runs another codec than the preferences give: the link's value, the stack's reason */
        os->codec.value = in->neg_codec; os->codec.why = PC_WHY_SINK; os->codec.lim = PC_LIM_MAXCODEC; os->codec.limit = in->neg_codec; os->codec.also = 0;
    }
    const pc_btc_t used = (pc_btc_t)os->codec.value;

    /* link rate: preference, this build, the sink's rate for this codec */
    {
        cons_t c;
        c.lim[0] = in->sink_rate[used] ? in->sink_rate[used] : 96000;
        c.lim[1] = PC_BT_BUILD_MAX_RATE;
        c.lim[2] = in->pref_rate ? in->pref_rate : 96000;
        const uint32_t actual_r = pc_bt_rate_pick(c.lim[0] < c.lim[1] ? (c.lim[0] < c.lim[2] ? c.lim[0] : c.lim[2]) : (c.lim[1] < c.lim[2] ? c.lim[1] : c.lim[2]));
        for (int i = 0; i < 3; i++) c.binds[i] = c.lim[i] < 96000;
        uint16_t also;
        int p = primary(&c, &also);
        pc_stage_t *s = &os->lrate;
        s->value = actual_r; s->also = also;
        if (p < 0 || (in->src_rate && actual_r >= in->src_rate)) { s->why = PC_WHY_NONE; s->lim = PC_LIM_NONE; s->limit = actual_r; s->also = 0; }   /* nothing above the source is missing */
        else { s->why = (uint8_t)why_of(p); s->lim = PC_LIM_MAXRATE; s->limit = pc_bt_rate_pick(c.lim[p]); }
        if (in->neg && in->neg_rate && in->neg_rate != s->value) {   /* the stack configured another rate than the planner expects: the link's value */
            s->value = in->neg_rate;
            if (in->src_rate && in->neg_rate >= in->src_rate) { s->why = PC_WHY_NONE; s->lim = PC_LIM_NONE; s->limit = in->neg_rate; s->also = 0; }
            else { s->why = PC_WHY_SINK; s->lim = PC_LIM_MAXRATE; s->limit = in->neg_rate; s->also = 0; }
        }
    }

    /* link bits: preference (16 / 24), this build, the codec and the sink */
    {
        cons_t c;
        c.lim[0] = in->sink_bits[used] && in->sink_bits[used] < CODEC_BITS[used] ? in->sink_bits[used] : CODEC_BITS[used];
        c.lim[1] = PC_BT_BUILD_MAX_BITS;
        c.lim[2] = in->pref_bits == 16 ? 16 : 24;
        const uint32_t m = c.lim[0] < c.lim[1] ? (c.lim[0] < c.lim[2] ? c.lim[0] : c.lim[2]) : (c.lim[1] < c.lim[2] ? c.lim[1] : c.lim[2]);
        for (int i = 0; i < 3; i++) c.binds[i] = c.lim[i] < 24;
        uint16_t also;
        int p = primary(&c, &also);
        pc_stage_t *s = &os->lbits;
        s->value = m; s->also = also;
        if (p < 0 || (in->src_bits && m >= in->src_bits)) { s->why = PC_WHY_NONE; s->lim = PC_LIM_NONE; s->limit = m; s->also = 0; }
        else {
            s->why = (uint8_t)why_of(p); s->lim = PC_LIM_MAXBITS; s->limit = c.lim[p];
            if (p == 0 && os->codec.why != PC_WHY_NONE && CODEC_BITS[used] < 24) s->why = os->codec.why;   /* the codec's own width: the codec's reason */
        }
    }

    /* channel mode: joint stereo is an SBC mode; every other codec is plain stereo */
    os->chan = (uint8_t)(used == PC_BTC_SBC && in->pref_chan == 1 ? PC_CHAN_JOINT : PC_CHAN_STEREO);
    if (in->neg && in->neg_chan) os->chan = in->neg_chan;                  /* the stack's pick (SBC with bt_chan 0 offers both modes) */

    /* bit rate */
    memset(&os->kbps, 0, sizeof os->kbps);
    if (used == PC_BTC_LDAC) {
        int top = in->ldac_kbps >= 990 ? 990 : in->ldac_kbps >= 660 ? 660 : in->ldac_kbps >= 492 ? 492 : 330;
        int why = top < 990 ? PC_WHY_SETTING : PC_WHY_NONE;
        int target = top;
        if (in->wifi_on && target > 660) { target = 660; why = PC_WHY_SIGNAL; }
        pc_stage_t *s = &os->kbps;
        s->value = (uint32_t)target; s->limit = (uint32_t)target; s->why = (uint8_t)why; s->lim = why ? PC_LIM_MAXRATE : PC_LIM_NONE;
        if (why == PC_WHY_SIGNAL) s->lim = PC_LIM_ABR;
        if (in->live_kbps > 0 && in->live_kbps < target) { s->value = (uint32_t)in->live_kbps; s->why = PC_WHY_SIGNAL; s->lim = PC_LIM_ABR; s->limit = (uint32_t)target; }
    } else if (used == PC_BTC_APTX_HD) { st(&os->kbps, 576, 576, PC_WHY_NONE, PC_LIM_NONE); }
    else if (used == PC_BTC_APTX) { st(&os->kbps, 384, 384, PC_WHY_NONE, PC_LIM_NONE); }
    else if (used == PC_BTC_AAC) { st(&os->kbps, 256, 256, PC_WHY_NONE, PC_LIM_NONE); }
}

/* ---------- text ---------- */
static void codec_text(sb_t *s, uint32_t c) { put(s, "%s", c < PC_BTC_N ? pc_btc_name((pc_btc_t)c) : "?"); }

static void limit_text(sb_t *s, const pc_stage_t *st_, const char *note, pc_lang_t l, bool cjk)
{
    char r[24];
    pc_os_rate_text(r, sizeof r, st_->limit);
    switch (st_->lim) {
    case PC_LIM_HALVE: put(s, L3("折半 %s", "折半 %s", "halve at %s"), r); break;
    case PC_LIM_CEIL: put(s, L3("有線上限 %s", "有线上限 %s", "wired limit %s"), r); break;
    case PC_LIM_HARD: put(s, L3("硬上限 %s", "硬上限 %s", "hard limit %s"), r); break;
    case PC_LIM_MINRATE: put(s, L3("最低 %s，升頻", "最低 %s，升频", "min %s, upsampled"), r); break;
    case PC_LIM_CLOCK: put(s, L3("DAC 時脈上限 %s", "DAC 时钟上限 %s", "DAC clock limit %s"), r); break;
    case PC_LIM_BURST: put(s, L3("格式讀取上限 %s", "格式读取上限 %s", "format read limit %s"), r); break;
    case PC_LIM_BELOW: put(s, L3("低於 %s", "低于 %s", "below %s"), r); break;
    case PC_LIM_MAXRATE: put(s, L3("上限 %s", "上限 %s", "max %s"), r); break;
    case PC_LIM_ABOVE: put(s, "> %s", r); break;
    case PC_LIM_MAXCODEC: put(s, "%s", L3("最高 ", "最高 ", "max ")); codec_text(s, st_->limit); break;
    case PC_LIM_PREFCODEC: put(s, "%s", L3("偏好 ", "偏好 ", "set to ")); codec_text(s, st_->limit); break;
    case PC_LIM_NOCODEC: put(s, "%s", L3("此版不含 ", "此版不含 ", "not in this build: ")); codec_text(s, st_->limit); break;
    case PC_LIM_MAXBITS: put(s, L3("上限 %u-bit", "上限 %u-bit", "max %u-bit"), (unsigned)st_->limit); break;
    case PC_LIM_ABR: put(s, "%s", L3("自動降速", "自动降速", "auto step-down")); break;
    case PC_LIM_TEXT: if (note) put(s, "%s", note); break;
    default: break;
    }
}

int pc_os_why_text(char *b, size_t n, const pc_stage_t *s, const char *note, bool with_orig, bool with_also, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    const char *open = L3("（", "（", " ("), *close = L3("）", "）", ")"), *colon = L3("：", "：", ": ");
    if (s->why == PC_WHY_NONE) {
        if (with_orig) put(&o, "%s%s%s", open, pc_why_tag(PC_WHY_NONE, l, cjk), close);
    } else {
        put(&o, "%s%s", open, pc_why_tag(s->why, l, cjk));
        if (s->lim != PC_LIM_NONE) { put(&o, "%s", colon); limit_text(&o, s, note, l, cjk); }
        if (with_also && s->also) {
            put(&o, "%s", L3("・另：", "・另：", "; also: "));
            bool first = true;
            for (int w = 1; w < PC_WHY_N; w++) if (s->also & (1u << w)) { put(&o, "%s%s", first ? "" : " ", pc_why_tag(w, l, cjk)); first = false; }
        }
        put(&o, "%s", close);
    }
    return (int)o.len;
}

static const char *chan_name(int c, pc_lang_t l, bool cjk)
{
    return c == PC_CHAN_JOINT ? L3("聯合立體聲", "联合立体声", "joint stereo") : L3("立體聲", "立体声", "stereo");
}

static void bt_chain(sb_t *o, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    char r[24];
    codec_text(o, os->codec.value);
    pc_os_rate_text(r, sizeof r, os->lrate.value);
    put(o, " %s / %u-bit / %s", r, (unsigned)os->lbits.value, chan_name(os->chan, l, cjk));
    if (os->kbps.value) {
        if (os->kbps.why == PC_WHY_SIGNAL && os->kbps.limit > os->kbps.value) put(o, " / %u→%u kbps", (unsigned)os->kbps.limit, (unsigned)os->kbps.value);
        else put(o, " / %u kbps", (unsigned)os->kbps.value);
    }
}

/* the main reason of a chain: the first stage below the best, runtime reasons first */
static const pc_stage_t *main_stage(const pc_outstat_t *os)
{
    const pc_stage_t *c[] = { &os->kbps, &os->codec, &os->lrate, &os->lbits };   /* the live (radio) reason first, then the codec, then the steady limits */
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) if (c[i]->why != PC_WHY_NONE) return c[i];
    return NULL;
}

int pc_os_live_bt(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    if (os->kind != PC_OS_A2DP || !os->conn) { put(&o, "%s", L3("未連線", "未连接", "Not connected")); return (int)o.len; }
    put(&o, "%s", L3("正在傳輸：", "正在传输：", "Streaming: "));
    bt_chain(&o, os, l, cjk);
    const pc_stage_t *m = main_stage(os);
    if (m) { char w[96]; pc_os_why_text(w, sizeof w, m, NULL, false, false, l, cjk); put(&o, "%s", w); }
    return (int)o.len;
}

int pc_os_info_best(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    put(&o, "%s", L3("耳機最高：", "耳机最高：", "Headset max: "));
    if (os->kind != PC_OS_A2DP || !os->conn || os->best_codec >= PC_BTC_N) { put(&o, "%s", L3("未連線", "未连接", "not connected")); return (int)o.len; }
    codec_text(&o, os->best_codec);
    char r[24];
    if (os->best_rate) { pc_os_rate_text(r, sizeof r, os->best_rate); put(&o, " %s", r); }
    if (os->best_bits) put(&o, " / %u-bit", (unsigned)os->best_bits);
    return (int)o.len;
}

int pc_os_info_actual(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    put(&o, "%s", L3("實際：", "实际：", "Actual: "));
    if (os->kind != PC_OS_A2DP || !os->conn) { put(&o, "%s", L3("未連線", "未连接", "not connected")); return (int)o.len; }
    char r[24];
    codec_text(&o, os->codec.value);
    pc_os_rate_text(r, sizeof r, os->lrate.value);
    put(&o, " %s / %u-bit", r, (unsigned)os->lbits.value);
    return (int)o.len;
}

int pc_os_info_next(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    put(&o, "%s", L3("下次連線：", "下次连接：", "Next connection: "));
    if (os->kind != PC_OS_A2DP || !os->conn) { put(&o, "%s", L3("未連線", "未连接", "not connected")); return (int)o.len; }
    char r[24];
    codec_text(&o, os->codec.value);
    pc_os_rate_text(r, sizeof r, os->lrate.value);
    put(&o, " %s / %u-bit / %s", r, (unsigned)os->lbits.value, chan_name(os->chan, l, cjk));
    return (int)o.len;
}

int pc_os_build_note(char *b, size_t n, int pref_codec, bool build_ldac, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    if (!build_ldac && pref_codec == PC_BT_PREF_LDAC) put(&o, "%s", L3("此版韌體不含 LDAC", "此版固件不含 LDAC", "This firmware has no LDAC"));
    else if (!build_ldac && pref_codec == PC_BT_PREF_APTX) put(&o, "%s", L3("此版韌體不含 aptX", "此版固件不含 aptX", "This firmware has no aptX"));
    return (int)o.len;
}

/* FIX4 I4: a stage that changes the samples is running (speed other than 100 %, an effect): the rate may pass through, the audio does not */
static bool speed_on(const pc_outstat_t *os) { return os->speed.value && os->speed.value != 100 && os->speed.why == PC_WHY_NONE; }
static bool fx_on(const pc_outstat_t *os) { return os->fx.value > 0; }
static bool altered(const pc_outstat_t *os) { return speed_on(os) || fx_on(os); }
static const char *proc_name_os(const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    if (os->proc == PC_PROC_PASS && altered(os)) return L3("取樣率直通", "采样率直通", "rate passthrough");   /* never a bare 直通 while speed or effects change the audio */
    switch (os->proc) {
    case PC_PROC_PASS: return L3("直通", "直通", "passthrough");
    case PC_PROC_HALVED: return L3("折半", "折半", "halved");
    case PC_PROC_UPSAMPLED: return L3("升頻", "升频", "upsampled");
    case PC_PROC_RESAMPLED: return L3("重取樣", "重采样", "resampled");
    case PC_PROC_REFUSED: return L3("無法播放", "无法播放", "cannot play");
    default: return "";
    }
}
static void stages_text(sb_t *o, const pc_outstat_t *os, pc_lang_t l, bool cjk)   /* each active processing stage, for the one-line views */
{
    if (speed_on(os)) put(o, "・%s %u%%", L3("倍速", "倍速", "speed"), (unsigned)os->speed.value);
    if (fx_on(os)) put(o, "・%s %u", L3("音效", "音效", "effects"), (unsigned)os->fx.value);
}
static const char *proc_name(int p, pc_lang_t l, bool cjk)
{
    switch (p) {
    case PC_PROC_PASS: return L3("直通", "直通", "passthrough");
    case PC_PROC_HALVED: return L3("折半", "折半", "halved");
    case PC_PROC_UPSAMPLED: return L3("升頻", "升频", "upsampled");
    case PC_PROC_RESAMPLED: return L3("重取樣", "重采样", "resampled");
    case PC_PROC_REFUSED: return L3("無法播放", "无法播放", "cannot play");
    default: return "";
    }
}

static void db_text(sb_t *o, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    if (os->dac_db_mute) put(o, "DAC %s", L3("靜音", "静音", "muted"));
    else if (os->dac_db_valid) {
        int d = os->dac_db_x10;
        put(o, "DAC %s%d.%d dB", d < 0 ? "-" : "", (d < 0 ? -d : d) / 10, (d < 0 ? -d : d) % 10);
    }
}

int pc_os_live_wired(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk)
{
    sb_t o = sb_new(b, n);
    if (os->kind != PC_OS_WIRED || !os->src_rate) { put(&o, "%s", L3("沒有在播放", "没有在播放", "Nothing playing")); return (int)o.len; }
    char a[24], r[24];
    pc_os_rate_text(a, sizeof a, os->src_rate);
    if (os->proc == PC_PROC_REFUSED) put(&o, "%s %s・%s", L3("輸入", "输入", "In"), a, proc_name(os->proc, l, cjk));
    else {
        pc_os_rate_text(r, sizeof r, os->rate.value);
        put(&o, "%s %s → %s %s・%s", L3("輸入", "输入", "In"), a, L3("輸出", "输出", "out"), r, proc_name_os(os, l, cjk));
    }
    char w[112];
    pc_os_why_text(w, sizeof w, &os->rate, NULL, true, false, l, cjk);
    put(&o, "%s", w);
    stages_text(&o, os, l, cjk);
    if (os->slot.value == 24) put(&o, "・%s", L3("24-bit 槽", "24-bit 槽", "24-bit slot"));
    if (os->dac_db_valid || os->dac_db_mute) { put(&o, "・"); db_text(&o, os, l, cjk); }
    return (int)o.len;
}

int pc_os_block(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk) { return pc_os_block_icons(b, n, os, l, cjk, NULL); }

int pc_os_block_icons(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk, const char *const *icons)
{
    sb_t o = sb_new(b, n);
    int lines = 0;
    char w[128], r[24];
#define LINE_BEGIN(id, lbl_tw, lbl_cn, lbl_en) do { if (lines++) put(&o, "\n"); if (icons && icons[id]) put(&o, "%s ", icons[id]); put(&o, "%s%s", L3(lbl_tw, lbl_cn, lbl_en), L3("：", "：", ": ")); } while (0)
    if (os->kind == PC_OS_NONE) return 0;
    LINE_BEGIN(PC_OSI_OUTPUT, "輸出", "输出", "Output");
    put(&o, "%s", os->kind == PC_OS_WIRED ? L3("有線", "有线", "wired") : os->kind == PC_OS_A2DP ? L3("藍牙耳機", "蓝牙耳机", "Bluetooth headphones") : "Auracast");
    if (os->fmt) {
        LINE_BEGIN(PC_OSI_SOURCE, "來源", "来源", "Source");
        put(&o, "%s", os->fmt);
        if (os->src_rate) { pc_os_rate_text(r, sizeof r, os->src_rate); put(&o, " %s", r); }
        if (os->src_bits) put(&o, " / %u-bit", (unsigned)os->src_bits);
    }
    if (os->kind == PC_OS_WIRED && os->src_rate) {
        LINE_BEGIN(PC_OSI_PROC, "處理", "处理", "Processing");
        pc_os_rate_text(r, sizeof r, os->src_rate);
        put(&o, "%s", proc_name_os(os, l, cjk));
        if (os->proc != PC_PROC_REFUSED) {
            char r2[24];
            pc_os_rate_text(r2, sizeof r2, os->rate.value);
            if (os->proc == PC_PROC_PASS) put(&o, " %s", r2); else put(&o, " %s→%s", r, r2);
        }
        pc_os_why_text(w, sizeof w, &os->rate, NULL, true, true, l, cjk);
        put(&o, "%s", w);
        if (os->proc != PC_PROC_REFUSED) {
            LINE_BEGIN(PC_OSI_BITS, "位元", "位数", "Bits");
            put(&o, "%u-bit", (unsigned)os->bits.value);
            pc_os_why_text(w, sizeof w, &os->bits, NULL, false, true, l, cjk);
            put(&o, "%s", w);
            if (os->slot.value) {
                LINE_BEGIN(PC_OSI_SLOT, "槽寬", "槽宽", "Slot");
                put(&o, "%u-bit", (unsigned)os->slot.value);
                pc_os_why_text(w, sizeof w, &os->slot, NULL, false, false, l, cjk);
                put(&o, "%s", w);
            }
        }
        if (os->dac_db_valid || os->dac_db_mute) { LINE_BEGIN(PC_OSI_VOLUME, "音量", "音量", "Volume"); db_text(&o, os, l, cjk); }
    } else if (os->kind == PC_OS_A2DP) {
        if (!os->conn) { LINE_BEGIN(PC_OSI_STATE, "狀態", "状态", "State"); put(&o, "%s", L3("未連線", "未连接", "Not connected")); }
        else {
            if (os->src_rate) {
                LINE_BEGIN(PC_OSI_PROC, "處理", "处理", "Processing");
                pc_os_rate_text(r, sizeof r, os->src_rate);
                char r2[24];
                pc_os_rate_text(r2, sizeof r2, os->lrate.value);
                put(&o, "%s %s→%s", os->src_rate == os->lrate.value ? (altered(os) ? L3("取樣率直通", "采样率直通", "rate passthrough") : L3("直通", "直通", "passthrough")) : L3("重取樣", "重采样", "resampled"), r, r2);
                pc_os_why_text(w, sizeof w, &os->lrate, NULL, os->src_rate == os->lrate.value, true, l, cjk);
                put(&o, "%s", w);
            }
            LINE_BEGIN(PC_OSI_CODEC, "編碼", "编码", "Codec");
            codec_text(&o, os->codec.value);
            pc_os_why_text(w, sizeof w, &os->codec, NULL, false, true, l, cjk);
            put(&o, "%s", w);
            LINE_BEGIN(PC_OSI_RATE, "取樣率", "采样率", "Rate");
            pc_os_rate_text(r, sizeof r, os->lrate.value);
            put(&o, "%s", r);
            pc_os_why_text(w, sizeof w, &os->lrate, NULL, false, true, l, cjk);
            put(&o, "%s", w);
            LINE_BEGIN(PC_OSI_BITS, "位元", "位数", "Bits");
            put(&o, "%u-bit", (unsigned)os->lbits.value);
            pc_os_why_text(w, sizeof w, &os->lbits, NULL, false, true, l, cjk);
            put(&o, "%s", w);
            LINE_BEGIN(PC_OSI_CHAN, "聲道", "声道", "Channels");
            put(&o, "%s", chan_name(os->chan, l, cjk));
            if (os->kbps.value) {
                LINE_BEGIN(PC_OSI_BITRATE, "速率", "速率", "Bit rate");
                if (os->kbps.why == PC_WHY_SIGNAL && os->kbps.limit > os->kbps.value) put(&o, "%u→%u kbps", (unsigned)os->kbps.limit, (unsigned)os->kbps.value);
                else put(&o, "%u kbps", (unsigned)os->kbps.value);
                pc_os_why_text(w, sizeof w, &os->kbps, NULL, false, true, l, cjk);
                put(&o, "%s", w);
            }
        }
    } else if (os->kind == PC_OS_CAST) {
        LINE_BEGIN(PC_OSI_CODEC, "編碼", "编码", "Codec");
        pc_os_rate_text(r, sizeof r, os->lc3_rate);
        put(&o, "LC3 %s / %u kbps", r, (unsigned)os->lc3_kbps);
    }
    if (os->speed.why != PC_WHY_NONE) {                      /* asked for and off (M10: a crossfade alone, at 100 %, is named too) */
        if (os->speed.value == 100) { LINE_BEGIN(PC_OSI_SPEED, "淡入淡出", "淡入淡出", "Crossfade"); }
        else { LINE_BEGIN(PC_OSI_SPEED, "倍速", "倍速", "Speed"); }
        put(&o, "%s", L3("關", "关", "off"));
        pc_os_why_text(w, sizeof w, &os->speed, os->speed_note, false, false, l, cjk);
        put(&o, "%s", w);
    } else if (speed_on(os)) {                               /* FIX4 I4: running */
        LINE_BEGIN(PC_OSI_SPEED, "倍速", "倍速", "Speed");
        put(&o, "%u%%", (unsigned)os->speed.value);
    }
    if (os->fx.why != PC_WHY_NONE) {
        LINE_BEGIN(PC_OSI_FX, "音效", "音效", "Effects");
        put(&o, "%s", L3("暫停", "暂停", "paused"));
        pc_os_why_text(w, sizeof w, &os->fx, os->fx_note, false, false, l, cjk);
        put(&o, "%s", w);
        if (os->fx.value) put(&o, "・%u %s", (unsigned)os->fx.value, L3("個執行中", "个运行中", "running"));
    } else if (fx_on(os)) {                                  /* FIX4 I4: running */
        LINE_BEGIN(PC_OSI_FX, "音效", "音效", "Effects");
        put(&o, "%u %s", (unsigned)os->fx.value, L3("個執行中", "个运行中", "running"));
    }
#undef LINE_BEGIN
    return lines;
}
