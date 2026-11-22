// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#include "pc/pc_paths.h"
#ifdef __cplusplus
extern "C" {
#endif

/* List uploads from the settings page (spec 2026-11-11-glyphpack-design.md section 7): pure parts, host-tested.
 * The firmware side (app/main/lists_ctl.c) writes <name>.new and renames it after the CRC and a pack check. */

#define PC_LIST_DIR       PC_LISTS_DIR "/"
#define PC_LIST_NAME_MAX  60                 /* bytes of UTF-8, with ".zip" */
#define PC_LIST_PATH_MAX  96                 /* fits PC_LIST_DIR + a name + ".new" */
#define PC_LIST_MAX_BYTES (8u << 20)         /* bigger packs: copy them in USB drive mode */
#define PC_LIST_CHUNK     2048               /* raw bytes per request (base64 ~2.7 KB: within the console rx buffer) */

/* "<name>.zip" -> PC_LISTS_DIR "/<name>.zip". Only a plain file name: valid UTF-8, no '/', '\\', "..", leading dot or
 * space, control or FAT-illegal characters (: * ? " < > |), no space before ".zip". 0 or -EINVAL. */
int pc_list_path(const char *name, char *out, uint32_t cap);

int pc_b64_decode(const char *in, uint8_t *out, uint32_t cap);   /* bytes, -EINVAL bad input, -ENOSPC */
int pc_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap);   /* chars (0-terminated), -ENOSPC */

typedef struct { uint32_t size, got, crc, last_off; uint8_t active; } pc_upload_t;
enum { PC_UPLOAD_WRITE = 1, PC_UPLOAD_DUP = 2 };
int pc_upload_begin(pc_upload_t *u, uint32_t size);                              /* -EINVAL empty, -E2BIG */
/* in order only: off == got -> PC_UPLOAD_WRITE (write it, crc updated); any chunk already written -> PC_UPLOAD_DUP
 * (answer ok, write nothing); else -EINVAL, or -E2BIG past the size */
int pc_upload_chunk(pc_upload_t *u, uint32_t off, const uint8_t *data, uint32_t n);
int pc_upload_end(pc_upload_t *u, uint32_t crc);                                  /* 0 (finished), -EINVAL short, -EBADMSG */

#ifdef __cplusplus
}
#endif
