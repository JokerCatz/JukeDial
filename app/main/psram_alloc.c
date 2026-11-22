// SPDX-License-Identifier: LGPL-2.1-or-later
/* Small allocations of LVGL, cJSON and tomlc99 go to PSRAM (spec 4.2: internal RAM is kept for
 * DMA, stacks and the radios). Each falls back to internal RAM if PSRAM is exhausted. */
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "pc/pc_preset.h"
#include "psram_alloc.h"

#ifndef psram_fallback_malloc   /* the simulator's shim (sim/shim/esp_heap_caps.h) defines both to its allocation-failure hook */
#define psram_fallback_malloc malloc
#define psram_fallback_calloc calloc
#endif

void *psram_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : psram_fallback_malloc(n);
}

void *psram_calloc(size_t n, size_t sz)
{
    void *p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : psram_fallback_calloc(n, sz);
}

void psram_free(void *p) { heap_caps_free(p); }

char *psram_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = psram_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void psram_alloc_hooks(void)
{
    cJSON_Hooks h = { .malloc_fn = psram_malloc, .free_fn = psram_free };
    cJSON_InitHooks(&h);
    pc_preset_set_alloc(psram_malloc, psram_free);
}

/* LVGL 9 with CONFIG_LV_USE_CUSTOM_MALLOC */
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }
/* lv_bytes: what LVGL holds right now (leak counter, console "mem"). Counted with the allocator's own sizes,
 * so it also covers the internal-RAM fallback. LVGL runs under its lock: no atomics needed. */
static int32_t s_lv_bytes;
int32_t psram_lv_bytes(void) { return s_lv_bytes; }
static size_t lv_size(void *p) { return p ? heap_caps_get_allocated_size(p) : 0; }

void *lv_malloc_core(size_t size)
{
    void *p = psram_malloc(size);
    if (p) s_lv_bytes += (int32_t)lv_size(p);
    return p;
}
void *lv_realloc_core(void *p, size_t n)
{
    size_t old = lv_size(p);
    s_lv_bytes -= (int32_t)old;
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!q && n) q = realloc(p, n);
    if (q) s_lv_bytes += (int32_t)lv_size(q);
    else if (n) s_lv_bytes += (int32_t)old;      /* failed: the old block is still there */
    return q;
}
void lv_free_core(void *p)
{
    s_lv_bytes -= (int32_t)lv_size(p);
    heap_caps_free(p);
}
void lv_mem_monitor_core(lv_mem_monitor_t *mon) { (void)mon; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
