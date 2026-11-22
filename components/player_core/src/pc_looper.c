// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_looper.h"
#include <math.h>
#include <string.h>

/* events heard up to a quarter beat before the take starts belong to its first sample (played a touch early) */
#define PREROLL_DIV 4

static bool alive(const pc_looper_t *lp, const pc_lp_ev_t *e)
{
    return !(e->flags & PC_LP_F_DEAD) && e->pass > lp->tclear[e->track];
}
static void bit_set(uint8_t *m, int n, bool on) { if (on) m[n >> 3] |= (uint8_t)(1u << (n & 7)); else m[n >> 3] &= (uint8_t)~(1u << (n & 7)); }
static bool bit_get(const uint8_t *m, int n) { return m[n >> 3] >> (n & 7) & 1u; }

void pc_lp_init(pc_looper_t *lp, pc_lp_ev_t *pool, uint32_t cap, uint16_t *head, uint32_t nhead)
{
    memset(lp, 0, sizeof *lp);
    lp->ev = pool;
    lp->cap = cap > PC_LP_NIL ? PC_LP_NIL : cap;     /* indexes are 16-bit, NIL excluded */
    lp->head = head;
    lp->nhead = nhead;
    for (uint32_t i = 0; i < nhead; i++) head[i] = PC_LP_NIL;
    pc_lp_reset(lp);
}

static uint32_t used_buckets(const pc_looper_t *lp)
{
    uint32_t top = lp->len > lp->max_frame ? lp->len : lp->max_frame + 1;
    uint32_t b = top / PC_LP_BUCKET + 1;
    return b > lp->nhead ? lp->nhead : b;
}

/* The heads are not cleared here: a long loop has up to 64 K of them (128 KB of PSRAM, more than an audio
 * block). The audio task wipes a few per block (pc_lp_wipe); arm / load finish the rest first. */
void pc_lp_reset(pc_looper_t *lp)
{
    uint32_t nb = used_buckets(lp);
    if (lp->wipe > nb) nb = lp->wipe;
    pc_lp_ev_t *ev = lp->ev;
    uint32_t cap = lp->cap, nhead = lp->nhead;
    uint16_t *head = lp->head;
    memset(lp, 0, sizeof *lp);
    lp->ev = ev;
    lp->cap = cap;
    lp->head = head;
    lp->nhead = nhead;
    lp->wipe = nb;
    lp->state = PC_LP_EMPTY;
}

uint32_t pc_lp_wipe(pc_looper_t *lp, uint32_t max)
{
    uint32_t k = lp->wipe < max ? lp->wipe : max;
    lp->wipe -= k;
    for (uint32_t i = 0; i < k; i++) lp->head[lp->wipe + i] = PC_LP_NIL;
    return lp->wipe;
}

uint32_t pc_lp_max_len(const pc_looper_t *lp) { return lp->nhead * PC_LP_BUCKET; }

void pc_lp_set_quant(pc_looper_t *lp, int q) { lp->grid = q == 1 ? lp->beat / 2 : q == 2 ? lp->beat / 4 : 0; }

static void begin_pass(pc_looper_t *lp)
{
    lp->op++;
    lp->undo_kind = PC_LP_UNDO_PASS;
    lp->undo_op = lp->op;
    lp->undo_first = lp->n;
    memset(lp->held, 0, sizeof lp->held);
    memset(lp->qd, 0, sizeof lp->qd);
    memset(lp->ptrack, 0, sizeof lp->ptrack);
}

int pc_lp_arm(pc_looper_t *lp, int64_t start, uint32_t bar, uint32_t beat, int bars, int quant)
{
    if (lp->state != PC_LP_EMPTY || !bar || !beat || bars < 0) return -1;
    pc_lp_wipe(lp, UINT32_MAX);
    if ((uint64_t)bars * bar > pc_lp_max_len(lp) || bar > pc_lp_max_len(lp)) return -1;
    lp->start = start;
    lp->bar = bar;
    lp->beat = beat;
    lp->bars = bars;
    lp->len = (uint32_t)bars * bar;
    lp->max_frame = 0;
    lp->play_pos = 0;
    lp->passes = 0;
    pc_lp_set_quant(lp, quant);
    begin_pass(lp);
    lp->state = PC_LP_COUNTIN;
    return 0;
}

void pc_lp_end_take(pc_looper_t *lp, int64_t now)
{
    if (lp->state != PC_LP_TAKE && lp->state != PC_LP_COUNTIN) return;
    int64_t rel = now - lp->start;
    uint32_t k = rel <= 0 ? 1 : (uint32_t)((rel + lp->bar - 1) / lp->bar);
    if (k < 1) k = 1;
    uint32_t maxk = pc_lp_max_len(lp) / lp->bar;
    if (k > maxk) k = maxk;
    if (!lp->len || k * lp->bar < lp->len) lp->len = k * lp->bar;
}

/* sorted insert into the frame's bucket (equal frames keep arrival order) */
static void insert(pc_looper_t *lp, uint32_t frame, const pc_midi_ev_t *e, uint8_t flags, int track)
{
    uint32_t b = frame / PC_LP_BUCKET;
    if (b >= lp->nhead) return;
    if (lp->n >= lp->cap) { lp->full = true; return; }
    uint16_t i = (uint16_t)lp->n++;
    pc_lp_ev_t *x = &lp->ev[i];
    x->frame = frame;
    x->pass = lp->op;
    x->type = e->type;
    x->ch = e->ch;
    if (e->type == PC_MIDI_BEND) { x->a = (uint8_t)(e->bend & 0xFF); x->b = (uint8_t)((uint16_t)e->bend >> 8); }
    else { x->a = e->a; x->b = e->b; }
    x->track = (uint8_t)track;
    x->flags = flags;
    x->pad = 0;
    uint16_t *link = &lp->head[b];
    while (*link != PC_LP_NIL && lp->ev[*link].frame <= frame) link = &lp->ev[*link].next;
    x->next = *link;
    *link = i;
    lp->cnt[track]++;
    if (frame > lp->max_frame) lp->max_frame = frame;
}

static bool recordable(const pc_midi_ev_t *e)
{
    if (e->ch >= PC_LP_CHANNELS) return false;
    switch (e->type) {
    case PC_MIDI_NOTE_ON: case PC_MIDI_NOTE_OFF: case PC_MIDI_BEND: case PC_MIDI_PROGRAM: return true;
    case PC_MIDI_CC: return e->a < 120;              /* channel mode messages (all off ...) are not music */
    default: return false;
    }
}

static void record_at(pc_looper_t *lp, const pc_midi_ev_t *e, int64_t heard)
{
    int64_t rel = heard - lp->start;
    bool take = lp->state == PC_LP_TAKE || lp->state == PC_LP_COUNTIN;
    uint32_t L = lp->len;
    if (take) {
        if (rel < 0) {
            if (-rel > (int64_t)(lp->beat / PREROLL_DIV)) return;
            rel = 0;
        }
        if (L) rel %= L;
        else if (rel >= (int64_t)pc_lp_max_len(lp)) return;
    } else {
        rel %= (int64_t)L;
        if (rel < 0) rel += L;
    }
    uint32_t f = (uint32_t)rel;
    int ch = e->ch, n = e->a & 127;
    bool on = e->type == PC_MIDI_NOTE_ON && e->b > 0, off = e->type == PC_MIDI_NOTE_OFF || (e->type == PC_MIDI_NOTE_ON && !e->b);
    int32_t shift = 0;
    if (lp->grid && on) shift = (int32_t)((f + lp->grid / 2) / lp->grid * lp->grid) - (int32_t)f;
    else if (lp->grid && off) shift = lp->qd[ch][n];
    /* the track: the selected one, but an off goes with its on (the selection may have moved meanwhile) */
    int track = lp->sel;
    if (on) { lp->qd[ch][n] = shift; bit_set(lp->held[ch], n, true); lp->htrack[ch][n] = (uint8_t)track; }
    if (off) { if (bit_get(lp->held[ch], n)) track = lp->htrack[ch][n]; bit_set(lp->held[ch], n, false); }
    if (e->type == PC_MIDI_CC && e->a == 64 && ch < 16) {
        if (e->b >= 64) { if (lp->ptrack[ch]) track = lp->ptrack[ch] - 1; else lp->ptrack[ch] = (uint8_t)(track + 1); }
        else { if (lp->ptrack[ch]) track = lp->ptrack[ch] - 1; lp->ptrack[ch] = 0; }
    }
    int64_t q = (int64_t)f + shift;
    uint8_t flags = 0;
    if (L) {
        bool wrapped = q >= L;
        if (wrapped) q -= L;
        if (q < 0) q += L;
        if (take) { if (wrapped) flags = PC_LP_F_SKIP; }
        else {
            uint32_t d = (lp->play_pos + L - f) % L;     /* how far the playhead is past the played position */
            if (shift > 0 && (uint32_t)shift >= d) flags = PC_LP_F_SKIP;   /* snapped ahead of it: not again at once */
        }
    } else if (q < 0) q = 0;
    insert(lp, (uint32_t)q, e, flags, track);
}

void pc_lp_record(pc_looper_t *lp, const pc_midi_ev_t *e, int64_t heard)
{
    if (!recordable(e)) return;
    if (lp->state == PC_LP_COUNTIN || lp->state == PC_LP_TAKE || (lp->state == PC_LP_PLAY && lp->rec)) record_at(lp, e, heard);
}

static void emit_ev(pc_looper_t *lp, const pc_lp_ev_t *x, pc_lp_emit_fn emit, void *ctx)
{
    lp->emit_frame = x->frame;
    pc_midi_ev_t e = { x->type, x->ch, x->a, x->b, 0, 0 };
    if (x->type == PC_MIDI_BEND) { e.bend = (int16_t)(x->a | x->b << 8); e.a = e.b = 0; }
    if (x->type == PC_MIDI_NOTE_ON || x->type == PC_MIDI_NOTE_OFF)
        bit_set(lp->sounding[x->track][x->ch], x->a & 127, x->type == PC_MIDI_NOTE_ON && x->b > 0);
    if (x->type == PC_MIDI_CC && x->a == 64 && x->ch < 16) {
        if (x->b >= 64) lp->pedal[x->track] |= (uint16_t)(1u << x->ch);
        else lp->pedal[x->track] &= (uint16_t)~(1u << x->ch);
    }
    emit(ctx, &e);
}

/* events in [lo, lo + n), within one pass */
static void emit_span(pc_looper_t *lp, uint32_t lo, uint32_t n, pc_lp_emit_fn emit, void *ctx)
{
    if (!n) return;
    uint32_t hi = lo + n;
    for (uint32_t b = lo / PC_LP_BUCKET; b <= (hi - 1) / PC_LP_BUCKET && b < lp->nhead; b++) {
        for (uint16_t i = lp->head[b]; i != PC_LP_NIL; i = lp->ev[i].next) {
            pc_lp_ev_t *x = &lp->ev[i];
            if (x->frame < lo) continue;
            if (x->frame >= hi) break;
            if (!alive(lp, x) || (lp->mute >> x->track & 1u)) continue;
            if (x->flags & PC_LP_F_SKIP) { x->flags &= (uint8_t)~PC_LP_F_SKIP; continue; }
            emit_ev(lp, x, emit, ctx);
        }
    }
}

static void emit_range(pc_looper_t *lp, uint32_t lo, uint32_t n, pc_lp_emit_fn emit, void *ctx)
{
    uint32_t L = lp->len;
    while (n) {
        uint32_t k = lo + n > L ? L - lo : n;
        emit_span(lp, lo, k, emit, ctx);
        n -= k;
        lo += k;
        if (lo >= L) { lo = 0; lp->passes++; }
    }
}

/* the take is over: events quantised past its end go to the start (skipped once), notes still held end at the
 * seam (skipped once too: the player may still be holding them) */
static void finish_take(pc_looper_t *lp)
{
    uint32_t L = lp->len;
    if (lp->max_frame >= L) {
        for (uint32_t b = L / PC_LP_BUCKET; b <= lp->max_frame / PC_LP_BUCKET && b < lp->nhead; b++) {
            uint16_t *link = &lp->head[b];
            while (*link != PC_LP_NIL) {
                pc_lp_ev_t *x = &lp->ev[*link];
                if (x->frame < L) { link = &x->next; continue; }
                uint16_t i = *link;
                *link = x->next;                     /* unlink, then back in at frame - L */
                x->frame %= L;
                x->flags |= PC_LP_F_SKIP;
                uint16_t *l2 = &lp->head[x->frame / PC_LP_BUCKET];
                while (*l2 != PC_LP_NIL && lp->ev[*l2].frame <= x->frame) l2 = &lp->ev[*l2].next;
                x->next = *l2;
                *l2 = i;
            }
        }
        lp->max_frame = L - 1;
    }
    for (int ch = 0; ch < PC_LP_CHANNELS; ch++)
        for (int n = 0; n < 128; n++)
            if (bit_get(lp->held[ch], n)) {
                pc_midi_ev_t off = { PC_MIDI_NOTE_OFF, (uint8_t)ch, (uint8_t)n, 0, 0, 0 };
                insert(lp, 0, &off, PC_LP_F_SKIP, lp->htrack[ch][n]);
            }
    memset(lp->held, 0, sizeof lp->held);
    lp->state = PC_LP_PLAY;
    lp->rec = false;
    lp->passes = 0;
    if (lp->cnt[lp->sel])                            /* on to the next empty track */
        for (int k = 1; k < PC_LP_TRACKS; k++) {
            int t = (lp->sel + k) % PC_LP_TRACKS;
            if (!lp->cnt[t]) { lp->sel = t; break; }
        }
}

void pc_lp_block(pc_looper_t *lp, int64_t pos, int frames, pc_lp_emit_fn emit, void *ctx)
{
    int64_t end_pos = pos + frames;
    switch (lp->state) {
    case PC_LP_COUNTIN:
        if (end_pos <= lp->start) return;
        lp->state = PC_LP_TAKE;
        /* fall through */
    case PC_LP_TAKE: {
        if (!lp->len) {                              /* free take, no tap: the longest whole bars that fit */
            uint32_t maxb = pc_lp_max_len(lp) / lp->bar;
            if (maxb > PC_LP_MAX_BARS_FREE) maxb = PC_LP_MAX_BARS_FREE;
            if (end_pos > lp->start + (int64_t)maxb * lp->bar) lp->len = maxb * lp->bar;
        }
        int64_t end = lp->start + lp->len;
        if (!lp->len || end_pos <= end) { lp->play_pos = (uint32_t)(pos > lp->start ? end_pos - lp->start : 0); return; }
        finish_take(lp);
        uint32_t over = (uint32_t)(end_pos - end);   /* only the part after the seam plays */
        emit_range(lp, 0, over, emit, ctx);
        lp->play_pos = over % lp->len;
        return;
    }
    case PC_LP_PLAY: {
        if (end_pos <= lp->start) return;
        int64_t rel = pos - lp->start;
        uint32_t lo = rel < 0 ? 0 : (uint32_t)(rel % lp->len), n = rel < 0 ? (uint32_t)(end_pos - lp->start) : (uint32_t)frames;
        emit_range(lp, lo, n, emit, ctx);
        lp->play_pos = (lo + n) % lp->len;
        return;
    }
    default: return;
    }
}

void pc_lp_all_off(pc_looper_t *lp, int mask, pc_lp_emit_fn emit, void *ctx)
{
    for (int t = 0; t < PC_LP_TRACKS; t++) {
        if (!(mask >> t & 1)) continue;
        for (int ch = 0; ch < PC_LP_CHANNELS; ch++)
            for (int n = 0; n < 128; n++)
                if (bit_get(lp->sounding[t][ch], n)) {
                    bit_set(lp->sounding[t][ch], n, false);
                    pc_midi_ev_t e = { PC_MIDI_NOTE_OFF, (uint8_t)ch, (uint8_t)n, 0, 0, 0 };
                    if (emit) emit(ctx, &e);
                }
        for (int ch = 0; ch < 16; ch++)
            if (lp->pedal[t] >> ch & 1u) {
                pc_midi_ev_t e = { PC_MIDI_CC, (uint8_t)ch, 64, 0, 0, 0 };
                if (emit) emit(ctx, &e);
            }
        lp->pedal[t] = 0;
    }
}

void pc_lp_overdub(pc_looper_t *lp, bool on, int64_t heard)
{
    if (lp->state != PC_LP_PLAY || on == lp->rec) return;
    if (on) { begin_pass(lp); lp->rec = true; return; }
    for (int ch = 0; ch < PC_LP_CHANNELS; ch++)       /* held notes end at the punch-out */
        for (int n = 0; n < 128; n++)
            if (bit_get(lp->held[ch], n)) {
                pc_midi_ev_t off = { PC_MIDI_NOTE_OFF, (uint8_t)ch, (uint8_t)n, 0, 0, 0 };
                record_at(lp, &off, heard);
            }
    lp->rec = false;
}

void pc_lp_stop(pc_looper_t *lp, int64_t heard, pc_lp_emit_fn emit, void *ctx)
{
    if (lp->state == PC_LP_COUNTIN || lp->state == PC_LP_TAKE) { pc_lp_reset(lp); return; }
    if (lp->state != PC_LP_PLAY) return;
    pc_lp_overdub(lp, false, heard);
    pc_lp_all_off(lp, (1 << PC_LP_TRACKS) - 1, emit, ctx);
    lp->state = PC_LP_STOP;
}

void pc_lp_play(pc_looper_t *lp, int64_t start)
{
    if (lp->state != PC_LP_STOP) return;
    lp->start = start;
    lp->play_pos = 0;
    lp->passes = 0;
    lp->state = PC_LP_PLAY;
}

void pc_lp_select(pc_looper_t *lp, int t) { if (t >= 0 && t < PC_LP_TRACKS) lp->sel = t; }

void pc_lp_mute(pc_looper_t *lp, int t, pc_lp_emit_fn emit, void *ctx)
{
    if (t < 0 || t >= PC_LP_TRACKS) return;
    lp->mute ^= (uint8_t)(1u << t);
    if (lp->mute >> t & 1u) pc_lp_all_off(lp, 1 << t, emit, ctx);
}

int pc_lp_clear(pc_looper_t *lp, int t, pc_lp_emit_fn emit, void *ctx)
{
    if (t < 0 || t >= PC_LP_TRACKS || !lp->cnt[t] || lp->state == PC_LP_TAKE || lp->state == PC_LP_COUNTIN) return 0;
    if (lp->rec) pc_lp_overdub(lp, false, 0);        /* never mind the held-note offs: they are being cleared */
    int removed = (int)lp->cnt[t];
    lp->op++;
    lp->undo_kind = PC_LP_UNDO_CLEAR;
    lp->undo_op = lp->op;
    lp->undo_track = t;
    lp->undo_tclear = lp->tclear[t];
    lp->undo_cnt = lp->cnt[t];
    lp->tclear[t] = lp->op;                          /* every event so far has a smaller pass */
    lp->cnt[t] = 0;
    pc_lp_all_off(lp, 1 << t, emit, ctx);
    return removed;
}

int pc_lp_undo(pc_looper_t *lp, pc_lp_emit_fn emit, void *ctx)
{
    if (lp->state != PC_LP_PLAY && lp->state != PC_LP_STOP) return 0;
    if (lp->undo_kind == PC_LP_UNDO_PASS) {
        lp->rec = false;
        int mask = 0;
        for (uint32_t i = lp->undo_first; i < lp->n; i++) {
            pc_lp_ev_t *x = &lp->ev[i];
            if (x->pass != lp->undo_op || !alive(lp, x)) continue;
            x->flags |= PC_LP_F_DEAD;
            lp->cnt[x->track]--;
            mask |= 1 << x->track;
        }
        pc_lp_all_off(lp, mask, emit, ctx);
    } else if (lp->undo_kind == PC_LP_UNDO_CLEAR) {
        lp->tclear[lp->undo_track] = lp->undo_tclear;
        lp->cnt[lp->undo_track] = lp->undo_cnt;
    } else return 0;
    lp->undo_kind = PC_LP_UNDO_NONE;
    return 1;
}

bool pc_lp_has_content(const pc_looper_t *lp)
{
    for (int t = 0; t < PC_LP_TRACKS; t++) if (lp->cnt[t]) return true;
    return false;
}

int64_t pc_lp_next_downbeat(int64_t now, int64_t beat_pos, uint32_t beat_idx, double interval, int bpb, double min_ahead)
{
    double p = (double)beat_pos;
    if (bpb < 1) bpb = 1;
    for (uint32_t k = beat_idx; k % (uint32_t)bpb; k++) p += interval;
    while (p < (double)now + min_ahead) p += bpb * interval;
    return llround(p);
}

/* ---- files ---- */
#define HDR 36
#define EVB 10
static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v); w16(p + 2, v >> 16); }
static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | r16(p + 2) << 16; }

static uint32_t total(const pc_looper_t *lp)
{
    uint32_t n = 0;
    for (int t = 0; t < PC_LP_TRACKS; t++) n += lp->cnt[t];
    return n;
}

size_t pc_lp_save_size(const pc_looper_t *lp) { return HDR + (size_t)EVB * total(lp); }

size_t pc_lp_save(const pc_looper_t *lp, const pc_lp_meta_t *m, uint8_t *out, size_t cap)
{
    size_t need = pc_lp_save_size(lp);
    if (cap < need || !lp->len) return 0;
    memset(out, 0, HDR);
    memcpy(out, "JCLP", 4);
    out[4] = 1;
    out[5] = PC_LP_TRACKS;
    out[6] = m->bpb;
    out[7] = m->bars;
    w32(out + 8, m->rate);
    w32(out + 12, lp->len);
    w16(out + 16, m->bpm_x100);
    out[18] = m->quant;
    out[19] = lp->mute;
    memcpy(out + 20, m->chip, strnlen(m->chip, sizeof m->chip));
    uint8_t *p = out + HDR;
    uint32_t n = 0;
    for (uint32_t b = 0; b < used_buckets(lp); b++)
        for (uint16_t i = lp->head[b]; i != PC_LP_NIL; i = lp->ev[i].next) {
            const pc_lp_ev_t *x = &lp->ev[i];
            if (!alive(lp, x) || x->frame >= lp->len) continue;
            w32(p, x->frame);
            p[4] = x->type; p[5] = x->ch; p[6] = x->a; p[7] = x->b; p[8] = x->track; p[9] = 0;
            p += EVB;
            n++;
        }
    w32(out + 32, n);
    return HDR + (size_t)EVB * n;
}

/* a stored event a live one could have been (bytes in range; the pad channel has notes only) */
static bool loadable(const pc_midi_ev_t *e)
{
    if (e->type == PC_MIDI_BEND) return e->bend >= -8192 && e->bend <= 8191;
    if (e->a > 127 || e->b > 127) return false;
    if (e->ch == PC_LP_CHANNELS - 1 && e->type != PC_MIDI_NOTE_ON && e->type != PC_MIDI_NOTE_OFF) return false;
    return recordable(e);
}

int pc_lp_load(pc_looper_t *lp, pc_lp_meta_t *m, const uint8_t *in, size_t sz)
{
    if (sz < HDR || memcmp(in, "JCLP", 4) || in[4] != 1 || in[5] != PC_LP_TRACKS) return -1;
    uint32_t len = r32(in + 12), n = r32(in + 32), rate = r32(in + 8), bpm = r16(in + 16);
    if (!len || len > pc_lp_max_len(lp) || n > lp->cap || sz < HDR + (size_t)EVB * n || !rate || bpm < 3000 || bpm > 30000 || !in[6]) return -1;
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *p = in + HDR + k * EVB;
        if (r32(p) >= len || p[5] >= PC_LP_CHANNELS || p[8] >= PC_LP_TRACKS || p[4] < PC_MIDI_NOTE_ON || p[4] > PC_MIDI_BEND) return -1;
    }
    memset(m, 0, sizeof *m);
    m->rate = rate;
    m->len = len;
    m->bpm_x100 = (uint16_t)bpm;
    m->bpb = in[6];
    m->bars = in[7];
    m->quant = in[18];
    m->mute = in[19];
    memcpy(m->chip, in + 20, sizeof m->chip - 1);
    pc_lp_reset(lp);
    pc_lp_wipe(lp, UINT32_MAX);                      /* not the audio task: the looper is parked */
    lp->len = len;
    lp->beat = (uint32_t)((uint64_t)rate * 6000u / bpm);
    lp->bar = lp->beat * m->bpb;
    lp->bars = m->bars;
    pc_lp_set_quant(lp, m->quant);
    lp->op = 1;
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *p = in + HDR + k * EVB;
        pc_midi_ev_t e = { p[4], p[5], p[6], p[7], 0, 0 };
        if (e.type == PC_MIDI_BEND) e.bend = (int16_t)(p[6] | p[7] << 8);
        if (!loadable(&e)) continue;
        insert(lp, r32(p), &e, 0, p[8]);
    }
    lp->mute = m->mute & ((1u << PC_LP_TRACKS) - 1);
    lp->undo_kind = PC_LP_UNDO_NONE;
    lp->state = PC_LP_STOP;
    return 0;
}

/* ---- ProTracker export ---- */
#define MOD_HDR 1084u
#define SQ_LEN 32u
#define NOISE_LEN 256u
#define MOD_MAX_PATTERNS 64u
static const uint16_t k_period[36] = {           /* ProTracker, finetune 0: C-1 (MIDI 48) .. B-3 */
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};

typedef struct { int key, on_row; uint32_t age; } mch_t;

/* channel allocation over the loop in time order; write == NULL: count the most channels in use */
static int mod_pass(const pc_looper_t *lp, uint32_t rows, double rowlen, int nch, uint8_t *pat, pc_lp_mod_info_t *in)
{
    mch_t c[8];
    for (int i = 0; i < 8; i++) c[i].key = -1, c[i].on_row = -1, c[i].age = 0;
    uint32_t age = 0;
    int most = 0;
    for (uint32_t b = 0; b < used_buckets(lp); b++)
        for (uint16_t i = lp->head[b]; i != PC_LP_NIL; i = lp->ev[i].next) {
            const pc_lp_ev_t *x = &lp->ev[i];
            if (!alive(lp, x) || (lp->mute >> x->track & 1u) || x->frame >= lp->len) continue;
            bool on = x->type == PC_MIDI_NOTE_ON && x->b > 0, off = x->type == PC_MIDI_NOTE_OFF || (x->type == PC_MIDI_NOTE_ON && !x->b);
            if (!on && !off) continue;
            int key = x->ch << 8 | x->a;
            uint32_t row = (uint32_t)llround(x->frame / rowlen) % rows;
            uint8_t *cell = NULL;
            if (on) {
                int k = -1;
                for (int j = 0; j < nch; j++) if (c[j].key == key) k = j;        /* the same note again: same channel */
                for (int j = 0; j < nch && k < 0; j++) if (c[j].key < 0) k = j;
                if (k < 0) {                                                       /* steal the oldest */
                    k = 0;
                    for (int j = 1; j < nch; j++) if (c[j].age < c[k].age) k = j;
                    if (pat) in->dropped++;
                }
                c[k].key = key;
                c[k].on_row = (int)row;
                c[k].age = ++age;
                int busy = 0;
                for (int j = 0; j < nch; j++) busy += c[j].key >= 0;
                if (busy > most) most = busy;
                if (!pat) continue;
                cell = pat + (size_t)row * nch * 4 + k * 4;
                if (cell[0] || cell[1]) in->dropped++;
                int note = x->a;
                if (note < 48 || note > 83) in->folded++;
                while (note < 48) note += 12;
                while (note > 83) note -= 12;
                uint16_t per = k_period[note - 48];
                uint8_t ins = x->ch == 9 || x->ch == 16 ? 2 : 1;
                cell[0] = (uint8_t)((ins & 0xF0) | per >> 8);
                cell[1] = (uint8_t)per;
                cell[2] = (uint8_t)((ins & 0x0F) << 4 | 0x0C);
                cell[3] = (uint8_t)(x->b * 64 / 127);
                in->notes++;
            } else {
                int k = -1;
                for (int j = 0; j < nch; j++) if (c[j].key == key) k = j;
                if (k < 0) continue;
                c[k].key = -1;
                if (!pat) continue;
                if ((int)row == c[k].on_row) { in->dropped++; continue; }   /* shorter than a row: sustains */
                cell = pat + (size_t)row * nch * 4 + k * 4;
                if (cell[0] || cell[1]) continue;
                cell[2] = (uint8_t)((cell[2] & 0xF0) | 0x0C);
                cell[3] = 0;
            }
        }
    return most;
}

static void be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

size_t pc_lp_mod(const pc_looper_t *lp, double bpm, int bpb, uint8_t *out, size_t cap, pc_lp_mod_info_t *info)
{
    (void)bpb;
    pc_lp_mod_info_t in;
    memset(&in, 0, sizeof in);
    if (!lp->len || !lp->beat || bpm <= 0) return 0;
    double rowlen = lp->beat / 4.0;
    uint32_t rows = (uint32_t)llround(lp->len / rowlen);
    if (rows < 1) rows = 1;
    if (rows > MOD_MAX_PATTERNS * 64) rows = MOD_MAX_PATTERNS * 64;
    uint32_t patterns = (rows + 63) / 64;
    int nch = mod_pass(lp, rows, rowlen, 8, NULL, &in) > 4 ? 8 : 4;
    size_t need = MOD_HDR + (size_t)patterns * 64 * nch * 4 + SQ_LEN + NOISE_LEN;
    if (cap < need) return 0;
    memset(out, 0, need);
    memcpy(out, "JC loop", 7);
    for (int s = 0; s < 31; s++) {
        uint8_t *h = out + 20 + s * 30;
        if (s == 0 || s == 1) {
            memcpy(h, s ? "noise" : "square", s ? 5 : 6);
            uint32_t words = (s ? NOISE_LEN : SQ_LEN) / 2;
            be16(h + 22, words);
            h[25] = 64;
            be16(h + 28, words);                     /* loops whole: held notes sustain */
        } else be16(h + 28, 1);
    }
    out[950] = (uint8_t)patterns;
    out[951] = 127;
    for (uint32_t p = 0; p < patterns; p++) out[952 + p] = (uint8_t)p;
    memcpy(out + 1080, nch == 8 ? "8CHN" : "M.K.", 4);
    uint8_t *pat = out + MOD_HDR;
    mod_pass(lp, rows, rowlen, nch, pat, &in);
    /* row 0, the last two channels: ticks per row and tempo, so that a row is a 16th */
    uint32_t speed = 6, t = 0;
    for (;;) {
        t = (uint32_t)llround(bpm * speed / 6.0);
        if (t > 255 && speed > 1) { speed--; continue; }
        if (t < 32 && speed < 31) { speed++; continue; }
        break;
    }
    for (int k = 0; k < 2; k++) {
        uint8_t *c = pat + (nch - 2 + k) * 4;
        if (c[2] & 0x0F || c[3]) in.dropped++;
        c[2] = (uint8_t)((c[2] & 0xF0) | 0x0F);
        c[3] = (uint8_t)(k ? t : speed);
    }
    if (rows % 64) {                                 /* the last pattern ends early */
        uint8_t *c = pat + (size_t)(rows - 1) * nch * 4 + (nch - 1) * 4;
        if (c[2] & 0x0F || c[3]) in.dropped++;
        c[2] = (uint8_t)((c[2] & 0xF0) | 0x0D);
        c[3] = 0;
    }
    int8_t *smp = (int8_t *)(pat + (size_t)patterns * 64 * nch * 4);
    for (uint32_t i = 0; i < SQ_LEN; i++) smp[i] = (int8_t)(i < SQ_LEN / 2 ? 112 : -112);
    uint32_t st = 0x4A434C50u;                       /* "JCLP": the same noise every export */
    for (uint32_t i = 0; i < NOISE_LEN; i++) { st = st * 1664525u + 1013904223u; smp[SQ_LEN + i] = (int8_t)(st >> 24); }
    in.channels = nch;
    in.rows = (int)rows;
    in.patterns = (int)patterns;
    if (info) *info = in;
    return need;
}
