// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
/* A theme file (card: /sdcard/_player/themes/<name>.txt, cfg theme=<name>): plain text, one `key = value` per line, key = a token name of
 * app/main/ui/ui_tokens.inc (or a pc_theme_t colour), value = #RRGGBB (the `base` key takes a built-in theme name). A line whose first
 * non-blank character is # or ; is a comment; a # or ; after the value starts a trailing comment. Keys are case-insensitive. A UTF-8 BOM, CR LF
 * and a missing final newline are fine. Nothing here allocates or fails hard: a bad line is counted and skipped. */
typedef struct { const char *key; const char *val; } pc_themefile_kv_t;   /* both NUL-terminated, valid during the callback only */
/* cb returns 0 when it took the line (known key, usable value), non-zero when not: the line counts as bad. Returns the number of bad lines. */
int pc_themefile_parse(const char *text, size_t len, int (*cb)(void *ctx, const pc_themefile_kv_t *kv), void *ctx);
int pc_themefile_rgb(const char *val, uint32_t *rgb);   /* "#RRGGBB" (exactly six hex digits) -> 0 and *rgb; else -1 */
