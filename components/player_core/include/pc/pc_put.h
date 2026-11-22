// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Resumable file uploads over the console (tools/board/tf_put.py, app/main/console.c "pput"; log
 * an internal note). Every chunk carries its offset and every reply the size of the
 * file being written (<path>.new), so a chunk sent again after a lost reply is harmless and the host always knows
 * where to go on. */

#define PC_PUT_CHUNK_MAX 2048            /* raw bytes per line (base64 2732 chars: within the console rx buffer) */

typedef struct { uint32_t off, n; const char *b64; } pc_put_chunk_t;

/* "@<off> <n> <base64>": decimal, no signs or extra spaces, off < 2^32, 1 <= n <= PC_PUT_CHUNK_MAX. 0 or -EINVAL.
 * b64 points into line (not checked here: the caller decodes it and compares the length with n). */
int pc_put_parse(const char *line, pc_put_chunk_t *c);

enum { PC_PUT_WRITE = 1, PC_PUT_HAVE = 2, PC_PUT_GAP = 3 };
/* cur = bytes already in the file. off == cur -> PC_PUT_WRITE (append it); off + n <= cur -> PC_PUT_HAVE (already
 * there: acknowledge, write nothing); else PC_PUT_GAP (refuse; the host resumes from cur). n == 0 or past 4 GB: GAP. */
int pc_put_decide(uint32_t cur, uint32_t off, uint32_t n);

#ifdef __cplusplus
}
#endif
