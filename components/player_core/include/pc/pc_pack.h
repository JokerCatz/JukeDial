// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
/* The TF card content pack's version stamp (_player/pack.txt, written by tools/gen/make_card_zip.py).
 *   pack=<N>        the pack revision (an integer)
 *   version=<V>     the release the pack was built with (log text only)
 *   <sha256>  <path> one line per other file in the pack (not read by the firmware)
 * PC_PACK_MIN is the lowest pack revision this firmware works well with. HOW IT IS BUMPED: in the same commit that changes
 * the card content in a way the firmware needs (a new face layer, a new required file, a changed file format), raise PC_PACK_MIN by one;
 * make_card_zip.py writes pack=PC_PACK_MIN, so the next pack built is the new revision. A change that only adds optional content does not bump it. */
#define PC_PACK_MIN 1
typedef enum { PC_PACK_MISSING, PC_PACK_OK, PC_PACK_OLD } pc_pack_state_t;
typedef struct { int rev; char version[40]; } pc_pack_t;
/* parses the stamp text; returns 0 and fills *p, or -1 when there is no "pack=<digits>" line (version stays "" then) */
int pc_pack_parse(const char *txt, size_t n, pc_pack_t *p);
/* rev < 0 = no readable stamp (card without the pack, or an old pack that had none) */
pc_pack_state_t pc_pack_state(int rev);
