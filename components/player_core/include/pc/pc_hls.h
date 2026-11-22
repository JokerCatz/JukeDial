// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* HLS (m3u8) for live radio: a playlist of short segments that keeps growing ("a folder that keeps getting files").
 * The device reads the list, downloads the segments it has not played, and repeats. */
#define PC_HLS_MAX_SEG 16
typedef struct {
    bool master;                          /* a list of variants, not of segments */
    char variant[384];                    /* master: the chosen variant, absolute */
    uint64_t first_seq;                   /* media: #EXT-X-MEDIA-SEQUENCE */
    int target_s;                         /* #EXT-X-TARGETDURATION */
    int nseg;
    char seg[PC_HLS_MAX_SEG][384];        /* absolute; the last PC_HLS_MAX_SEG of the list */
    uint64_t seg_seq[PC_HLS_MAX_SEG];
    bool ended;                           /* #EXT-X-ENDLIST */
} pc_hls_t;
bool pc_hls_is_hls(const char *body);    /* "#EXTM3U" with "#EXT-X-" tags (a plain .m3u has none) */
/* base = the playlist's own address; prefers the highest bandwidth at or under max_bps (else the lowest) */
int  pc_hls_parse(const char *body, const char *base, uint32_t max_bps, pc_hls_t *out);   /* 0 ok */
/* index of the first segment to fetch after `last` (UINT64_MAX = start: a few segments back from live;
 * also when the list's numbers went below last - the encoder restarted), -1 none */
int  pc_hls_next(const pc_hls_t *h, uint64_t last);
int  pc_url_resolve(const char *base, const char *ref, char *out, size_t cap);   /* 0 ok */
#ifdef __cplusplus
}
#endif
