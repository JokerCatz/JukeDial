// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_btmeta.h"
#include <stdio.h>
#include <string.h>

/* the length of the valid UTF-8 sequence at p[0..n) (1..4), 0 when it is broken (cut, overlong, a surrogate, past U+10FFFF) */
static int seq_len(const uint8_t *p, size_t n)
{
    uint8_t c = p[0];
    int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (k == 0 || (size_t)k > n) return 0;
    uint32_t cp = k == 1 ? c : k == 2 ? (c & 0x1Fu) : k == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (int i = 1; i < k; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        cp = cp << 6 | (p[i] & 0x3Fu);
    }
    static const uint32_t min[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < min[k] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return k;
}

size_t pc_btmeta_copy(char *dst, size_t cap, const uint8_t *src, int len)
{
    if (!cap) return 0;
    size_t o = 0, n = src && len > 0 ? (size_t)len : 0;
    for (size_t i = 0; i < n && src[i]; ) {
        int k = seq_len(src + i, n - i);
        if (k == 0) { i++; continue; }                     /* a broken byte: dropped */
        if (src[i] == 0x7F || src[i] < 0x20) {             /* control: one space (none at the start) */
            if (o && o + 1 < cap) dst[o++] = ' ';
            i++;
            continue;
        }
        if (src[i] == ' ' && !o) { i++; continue; }        /* leading spaces */
        if (o + (size_t)k >= cap) break;                   /* it does not fit whole: the text ends here */
        memcpy(dst + o, src + i, (size_t)k);
        o += (size_t)k;
        i += (size_t)k;
    }
    while (o && dst[o - 1] == ' ') o--;                    /* trailing spaces */
    dst[o] = 0;
    return o;
}

static uint32_t number(const uint8_t *t, int len, uint32_t max)   /* ASCII decimal, spaces around it; anything else: 0 */
{
    char b[24];
    pc_btmeta_copy(b, sizeof b, t, len);
    if (!b[0]) return 0;
    uint64_t v = 0;
    for (const char *p = b; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + (uint64_t)(*p - '0');
        if (v > max) v = max;                              /* clamped: a long digit string never wraps */
    }
    return (uint32_t)v;
}

static bool set_text(char *f, const uint8_t *t, int len)
{
    char b[PC_BTMETA_TEXT] = { 0 };                        /* zeroed: the bytes after the text are stored too (a struct compare sees equal texts as equal) */
    pc_btmeta_copy(b, sizeof b, t, len);
    if (!strcmp(b, f)) return false;
    memcpy(f, b, sizeof b);
    return true;
}

bool pc_btmeta_put(pc_btmeta_t *m, uint8_t attr_id, const uint8_t *text, int len)
{
    switch (attr_id) {
    case PC_BTMETA_TITLE: return set_text(m->title, text, len);
    case PC_BTMETA_ARTIST: return set_text(m->artist, text, len);
    case PC_BTMETA_ALBUM: return set_text(m->album, text, len);
    case PC_BTMETA_TRACK: { uint16_t v = (uint16_t)number(text, len, 65535); bool ch = v != m->track; m->track = v; return ch; }
    case PC_BTMETA_TOTAL: { uint16_t v = (uint16_t)number(text, len, 65535); bool ch = v != m->total; m->total = v; return ch; }
    case PC_BTMETA_TIME: { uint32_t v = number(text, len, UINT32_MAX); bool ch = v != m->play_ms; m->play_ms = v; return ch; }
    case PC_BTMETA_ART: {
        char b[PC_BTMETA_HANDLE + 2] = { 0 };
        size_t n = pc_btmeta_copy(b, sizeof b, text, len);
        bool ok = n == PC_BTMETA_HANDLE && strspn(b, "0123456789") == PC_BTMETA_HANDLE;
        if (!ok) memset(b, 0, sizeof b);                   /* not a handle: no cover art */
        if (!strcmp(b, m->art)) return false;
        memcpy(m->art, b, sizeof m->art);
        m->art[PC_BTMETA_HANDLE] = 0;
        return true;
    }
    default: return false;                                 /* the genre and unknown ids are not kept */
    }
}

bool pc_btmeta_known(const pc_btmeta_t *m) { return m && m->title[0]; }

void pc_btmeta_line(const pc_btmeta_t *m, char *out, size_t cap)
{
    if (!cap) return;
    if (m->artist[0]) snprintf(out, cap, "%s  -  %s", m->title, m->artist);
    else snprintf(out, cap, "%s", m->title);
    size_t n = strlen(out), k = n;                         /* snprintf may have cut inside a character: back to its start */
    while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
    if (k > 0) {
        unsigned char lead = (unsigned char)out[k - 1];
        size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (k - 1 + want > n) out[k - 1] = 0;
    }
}
