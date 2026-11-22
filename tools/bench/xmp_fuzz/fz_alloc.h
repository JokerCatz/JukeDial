// SPDX-License-Identifier: LGPL-2.1-or-later
/* Force-included into every libxmp unit of the fuzz build (stands in for components/libxmp/port/xmp_psram.h): the heap of the device
 * in miniature. Every allocation is counted (a loader that leaks on an error path shows as a non-zero balance after the context is
 * freed), a single allocation and the live total are capped like the 8 MB-class PSRAM, and FZ_TRUNC32=1 truncates every request to
 * 32 bits (calloc: a product above 32 bits fails, as heap_caps_calloc does) so a size computation that wraps on the RISC-V target
 * becomes a too-small buffer that ASan then catches. */
#pragma once
#include <stdlib.h>
#include <string.h>
void *fz_malloc(size_t n);
void *fz_calloc(size_t n, size_t size);
void *fz_realloc(void *p, size_t n);
void fz_free(void *p);
#define malloc(n) fz_malloc(n)
#define calloc(n, size) fz_calloc(n, size)
#define realloc(p, n) fz_realloc(p, n)
#define free(p) fz_free(p)
