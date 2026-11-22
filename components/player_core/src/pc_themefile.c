// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_themefile.h"
#include <ctype.h>
#include <string.h>

int pc_themefile_rgb(const char *v, uint32_t *rgb)
{
    if (!v || v[0] != '#') return -1;
    uint32_t x = 0;
    for (int i = 1; i <= 6; i++) {
        int c = (unsigned char)v[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        x = x << 4 | (uint32_t)d;
    }
    if (v[7]) return -1;
    if (rgb) *rgb = x;
    return 0;
}

int pc_themefile_parse(const char *text, size_t len, int (*cb)(void *, const pc_themefile_kv_t *), void *ctx)
{
    int bad = 0;
    size_t i = 0;
    if (!text) return 0;
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) i = 3;
    while (i < len) {
        char line[96];
        size_t n = 0, over = 0;
        while (i < len && text[i] != '\n') {
            if (n < sizeof line - 1) line[n++] = text[i]; else over = 1;
            i++;
        }
        i++;
        line[n] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p || *p == '#' || *p == ';') continue;
        if (over) { bad++; continue; }
        char *eq = strchr(p, '=');
        if (!eq) { bad++; continue; }
        char *ke = eq;
        while (ke > p && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        *ke = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *ve = v;
        while (*ve && *ve != ' ' && *ve != '\t' && *ve != '\r' && *ve != ';' && !(*ve == '#' && ve != v)) ve++;
        *ve = 0;
        for (char *k = p; *k; k++) *k = (char)tolower((unsigned char)*k);
        pc_themefile_kv_t kv = { p, v };
        if (!*p || !cb || cb(ctx, &kv)) bad++;
    }
    return bad;
}
