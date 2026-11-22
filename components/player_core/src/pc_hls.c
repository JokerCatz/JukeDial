// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_hls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool pc_hls_is_hls(const char *b)
{
    while (*b == ' ' || *b == '\r' || *b == '\n' || (unsigned char)*b == 0xEF || (unsigned char)*b == 0xBB || (unsigned char)*b == 0xBF) b++;
    return !strncmp(b, "#EXTM3U", 7) && strstr(b, "#EXT-X-") != NULL;
}

static size_t host_end(const char *u)            /* index just after "scheme://host[:port]" */
{
    const char *p = strstr(u, "://");
    p = p ? p + 3 : u;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    return (size_t)(p - u);
}

int pc_url_resolve(const char *base, const char *ref, char *out, size_t cap)
{
    int n;
    if (!strncasecmp(ref, "http://", 7) || !strncasecmp(ref, "https://", 8)) n = snprintf(out, cap, "%s", ref);
    else if (!strncmp(ref, "//", 2)) {
        const char *c = strstr(base, "://");
        n = snprintf(out, cap, "%.*s:%s", c ? (int)(c - base) : 4, c ? base : "http", ref);
    } else if (ref[0] == '/') n = snprintf(out, cap, "%.*s%s", (int)host_end(base), base, ref);
    else {                                            /* relative to the playlist's directory */
        size_t he = host_end(base), q = he + strcspn(base + he, "?#"), dir = he;
        for (size_t i = he; i < q; i++) if (base[i] == '/') dir = i + 1;
        char path[512], norm[512];
        snprintf(path, sizeof path, "%s%.*s%s", dir > he ? "" : "/", (int)(dir - he), base + he, ref);
        size_t o = 0, qp = strcspn(path, "?#");
        for (size_t i = 0; i < qp;) {                     /* drop "/." and fold "dir/.." */
            if (!strncmp(path + i, "/./", 3)) { i += 2; continue; }
            if (!strncmp(path + i, "/../", 4)) { while (o > 0 && norm[--o] != '/') {} i += 3; continue; }
            if (o < sizeof norm - 1) norm[o++] = path[i];
            i++;
        }
        norm[o] = 0;
        n = snprintf(out, cap, "%.*s%s%s", (int)he, base, norm, path + qp);
    }
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

int pc_hls_parse(const char *body, const char *base, uint32_t max_bps, pc_hls_t *h)
{
    memset(h, 0, sizeof *h);
    if (!pc_hls_is_hls(body)) return -1;
    long bw = -1, best_bw = -1, low_bw = -1;
    char best[384] = "", low[384] = "";
    uint64_t idx = 0;
    for (const char *p = body; *p;) {
        const char *e = strpbrk(p, "\r\n");
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[512];
        snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), p);
        p += len;
        while (*p == '\r' || *p == '\n') p++;
        if (!line[0]) continue;
        if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
            h->master = true;
            const char *b = strstr(line, "BANDWIDTH=");
            bw = b ? strtol(b + 10, NULL, 10) : 0;
        } else if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22)) h->first_seq = strtoull(line + 22, NULL, 10);
        else if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22)) h->target_s = atoi(line + 22);
        else if (!strncmp(line, "#EXT-X-ENDLIST", 14)) h->ended = true;
        else if (line[0] != '#') {
            char abs[384];
            if (pc_url_resolve(base, line, abs, sizeof abs)) continue;
            if (bw >= 0) {                                    /* a variant */
                if (bw <= (long)max_bps && bw > best_bw) { best_bw = bw; snprintf(best, sizeof best, "%s", abs); }
                if (low_bw < 0 || bw < low_bw) { low_bw = bw; snprintf(low, sizeof low, "%s", abs); }
                bw = -1;
            } else {                                          /* a segment: keep the newest PC_HLS_MAX_SEG */
                if (h->nseg == PC_HLS_MAX_SEG) {
                    memmove(h->seg[0], h->seg[1], sizeof h->seg[0] * (PC_HLS_MAX_SEG - 1));
                    memmove(&h->seg_seq[0], &h->seg_seq[1], sizeof h->seg_seq[0] * (PC_HLS_MAX_SEG - 1));
                    h->nseg--;
                }
                snprintf(h->seg[h->nseg], sizeof h->seg[0], "%s", abs);
                h->seg_seq[h->nseg++] = h->first_seq + idx;
                idx++;
            }
        }
    }
    if (h->master) {
        snprintf(h->variant, sizeof h->variant, "%s", best[0] ? best : low);
        return h->variant[0] ? 0 : -1;
    }
    if (h->target_s <= 0) h->target_s = 6;
    return h->nseg ? 0 : -1;
}

int pc_hls_next(const pc_hls_t *h, uint64_t last)
{
    if (!h->nseg) return -1;
    if (last == UINT64_MAX || h->seg_seq[h->nseg - 1] < last)       /* first time, or the numbering restarted */
        return h->nseg > 3 ? h->nseg - 3 : 0;                         /* a little behind live: room to buffer */
    for (int i = 0; i < h->nseg; i++) if (h->seg_seq[i] > last) return i;
    return -1;
}
