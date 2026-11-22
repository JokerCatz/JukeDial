// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_midits.h"

/* the sender's clock: remote ms; arrival = remote + offset + transit (+ jitter) */
static uint16_t ts13(int64_t remote_ms) { return (uint16_t)(remote_ms & 0x1FFF); }

static void test_jitter_removed(void)
{
    pc_midits_t s;
    pc_midits_reset(&s);
    const int64_t off = 5000000, delay = 8000;
    uint32_t r = 1;
    int64_t prev_due = 0;
    int ok = 0;
    for (int i = 0; i < 2000; i++) {
        int64_t remote = 1000 + i * 10;                 /* one note each 10 ms; crosses the 8192 ms wrap */
        r = r * 1103515245u + 12345u;
        int jit = (int)(r >> 16) % 6000;                /* 0..6 ms of transit jitter */
        int64_t arrival = remote * 1000 + off + 3000 + jit;
        int64_t due = pc_midits_due(&s, ts13(remote), arrival, delay);
        CHECK(due >= arrival);                          /* never in the past */
        CHECK(due <= arrival + delay);
        if (i > 50) {                                   /* after the minimum is learned: even spacing */
            int64_t gap = due - prev_due;
            if (gap >= 9800 && gap <= 10200) ok++;
        }
        prev_due = due;
    }
    CHECK(ok > 1900);
}

static void test_late_packet_plays_now(void)
{
    pc_midits_t s;
    pc_midits_reset(&s);
    int64_t base = 1000000;
    pc_midits_due(&s, ts13(100), base + 100000, 8000);   /* learn: transit 0 */
    int64_t a = base + 110000 + 20000;                   /* 20 ms late */
    CHECK_EQ_INT(pc_midits_due(&s, ts13(110), a, 8000), a);
}

static void test_clock_drift_and_restart(void)
{
    pc_midits_t s;
    pc_midits_reset(&s);
    /* the sender's clock runs 100 ppm slow: arrival - remote grows 0.1 ms each second */
    int64_t due = 0, arrival = 0;
    for (int i = 0; i < 6000; i++) {                     /* 60 s, a note each 10 ms */
        int64_t remote = i * 10;
        arrival = remote * 1000 + remote / 10 + 2000;    /* + 0.1 us per ms */
        due = pc_midits_due(&s, ts13(remote), arrival, 8000);
    }
    CHECK(due - arrival > 5000);                         /* still about the full delay, not drifted away */
    /* a new connection: the remote clock starts again elsewhere */
    int64_t a2 = arrival + 3000000;
    int64_t d2 = pc_midits_due(&s, ts13(4321), a2, 8000);
    CHECK_EQ_INT(d2, a2 + 8000);
}

int main(void)
{
    RUN(test_jitter_removed);
    RUN(test_late_packet_plays_now);
    RUN(test_clock_drift_and_restart);
    return 0;
}
