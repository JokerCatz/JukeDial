// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extsend.h"
#include <string.h>

#define KEY_LEFT 0x50
#define KEY_RIGHT 0x4F
#define CONS_VOL_UP 0xE9
#define CONS_VOL_DOWN 0xEA

void pc_extsend_init(pc_extsend_t *q) { memset(q, 0, sizeof *q); }

static uint32_t room(const pc_extsend_t *q)
{
    uint32_t h = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE), t = q->tail;
    return PC_EXTSEND_CAP - 1 - (t - h);
}
int pc_extsend_count(const pc_extsend_t *q) { return (int)(__atomic_load_n(&q->tail, __ATOMIC_ACQUIRE) - __atomic_load_n(&q->head, __ATOMIC_ACQUIRE)); }

/* a staging cursor: events are written past tail and published once */
typedef struct { pc_extsend_t *q; uint32_t n, max; } stage_t;
static bool put(stage_t *s, uint8_t kind, uint8_t mod, uint16_t code, uint16_t after)
{
    if (s->n >= s->max) return false;
    pc_extsend_ev *e = &s->q->ev[(s->q->tail + s->n) % PC_EXTSEND_CAP];
    e->kind = kind; e->mod = mod; e->code = code; e->after_ms = after;
    s->n++;
    return true;
}
static bool press(stage_t *s, uint8_t kind, uint8_t mod, uint16_t code)   /* down, then the release */
{
    return put(s, kind, mod, code, PC_EXTSEND_HOLD_MS) && put(s, kind, 0, 0, PC_EXTSEND_GAP_MS);
}
static bool publish(stage_t *s, bool ok)
{
    if (!ok) { s->q->drops++; return false; }
    __atomic_store_n(&s->q->tail, s->q->tail + s->n, __ATOMIC_RELEASE);
    return true;
}

static bool type_text(stage_t *s, const char *text, int n)
{
    bool ok = true;
    for (int i = 0; ok && i < n && text[i]; i++) {
        uint8_t mod = 0;
        int u = pc_extkeys_ascii_usage(text[i], &mod);
        if (u) ok = press(s, PC_XEV_KBD, mod, (uint16_t)u);   /* every character is down then up: a doubled letter is two presses */
    }
    return ok;
}
bool pc_extsend_text(pc_extsend_t *q, const char *text)
{
    stage_t s = { q, 0, room(q) };
    bool ok = type_text(&s, text, (int)strlen(text));
    if (ok && s.n == 0) return false;
    return publish(&s, ok);
}

bool pc_extsend_key(pc_extsend_t *q, const pc_extkeys_key_t *k)
{
    stage_t s = { q, 0, room(q) };
    bool ok = true;
    switch (k->act) {
    case PC_XACT_CONSUMER:
        if (!k->code) return false;
        ok = press(&s, PC_XEV_CONS, 0, k->code);
        break;
    case PC_XACT_COMBO:
        if (!k->code && !k->mod) return false;
        ok = press(&s, PC_XEV_KBD, k->mod, k->code);
        break;
    case PC_XACT_TEXT:
        ok = type_text(&s, k->u.text, k->n);
        break;
    case PC_XACT_MACRO:
        for (int i = 0; ok && i < k->n; i++) {
            const pc_extkeys_step *st = &k->u.steps[i];
            if (st->type == PC_XSTEP_WAIT) ok = put(&s, PC_XEV_WAIT, 0, 0, st->val);
            else ok = press(&s, PC_XEV_KBD, st->mod, st->val);
        }
        break;
    default:
        return false;
    }
    if (ok && s.n == 0) return false;
    return publish(&s, ok);
}

int pc_extsend_knob(pc_extsend_t *q, int knob, int detents)
{
    if (knob == PC_KNOB_NONE || detents == 0) return 0;
    int n = detents > 0 ? detents : -detents, fwd = detents > 0;
    if (n > PC_EXTSEND_KNOB_MAX) n = PC_EXTSEND_KNOB_MAX;
    stage_t s = { q, 0, room(q) };
    int per = knob == PC_KNOB_SCROLL ? 1 : 2;                      /* events per detent: a wheel notch is one report, a key is down + up */
    for (int i = 0; i < n; i++) {
        bool ok;
        if (knob == PC_KNOB_SCROLL) ok = put(&s, PC_XEV_WHEEL, 0, (uint16_t)(int16_t)(fwd ? -1 : 1), PC_EXTSEND_GAP_MS);
        else if (knob == PC_KNOB_VOLUME) ok = press(&s, PC_XEV_CONS, 0, fwd ? CONS_VOL_UP : CONS_VOL_DOWN);
        else ok = press(&s, PC_XEV_KBD, 0, fwd ? KEY_RIGHT : KEY_LEFT);
        if (!ok) { s.n -= s.n % (uint32_t)per; break; }   /* a half pair is not published */
    }
    if (s.n == 0) { q->drops++; return 0; }
    publish(&s, true);
    return (int)(s.n / (uint32_t)per);
}

const pc_extsend_ev *pc_extsend_peek(pc_extsend_t *q, uint32_t now_ms)
{
    if (__atomic_load_n(&q->head, __ATOMIC_RELAXED) == __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE)) return NULL;
    if ((int32_t)(now_ms - q->due_ms) < 0) return NULL;
    return &q->ev[q->head % PC_EXTSEND_CAP];
}
void pc_extsend_pop(pc_extsend_t *q, uint32_t now_ms)
{
    uint32_t h = __atomic_load_n(&q->head, __ATOMIC_RELAXED);
    if (h == __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE)) return;
    q->due_ms = now_ms + q->ev[h % PC_EXTSEND_CAP].after_ms;
    __atomic_store_n(&q->head, h + 1, __ATOMIC_RELEASE);
}
static bool is_release(const pc_extsend_ev *e) { return (e->kind == PC_XEV_KBD && !e->mod && !e->code) || (e->kind == PC_XEV_CONS && !e->code); }
void pc_extsend_pump(pc_extsend_t *q, uint32_t now_ms, pc_extsend_sink_fn sink)
{
    for (int guard = 0; guard < PC_EXTSEND_CAP; guard++) {
        const pc_extsend_ev *e = pc_extsend_peek(q, now_ms);
        if (!e) { q->stalled = false; return; }
        if (!sink(e)) {
            if (!q->stalled) { q->stalled = true; q->stall_since = now_ms; }
            if (now_ms - q->stall_since < PC_EXTSEND_STALL_MS) return;      /* wait for the link */
            if (is_release(e)) return;                                      /* never drop a release: keep trying */
            for (;;) {                                                      /* a press the link would not take: skip it and what follows up to the next release */
                pc_extsend_pop(q, now_ms);
                q->due_ms = now_ms;
                const pc_extsend_ev *n = pc_extsend_peek(q, now_ms);
                if (!n || is_release(n)) break;
            }
            q->stalled = false;
            continue;
        }
        q->stalled = false;
        pc_extsend_pop(q, now_ms);
    }
}
void pc_extsend_flush_up(pc_extsend_t *q, pc_extsend_sink_fn sink)
{
    if (sink) {
        const pc_extsend_ev k = { PC_XEV_KBD, 0, 0, 0 }, c = { PC_XEV_CONS, 0, 0, 0 };
        sink(&k);
        sink(&c);
    }
    pc_extsend_flush(q);
    q->stalled = false;
}
void pc_extsend_flush(pc_extsend_t *q) { __atomic_store_n(&q->head, __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE); }
