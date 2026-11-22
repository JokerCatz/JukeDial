// SPDX-License-Identifier: LGPL-2.1-or-later
/* The speaker mode's AVRCP controller as a pure state machine (pc_btmeta.h, fix round 1). One caller task; no IDF types. */
#include "pc/pc_btmeta.h"
#include <string.h>

void pc_bma_init(pc_bma_t *s, void *(*grow)(void *, size_t), void (*drop)(void *))
{
    memset(s, 0, sizeof *s);
    s->grow = grow;
    s->drop = drop;
    s->art_state = "none";
}

static void rx_free(pc_bma_t *s)
{
    if (s->rx) s->drop(s->rx);
    s->rx = NULL;
    s->rx_len = s->rx_cap = 0;
    s->rx_big = false;
}
void pc_bma_free(pc_bma_t *s) { rx_free(s); }

static void set_hdl(char *d, const char *h) { memcpy(d, h, PC_BTMETA_HANDLE); d[PC_BTMETA_HANDLE] = 0; if (strlen(h) < PC_BTMETA_HANDLE) strcpy(d, h); }

/* the page shows a real track (not nothing, not the wait mark) */
static bool known(const pc_bma_t *s) { return s->shown.title[0] && strcmp(s->shown.title, PC_BTMETA_WAIT); }

static void publish(pc_bma_t *s, pc_bma_out_t *out) { out->act |= PC_BMA_DO_META; out->meta = &s->shown; }

static void ask(pc_bma_t *s, pc_bma_out_t *out, uint32_t now)
{
    if (s->asks_out < 8) s->asks_out++;
    s->ask_at = now;
    out->act |= PC_BMA_DO_ASK;
    out->ask_mask = (uint8_t)(PC_BMA_META_MASK | (s->tg_art ? PC_BTMETA_ART : 0));
}

static void fail(pc_bma_t *s, pc_bma_out_t *out, const char *why)
{
    out->act |= PC_BMA_DO_FAIL;
    out->fail = why;
    s->art_state = "fail";
    s->last_fail = why;
}

/* the GET in flight ends (why NULL: its last piece came). A stale or abandoned one is dropped without a word. */
static void end_get(pc_bma_t *s, pc_bma_out_t *out, const char *why, uint32_t now)
{
    bool quiet = s->stale || s->abandoned;
    s->getting = s->stale = s->abandoned = false;
    if (quiet) { rx_free(s); return; }
    if (!why && s->rx_big) why = "too_big";
    if (!why && !s->rx_len) why = "empty";
    set_hdl(s->have, s->asked);                         /* not asked again for this handle */
    if (why) { rx_free(s); fail(s, out, why); return; }
    out->act |= PC_BMA_DO_ART;
    out->art = s->rx;
    out->art_len = s->rx_len;
    out->art_ms = now - s->get_at;
    set_hdl(out->art_hdl, s->asked);
    s->rx = NULL;                                       /* the receiver owns it now */
    s->rx_len = s->rx_cap = 0;
    s->art_state = "ok";
}

/* what the cover needs next: connect the cover-art link (at most once per track), or GET the shown handle (one at a time) */
static void art_step(pc_bma_t *s, pc_bma_out_t *out, uint32_t now)
{
    const char *want = known(s) ? s->shown.art : "";
    if (s->getting) { s->stale = strcmp(s->asked, want) != 0; return; }   /* a newer handle (or none): this one's data will be dropped */
    if (!s->link) return;
    if (!s->tg_art) { s->art_state = "no_ca"; return; }
    if (s->ca == PC_BMA_CA_DOWN) {
        if (s->ca_track == s->track_n + 1) return;      /* tried on this track already */
        s->ca_track = s->track_n + 1;
        s->ca = PC_BMA_CA_CONNECTING;
        s->ca_at = now;
        s->art_state = "connecting";
        out->act |= PC_BMA_DO_CA_CONNECT;
        return;
    }
    if (s->ca != PC_BMA_CA_UP) return;
    if (!want[0]) { if (known(s)) s->art_state = "none"; return; }   /* this track has no cover */
    if (!strcmp(want, s->have)) return;
    s->getting = true;
    s->stale = false;
    s->gen++;
    s->get_at = now;
    set_hdl(s->asked, want);
    rx_free(s);
    s->art_state = "asking";
    out->act |= PC_BMA_DO_GET;
    set_hdl(out->get, want);
}

/* we cannot use this GET any more (no memory, pieces lost) but the phone goes on sending it: said now, kept in flight until it really ends */
static void early_end(pc_bma_t *s, pc_bma_out_t *out, const char *why)
{
    if (s->stale || s->abandoned) return;               /* already nobody's: nothing to say */
    rx_free(s);
    set_hdl(s->have, s->asked);
    fail(s, out, why);
    s->abandoned = true;
}

static void reset(pc_bma_t *s, bool link, pc_bma_out_t *out)
{
    void *(*grow)(void *, size_t) = s->grow;
    void (*drop)(void *) = s->drop;
    rx_free(s);
    pc_bma_init(s, grow, drop);
    s->link = link;
    publish(s, out);                                    /* nothing of the old phone stays on the page */
    out->act |= PC_BMA_DO_ART_CLEAR;
}

/* an answer is complete: the page gets it, unless a newer ask is still out (then it described an older state) */
static void burst_end(pc_bma_t *s, pc_bma_out_t *out)
{
    s->burst = false;
    if (s->asks_out > 1) { s->asks_out--; return; }
    s->asks_out = 0;
    if (memcmp(&s->shown, &s->stage, sizeof s->shown)) { s->shown = s->stage; publish(s, out); }
}

static void data(pc_bma_t *s, const pc_bma_ev_t *ev, pc_bma_out_t *out)
{
    if (!s->getting) return;                            /* after a timeout, a disconnect, a link change: not ours any more */
    if (s->stale || s->abandoned) {                     /* nothing is collected; only its end matters (then the next GET may go) */
        if (ev->status || ev->final) end_get(s, out, NULL, ev->now_ms);
        return;
    }
    if (ev->status) { end_get(s, out, "status", ev->now_ms); return; }
    if (!s->rx_big && ev->len > 0 && ev->data) {
        uint32_t need = s->rx_len + (uint32_t)ev->len;
        if (need > PC_BMA_ART_MAX) { rx_free(s); s->rx_big = true; }
        else {
            if (need > s->rx_cap) {
                uint32_t cap = s->rx_cap ? s->rx_cap : 32u * 1024u;
                while (cap < need) cap *= 2;
                if (cap > PC_BMA_ART_MAX) cap = PC_BMA_ART_MAX;
                uint8_t *n = s->grow(s->rx, cap);
                if (!n) { early_end(s, out, "no_mem"); return; }   /* the old block is freed; the transfer goes on (dropped) */
                s->rx = n;
                s->rx_cap = cap;
            }
            memcpy(s->rx + s->rx_len, ev->data, (size_t)ev->len);
            s->rx_len = need;
        }
    }
    if (ev->final) end_get(s, out, NULL, ev->now_ms);
}

void pc_bma_event(pc_bma_t *s, const pc_bma_ev_t *ev, pc_bma_out_t *out)
{
    memset(out, 0, sizeof *out);
    uint32_t now = ev->now_ms;
    switch (ev->kind) {
    case PC_BMA_LINK:
        reset(s, ev->on, out);
        if (ev->on) out->act |= PC_BMA_DO_CAPS;
        return;
    case PC_BMA_FEAT:
        s->tg_art = ev->on;
        break;
    case PC_BMA_CAPS:
        if (!s->link) return;
        s->track_ok = ev->on;
        if (ev->on) out->act |= PC_BMA_DO_REG;
        ask(s, out, now);
        break;
    case PC_BMA_TRACK:
        if (!s->link) return;
        s->track_n++;
        if (s->track_ok) out->act |= PC_BMA_DO_REG;    /* one-shot: again */
        if (known(s) || !strcmp(s->shown.title, PC_BTMETA_WAIT)) {   /* the old track's fields go at once; the wait mark until the answer */
            memset(&s->shown, 0, sizeof s->shown);
            strcpy(s->shown.title, PC_BTMETA_WAIT);
            publish(s, out);
        }
        ask(s, out, now);
        break;
    case PC_BMA_ATTR:
        if (!s->link) return;
        if (s->burst && (s->burst_ids & ev->id)) burst_end(s, out);   /* the same attribute again: the next answer */
        if (!s->burst) { s->burst = true; s->burst_ids = 0; memset(&s->stage, 0, sizeof s->stage); }
        s->burst_ids |= ev->id;
        pc_btmeta_put(&s->stage, ev->id, ev->data, ev->len);
        s->settle_at = now + PC_BMA_SETTLE_MS;
        break;
    case PC_BMA_CA:
        if (ev->on) {
            s->ca = PC_BMA_CA_UP;
            s->have[0] = 0;                             /* handles belong to one cover-art session: the shown one waits for the new answer */
            if (s->shown.art[0]) { memset(s->shown.art, 0, sizeof s->shown.art); publish(s, out); }
            s->art_state = "none";
            out->act |= PC_BMA_DO_ART_CLEAR;
            if (s->link) ask(s, out, now);              /* the handle comes with the attributes once the link is up */
        } else {
            uint8_t was = s->ca;
            s->ca = PC_BMA_CA_DOWN;
            if (s->getting) end_get(s, out, "disconnect", now);
            else if (was == PC_BMA_CA_CONNECTING) s->art_state = "no_ca";
            out->act |= PC_BMA_DO_ART_CLEAR;
        }
        break;
    case PC_BMA_DATA:
        data(s, ev, out);
        break;
    case PC_BMA_LOST:
        if (!s->link) return;
        if (s->getting) early_end(s, out, "lost");
        ask(s, out, now);
        break;
    case PC_BMA_TICK:
        if (s->burst && (int32_t)(now - s->settle_at) >= 0) burst_end(s, out);   /* no attribute for a while: the answer is complete */
        if (s->asks_out && !s->burst && now - s->ask_at >= PC_BMA_ASK_FORGET_MS) {
            s->asks_out = 0;
            if (!strcmp(s->shown.title, PC_BTMETA_WAIT) && s->reask_track != s->track_n + 1) {   /* the newest answer never came: once more */
                s->reask_track = s->track_n + 1;
                ask(s, out, now);
            }
        }
        if (s->ca == PC_BMA_CA_CONNECTING && now - s->ca_at >= PC_BMA_ART_WAIT_MS) {   /* the connect never answered: no cover on this track */
            s->ca = PC_BMA_CA_DOWN;
            s->art_state = "no_ca";
        }
        if (s->getting && now - s->get_at >= PC_BMA_ART_WAIT_MS) {   /* this GET only: its data are dropped from now on */
            bool quiet = s->stale || s->abandoned;
            s->getting = s->stale = s->abandoned = false;
            rx_free(s);
            if (!quiet) { set_hdl(s->have, s->asked); fail(s, out, "timeout"); }
            s->ca = PC_BMA_CA_DROPPING;                 /* the link may still carry it: no new GET before it was cycled */
            out->act |= PC_BMA_DO_CA_DROP;
        }
        break;
    default:
        return;
    }
    art_step(s, out, now);
}
bool pc_bma_busy(const pc_bma_t *s) { return s->link && (s->burst || s->asks_out || s->getting || s->ca == PC_BMA_CA_CONNECTING); }   /* the settle, forget and timeout clocks */
