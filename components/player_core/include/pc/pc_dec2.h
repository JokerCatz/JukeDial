// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 2:1 decimator for interleaved stereo float: Kaiser halfband FIR (101 taps, +-0.05 dB to 20 kHz, >= 90 dB from 28 kHz at 96k -> 48k, unity DC gain).
 * State carries across calls of any size. */
typedef struct pc_dec2 pc_dec2_t;
size_t pc_dec2_size(void);                       /* bytes the caller allocates (multiple of 8) */
void   pc_dec2_init(pc_dec2_t *d);               /* clears the history */
/* in_frames input frames -> returns the output frames written (about half). An odd input frame is kept for the next call.
 * in == out is allowed (the output never overtakes the input). */
int    pc_dec2_run(pc_dec2_t *d, const float *in, int in_frames, float *out);

/* x2 interpolator, the same halfband (coefficients x 2, unity passband gain; a zero-stuffed input filtered).
 * in_frames in -> 2 * in_frames out (no in-place: the output is bigger). Plan 2 consumer: fixed 96 kHz sink. */
typedef struct pc_int2 pc_int2_t;
size_t pc_int2_size(void);                       /* bytes the caller allocates (multiple of 8) */
void   pc_int2_init(pc_int2_t *s);
int    pc_int2_run(pc_int2_t *s, const float *in, int in_frames, float *out);

#define PC_DEC2_TAPS 101                          /* halfband length, see tools/gen/gen_halfband.py */

#ifdef __cplusplus
}
#endif
