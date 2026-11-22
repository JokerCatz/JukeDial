// SPDX-License-Identifier: LGPL-2.1-or-later
/* The sound bank's text format (pc_sfx.h; the firmware's bank and the format's full description: app/main/sfx_bank.def). Parsed once, at boot:
 * every line is checked, an entry with anything the voices cannot do is dropped whole (the log callback says why) and the rest still loads. */
#include "pc/pc_sfx.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const GROUPS[PC_SFX_GROUPS] = { "ui", "toy", "game", "more" };
const char *pc_sfx_group_key(int g) { return g >= 0 && g < PC_SFX_GROUPS ? GROUPS[g] : NULL; }

int pc_sfx_count(const pc_sfx_bank_t *b) { return b ? b->n : 0; }

int pc_sfx_id(const pc_sfx_bank_t *b, const char *name)
{
    if (!b || !name) return -1;
    for (int i = 0; i < b->n; i++) if (!strcmp(b->e[i].name, name)) return i;
    return -1;
}

const pc_sfx_entry_t *pc_sfx_entry(const pc_sfx_bank_t *b, int id) { return b && id >= 0 && id < b->n ? &b->e[id] : NULL; }
const char *pc_sfx_name(const pc_sfx_bank_t *b, int id) { const pc_sfx_entry_t *e = pc_sfx_entry(b, id); return e ? e->name : NULL; }
uint32_t pc_sfx_len_ms(const pc_sfx_bank_t *b, int id) { const pc_sfx_entry_t *e = pc_sfx_entry(b, id); return e ? (uint32_t)ceilf(e->len_ms) : 0; }

/* the AY's 16 volume steps (logarithmic, about 3 dB a step at the top; MAME's measured table normalised), shared with the synth */
extern const float pc_sfx_ay_vol[16];
static float vol_amp(uint8_t vol, uint8_t ay) { return ay ? pc_sfx_ay_vol[vol & 15] : (float)(vol & 15) / 15.0f; }

typedef struct {
    pc_sfx_bank_t *b;
    void (*log)(const char *, void *);
    void *u;
    int line;
    bool open, bad;                  /* an entry is being read; something in it was refused */
    char why[96];
    int ev_start;                    /* b->nev when the entry began (a dropped entry gives its notes back) */
    float tempo;
    pc_sfx_entry_t cur;
} parser_t;

static void say(parser_t *p, const char *name, const char *why)
{
    if (!p->log) return;
    char m[160];
    snprintf(m, sizeof m, "line %d %s: %s", p->line, name && name[0] ? name : "-", why);
    p->log(m, p->u);
}

static void refuse(parser_t *p, const char *why)
{
    if (p->bad) return;              /* the first reason is the one reported */
    p->bad = true;
    snprintf(p->why, sizeof p->why, "%s", why);
}

static char *tok(char **s)                       /* the next blank-separated word of *s (cut in place), NULL at the end */
{
    char *p = *s;
    if (!p) return NULL;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) { *s = p; return NULL; }
    char *w = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    *s = p;
    return w;
}

static bool num(const char *s, float *out)       /* a whole token as a number */
{
    if (!s || !*s) return false;
    char *e;
    float v = strtof(s, &e);
    if (*e || !isfinite(v)) return false;
    *out = v;
    return true;
}

static void finish(parser_t *p)
{
    if (!p->open) return;
    p->open = false;
    pc_sfx_bank_t *b = p->b;
    pc_sfx_entry_t *e = &p->cur;
    if (!p->bad && !e->ntr) refuse(p, "no tracks");
    if (!p->bad && pc_sfx_id(b, e->name) >= 0) refuse(p, "the name is there twice");
    if (!p->bad && b->n >= PC_SFX_ENTRIES) refuse(p, "the bank is full");
    float len = 0, sum = 0;
    for (int k = 0; !p->bad && k < e->ntr; k++) {
        const pc_sfx_track_t *t = &e->tr[k];
        float loud = 0;
        for (int i = 0; i < t->nev; i++) {
            const pc_sfx_ev_t *v = &b->ev[t->ev0 + i];
            if (v->t_off > len) len = v->t_off;
            float a = vol_amp(v->vol, t->ay);
            if (a > loud) loud = a;
        }
        sum += t->level * loud;
    }
    if (!p->bad && len > PC_SFX_MAX_MS) refuse(p, "too long");
    if (!p->bad && len <= 0) refuse(p, "silent");
    if (p->bad) {
        b->nev = p->ev_start;
        b->dropped++;
        say(p, e->name, p->why);
        return;
    }
    if (sum > 1.0f) for (int k = 0; k < e->ntr; k++) e->tr[k].level /= sum;   /* the peak rule: the loudest levels sum to at most 1 */
    e->len_ms = len;
    b->e[b->n++] = *e;
}

static void track_defaults(pc_sfx_track_t *t)
{
    memset(t, 0, sizeof *t);
    t->duty = 8;
    t->sus = 1.0f;
    t->level = 1.0f;
}

static bool add_event(parser_t *p, pc_sfx_track_t *t, float hz, float t_on, float t_off, int vol)
{
    pc_sfx_bank_t *b = p->b;
    if (b->nev >= PC_SFX_EVENTS) { refuse(p, "the bank's notes are full"); return false; }
    if (!t->nev) t->ev0 = (uint16_t)b->nev;
    pc_sfx_ev_t *v = &b->ev[b->nev++];
    v->hz = hz;
    v->t_on = t_on;
    v->t_off = t_off;
    v->vol = (uint8_t)vol;
    t->nev++;
    return true;
}

static bool hz_ok(const pc_sfx_track_t *t, float hz) { return hz >= 20.0f && hz <= (t->wave == PC_SFX_NOISE ? 32000.0f : 12000.0f); }

/* MML: t<bpm> o<octave> l<len>[.] v<0..15> q<1..8> < > r[len][.] and notes a-g with +, # or - and an optional length and dot (o4 a = 440 Hz) */
static void mml(parser_t *p, pc_sfx_track_t *t, const char *s)
{
    float tempo = p->tempo, deflen = 4, gate = 8;
    int oct = 4, vol = 15;
    bool defdot = false;
    float at = 0;
    static const int SEMI[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* a b c d e f g */
    while (*s && !p->bad) {
        char c = (char)tolower((unsigned char)*s++);
        if (c == ' ' || c == '\t') continue;
        if (c == '<') { oct--; continue; }
        if (c == '>') { oct++; continue; }
        int n = -1, semi = 0;
        bool have = false, dot = false, rest = c == 'r', note = c >= 'a' && c <= 'g';
        if (note) {
            semi = SEMI[c - 'a'];
            while (*s == '+' || *s == '#' || *s == '-') semi += *s++ == '-' ? -1 : 1;
        }
        if (isdigit((unsigned char)*s)) { n = (int)strtol(s, (char **)&s, 10); have = true; }
        if (*s == '.') { dot = true; s++; }
        if (!note && !rest) {
            if (c == 't') { if (!have || n < 30 || n > 600) refuse(p, "mml tempo"); tempo = (float)n; }
            else if (c == 'o') { if (!have || n < 0 || n > 8) refuse(p, "mml octave"); oct = n; }
            else if (c == 'v') { if (!have || n > 15) refuse(p, "mml volume"); vol = n; }
            else if (c == 'q') { if (!have || n < 1 || n > 8) refuse(p, "mml gate"); gate = (float)n; }
            else if (c == 'l') { if (!have || n < 1 || n > 64) refuse(p, "mml length"); deflen = (float)n; defdot = dot; }
            else refuse(p, "mml: unknown token");
            continue;
        }
        if (have && (n < 1 || n > 64)) { refuse(p, "mml length"); break; }
        float ln = have ? (float)n : deflen;
        bool dd = have || dot ? dot : defdot;
        float ms = 60000.0f / tempo * 4.0f / ln * (dd ? 1.5f : 1.0f);
        if (!rest) {
            int midi = 12 * (oct + 1) + semi;
            float hz = 440.0f * exp2f((float)(midi - 69) / 12.0f);
            if (!hz_ok(t, hz)) { refuse(p, "mml: note out of range"); break; }
            if (!add_event(p, t, hz, at, at + ms * gate / 8.0f, vol)) break;
        }
        at += ms;
        if (at > PC_SFX_MAX_MS) { refuse(p, "too long"); break; }
    }
    if (!p->bad && !t->nev) refuse(p, "mml: no notes");
}

static int duty16(float frac)                    /* the nearest NES duty (12.5, 25, 50, 75 %) in 1/16 */
{
    static const int D[4] = { 2, 4, 8, 12 };
    int best = 2;
    for (int i = 0; i < 4; i++) if (fabsf((float)D[i] / 16.0f - frac) < fabsf((float)best / 16.0f - frac)) best = D[i];
    return best;
}

static int wave_index(const pc_sfx_bank_t *b, const char *name)
{
    for (int i = 0; i < b->nwave; i++) if (!strcmp(b->wname[i], name)) return i;
    return -1;
}

/* ZzFX (v1.4 parameter order): volume, randomness, frequency, attack, sustain, release, shape, shapeCurve, slide, deltaSlide, pitchJump,
 * pitchJumpTime, repeatTime, noise, modulation, bitCrush, delay, sustainVolume, decay, tremolo, filter. Randomness is left out (the bank is
 * deterministic, as ZzFX's own ZZFXSound cache); slide / deltaSlide are 500 Hz/s and 500 Hz/s^2 a unit at any sample rate. */
static void zzfx(parser_t *p, pc_sfx_track_t *t, const char *list)
{
    float v[21] = { 1, .05f, 220, 0, 0, .1f, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
    int i = 0;
    const char *s = list;
    while (i < 21) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char tok[24];
        if (n >= sizeof tok) { refuse(p, "zzfx: a value is too long"); return; }
        memcpy(tok, s, n);
        tok[n] = 0;
        if (n && !num(tok, &v[i])) { refuse(p, "zzfx: not a number"); return; }
        i++;
        if (!e) break;
        s = e + 1;
        if (i == 21) { refuse(p, "zzfx: more than 21 values"); return; }
    }
    static const struct { int i; const char *why; } NO[] = { { 13, "zzfx noise (phase noise) is not a voice" }, { 14, "zzfx modulation (FM) is not a voice" },
        { 15, "zzfx bitcrush is not a voice" }, { 16, "zzfx delay (echo) is not a voice" }, { 19, "zzfx tremolo is not a voice" }, { 20, "zzfx filter is not a voice" } };
    for (unsigned k = 0; k < sizeof NO / sizeof NO[0]; k++) if (v[NO[k].i] != 0.0f) { refuse(p, NO[k].why); return; }
    /* every value in a range the voices can hold (as the native keys); `num` already refused NaN and Inf */
    static const struct { int i; float lo, hi; const char *why; } RNG[] = {
        { 0, 0, 4, "zzfx: volume 0..4" }, { 2, -32000, 32000, "zzfx: frequency out of range" }, { 3, 0, 4, "zzfx: attack 0..4 s" },
        { 4, 0, 4, "zzfx: sustain 0..4 s" }, { 5, 0, 4, "zzfx: release 0..4 s" }, { 6, 0, 5, "zzfx: unknown shape" }, { 7, 0, 10, "zzfx: shape curve 0..10" },
        { 8, -2000, 2000, "zzfx: slide -2000..2000" }, { 9, -20000, 20000, "zzfx: delta slide -20000..20000" },
        { 10, -20000, 20000, "zzfx: pitch jump -20000..20000 Hz" }, { 11, 0, 4, "zzfx: pitch jump time 0..4 s" }, { 12, 0, 4, "zzfx: repeat time 0..4 s" },
        { 17, 0, 1, "zzfx: sustain volume 0..1" }, { 18, 0, 4, "zzfx: decay 0..4 s" } };
    for (unsigned k = 0; k < sizeof RNG / sizeof RNG[0]; k++) if (!(v[RNG[k].i] >= RNG[k].lo && v[RNG[k].i] <= RNG[k].hi)) { refuse(p, RNG[k].why); return; }
    if (v[12] > 0 && v[12] < 0.001f) { refuse(p, "zzfx: repeat time under 1 ms"); return; }
    int shape = (int)v[6];
    float curve = v[7];
    if (v[6] != (float)shape || shape < 0 || shape > 5) { refuse(p, "zzfx: unknown shape"); return; }
    if (shape == 3) { refuse(p, "zzfx tan shape is not a voice"); return; }
    if (shape == 5) { t->wave = PC_SFX_PULSE; t->duty = (uint8_t)duty16(curve / 2.0f); }
    else if (shape == 4) t->wave = PC_SFX_NOISE;
    else if (curve < 0.6f) { t->wave = PC_SFX_PULSE; t->duty = 8; }   /* a shape curve this low is nearly a square: remade as the 50 % pulse */
    else if (shape == 1) t->wave = PC_SFX_TRI;                       /* other curves: the plain shape (an approximation) */
    else {
        int w = wave_index(p->b, shape == 0 ? "sine" : "saw");
        if (w < 0) { refuse(p, shape == 0 ? "zzfx sine: no wave table sine" : "zzfx saw: no wave table saw"); return; }
        t->wave = PC_SFX_WAVE;
        t->sub = (uint8_t)w;
    }
    float hz = fabsf(v[2]);
    if (shape == 4) hz = hz * 8.0f > 32000.0f ? 32000.0f : hz * 8.0f;   /* ZzFX's noise shape is close to white: the LFSR clocked 8 x the frequency */
    if (!hz_ok(t, hz)) { refuse(p, "zzfx: frequency out of range"); return; }
    t->level = v[0];
    t->att = v[3] > 0 ? v[3] * 1000.0f : 9000.0f / 44100.0f;        /* ZzFX's minimum attack: 9 samples */
    t->dec = v[18] * 1000.0f;
    t->sus = v[17];
    t->rel = v[5] * 1000.0f;
    t->lin = 500.0f * v[8];
    t->acc = 500.0f * v[9];
    t->jump_hz = v[10];
    t->jump_ms = v[11] * 1000.0f;
    t->rep_ms = v[12] * 1000.0f;
    float len = t->att + t->dec + v[4] * 1000.0f + t->rel;
    if (len > PC_SFX_MAX_MS) { refuse(p, "zzfx: too long"); return; }
    add_event(p, t, hz, 0, len, 15);
}

typedef struct { const char *key; float lo, hi; } range_t;

static void track_line(parser_t *p, char *line)
{
    pc_sfx_entry_t *e = &p->cur;
    if (e->ntr >= PC_SFX_TRACKS) { refuse(p, "more than 4 tracks"); return; }
    pc_sfx_track_t *t = &e->tr[e->ntr];
    track_defaults(t);
    char *bar = strchr(line, '|');
    if (bar) *bar++ = 0;
    char *save = line, *w = tok(&save);
    if (!w) { refuse(p, "an empty track"); return; }
    if (!strcmp(w, "zzfx")) {
        char *list = tok(&save);
        if (!list || tok(&save) || bar) { refuse(p, "zzfx takes one comma list"); return; }
        zzfx(p, t, list);
        if (!p->bad) e->ntr++;
        return;
    }
    if (!strcmp(w, "pulse")) t->wave = PC_SFX_PULSE;
    else if (!strcmp(w, "tri")) t->wave = PC_SFX_TRI;
    else if (!strcmp(w, "noise")) t->wave = PC_SFX_NOISE;
    else if (!strcmp(w, "wave")) t->wave = PC_SFX_WAVE;
    else if (!strcmp(w, "ay")) { t->wave = PC_SFX_PULSE; t->ay = 1; }
    else if (!strcmp(w, "aynoise")) { t->wave = PC_SFX_NOISE; t->sub = PC_SFX_NZ_AY; t->ay = 1; }
    else if (!strcmp(w, "aymix")) { t->wave = PC_SFX_AYMIX; t->ay = 1; t->nz_hz = 8000.0f; }
    else { refuse(p, "unknown wave (pulse tri noise wave ay aynoise aymix zzfx)"); return; }
    float f = 0, to = 0, len = 0, del = 0;
    int vol = 15;
    bool have_f = false, have_len = false, have_tab = t->wave != PC_SFX_WAVE;
    static const range_t R[] = {
        { "lvl", 0, 1 }, { "att", 0, 4000 }, { "dec", 0, 4000 }, { "sus", 0, 1 }, { "rel", 0, 4000 }, { "oct", -100, 100 }, { "lin", -1e6f, 1e6f },
        { "acc", -1e7f, 1e7f }, { "jump", -20000, 20000 }, { "jt", 0, 4000 }, { "rep", 0, 4000 }, { "arpms", 1, 1000 }, { "vib", 0, 0.5f },
        { "vibhz", 0, 50 }, { "vibms", 0, 4000 }, { "env", 0, 15 }, { "envhz", 0.1f, 4000 }, { "vol", 0, 15 }, { "f", 0, 1e6f }, { "to", 0, 1e6f },
        { "len", 1, 1e6f }, { "del", 0, 1e6f }, { "nhz", 20, 32000 }, { "duty", 0, 100 },
    };
    for (char *kv; !p->bad && (kv = tok(&save));) {
        char *eq = strchr(kv, '=');
        if (!eq) { refuse(p, "a key without a value"); return; }
        *eq++ = 0;
        if (!strcmp(kv, "mode")) {
            if (t->wave != PC_SFX_NOISE || t->ay) { refuse(p, "mode is for noise"); return; }
            if (!strcmp(eq, "long")) t->sub = PC_SFX_NZ_LONG;
            else if (!strcmp(eq, "short")) t->sub = PC_SFX_NZ_SHORT;
            else { refuse(p, "noise mode: long or short"); return; }
            continue;
        }
        if (!strcmp(kv, "tab")) {
            int i = wave_index(p->b, eq);
            if (t->wave != PC_SFX_WAVE || i < 0) { refuse(p, "no such wave table"); return; }
            t->sub = (uint8_t)i;
            have_tab = true;
            continue;
        }
        if (!strcmp(kv, "arp")) {
            int n = 0, commas = 0;
            for (const char *q = eq; *q; q++) commas += *q == ',';
            if (commas > 3) { refuse(p, "arp: at most 4 steps"); return; }
            for (char *q = eq; *q && n < 4;) {
                char *end;
                long s = strtol(q, &end, 10);
                if (end == q || s < -24 || s > 24) { refuse(p, "arp: semitones -24..24"); return; }
                t->arp[n++] = (int8_t)s;
                q = *end == ',' ? end + 1 : end;
                if (*end && *end != ',') { refuse(p, "arp: a comma list"); return; }
            }
            if (!n) { refuse(p, "arp: no steps"); return; }
            t->arp_n = (uint8_t)n;
            if (t->arp_ms <= 0) t->arp_ms = 50;
            continue;
        }
        unsigned k = 0;
        while (k < sizeof R / sizeof R[0] && strcmp(R[k].key, kv)) k++;
        float x;
        if (k == sizeof R / sizeof R[0]) { refuse(p, "unknown key"); return; }
        if (!num(eq, &x)) { refuse(p, "not a number"); return; }
        if (x < R[k].lo || x > R[k].hi) { refuse(p, "a value out of range"); return; }
        const char *key = R[k].key;
        if (!strcmp(key, "lvl")) t->level = x;
        else if (!strcmp(key, "att")) t->att = x;
        else if (!strcmp(key, "dec")) t->dec = x;
        else if (!strcmp(key, "sus")) t->sus = x;
        else if (!strcmp(key, "rel")) t->rel = x;
        else if (!strcmp(key, "oct")) t->oct = x;
        else if (!strcmp(key, "lin")) t->lin = x;
        else if (!strcmp(key, "acc")) t->acc = x;
        else if (!strcmp(key, "jump")) t->jump_hz = x;
        else if (!strcmp(key, "jt")) t->jump_ms = x;
        else if (!strcmp(key, "rep")) t->rep_ms = x;
        else if (!strcmp(key, "arpms")) t->arp_ms = x;
        else if (!strcmp(key, "vib")) t->vib = x;
        else if (!strcmp(key, "vibhz")) t->vib_hz = x;
        else if (!strcmp(key, "vibms")) t->vib_ms = x;
        else if (!strcmp(key, "env")) { if (x != floorf(x)) { refuse(p, "env: a shape 0..15"); return; } t->env = (uint8_t)(x + 1); if (t->env_hz <= 0) t->env_hz = 2; }
        else if (!strcmp(key, "envhz")) t->env_hz = x;
        else if (!strcmp(key, "vol")) { if (x != floorf(x)) { refuse(p, "vol: 0..15"); return; } vol = (int)x; }
        else if (!strcmp(key, "f")) { f = x; have_f = true; }
        else if (!strcmp(key, "to")) to = x;
        else if (!strcmp(key, "len")) { len = x; have_len = true; }
        else if (!strcmp(key, "del")) del = x;
        else if (!strcmp(key, "nhz")) { if (t->wave != PC_SFX_AYMIX) { refuse(p, "nhz is for aymix"); return; } t->nz_hz = x; }
        else if (!strcmp(key, "duty")) {
            if (t->wave != PC_SFX_PULSE || t->ay) { refuse(p, "duty is for pulse"); return; }
            if (x == 12 || x == 12.5f) t->duty = 2;
            else if (x == 25) t->duty = 4;
            else if (x == 50) t->duty = 8;
            else if (x == 75) t->duty = 12;
            else { refuse(p, "duty: 12, 25, 50 or 75"); return; }
        }
    }
    if (p->bad) return;
    if (!have_tab) { refuse(p, "wave needs tab=<table>"); return; }
    if (bar) {
        if (have_f || have_len || to > 0 || del > 0) { refuse(p, "f / to / len / del are for one-note tracks (no |)"); return; }
        mml(p, t, bar);
    } else {
        if (!have_len) { refuse(p, "a one-note track needs len="); return; }
        if (!have_f) { refuse(p, "a one-note track needs f="); return; }
        if (del + len > PC_SFX_MAX_MS) { refuse(p, "too long"); return; }
        if (!hz_ok(t, f) || (to > 0 && !hz_ok(t, to))) { refuse(p, "frequency out of range"); return; }
        if (to > 0) t->oct = log2f(to / f) / (len * 0.001f);   /* an exponential slide that reaches `to` at the note's end */
        add_event(p, t, f, del, del + len, vol);
    }
    if (!p->bad) e->ntr++;
}

static void entry_line(parser_t *p, char *rest)
{
    finish(p);
    p->open = true;
    p->bad = false;
    p->ev_start = p->b->nev;
    p->tempo = 120;
    memset(&p->cur, 0, sizeof p->cur);
    char *save = rest;
    char *nm = tok(&save), *gr = tok(&save), *zh = tok(&save),
         *en = tok(&save), *cr = tok(&save);
    if (!nm || strlen(nm) >= PC_SFX_NAME) { refuse(p, "a name of 1..15 characters"); return; }
    for (const char *c = nm; *c; c++) if (!(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '_')) { refuse(p, "a name is a-z 0-9 _"); }
    snprintf(p->cur.name, sizeof p->cur.name, "%s", nm);
    if (p->bad) return;
    if (!gr || !zh || !en || !cr) { refuse(p, "@entry <name> <group> <zh> <en> <credit>"); return; }
    int g = 0;
    while (g < PC_SFX_GROUPS && strcmp(GROUPS[g], gr)) g++;
    if (g == PC_SFX_GROUPS) { refuse(p, "unknown group (ui toy game more)"); return; }
    p->cur.group = (uint8_t)g;
    if (strlen(zh) >= PC_SFX_LABEL || strlen(en) >= PC_SFX_LABEL) { refuse(p, "a label is too long"); return; }
    snprintf(p->cur.zh, sizeof p->cur.zh, "%s", zh);
    snprintf(p->cur.en, sizeof p->cur.en, "%s", en);
    for (char *c = p->cur.en; *c; c++) if (*c == '_') *c = ' ';
    int c = 0;
    while (c < p->b->ncredit && strcmp(p->b->ckey[c], cr)) c++;
    if (c == p->b->ncredit) { refuse(p, "unknown credit (a @credit line first)"); return; }
    p->cur.credit = (uint8_t)c;
    for (char *kv; (kv = tok(&save));) {
        float x;
        if (!strncmp(kv, "tempo=", 6) && num(kv + 6, &x) && x >= 30 && x <= 600) p->tempo = x;
        else { refuse(p, "@entry takes only tempo=<30..600> after the credit"); return; }
    }
}

static void credit_line(parser_t *p, char *rest)
{
    pc_sfx_bank_t *b = p->b;
    while (*rest == ' ' || *rest == '\t') rest++;
    char *sp = strpbrk(rest, " \t");
    if (!sp || sp == rest || (size_t)(sp - rest) >= PC_SFX_NAME) { say(p, "@credit", "@credit <key> <text>"); return; }
    *sp++ = 0;
    while (*sp == ' ' || *sp == '\t') sp++;
    if (b->ncredit >= PC_SFX_CREDITS) { say(p, rest, "too many credits"); return; }
    snprintf(b->ckey[b->ncredit], PC_SFX_NAME, "%s", rest);
    snprintf(b->credit[b->ncredit], PC_SFX_CREDIT, "%s", sp);
    b->ncredit++;
}

static void wave_line(parser_t *p, char *rest)
{
    pc_sfx_bank_t *b = p->b;
    char *save = rest, *nm = tok(&save), *hex = tok(&save);
    if (!nm || !hex || strlen(nm) >= PC_SFX_NAME || strlen(hex) != 32) { say(p, "@wave", "@wave <name> <32 hex digits>"); return; }
    if (b->nwave >= PC_SFX_WAVES) { say(p, nm, "too many wave tables"); return; }
    for (int i = 0; i < 32; i++) {
        char c = (char)toupper((unsigned char)hex[i]);
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) { say(p, nm, "a wave table is 32 hex digits"); return; }
        b->wave[b->nwave][i] = ((float)v - 7.5f) / 7.5f;
    }
    snprintf(b->wname[b->nwave], PC_SFX_NAME, "%s", nm);
    b->nwave++;
}

int pc_sfx_bank_parse(pc_sfx_bank_t *b, const char *text, void (*log)(const char *msg, void *u), void *u)
{
    memset(b, 0, sizeof *b);
    parser_t p = { .b = b, .log = log, .u = u };
    char line[512];
    for (const char *s = text ? text : ""; *s;) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        p.line++;
        if (n >= sizeof line) { say(&p, p.open ? p.cur.name : "-", "a line is too long"); if (p.open) refuse(&p, "a line is too long"); s += n + (nl ? 1 : 0); continue; }
        memcpy(line, s, n);
        line[n] = 0;
        s += n + (nl ? 1 : 0);
        char *l = line;
        while (*l == ' ' || *l == '\t') l++;
        size_t ln = strlen(l);
        while (ln && (l[ln - 1] == ' ' || l[ln - 1] == '\t' || l[ln - 1] == '\r')) l[--ln] = 0;
        if (!*l || *l == '#') continue;
        if (!strncmp(l, "@entry ", 7)) entry_line(&p, l + 7);
        else if (!strncmp(l, "@credit ", 8)) { finish(&p); credit_line(&p, l + 8); }
        else if (!strncmp(l, "@wave ", 6)) { finish(&p); wave_line(&p, l + 6); }
        else if (!p.open) say(&p, "-", "a track line outside an entry");
        else if (!p.bad) track_line(&p, l);
    }
    finish(&p);
    return b->n;
}
