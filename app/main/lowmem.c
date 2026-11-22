// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lowmem.h"
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "pc/pc_lowmem.h"

static pc_lowmem_t s_m;
static volatile int s_level, s_force = -1;

static void sample(void *arg)
{
    (void)arg;
    size_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), l = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    int was = s_m.level, now = pc_lowmem_update(&s_m, f, l, (uint32_t)(esp_timer_get_time() / 1000));
    if (now != was) printf("APP lowmem level=%d free=%u largest=%u\n", now, (unsigned)f, (unsigned)l);
    s_level = s_force >= 0 ? s_force : now;
}

void lowmem_start(void)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t a = { .callback = sample, .name = "lowmem" };
    if (t || esp_timer_create(&a, &t) != ESP_OK) return;
    sample(NULL);
    esp_timer_start_periodic(t, 500 * 1000);
}

int lowmem_level(void) { return s_level; }

void lowmem_force(int level)
{
    s_force = level > PC_LOWMEM_NO_BACKGND ? PC_LOWMEM_NO_BACKGND : level;
    s_level = s_force >= 0 ? s_force : s_m.level;
    printf("APP lowmem force=%d level=%d measured=%d\n", s_force, s_level, s_m.level);
}
