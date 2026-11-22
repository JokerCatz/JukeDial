// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* SHA-256 (FIPS 180-4), small and portable: the font upload checks the whole file with it (app/main/fonts_ctl.c) on the board and in the simulator alike. Host-tested. */
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t buf[64]; uint32_t used; } pc_sha256_t;
void pc_sha256_init(pc_sha256_t *s);
void pc_sha256_update(pc_sha256_t *s, const void *data, uint32_t n);
void pc_sha256_final(pc_sha256_t *s, uint8_t out[32]);
void pc_sha256_hex(const uint8_t digest[32], char hex[65]);
#ifdef __cplusplus
}
#endif
