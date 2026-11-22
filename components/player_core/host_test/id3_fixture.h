// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Builds ID3v2 tags in memory for tests (no binary fixtures). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t b[1 << 16]; size_t n; int ver; } id3b_t;

static inline void id3b_begin(id3b_t *t, int ver)
{
    memset(t, 0, sizeof *t);
    t->ver = ver;
    memcpy(t->b, "ID3", 3);
    t->b[3] = (uint8_t)ver;
    t->n = 10;
}

static inline void id3b_raw(id3b_t *t, const void *p, size_t n) { memcpy(t->b + t->n, p, n); t->n += n; }

static inline void put_size(uint8_t *d, uint32_t v, int syncsafe)
{
    if (syncsafe) { d[0] = (v >> 21) & 0x7f; d[1] = (v >> 14) & 0x7f; d[2] = (v >> 7) & 0x7f; d[3] = v & 0x7f; }
    else { d[0] = v >> 24; d[1] = v >> 16; d[2] = v >> 8; d[3] = v; }
}

/* frame with a raw body */
static inline void id3b_frame(id3b_t *t, const char *id, const void *body, size_t n)
{
    uint8_t h[10] = { 0 };
    memcpy(h, id, 4);
    put_size(h + 4, (uint32_t)n, t->ver == 4);
    id3b_raw(t, h, 10);
    id3b_raw(t, body, n);
}

/* UTF-8 (BMP + surrogates) -> UTF-16LE, no terminator; returns bytes */
static inline size_t u8_to_u16le(const char *s, uint8_t *out)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0) { cp = (uint32_t)(*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = (uint32_t)(*p & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
        else { cp = (uint32_t)(*p & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F); p += 4; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            uint32_t hi = 0xD800 + (cp >> 10), lo = 0xDC00 + (cp & 0x3FF);
            out[n++] = hi & 0xff; out[n++] = hi >> 8; out[n++] = lo & 0xff; out[n++] = lo >> 8;
        } else { out[n++] = cp & 0xff; out[n++] = cp >> 8; }
    }
    return n;
}

/* text frame, encoding 1 (UTF-16 with BOM) */
static inline void id3b_text16(id3b_t *t, const char *id, const char *utf8)
{
    uint8_t body[8192];
    body[0] = 1; body[1] = 0xFF; body[2] = 0xFE;
    size_t n = 3 + u8_to_u16le(utf8, body + 3);
    id3b_frame(t, id, body, n);
}

/* text frame, encoding 0 (Latin-1, ASCII here) */
static inline void id3b_text0(id3b_t *t, const char *id, const char *ascii)
{
    uint8_t body[1024];
    body[0] = 0;
    size_t n = strlen(ascii);
    memcpy(body + 1, ascii, n);
    id3b_frame(t, id, body, n + 1);
}

/* TXXX, UTF-16: desc\0\0 value */
static inline void id3b_txxx(id3b_t *t, const char *desc, const char *val)
{
    uint8_t body[8192];
    size_t n = 0;
    body[n++] = 1; body[n++] = 0xFF; body[n++] = 0xFE;
    n += u8_to_u16le(desc, body + n);
    body[n++] = 0; body[n++] = 0;
    body[n++] = 0xFF; body[n++] = 0xFE;
    n += u8_to_u16le(val, body + n);
    id3b_frame(t, "TXXX", body, n);
}

/* APIC with a fake JPEG of len bytes starting FF D8; returns offset of the image inside the tag buffer */
static inline size_t id3b_apic(id3b_t *t, size_t len)
{
    uint8_t body[4096];
    size_t n = 0;
    body[n++] = 0;
    memcpy(body + n, "image/jpeg", 11); n += 11;
    body[n++] = 3;            /* front cover */
    body[n++] = 0;            /* empty description */
    size_t img = n;
    for (size_t i = 0; i < len; i++) body[n++] = (uint8_t)(i * 7);
    body[img] = 0xFF; body[img + 1] = 0xD8;
    size_t frame_at = t->n;
    id3b_frame(t, "APIC", body, n);
    return frame_at + 10 + img;
}

/* closes the tag (syncsafe size, plus padding) and appends fake audio */
static inline void id3b_end(id3b_t *t, size_t padding, size_t audio)
{
    memset(t->b + t->n, 0, padding);
    t->n += padding;
    put_size(t->b + 6, (uint32_t)(t->n - 10), 1);
    for (size_t i = 0; i < audio; i++) t->b[t->n++] = 0xFF;
}

static inline int id3b_write(const id3b_t *t, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(t->b, 1, t->n, f);
    return fclose(f);
}
