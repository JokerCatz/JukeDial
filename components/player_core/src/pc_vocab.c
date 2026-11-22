// SPDX-License-Identifier: LGPL-2.1-or-later
/* 單字 vocabulary quiz logic (pc_vocab.h; spec an internal note). */
#include "pc/pc_vocab.h"
#include <errno.h>
#include <string.h>
#include "pc/pc_crc32.h"

/* ---------------- deck ---------------- */
uint32_t pc_vocab_count(const pc_gp_t *gp)
{
    pc_gp_node_t root;
    if (!gp || pc_gp_kind(gp) != PC_GP_KIND_VOCAB || pc_gp_node(gp, 0, &root)) return 0;
    return root.nchild;
}

int pc_vocab_entry(const pc_gp_t *gp, uint32_t card, pc_vocab_entry_t *e)
{
    pc_gp_node_t root, c, m, x;
    if (card >= pc_vocab_count(gp) || pc_gp_node(gp, 0, &root) || pc_gp_node(gp, root.first_child + card, &c)) return -ERANGE;
    memset(e, 0, sizeof *e);
    e->word = c.text;
    e->word_len = c.len;
    e->cls = c.flags;
    e->band = c.weight;
    if (c.nchild < 1 || pc_gp_node(gp, c.first_child, &m)) return -EINVAL;
    e->meaning = m.text;
    e->meaning_len = m.len;
    if (c.nchild >= 2 && !pc_gp_node(gp, c.first_child + 1, &x) && x.len) { e->extra = x.text; e->extra_len = x.len; }
    return 0;
}

static uint32_t fnv(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 16777619u; }
    return h;
}

uint32_t pc_vocab_hash(const pc_gp_t *gp)
{
    uint32_t h = 2166136261u, nn = gp ? pc_gp_node_count(gp) : 0;
    h = fnv(h, nn);
    for (uint32_t i = 0; i < nn; i++) {
        pc_gp_node_t n;
        if (pc_gp_node(gp, i, &n)) continue;
        h = fnv(h, (uint32_t)n.len | (uint32_t)n.weight << 16 | (uint32_t)n.flags << 24);
        h = fnv(h, n.nchild);
        for (uint32_t k = 0; k < n.len; k++) h = fnv(h, n.text[k]);
    }
    return h ? h : 1;
}

uint32_t pc_vocab_skipbits(const pc_gp_t *gp, uint8_t *bits, uint32_t nbytes)
{
    uint32_t n = pc_vocab_count(gp), ok = 0;
    if (nbytes < pc_vocab_bitbytes(n)) return 0;
    memset(bits, 0, pc_vocab_bitbytes(n));
    for (uint32_t i = 0; i < n; i++) {
        pc_vocab_entry_t e;
        if (pc_vocab_entry(gp, i, &e) || !e.band || !e.word_len || !e.meaning_len) bits[i >> 3] |= (uint8_t)(1u << (i & 7));
        else ok++;
    }
    return ok;
}

/* ---------------- distractors ---------------- */
static void answer_text(const pc_vocab_entry_t *e, int field, const uint16_t **t, uint16_t *n)
{
    *t = field ? e->meaning : e->word;
    *n = field ? e->meaning_len : e->word_len;
}

static bool same(const uint16_t *a, uint16_t na, const uint16_t *b, uint16_t nb) { return na == nb && !memcmp(a, b, 2u * na); }

#define DS_SAMPLES 256
#define DS_MAX 8

int pc_vocab_distractors(pc_rng_t *r, const pc_gp_t *gp, const uint8_t *skip, uint32_t card, int field,
                         uint32_t *out, int want)
{
    uint32_t n = pc_vocab_count(gp);
    pc_vocab_entry_t c;
    if (want <= 0 || card >= n || pc_vocab_entry(gp, card, &c)) return 0;
    if (want > DS_MAX) want = DS_MAX;
    bool parts = c.cls >= PC_VC_PREFIX;
    uint32_t best_i[DS_MAX], best_k[DS_MAX];
    int nb = 0;
    bool all = n <= DS_SAMPLES;
    uint32_t tries = all ? n : DS_SAMPLES;
    for (uint32_t t = 0; t < tries; t++) {
        uint32_t i = all ? t : pc_rng_below(r, n);
        pc_vocab_entry_t e;
        if (i == card || pc_vocab_bit(skip, i) || pc_vocab_entry(gp, i, &e) || !e.band) continue;
        const uint16_t *et;
        uint16_t en;
        answer_text(&e, field, &et, &en);
        /* the same word or the same meaning would be a second right answer */
        if (!en || same(e.word, e.word_len, c.word, c.word_len) || same(e.meaning, e.meaning_len, c.meaning, c.meaning_len)) continue;
        int bd = (int)e.band - (int)c.band;
        if (bd < 0) bd = -bd;
        if (bd > 4) bd = 4;
        uint32_t score = (e.cls == c.cls ? 0u : (e.cls >= PC_VC_PREFIX) == parts ? 4u : 8u) + (uint32_t)bd;
        uint32_t key = score << 20 | (pc_rng_next(r) & 0xFFFFF);   /* random among equals */
        /* already in (sampled twice) or the same answer text as one in: keep the better key */
        int dup = -1;
        for (int k = 0; k < nb && dup < 0; k++) {
            if (best_i[k] == i) { dup = k; break; }
            pc_vocab_entry_t o;
            const uint16_t *ot;
            uint16_t on;
            pc_vocab_entry(gp, best_i[k], &o);
            answer_text(&o, field, &ot, &on);
            if (same(et, en, ot, on)) dup = k;
        }
        if (dup >= 0) {
            if (best_i[dup] == i || key >= best_k[dup]) continue;
            for (int k = dup; k + 1 < nb; k++) { best_i[k] = best_i[k + 1]; best_k[k] = best_k[k + 1]; }
            nb--;
        }
        if (nb == want && key >= best_k[nb - 1]) continue;
        int pos = nb < want ? nb : want - 1;
        while (pos > 0 && best_k[pos - 1] > key) { best_i[pos] = best_i[pos - 1]; best_k[pos] = best_k[pos - 1]; pos--; }
        best_i[pos] = i;
        best_k[pos] = key;
        if (nb < want) nb++;
    }
    for (int k = 0; k < nb; k++) out[k] = best_i[k];
    return nb;
}

/* ---------------- progress file ---------------- */
static const uint16_t IVL[PC_VOCAB_BOX_MAX + 1] = { 0, 0, 1, 2, 4, 8, 16, 32, 64 };

uint16_t pc_vocab_interval(int box)
{
    if (box < 0) return 0;
    return IVL[box > PC_VOCAB_BOX_MAX ? PC_VOCAB_BOX_MAX : box];
}

uint16_t pc_vocab_day(int64_t t)
{
    int64_t d = t / 86400 - 10957;          /* 1970-01-01 .. 2000-01-01 */
    if (t < 0) d = -1;
    if (d < 1) d = 1;
    if (d > 65535) d = 65535;
    return (uint16_t)d;
}

void pc_vocab_reset(pc_vocab_prog_t *p, uint32_t hash, uint32_t ncards)
{
    pc_vocab_card_t *c = p->cards;
    memset(p, 0, sizeof *p);
    p->cards = c;
    p->deck_hash = hash;
    p->ncards = ncards;
    if (c && ncards) memset(c, 0, sizeof *c * ncards);
}

size_t pc_vocab_file_bytes(uint32_t ncards) { return PC_VOCAB_FILE_HDR + 4u * (size_t)ncards; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static uint32_t file_crc(const uint8_t *b, size_t len)
{
    uint32_t c = pc_crc32(0, b, 44);
    return pc_crc32(c, b + PC_VOCAB_FILE_HDR, (uint32_t)(len - PC_VOCAB_FILE_HDR));
}

static int load(pc_vocab_prog_t *p, const uint8_t *b, size_t len, uint32_t hash, uint32_t ncards)
{
    if (!b || len < PC_VOCAB_FILE_HDR || memcmp(b, "JVPR", 4)) return -EINVAL;
    if (rd16(b + 4) != PC_VOCAB_FILE_VER) return -EPROTONOSUPPORT;
    uint32_t n = rd32(b + 12);
    if (rd16(b + 6) != PC_VOCAB_FILE_HDR || n > PC_VOCAB_MAX_CARDS || len != pc_vocab_file_bytes(n)) return -EINVAL;
    if (file_crc(b, len) != rd32(b + 44)) return -EINVAL;
    if (rd32(b + 8) != hash || n != ncards) return -ESTALE;
    for (uint32_t i = 0; i < n; i++) if (b[PC_VOCAB_FILE_HDR + 4 * i] > PC_VOCAB_BOX_MAX) return -EINVAL;
    p->deck_hash = hash;
    p->ncards = n;
    const uint8_t *h = b + 16;
    p->last_day = rd16(h); p->streak = rd16(h + 2); p->streak_day = rd16(h + 4); p->new_day = rd16(h + 6);
    p->new_count = rd16(h + 8); p->stat_day = rd16(h + 10); p->stat_n = rd16(h + 12); p->stat_ok = rd16(h + 14);
    p->reviews = rd32(b + 32);
    p->correct = rd32(b + 36);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *q = b + PC_VOCAB_FILE_HDR + 4 * i;
        p->cards[i] = (pc_vocab_card_t){ q[0], q[1], rd16(q + 2) };
    }
    return 0;
}

int pc_vocab_load(pc_vocab_prog_t *p, const uint8_t *buf, size_t len, uint32_t hash, uint32_t ncards)
{
    int rc = load(p, buf, len, hash, ncards);
    if (rc) pc_vocab_reset(p, hash, ncards);
    return rc;
}

size_t pc_vocab_save(const pc_vocab_prog_t *p, uint8_t *b, size_t cap)
{
    size_t len = pc_vocab_file_bytes(p->ncards);
    if (cap < len) return 0;
    memset(b, 0, PC_VOCAB_FILE_HDR);
    memcpy(b, "JVPR", 4);
    wr16(b + 4, PC_VOCAB_FILE_VER);
    wr16(b + 6, PC_VOCAB_FILE_HDR);
    wr32(b + 8, p->deck_hash);
    wr32(b + 12, p->ncards);
    uint8_t *h = b + 16;
    wr16(h, p->last_day); wr16(h + 2, p->streak); wr16(h + 4, p->streak_day); wr16(h + 6, p->new_day);
    wr16(h + 8, p->new_count); wr16(h + 10, p->stat_day); wr16(h + 12, p->stat_n); wr16(h + 14, p->stat_ok);
    wr32(b + 32, p->reviews);
    wr32(b + 36, p->correct);
    for (uint32_t i = 0; i < p->ncards; i++) {
        uint8_t *q = b + PC_VOCAB_FILE_HDR + 4 * i;
        q[0] = p->cards[i].box;
        q[1] = p->cards[i].lapses;
        wr16(q + 2, p->cards[i].due);
    }
    wr32(b + 44, file_crc(b, len));
    return len;
}

/* ---------------- scheduler ---------------- */
void pc_vocab_session_init(pc_vocab_session_t *s, uint16_t new_limit)
{
    memset(s, 0, sizeof *s);
    s->new_limit = new_limit;
}

static bool recent(const pc_vocab_session_t *s, uint32_t c)
{
    for (int i = 0; i < s->nrecent; i++) if (s->recent[i] == c) return true;
    return false;
}

static int relearn_at(const pc_vocab_session_t *s, uint32_t c)
{
    for (int i = 0; i < s->nrelearn; i++) if (s->relearn[i] == c) return i;
    return -1;
}

/* pass 0: not recent, pass 1: anything (a tiny deck) */
static int32_t pick(const pc_vocab_session_t *s, const pc_vocab_prog_t *p, const uint8_t *skip, uint16_t today,
                    bool new_ok, pc_rng_t *r, int *why, int pass)
{
    /* most overdue due card, lowest box first; ties random */
    int32_t best = -1;
    uint32_t ties = 0, bkey = 0;
    for (uint32_t i = 0; i < p->ncards; i++) {
        const pc_vocab_card_t *c = &p->cards[i];
        if (!c->box || c->due > today || pc_vocab_bit(skip, i) || relearn_at(s, i) >= 0 || (!pass && recent(s, i))) continue;
        uint32_t key = (uint32_t)c->due << 8 | c->box;
        if (best < 0 || key < bkey) { best = (int32_t)i; bkey = key; ties = 1; }
        else if (key == bkey && pc_rng_below(r, ++ties) == 0) best = (int32_t)i;
    }
    if (best >= 0) { *why = PC_VN_DUE; return best; }
    bool seen = false;
    for (uint32_t i = 0; i < p->ncards && !seen; i++) seen = p->cards[i].box && !pc_vocab_bit(skip, i);
    if (new_ok || !seen)
        for (uint32_t i = 0; i < p->ncards; i++)
            if (!p->cards[i].box && !pc_vocab_bit(skip, i) && (pass || !recent(s, i))) { *why = PC_VN_NEW; return (int32_t)i; }
    /* practice: a random known card */
    ties = 0;
    for (uint32_t i = 0; i < p->ncards; i++) {
        if (!p->cards[i].box || pc_vocab_bit(skip, i) || (!pass && (recent(s, i) || relearn_at(s, i) >= 0))) continue;
        if (pc_rng_below(r, ++ties) == 0) best = (int32_t)i;
    }
    if (best >= 0) { *why = PC_VN_PRACTICE; return best; }
    return -1;
}

int32_t pc_vocab_next(pc_vocab_session_t *s, const pc_vocab_prog_t *p, const uint8_t *skip, uint16_t today,
                      bool clock_ok, pc_rng_t *r, int *why)
{
    *why = PC_VN_NONE;
    for (int i = 0; i < s->nrelearn; i++)
        if (!s->wait[i] && s->relearn[i] < p->ncards && !pc_vocab_bit(skip, s->relearn[i])) { *why = PC_VN_RELEARN; return (int32_t)s->relearn[i]; }
    uint16_t taught = clock_ok ? (p->new_day == today ? p->new_count : 0) : s->new_session;
    bool new_ok = taught < s->new_limit;
    int32_t c = pick(s, p, skip, today, new_ok, r, why, 0);
    if (c < 0) c = pick(s, p, skip, today, new_ok, r, why, 1);
    if (c < 0) {
        /* only missed cards still waiting their gap: the one closest to its turn */
        int b = -1;
        for (int i = 0; i < s->nrelearn; i++)
            if (s->relearn[i] < p->ncards && !pc_vocab_bit(skip, s->relearn[i]) && (b < 0 || s->wait[i] < s->wait[b])) b = i;
        if (b >= 0) { *why = PC_VN_RELEARN; return (int32_t)s->relearn[b]; }
        *why = PC_VN_NONE;
    }
    return c;
}

void pc_vocab_answer(pc_vocab_session_t *s, pc_vocab_prog_t *p, uint32_t card, int why, bool right, uint16_t today,
                     bool clock_ok)
{
    if (card >= p->ncards) return;
    pc_vocab_card_t *c = &p->cards[card];
    bool was_new = c->box == 0;
    /* session: recent ring, relearn gaps */
    if (!recent(s, card)) {
        if (s->nrecent < PC_VOCAB_RECENT) s->nrecent++;
        memmove(s->recent + 1, s->recent, sizeof s->recent[0] * (s->nrecent - 1));
    } else {
        int k = 0;
        while (s->recent[k] != card) k++;
        memmove(s->recent + 1, s->recent, sizeof s->recent[0] * (size_t)k);
    }
    s->recent[0] = card;
    for (int i = 0; i < s->nrelearn; i++) if (s->wait[i]) s->wait[i]--;
    int at = relearn_at(s, card);
    if (right && at >= 0) {
        memmove(s->relearn + at, s->relearn + at + 1, sizeof s->relearn[0] * (size_t)(s->nrelearn - at - 1));
        memmove(s->wait + at, s->wait + at + 1, (size_t)(s->nrelearn - at - 1));
        s->nrelearn--;
    } else if (!right) {
        if (at < 0 && s->nrelearn < PC_VOCAB_RELEARN) { at = s->nrelearn++; s->relearn[at] = card; }
        if (at >= 0) s->wait[at] = PC_VOCAB_RELEARN_GAP;
    }
    s->answered++;
    if (right) { s->right++; s->run++; if (s->run > s->best_run) s->best_run = s->run; }
    else s->run = 0;
    if (was_new) s->new_session++;
    /* progress */
    p->reviews++;
    if (right) p->correct++;
    if (p->stat_day != today) { p->stat_day = today; p->stat_n = p->stat_ok = 0; }
    if (p->stat_n < 65535) p->stat_n++;
    if (right && p->stat_ok < 65535) p->stat_ok++;
    if (clock_ok) {
        if (today > p->last_day) p->last_day = today;
        if (p->streak_day != today) {
            p->streak = p->streak_day && p->streak_day + 1 == today && p->streak < 65535 ? p->streak + 1 : 1;
            p->streak_day = today;
        }
        if (was_new) {
            if (p->new_day != today) { p->new_day = today; p->new_count = 0; }
            if (p->new_count < 65535) p->new_count++;
        }
    }
    if (right) {
        if (why == PC_VN_PRACTICE && !was_new) return;
        c->box = was_new ? 2 : c->box < PC_VOCAB_BOX_MAX ? c->box + 1 : PC_VOCAB_BOX_MAX;
        uint32_t d = (uint32_t)today + pc_vocab_interval(c->box);
        c->due = (uint16_t)(d > 65535 ? 65535 : d);
    } else {
        if (!was_new && c->lapses < 255) c->lapses++;
        c->box = 1;
        c->due = today;
    }
}

void pc_vocab_stats(const pc_vocab_prog_t *p, const uint8_t *skip, uint16_t today, pc_vocab_stats_t *st)
{
    memset(st, 0, sizeof *st);
    /* an unloaded deck (cards NULL, ncards possibly stale) counts as empty */
    for (uint32_t i = 0; p->cards && i < p->ncards; i++) {
        if (pc_vocab_bit(skip, i)) continue;
        const pc_vocab_card_t *c = &p->cards[i];
        st->total++;
        if (!c->box) { st->fresh++; continue; }
        st->seen++;
        if (c->box >= PC_VOCAB_LEARNED_BOX) st->learned++;
        if (c->due <= today) st->due++;
    }
    st->streak = p->streak_day && (p->streak_day == today || p->streak_day + 1 == today) ? p->streak : 0;
    if (p->stat_day == today) { st->today_n = p->stat_n; st->today_ok = p->stat_ok; }
}
