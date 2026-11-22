// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_thumbbg.h"
#include "pc/pc_i18n.h"
#include <stdio.h>
#include <string.h>
#include "pc/pc_thumb.h"   /* PC_THUMB_PAUSE_ABOVE (the macro only: no link dependency) */

/* hires T8e: see pc_thumbbg.h. The order of the checks is the precedence of the reasons. */
int pc_tbg_rule(const pc_tbg_in_t *in)
{
    if (!in->card) return PC_TBG_NO_CARD;
    if (in->manual && !in->requested) return PC_TBG_MANUAL;
    if (in->user_paused) return PC_TBG_USER;
    if (in->scanning) return PC_TBG_SCAN;
    if (in->src_rate > PC_THUMB_PAUSE_ABOVE) return PC_TBG_HIRES;   /* = pc_thumb_hires_pause */
    if (in->opening) return PC_TBG_OPENING;
    if (in->loaded && (in->underrun_holds >= PC_TBG_GIVEUP || in->underrun_age_ms < PC_TBG_QUIET_MS)) return PC_TBG_UNDERRUN;
    if (in->loaded && !in->space_known) return PC_TBG_STOP_NEEDED;
    if (in->low_mem) return PC_TBG_LOW_MEM;
    return PC_TBG_RUN;
}

uint32_t pc_tbg_breather_ms(bool loaded) { return loaded ? PC_TBG_BREATHER_PLAY_MS : PC_TBG_BREATHER_IDLE_MS; }

void pc_tbg_under_arm(pc_tbg_under_t *u, uint32_t gen, uint32_t count) { u->gen = gen; u->count = count; u->armed = gen != 0; }
void pc_tbg_under_disarm(pc_tbg_under_t *u) { u->armed = false; }

bool pc_tbg_under_check(pc_tbg_under_t *u, uint32_t gen, uint32_t count, int32_t pos_ms, int64_t now_ms)
{
    bool rise = u->armed && gen && gen == u->gen && count > u->count && pos_ms >= PC_TBG_START_MS;
    if (gen != u->gen) u->armed = false;                         /* another source: nothing of it is the pass's (until the next arm) */
    u->gen = gen;
    u->count = count;
    if (!rise) return false;
    if (!u->any || now_ms - u->last_ms > (int64_t)PC_TBG_QUIET_MS) u->holds++;   /* a new quiet period */
    u->any = true;
    u->last_ms = now_ms;
    return true;
}

void pc_tbg_under_step(pc_tbg_under_t *u, int rule, uint32_t gen, uint32_t count)
{
    if (rule == PC_TBG_RUN) pc_tbg_under_arm(u, gen, count);
    else u->armed = false;                                       /* held (手動, scan, opening, user, ...): nothing it sees is the pass's */
}
void pc_tbg_under_step_end(pc_tbg_under_t *u, bool worked) { if (!worked) u->armed = false; }

bool pc_tbg_sink_block(pc_tbg_sink_t *s, uint32_t sess, bool dry, int64_t now_ms)
{
    if (sess != s->sess) { s->sess = sess; s->fed = false; }
    if (!dry) { if (!s->fed) { s->fed = true; s->since = now_ms; } return false; }
    bool count = s->fed && now_ms - s->since >= PC_TBG_SINK_STEADY_MS;
    s->fed = false;
    return count;
}

bool pc_tbg_may_read_table(bool hires, bool opening, bool wanted, bool pass_active) { return !hires && !opening && (wanted || pass_active); }

uint32_t pc_tbg_under_age(const pc_tbg_under_t *u, int64_t now_ms)
{
    if (!u->any) return UINT32_MAX;
    int64_t d = now_ms - u->last_ms;
    return d <= 0 ? 0 : d >= (int64_t)UINT32_MAX - 1 ? UINT32_MAX - 1 : (uint32_t)d;
}

int pc_tbg_state(bool running, int why)
{
    if (why >= PC_TBG_FULL && why < PC_TBG_N) return PC_TBG_ST_HELD;   /* writes stopped: said even after the pass */
    if (!running) return PC_TBG_ST_DONE;
    return why == PC_TBG_RUN ? PC_TBG_ST_BUILDING : why == PC_TBG_MANUAL ? PC_TBG_ST_MANUAL : PC_TBG_ST_HELD;
}

int pc_tbg_pct(uint32_t done, uint32_t total)
{
    if (!total || done >= total) return 100;
    return (int)((uint64_t)done * 100 / total);
}

static const struct { const char *name, *zh, *en; } WHY[PC_TBG_N] = {
    [PC_TBG_RUN] = { "run", "", "" },
    [PC_TBG_NO_CARD] = { "no_card", "沒有記憶卡", "no card" },
    [PC_TBG_MANUAL] = { "manual", "手動", "manual" },
    [PC_TBG_USER] = { "user", "使用者暫停", "by you" },
    [PC_TBG_SCAN] = { "library_scan", "曲庫掃描中", "library scan" },
    [PC_TBG_HIRES] = { "hires", "高取樣率", "hi-res" },
    [PC_TBG_OPENING] = { "opening", "換曲中", "track opening" },
    [PC_TBG_UNDERRUN] = { "underrun", "播放不順", "playback stutter" },
    [PC_TBG_LOW_MEM] = { "low_internal", "記憶體不足", "low memory" },
    [PC_TBG_STOP_NEEDED] = { "stop_needed", "等停止播放", "waits for a stop" },
    [PC_TBG_FULL] = { "full", "記憶卡空間不足", "card full" },
    [PC_TBG_WRITE] = { "write", "寫入失敗", "write failed" },
    [PC_TBG_TABLE_FULL] = { "table_full", "快取表已滿", "cache table full" },
};
const char *pc_tbg_why_name(int w) { return w >= 0 && w < PC_TBG_N ? WHY[w].name : "?"; }
const char *pc_tbg_why_text(int w, bool zh) { return w >= 0 && w < PC_TBG_N ? (zh ? TZ(WHY[w].zh) : WHY[w].en) : ""; }

int pc_tbg_why_of(const char *name)
{
    if (!name) return PC_TBG_RUN;
    for (int w = 0; w < PC_TBG_N; w++) if (!strcmp(name, WHY[w].name)) return w;
    if (!strcmp(name, "playing") || !strcmp(name, "space_unknown")) return PC_TBG_STOP_NEEDED;   /* thumb_svc's gate / write gate words */
    if (!strcmp(name, "space")) return PC_TBG_FULL;
    return PC_TBG_WRITE;                                         /* cap, open_failed, anything else that stops writes */
}

/* snprintf, then cut back to a whole UTF-8 sequence (snprintf may cut inside one) */
static void put(char *b, size_t n, const char *fmt, const char *a, const char *c)
{
    if (!n) return;
    int r = snprintf(b, n, fmt, a, c);
    if (r < 0) { b[0] = 0; return; }
    size_t l = strlen(b);
    if ((size_t)r >= n) {                                        /* cut: drop a trailing partial sequence */
        size_t i = l;
        while (i > 0 && ((unsigned char)b[i - 1] & 0xC0) == 0x80) i--;
        if (i > 0 && ((unsigned char)b[i - 1] & 0x80)) {
            unsigned char lead = (unsigned char)b[i - 1];
            size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (l - (i - 1) < need) b[i - 1] = 0;
        }
    }
}

void pc_tbg_state_text(const pc_tbg_prog_t *p, bool zh, char *b, size_t n)
{
    switch (p->state) {
    case PC_TBG_ST_BUILDING: put(b, n, "%s%s", zh ? TZ("建置中") : "Building", ""); break;
    case PC_TBG_ST_HELD: put(b, n, zh ? TZ("暫停：%s%s") : "Paused: %s%s", pc_tbg_why_text(p->why, zh), ""); break;
    case PC_TBG_ST_MANUAL: put(b, n, "%s%s", zh ? TZ("手動") : "Manual", ""); break;
    default: put(b, n, "%s%s", zh ? TZ("完成") : "Done", p->tidy ? (zh ? TZ("・整理待停止") : " · tidy-up waits for a stop") : ""); break;
    }
}

void pc_tbg_count_text(const pc_tbg_prog_t *p, bool zh, char *b, size_t n)
{
    char x[64], u[40] = "";
    if (p->unchecked) snprintf(u, sizeof u, zh ? TZ("・未檢查 %lu 首") : " · %lu not checked", (unsigned long)p->unchecked);   /* fix 0: apart, not in 剩 */
    if (!p->total) { put(b, n, "%s%s", zh ? TZ("沒有封面") : "No covers", u); return; }
    uint32_t left = p->done < p->total ? p->total - p->done : 0;
    snprintf(x, sizeof x, zh ? TZ("%lu / %lu・%d%%・剩 %lu 首") : "%lu / %lu · %d%% · %lu left", (unsigned long)p->done, (unsigned long)p->total,
             pc_tbg_pct(p->done, p->total), (unsigned long)left);
    put(b, n, "%s%s", x, u);
}

bool pc_tbg_status_row(const pc_tbg_prog_t *p)
{
    if (p->state == PC_TBG_ST_BUILDING || p->state == PC_TBG_ST_HELD || p->tidy) return true;
    return p->state == PC_TBG_ST_MANUAL && (p->done < p->total || p->unchecked);
}
