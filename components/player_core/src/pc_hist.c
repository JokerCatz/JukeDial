// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_hist.h"
#include <string.h>

void pc_hist_add(pc_hist_t *h, int idx)
{
    h->n -= h->back;                       /* drop what is newer than the cursor */
    h->back = 0;
    if (h->n == PC_HIST_N) {
        memmove(h->idx, h->idx + 1, (PC_HIST_N - 1) * sizeof h->idx[0]);
        h->n--;
    }
    h->idx[h->n++] = idx;
}

void pc_hist_add_oldest(pc_hist_t *h, int idx)
{
    if (h->n == PC_HIST_N) h->n--;           /* the newest falls out */
    memmove(h->idx + 1, h->idx, (size_t)h->n * sizeof h->idx[0]);
    h->idx[0] = idx;
    h->n++;
    h->back = h->n - 1;
}

int pc_hist_prev(pc_hist_t *h)
{
    if (h->n - 1 - h->back < 1) return -1;   /* the cursor is on the oldest entry, or nothing is stored */
    h->back++;
    return h->idx[h->n - 1 - h->back];
}

int pc_hist_next(pc_hist_t *h)
{
    if (h->back <= 0) return -1;
    h->back--;
    return h->idx[h->n - 1 - h->back];
}

int pc_hist_peek_next(const pc_hist_t *h) { return h->back > 0 ? h->idx[h->n - h->back] : -1; }

void pc_hist_clear(pc_hist_t *h) { memset(h, 0, sizeof *h); }

void pc_hist_remap(pc_hist_t *h, int (*map)(int idx, void *u), void *u)
{
    int cur = h->n - 1 - h->back, n = 0, at = -1;                /* the cursor's position, the new one */
    for (int i = 0; i < h->n; i++) {
        int m = map(h->idx[i], u);
        if (m >= 0) h->idx[n++] = m;
        if (i == cur) at = n - 1;                                /* kept: itself; gone: the next older kept one (-1: none) */
    }
    h->n = n;
    if (at < 0) at = 0;
    h->back = n ? n - 1 - at : 0;
}
