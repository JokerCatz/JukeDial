// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_gapless.h"

/* frames from one source until the block is full or it stops; *rc 1 going, 0 ended, -1 error */
static int pull(pc_play_src_t *s, float *out, int frames, int *rc)
{
    int done = 0;
    *rc = 1;
    while (done < frames) {
        int n = s->be->render(s->h, out + 2 * done, frames - done);
        if (n <= 0) { *rc = n < 0 ? -1 : 0; break; }
        if (s->gain != 1.0f)
            for (int i = 2 * done; i < 2 * (done + n); i++) out[i] *= s->gain;
        done += n;
    }
    return done;
}

pc_fill_t pc_gapless_fill(pc_play_src_t *cur, pc_play_src_t *next, float *stereo, int frames)
{
    pc_fill_t r = { 0, 1, -1 };
    r.frames = pull(cur, stereo, frames, &r.cur_rc);
    if (r.cur_rc == 0 && next && next->h) {
        int rc;
        r.next_frames = pull(next, stereo + 2 * r.frames, frames - r.frames, &rc);
        r.frames += r.next_frames;
    }
    return r;
}

int32_t pc_gapless_remaining_ms(int32_t len_ms, int32_t pos_ms, uint32_t size, uint32_t read_end, uint32_t bytes_read)
{
    if (len_ms > 0) return len_ms > pos_ms ? len_ms - pos_ms : 0;
    if (!size || pos_ms < PC_GAPLESS_MIN_EST_MS || !bytes_read) return -1;
    if (read_end >= size) return 0;
    return (int32_t)((uint64_t)(size - read_end) * (uint64_t)pos_ms / bytes_read);
}
