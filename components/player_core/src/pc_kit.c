// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_kit.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_kit_note(const char *s)
{
    if (!s || !*s) return -1;
    char *e;
    if (isdigit((unsigned char)*s)) {
        long v = strtol(s, &e, 10);
        return *e || v > 127 ? -1 : (int)v;
    }
    static const int8_t PC[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* a b c d e f g */
    int c = tolower((unsigned char)*s);
    if (c < 'a' || c > 'g') return -1;
    int n = PC[c - 'a'];
    s++;
    if (*s == '#') { n++; s++; } else if (*s == 'b') { n--; s++; }
    if (!*s || (*s != '-' && !isdigit((unsigned char)*s))) return -1;
    long oct = strtol(s, &e, 10);
    if (*e) return -1;
    long v = (oct + 1) * 12 + n;
    return v < 0 || v > 127 ? -1 : (int)v;
}

static void set_path(char *dst, const char *prefix, const char *name)
{
    snprintf(dst, PC_KIT_PATH, "%s%s", prefix, name);
    for (char *p = dst; *p; p++) if (*p == '\\') *p = '/';
}

static void clear(pc_kit_def_t *k)
{
    k->n = 0;
    for (int i = 0; i < 8; i++) k->pad[i] = -1;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

int pc_kit_parse_txt(const char *text, pc_kit_def_t *k)
{
    clear(k);
    int16_t given[8];
    for (int i = 0; i < 8; i++) given[i] = -1;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[160];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p += nl ? (size_t)(nl - p) + 1 : len;
        for (char *c = line; *c; c++)                       /* '#' at the start or after a blank: a comment (F#2 is a note) */
            if (*c == '#' && (c == line || c[-1] == ' ' || c[-1] == '\t')) { *c = 0; break; }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(line), *val = trim(eq + 1);
        if (!*val) continue;
        if (!strncmp(key, "pad", 3) && key[3] >= '1' && key[3] <= '8' && !key[4]) {
            int n = pc_kit_note(val);
            if (n >= 0) given[key[3] - '1'] = (int16_t)n;
            continue;
        }
        int n = pc_kit_note(key);
        if (n < 0 || k->n >= PC_KIT_REGIONS) continue;
        pc_kit_region_t *r = &k->r[k->n++];
        set_path(r->file, "", val);
        r->lokey = r->hikey = r->center = (uint8_t)n;
        r->one_shot = true;
    }
    memcpy(k->pad, given, sizeof given);
    pc_kit_default_pads(k);
    return k->n;
}

/* ---- SFZ ---- */
typedef struct { char sample[PC_KIT_PATH]; int lokey, hikey, center, one_shot; } ops_t;

static void op_apply(ops_t *o, const char *name, const char *val, char *default_path)
{
    if (!strcmp(name, "sample")) snprintf(o->sample, sizeof o->sample, "%s", val);
    else if (!strcmp(name, "default_path")) snprintf(default_path, PC_KIT_PATH, "%s", val);
    else if (!strcmp(name, "key")) { int n = pc_kit_note(val); if (n >= 0) o->lokey = o->hikey = o->center = n; }
    else if (!strcmp(name, "lokey")) { int n = pc_kit_note(val); if (n >= 0) o->lokey = n; }
    else if (!strcmp(name, "hikey")) { int n = pc_kit_note(val); if (n >= 0) o->hikey = n; }
    else if (!strcmp(name, "pitch_keycenter")) { int n = pc_kit_note(val); if (n >= 0) o->center = n; }
    else if (!strcmp(name, "loop_mode")) o->one_shot = !strcmp(val, "one_shot");
}

static bool ident(int c) { return isalnum(c) || c == '_'; }
/* a value with spaces (sample=) runs to the line end, a '<', or a blank followed by "name=" */
static bool next_is_opcode(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    if (!ident((unsigned char)*p)) return *p == '<' || *p == '\n' || *p == '\r' || !*p;
    while (ident((unsigned char)*p)) p++;
    return *p == '=';
}

static void commit_region(pc_kit_def_t *k, const ops_t *o, const char *default_path)
{
    if (!o->sample[0] || k->n >= PC_KIT_REGIONS || o->lokey > o->hikey) return;
    pc_kit_region_t *r = &k->r[k->n++];
    set_path(r->file, default_path, o->sample);
    r->lokey = (uint8_t)o->lokey;
    r->hikey = (uint8_t)o->hikey;
    r->center = (uint8_t)o->center;
    r->one_shot = o->one_shot;
}

int pc_kit_parse_sfz(const char *text, pc_kit_def_t *k)
{
    clear(k);
    size_t len = strlen(text);
    char *buf = malloc(len + 1);
    if (!buf) return -1;
    /* comments out first */
    size_t j = 0;
    for (size_t i = 0; i < len;) {
        if (text[i] == '/' && text[i + 1] == '/') { while (i < len && text[i] != '\n') i++; }
        else if (text[i] == '/' && text[i + 1] == '*') { i += 2; while (i < len && !(text[i] == '*' && text[i + 1] == '/')) i++; i += 2; buf[j++] = ' '; }
        else buf[j++] = text[i++];
    }
    buf[j] = 0;
    static const ops_t DEF = { "", 0, 127, 60, 0 };
    ops_t global = DEF, group = DEF, region = DEF, *cur = &global;
    char default_path[PC_KIT_PATH] = "";
    bool in_region = false;
    for (char *p = buf; *p;) {
        if (*p == '<') {
            char *e = strchr(p, '>');
            if (!e) break;
            *e = 0;
            const char *h = p + 1;
            if (in_region) commit_region(k, &region, default_path);
            in_region = false;
            if (!strcmp(h, "region")) { region = group; cur = &region; in_region = true; }
            else if (!strcmp(h, "group")) { group = global; cur = &group; }
            else if (!strcmp(h, "global") || !strcmp(h, "master")) { global = DEF; group = DEF; cur = &global; }
            else cur = &global;                                   /* <control> and others: default_path only */
            p = e + 1;
            continue;
        }
        if (!ident((unsigned char)*p)) { p++; continue; }
        char *name = p;
        while (ident((unsigned char)*p)) p++;
        if (*p != '=') continue;
        *p++ = 0;
        char *val = p;
        bool spaces = !strcmp(name, "sample") || !strcmp(name, "default_path");
        while (*p && *p != '\n' && *p != '\r' && *p != '<') {
            if (*p == ' ' || *p == '\t') { if (!spaces || next_is_opcode(p)) break; }
            p++;
        }
        char end = *p;
        *p = 0;
        op_apply(cur, name, trim(val), default_path);
        if (end) { *p = end; if (end != '<') p++; }
    }
    if (in_region) commit_region(k, &region, default_path);
    free(buf);
    pc_kit_default_pads(k);
    return k->n;
}

void pc_kit_default_pads(pc_kit_def_t *k)
{
    int last = -1;
    for (int p = 0; p < 8; p++) {
        if (k->pad[p] >= 0) continue;
        int best = 128;                                        /* the lowest centre above the last, not on a pad */
        for (int i = 0; i < k->n; i++) {
            int c = k->r[i].center;
            if (c <= last || c >= best || pc_kit_pad_of(k, c) >= 0) continue;
            best = c;
        }
        if (best == 128) break;
        k->pad[p] = (int16_t)best;
        last = best;
    }
}

int pc_kit_find(const pc_kit_def_t *k, int note)
{
    for (int i = k->n - 1; i >= 0; i--)
        if (note >= k->r[i].lokey && note <= k->r[i].hikey) return i;
    return -1;
}

int pc_kit_pad_of(const pc_kit_def_t *k, int note)
{
    for (int p = 0; p < 8; p++) if (k->pad[p] == note) return p;
    return -1;
}

/* ---- WAV ---- */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int pc_wav_parse(const uint8_t *b, uint32_t n, pc_wav_t *w)
{
    if (n < 12) return PC_WAV_SHORT;
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return PC_WAV_NOT_WAV;
    bool fmt = false;
    uint32_t o = 12;
    while (o + 8 <= n) {
        uint32_t sz = rd32(b + o + 4);
        if (!memcmp(b + o, "fmt ", 4)) {
            if (o + 8 + 16 > n) return PC_WAV_SHORT;
            const uint8_t *f = b + o + 8;
            uint16_t tag = rd16(f), ch = rd16(f + 2), bits = rd16(f + 14);
            uint32_t rate = rd32(f + 4);
            if (tag == 0xFFFE && sz >= 40 && o + 8 + 26 <= n) tag = rd16(f + 24);   /* extensible: the sub-format */
            if ((tag != 1 && tag != 0xFFFE) || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 96000) return PC_WAV_FORMAT;
            w->rate = rate;
            w->channels = ch;
            fmt = true;
        } else if (!memcmp(b + o, "data", 4)) {
            if (!fmt) return PC_WAV_FORMAT;
            w->data_off = o + 8;
            w->data_len = sz & ~(uint32_t)(w->channels * 2 - 1);   /* whole frames */
            return PC_WAV_OK;
        }
        if ((uint64_t)o + 8 + sz + (sz & 1) > n) return PC_WAV_SHORT;   /* no wrap round (a huge size) */
        o += 8 + sz + (sz & 1);
    }
    return PC_WAV_SHORT;
}

int pc_wav_fit(pc_wav_t *w, uint64_t file_size)
{
    uint64_t room = file_size > w->data_off ? file_size - w->data_off : 0;
    if (w->data_len > room) w->data_len = (uint32_t)room & ~(uint32_t)(w->channels * 2 - 1);
    return w->data_len ? PC_WAV_OK : PC_WAV_SHORT;
}
