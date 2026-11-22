// SPDX-License-Identifier: LGPL-2.1-or-later
/* heap_caps_* on the host: plain libc, queries report a large fixed amount (the host has no internal/PSRAM split). */
#pragma once
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
#define MALLOC_CAP_8BIT     (1u << 2)
/* test hook (sim/test.sh, "simallocfail <min> [<max>]"): while min is non-zero, a PSRAM allocation of at least min bytes (and at most max when max is non-zero) fails (NULL), like a full PSRAM */
extern size_t sim_alloc_fail_min, sim_alloc_fail_max;
static inline int sim_alloc_hit(size_t n) { return sim_alloc_fail_min && n >= sim_alloc_fail_min && (!sim_alloc_fail_max || n <= sim_alloc_fail_max); }
static inline void *heap_caps_malloc(size_t n, uint32_t caps) { if (sim_alloc_hit(n) && (caps & MALLOC_CAP_SPIRAM)) return NULL; return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps) { if (sim_alloc_hit(n * sz) && (caps & MALLOC_CAP_SPIRAM)) return NULL; return calloc(n, sz); }
/* psram_alloc.c falls back to libc when PSRAM is full; a full board has neither, so its fallbacks go through these (the only users): a calloc of at least the hook's size fails too
 * (UI unify T8: the ring's "no memory" Back button), a malloc only when the hook has a window (max set: one exact size, a small string; LVGL's own allocations are never refused this way) */
static inline void *sim_calloc_hooked(size_t n, size_t sz) { if (sim_alloc_hit(n * sz)) return NULL; return calloc(n, sz); }
static inline void *sim_malloc_hooked(size_t n) { if (sim_alloc_fail_max && sim_alloc_hit(n)) return NULL; return malloc(n); }
#define psram_fallback_calloc sim_calloc_hooked
#define psram_fallback_malloc sim_malloc_hooked
static inline void *heap_caps_realloc(void *p, size_t n, uint32_t caps) { (void)caps; return realloc(p, n); }
static inline void  heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_allocated_size(void *p) { return p ? malloc_usable_size(p) : 0; }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 8u << 20; }
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 8u << 20; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 4u << 20; }
