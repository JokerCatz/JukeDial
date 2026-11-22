// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_MOON_H
#define TIME_MOON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Moon phase from the mean synodic month -- decision 0018's decoration
 * tier: pure arithmetic, ~twenty lines of it, accurate to about half a
 * day (the true moon leads or lags the mean by up to ~14 hours; an idle
 * screen's moon does not care). No tables of new-moon instants, no
 * floating point.
 *
 * Epoch: the standard reference new moon of 2000-01-06 18:14 UTC.
 * Synodic month: 29.530589 days = 2551442.9 s, carried as 2551443 -- the
 * tenth of a second accumulates to under a minute of drift per century. */

#define MOON_EPOCH_UTC_S INT64_C(947182440) /* 2000-01-06T18:14:00Z */
#define MOON_SYNODIC_S INT64_C(2551443)     /* 29.530589 days */

/* Seconds since the most recent mean new moon, [0, MOON_SYNODIC_S).
 * Well-defined for instants before the epoch too. */
uint32_t moon_age_s(int64_t utc_s);

/* Phase bucketed to the nearest eighth: 0=new, 1=waxing crescent,
 * 2=first quarter, 3=waxing gibbous, 4=full, 5=waning gibbous,
 * 6=last quarter, 7=waning crescent. */
uint8_t moon_phase8(int64_t utc_s);

/* Display name for a phase8 value. Out of range answers "?", never
 * NULL. */
const char *moon_phase_name(uint8_t phase8);

/* 1bpp disc: rows[y] bit x (LSB = leftmost pixel) is pixel (x, y) of a
 * MOON_DISC_PX-square image -- a 1px limb ring always, plus the lit
 * portion filled, waxing on the right / waning on the left (the
 * northern-hemisphere convention). Pure function of the instant, so the
 * picture is testable pixel-for-pixel without a display. */
#define MOON_DISC_PX 21
void moon_disc(int64_t utc_s, uint32_t rows[MOON_DISC_PX]);

/* Lit fraction of the disc, 0..100 (0 at new, 100 at full). Integer, from the same cosine table as moon_disc. */
uint8_t moon_lit_pct(int64_t utc_s);

/* Seconds until the next full or new moon, whichever comes first (always > 0: on the very instant of one, the
 * next event is the other kind). *is_full (may be NULL) = 1 for a full moon, 0 for a new moon. */
uint32_t moon_next_event_s(int64_t utc_s, int *is_full);

const char *moon_phase_name_zh(uint8_t phase8);   /* 新月 (new moon) … (JC player) */
#ifdef __cplusplus
}
#endif

#endif /* TIME_MOON_H */
