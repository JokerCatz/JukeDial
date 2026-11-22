// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
void *psram_malloc(size_t n);
void *psram_calloc(size_t n, size_t sz);
void  psram_free(void *p);
char *psram_strdup(const char *s);   /* a copy in PSRAM (internal RAM when PSRAM is full); NULL in, NULL out; free with psram_free */
void  psram_alloc_hooks(void);   /* call once at boot, before cJSON / TOML / LVGL are used */
int32_t psram_lv_bytes(void);   /* bytes LVGL holds right now (lv_malloc/realloc/free) */
