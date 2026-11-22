// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_TERMS_H
#define TIME_TERMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The 24 solar terms (jieqi), decision 0018's decoration tier: "more
 * arithmetic than the moon, still no tables". Each term is 15 degrees of
 * apparent solar longitude (solar_longitude.h); index 0 is Chunfen (the
 * March equinox, longitude 0), so index = floor(lambda / 15).
 *
 * Names are pinyin: the panel fonts are ASCII, and pinyin names both
 * stay readable on a 128px 1bpp panel and stay honest about what they
 * are -- transliterations, not translations.
 *
 * Accuracy: the shared series is good to ~0.01 deg, which puts a term
 * boundary within roughly a quarter hour of the published instant --
 * far inside the day-level display this feeds. The unit suite asserts
 * boundaries within 2 hours of documented equinox/solstice instants. */

#define TERMS_COUNT 24

/* Current term index (0..23) at the instant. */
int terms_index(int64_t utc_s);

/* Pinyin name for a term index ("Chunfen", "Qingming", ...). Out of
 * range answers "?", never NULL. */
const char *terms_name(int idx);

/* The UTC instant the NEXT term begins (the next 15-degree boundary
 * strictly after `utc_s`), by Newton iteration on the shared series;
 * *next_idx (when non-NULL) gets that term's index. */
int64_t terms_next_utc(int64_t utc_s, int *next_idx);

/* Whole LOCAL calendar days (zone tz_min, minutes east of UTC) from the day
 * containing now_utc to the day containing at_utc; 0 = the same local day.
 * Constant through a local day, unlike a ceiling on the seconds between. */
int terms_days_until(int64_t now_utc, int64_t at_utc, int32_t tz_min);

const char *terms_name_zh(int idx);   /* 春分 (spring equinox) … (JC player) */
#ifdef __cplusplus
}
#endif

#endif /* TIME_TERMS_H */
