// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* LRC lyrics (visualiser kind "歌詞"). Parsed once per track from <song basename>.lrc next to the audio file.
 * Format read: UTF-8 only (a UTF-8 BOM is skipped; a GBK / Big5 file is NOT converted: it is reported as PC_LRC_E_ENCODING and the page says so),
 * lines "[mm:ss.xx]text" with any number of time tags in front of one text ([mm:ss], [mm:ss.x], [mm:ss.xx], [mm:ss.xxx], [mm:ss:xx]; mm 1-3 digits),
 * "[offset:+-ms]" (positive = the lyrics show that much earlier, as in most players), every other tag ([ti:], [ar:], [al:], [by:], ...) is ignored, and
 * enhanced-LRC word times "<mm:ss.xx>" inside a text are removed. CR LF, LF and CR end a line. A time tag with no text is kept (an empty line: a pause).
 * Lines without a time tag are ignored. Output lines are sorted by time (stable).
 * Limits (the struct is the whole memory, ~28 KB, the caller puts it in PSRAM): file 64 KB read (the rest ignored), PC_LRC_MAX_LINES lines, PC_LRC_TEXT_BYTES of text
 * in all, PC_LRC_LINE_BYTES per line (cut at a character boundary). Anything beyond a limit is dropped and `truncated` says so. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_LRC_MAX_FILE 65536
#define PC_LRC_MAX_LINES 400
#define PC_LRC_TEXT_BYTES 24576
#define PC_LRC_LINE_BYTES 160
enum { PC_LRC_OK = 0, PC_LRC_E_EMPTY = -1, PC_LRC_E_ENCODING = -2, PC_LRC_E_ARG = -3 };   /* empty: no timed line at all */

typedef struct { int32_t ms; uint16_t off; } pc_lrc_line_t;     /* off: the text's place in `text` */
typedef struct {
    uint16_t n;                                  /* lines */
    uint16_t text_len;                           /* bytes of text used */
    bool truncated;                              /* a limit dropped something */
    pc_lrc_line_t line[PC_LRC_MAX_LINES];
    char text[PC_LRC_TEXT_BYTES];
} pc_lrc_t;

#ifdef __cplusplus
extern "C" {
#endif
/* Parses src[0..n) into *l (cleared first); returns PC_LRC_OK or an error (and l->n = 0). More than PC_LRC_MAX_FILE bytes: only the first are read. */
int pc_lrc_parse(pc_lrc_t *l, const char *src, size_t n);
/* The index of the line to show at pos_ms: the last line whose time <= pos_ms, -1 before the first line (or no lines). */
int pc_lrc_find(const pc_lrc_t *l, int32_t pos_ms);
const char *pc_lrc_text(const pc_lrc_t *l, int i);   /* "" for an index out of range */
/* "dir/song.mp3" -> "dir/song.lrc" (the last extension replaced; no extension: ".lrc" appended); false when out does not fit or audio is empty */
bool pc_lrc_path(const char *audio, char *out, size_t cap);
#ifdef __cplusplus
}
#endif
