// SPDX-License-Identifier: LGPL-2.1-or-later
/* One virtual clock behind lv_tick_get, esp_timer_get_time, gettimeofday and time(). Time only moves in sim_advance_ms. */
#define _GNU_SOURCE
#include <sys/time.h>
#include <time.h>
#include "lvgl.h"
#include "sim.h"

static int64_t g_now_us;        /* monotonic, virtual */
static int64_t g_wall_base_us;  /* UTC at g_now_us == 0 */

void sim_clock_init(int64_t wall_utc_s, bool realtime)
{
    g_now_us = 0;
    if (realtime) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        g_wall_base_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    } else g_wall_base_us = wall_utc_s * 1000000;
}

void sim_set_wall_us(int64_t wall_us) { g_wall_base_us = wall_us - g_now_us; }
int64_t sim_now_us(void) { return g_now_us; }
int64_t sim_wall_us(void) { return g_wall_base_us + g_now_us; }

/* Steps of at most one refresh period so animations land on the same frame every run.
 * Per step: advance the clocks, run the esp_timers that came due (in due order), then LVGL's timers.
 * ms == 0 runs one pass without moving time. */
static uint32_t g_frame_ms = LV_DEF_REFR_PERIOD;   /* `simframe <ms>`: the length of one virtual frame (the board under load: a frame of 60-100 ms, the LVGL timers run once per frame) */
void sim_set_frame_ms(uint32_t ms) { g_frame_ms = ms < 1 ? LV_DEF_REFR_PERIOD : ms; }
uint32_t sim_frame_ms(void) { return g_frame_ms; }

void sim_advance_ms(uint32_t ms)
{
    do {
        uint32_t step = ms > g_frame_ms ? g_frame_ms : ms;
        g_now_us += (int64_t)step * 1000;
        if (step) lv_tick_inc(step);
        sim_timers_run();
        lv_timer_handler();
        sim_tasks_run();   /* top level: queued task bodies (the library scan) run here, outside any timer callback */
        ms -= step;
    } while (ms);
}

/* link-time replacements (-Wl,--wrap=time,gettimeofday,settimeofday) */
time_t __wrap_time(time_t *t)
{
    time_t s = (time_t)(sim_wall_us() / 1000000);
    if (t) *t = s;
    return s;
}

int __wrap_gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        int64_t w = sim_wall_us();
        tv->tv_sec = (time_t)(w / 1000000);
        tv->tv_usec = (suseconds_t)(w % 1000000);
    }
    return 0;
}

int __wrap_settimeofday(const struct timeval *tv, const void *tz)   /* the firmware sets the clock by hand (clock_svc_set_manual) */
{
    (void)tz;
    if (tv) sim_set_wall_us((int64_t)tv->tv_sec * 1000000 + tv->tv_usec);
    return 0;
}
