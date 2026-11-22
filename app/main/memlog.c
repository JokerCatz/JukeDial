// SPDX-License-Identifier: LGPL-2.1-or-later
#include "memlog.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "psram_alloc.h"
#if !SIM
#include "esp_attr.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sysmode.h"
#include "bsp/bsp_display.h"
#endif

void memlog(const char *tag)
{
    printf("MEM %s internal_free=%u internal_min=%u internal_largest=%u psram_free=%u lv_bytes=%ld\n", tag,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (long)psram_lv_bytes());
}

#if SIM   /* memboot / memdump are board-only (heap caps, task stacks, RTC flag): no-ops in the simulator */
void memlog_boot_take(void) {}
void memlog_step(const char *tag) { (void)tag; }
void memlog_boot_request(void) {}
void memlog_dump(void) {}
void memlog_boot_end(void) {}
#else
#define MEMB_MAGIC 0x4D454D42u
static RTC_NOINIT_ATTR uint32_t s_flag;
static EXT_RAM_BSS_ATTR bool s_armed;          /* PSRAM: 0 new internal statics */
static EXT_RAM_BSS_ATTR unsigned s_prev_free;

void memlog_boot_take(void)
{
    s_armed = s_flag == MEMB_MAGIC;
    s_flag = 0;
    s_prev_free = 0;
}

void memlog_step(const char *tag)
{
    if (!s_armed) return;
    unsigned f = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("MEM step %-14s internal_free=%u delta=%d largest=%u min=%u psram_free=%u\n", tag, f,
           s_prev_free ? (int)f - (int)s_prev_free : 0,
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_prev_free = f;
}

void memlog_boot_request(void)
{
    s_flag = MEMB_MAGIC;
    printf("MEM memboot armed, rebooting\n");
    sysmode_reboot();
}

static int cmp_stack(const void *a, const void *b)
{
    const TaskStatus_t *x = a, *y = b;
    return (int)heap_caps_get_allocated_size(y->pxStackBase) - (int)heap_caps_get_allocated_size(x->pxStackBase);
}

void memlog_dump(void)
{
    memlog("dump");
    printf("MEM caps internal total=%u dma_free=%u 8bit_free=%u iram_free=%u\n",
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT));
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *ts = malloc(n * sizeof *ts);
    if (ts) {
        n = uxTaskGetSystemState(ts, n, NULL);
        qsort(ts, n, sizeof *ts, cmp_stack);
        unsigned in_int = 0, in_ps = 0;
        for (UBaseType_t i = 0; i < n; i++) {
            bool in = esp_ptr_internal(ts[i].pxStackBase);
            unsigned sz = (unsigned)heap_caps_get_allocated_size(ts[i].pxStackBase);
            if (in) in_int += sz; else in_ps += sz;
            printf("MEM task %-16s stack=%u where=%s hwm_free=%u prio=%u\n", ts[i].pcTaskName, sz, in ? "INTERNAL" : "psram",
                   (unsigned)ts[i].usStackHighWaterMark * (unsigned)sizeof(StackType_t), (unsigned)ts[i].uxCurrentPriority);
        }
        printf("MEM task sum tasks=%u stacks_internal=%u stacks_psram=%u\n", (unsigned)n, in_int, in_ps);
        free(ts);
    }
    /* the display path's DMA bounce buffers: the delta when released (same call as `mem settle`) */
    unsigned b = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    bsp_display_release_dma();
    unsigned a = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("MEM display_dma_released=%d bytes (internal_free %u -> %u)\n", (int)a - (int)b, b, a);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
}

void memlog_boot_end(void)
{
    if (!s_armed) return;
    memlog_step("boot_end");
    memlog_dump();
    s_armed = false;
}
#endif
