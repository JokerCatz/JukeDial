// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_nowinfo.h"
#include "pc/pc_i18n.h"
#include "pc/pc_thumbbg.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pc/pc_rate.h"

typedef struct { char *b; size_t n, len; int lines; bool zh; } out_t;

/* the end of b[0..len) never splits a UTF-8 sequence */
static size_t utf8_whole(const char *b, size_t len)
{
    size_t k = len;
    while (k > 0 && ((unsigned char)b[k - 1] & 0xC0) == 0x80) k--;
    if (k == 0) return len;
    unsigned char lead = (unsigned char)b[k - 1];
    size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    return k - 1 + want > len ? k - 1 : len;
}

static void put(out_t *o, const char *fmt, ...)
{
    if (o->n == 0) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(o->b + o->len, o->n - o->len, fmt, ap);
    va_end(ap);
    if (w < 0) w = 0;
    if (o->len + (size_t)w >= o->n) {            /* cut: keep whole characters, the rest of this and every later piece is dropped */
        o->len = utf8_whole(o->b, o->n - 1);
        o->b[o->len] = 0;
        o->len = o->n - 1;                       /* full: later puts write nothing (vsnprintf into a zero-size tail) */
        return;
    }
    o->len += (size_t)w;
}

/* one line: "<label><sep>" then the pieces the caller puts; the line count includes a line that was cut */
static void begin(out_t *o, const char *zh_label, const char *en_label)
{
    if (o->lines) put(o, "\n\n");
    o->lines++;
    put(o, o->zh ? TZ("%s：") : "%s: ", o->zh ? TZ(zh_label) : en_label);
}

void pc_nowinfo_rate(uint32_t hz, char *b, size_t n)
{
    unsigned k = hz / 1000, f = hz % 1000;
    if (!f) snprintf(b, n, "%u kHz", k);
    else if (f % 100 == 0) snprintf(b, n, "%u.%u kHz", k, f / 100);
    else if (f % 10 == 0) snprintf(b, n, "%u.%02u kHz", k, f / 10);
    else snprintf(b, n, "%u.%03u kHz", k, f);
}

static const char *why_text(uint8_t why, bool zh)
{
    switch (why) {
    case PC_PIPE_CAP: return zh ? TZ("上限設定") : "limit setting";
    case PC_PIPE_SINK: return zh ? TZ("藍牙") : "Bluetooth";
    case PC_PIPE_BURST: return zh ? TZ("區塊過大") : "block too large";
    case PC_PIPE_CLOCK: return zh ? TZ("時脈") : "clock";
    default: return "";
    }
}

int pc_nowinfo_text(const pc_nowinfo_t *ni, bool zh, char *b, size_t n)
{
    out_t o = { b, n, 0, 0, zh };
    if (n) b[0] = 0;
    const char *sep = zh ? TZ("・") : " · ";
    char r[24], r2[24];
    bool playing = ni->fmt != NULL;
    bool wired = ni->out == PC_NI_WIRED || ni->out == PC_NI_SPEAKER;

    if (ni->out == PC_NI_SPEAKER && playing) {                              /* the phone streams to us: its own format is the phone's business */
        begin(&o, "來源", "Source");
        put(&o, "%s", zh ? TZ("藍牙（手機）") : "Bluetooth (phone)");
        if (ni->peer_title && *ni->peer_title) put(&o, "%s%s", sep, ni->peer_title);   /* AVRCP: the phone's title, once known */
    } else if (playing) {
        begin(&o, "來源", "Source");
        put(&o, "%s", ni->fmt);
        if (ni->src_rate) { pc_nowinfo_rate(ni->src_rate, r, sizeof r); put(&o, " %s", r); }
        if (ni->src_bits) put(&o, "%s%u bit", sep, (unsigned)ni->src_bits);
        if (ni->kbps >= 1000) { unsigned t = (ni->kbps + 50) / 100; put(&o, "%s%u.%u Mbps", sep, t / 10, t % 10); }   /* one decimal */
        else if (ni->kbps) put(&o, "%s%u kbps", sep, (unsigned)ni->kbps);
    } else if (ni->nocard) {
        begin(&o, "來源", "Source");
        if (!strcmp(ni->nocard, "stuck")) put(&o, "%s", zh ? TZ("卡沒有正常回應（完整關機再開機，或重新插卡）") : "card not responding (power off and on, or re-insert)");   /* answered, cannot be set up: not "no card" */
        else put(&o, zh ? TZ("無卡（%s）") : "no card (%s)", ni->nocard);
    }

    if (playing && ni->pipe_rate) {
        begin(&o, "處理", "Processing");
        pc_nowinfo_rate(ni->pipe_rate, r, sizeof r);
        if (ni->pipe_why == PC_PIPE_NATIVE || !ni->src_rate || ni->pipe_rate >= ni->src_rate) put(&o, zh ? TZ("%s 原生") : "%s native", r);
        else {
            pc_nowinfo_rate(ni->src_rate, r2, sizeof r2);
            put(&o, zh ? TZ("%s（從 %s 減半：") : "%s (halved from %s: ", r, r2);
            if (ni->pipe_why == PC_PIPE_CLOCK && ni->hw_cap && ni->hw_cap < ni->cap) {   /* refused this boot, not a DAC without that clock */
                pc_nowinfo_rate(ni->hw_cap, r2, sizeof r2);
                put(&o, zh ? TZ("時脈（本次開機降為 %s））") : "clock (lowered to %s this boot))", r2);
            } else put(&o, "%s%s", why_text(ni->pipe_why, zh), zh ? TZ("）") : ")");
        }
    }

    begin(&o, "音效", "Effects");
    if (ni->fx_run <= 0 && ni->fx_paused <= 0) put(&o, "%s", zh ? TZ("關") : "off");
    else {
        put(&o, zh ? TZ("%d 個執行") : "%d running", ni->fx_run);
        if (ni->fx_paused > 0) {
            put(&o, zh ? TZ("・%d 個暫停") : " · %d paused", ni->fx_paused);
            if (ni->fx_why && *ni->fx_why) put(&o, zh ? TZ("（%s）") : " (%s)", ni->fx_why);
        }
    }

    begin(&o, "輸出", "Output");
    if (ni->out == PC_NI_A2DP) {
        put(&o, "%s", zh ? TZ("藍牙") : "Bluetooth");
        if (ni->link && *ni->link) put(&o, " %s", ni->link);
    } else if (ni->out == PC_NI_CAST) {
        put(&o, "%s", zh ? TZ("廣播") : "broadcast");
        if (ni->link && *ni->link) put(&o, " %s", ni->link);
    } else {
        put(&o, "%s", zh ? TZ("耳機孔") : "headphone jack");
        if (ni->out_rate) {
            pc_nowinfo_rate(ni->out_rate, r, sizeof r);
            put(&o, " %s", r);
            put(&o, "%s%u bit", sep, ni->out_bits >= 24 ? 24u : 16u);
        }
    }

    bool under = wired && playing, rel = wired && ni->released;
    bool pass = ni->covers_state == PC_TBG_ST_BUILDING || ni->covers_state == PC_TBG_ST_HELD || ni->covers_tidy;   /* hires T8e: the background pass, with its numbers */
    if (under || rel || ni->covers_paused || pass) {
        begin(&o, "狀況", "Status");
        if (under) put(&o, "underrun %u", (unsigned)ni->underruns);
        if (rel) put(&o, "%s%s", under ? sep : "", zh ? TZ("I2S 已釋放") : "I2S released");
        const char *s2 = under || rel ? sep : "";
        if (ni->covers_state == PC_TBG_ST_BUILDING)
            put(&o, zh ? TZ("%s封面建置中 %lu/%lu（%d%%）") : "%scovers building %lu/%lu (%d%%)", s2, (unsigned long)ni->covers_done, (unsigned long)ni->covers_total,
                pc_tbg_pct(ni->covers_done, ni->covers_total));
        else if (ni->covers_state == PC_TBG_ST_HELD)
            put(&o, zh ? TZ("%s封面建置暫停（%s）%lu/%lu") : "%scovers paused (%s) %lu/%lu", s2, pc_tbg_why_text(ni->covers_why, zh), (unsigned long)ni->covers_done,
                (unsigned long)ni->covers_total);
        else if (ni->covers_paused) put(&o, "%s%s", s2, zh ? TZ("封面建置暫停（高取樣率）") : "covers paused (hi-res)");
        else if (ni->covers_tidy) put(&o, "%s%s", s2, zh ? TZ("封面完成・整理待停止") : "covers done · tidy-up waits for a stop");   /* fix 0 (C4) */
    }
    return o.lines;
}
