// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* hires T12 fix 1: the maths of the DAC clock read-back (bsp_audio.c runs it in the I2S tx-done callback). The DMA walks a ring of `desc`
 * descriptors in a fixed order. The first `desc` distinct buffers seen are the ring order; after that each event's buffer gives its ring position,
 * and the number of descriptors since the last event is the position difference (a lost or merged interrupt only makes it larger than 1: it can
 * no longer undercount). Event 1 (the one that completed the ring order) is the reference: rate = (n - 1) * frames / (t1 - t0). */
#define PC_CLKMEAS_MAX_DESC 8
typedef struct {
    const void *bufs[PC_CLKMEAS_MAX_DESC];
    uint32_t t0, t1;       /* microsecond stamps of the reference event and the latest one (a difference wraps right) */
    uint32_t n;            /* descriptors completed since the reference, plus 1 (0 = no reference yet) */
    uint8_t desc, learned, last;
} pc_clkmeas_t;

void pc_clkmeas_reset(pc_clkmeas_t *m, uint8_t desc, uint32_t now);   /* before an enable; desc 0 or above PC_CLKMEAS_MAX_DESC: never measures */
void pc_clkmeas_event(pc_clkmeas_t *m, const void *buf, uint32_t t);  /* one tx-done event (interrupt) */
/* the output rate from the events after (n_from, t_from) (n_from = 1, t_from = t0 for the whole run): 0 when fewer than min_n descriptors or no time passed */
uint32_t pc_clkmeas_rate(const pc_clkmeas_t *m, uint32_t n_from, uint32_t t_from, uint32_t frames, uint32_t min_n);
/* got is the wanted rate within 1 % plus what a late interrupt can add to a window of dt_us (3 ms at each end): a wrong divider is far outside */
bool pc_clkmeas_close(uint32_t want, uint32_t got, uint32_t dt_us);

#ifdef __cplusplus
}
#endif
