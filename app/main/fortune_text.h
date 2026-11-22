// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
/* Built-in fortune texts (our own writing, const = flash). zh-TW here in app/main so the font subset
 * (tools/gen/make_font_subset.py scans app/main) carries every glyph. */
typedef struct { int8_t rank; const char *zh, *en; } fortune_verse_t;   /* rank: pc_kuji_t, -1 any */
extern const fortune_verse_t fortune_verses[];
extern const int fortune_nverses;
extern const char *const fortune_kuji_zh[6], *const fortune_kuji_en[6];
extern const char *const fortune_aspect_zh[4], *const fortune_aspect_en[4];
typedef struct { const char *zh, *en, *up_zh, *up_en, *rev_zh, *rev_en; } fortune_tarot_t;
extern const fortune_tarot_t fortune_tarot[22];
