// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_upload.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "pc/pc_zip.h"

static int utf8_ok(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = s[i], k = c < 0x80 ? 0 : c >= 0xC2 && c < 0xE0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : c >= 0xF0 && c < 0xF5 ? 3 : 9;
        if (k == 9 || i + k >= n) return 0;
        for (unsigned j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

int pc_list_path(const char *name, char *out, uint32_t cap)
{
    if (!name) return -EINVAL;
    size_t n = strlen(name);
    if (n <= 4 || n > PC_LIST_NAME_MAX || strcmp(name + n - 4, ".zip")) return -EINVAL;
    if (name[0] == '.' || name[0] == ' ' || name[n - 5] == ' ' || strstr(name, "..")) return -EINVAL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", c)) return -EINVAL;
    }
    if (!utf8_ok((const unsigned char *)name, n)) return -EINVAL;
    if ((uint32_t)snprintf(out, cap, "%s%s", PC_LIST_DIR, name) >= cap) return -EINVAL;
    return 0;
}

static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

int pc_b64_decode(const char *in, uint8_t *out, uint32_t cap)
{
    size_t n = strlen(in);
    if (n % 4) return -EINVAL;
    uint32_t k = 0;
    for (size_t i = 0; i < n; i += 4) {
        int v[4], pad = 0;
        for (int j = 0; j < 4; j++) {
            char c = in[i + j];
            if (c == '=' && i + 4 == n && j >= 2) { v[j] = 0; pad++; continue; }
            if (pad || (v[j] = b64v((unsigned char)c)) < 0) return -EINVAL;   /* nothing after a '=' */
        }
        uint32_t w = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
        int bytes = 3 - pad;
        if (k + (uint32_t)bytes > cap) return -ENOSPC;
        for (int j = 0; j < bytes; j++) out[k++] = (uint8_t)(w >> (16 - 8 * j));
    }
    return (int)k;
}

int pc_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t need = (n + 2) / 3 * 4;
    if (need + 1 > cap) return -ENOSPC;
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t w = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[k++] = T[w >> 18 & 63];
        out[k++] = T[w >> 12 & 63];
        out[k++] = i + 1 < n ? T[w >> 6 & 63] : '=';
        out[k++] = i + 2 < n ? T[w & 63] : '=';
    }
    out[k] = 0;
    return (int)k;
}

int pc_upload_begin(pc_upload_t *u, uint32_t size)
{
    memset(u, 0, sizeof *u);
    if (!size) return -EINVAL;
    if (size > PC_LIST_MAX_BYTES) return -E2BIG;
    u->size = size;
    u->active = 1;
    return 0;
}

int pc_upload_chunk(pc_upload_t *u, uint32_t off, const uint8_t *data, uint32_t n)
{
    if (!u->active || !data || !n) return -EINVAL;
    if (off != u->got && (uint64_t)off + n <= u->got) return PC_UPLOAD_DUP;   /* already written: a resend after a lost reply */
    if (off != u->got) return -EINVAL;
    if ((uint64_t)off + n > u->size) return -E2BIG;
    u->crc = pc_crc32(u->crc, data, n);
    u->last_off = off;
    u->got += n;
    return PC_UPLOAD_WRITE;
}

int pc_upload_end(pc_upload_t *u, uint32_t crc)
{
    if (!u->active || u->got != u->size) return -EINVAL;
    if (u->crc != crc) return -EBADMSG;
    u->active = 0;
    return 0;
}
