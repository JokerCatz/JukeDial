// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_SUN_H
#define TIME_SUN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sunrise/sunset, decision 0018's decoration tier: standard NOAA-style
 * solar-position arithmetic, pure functions, no tables. Double-precision
 * libm trig -- this runs a handful of times a DAY on the display path
 * (the idle clock page caches per civil date), so src/viz/'s no-float
 * rule does not apply; the render and service paths never call it.
 *
 * Accuracy: the low-precision apparent-place series (Astronomical
 * Almanac's "low accuracy" formulas, the same ones behind NOAA's solar
 * calculator worksheets) is good to ~0.01 deg over 1950-2050, and one
 * refinement pass puts rise/set within about a minute of published
 * tables for non-polar latitudes. The unit suite asserts +/-4 min
 * against documented times (its own tolerance comment says why 4:
 * minute-rounded authorities plus this code's ~2). */

#define SUN_OK 0
#define SUN_POLAR_DAY 1    /* sun never reaches -0.833 deg: no set (midnight sun) */
#define SUN_POLAR_NIGHT (-1) /* sun never rises that civil date (polar night) */

/* Sunrise and sunset for the civil date y-m-d in the offset
 * `tz_min` in force that day, at latitude/longitude in 1e-4 degrees (north/east positive;
 * the web page's 0.01-degree location, times 100). On SUN_OK, rise_min and set_min get
 * LOCAL minutes of day (0..1439) at the standard -0.833 deg zenith
 * (refraction + solar radius). On a polar answer neither output is
 * written. */
int sun_rise_set(int year, int month, int day, int32_t tz_min,
                 int32_t lat_e4, int32_t lon_e4, int *rise_min, int *set_min);

/* True (geometric, refraction-free) solar altitude in degrees at the
 * UTC instant, for latitude/longitude in 1e-4 degrees (the stored location): the same declination and equation-of-time
 * parameters sun_rise_set uses, taken one more step through the
 * standard hour-angle altitude formula. Range [-90, 90]. */
double sun_altitude_deg(int64_t utc_s, int32_t lat_e4, int32_t lon_e4);

/* The altitude bucketed for the sky art (task #28): round(alt / 6 deg)
 * clamped to -3..+15 -- 19 levels, one per art frame. 0 is the
 * horizon band (sunrise/sunset sit at -0.833 deg, well inside it), +15
 * the near-zenith tropics-only band, and -3 the floor: everything
 * below -15 deg is equally night (past astronomical twilight's
 * neighborhood, the sky looks the same). A level lasts ~24 min at the
 * fastest (the sun climbs at most ~0.25 deg/min), so callers may
 * recompute on the minute, not per frame. */
#define SUN_LEVEL_MIN (-3)
#define SUN_LEVEL_MAX 15
int sun_level(int64_t utc_s, int32_t lat_e4, int32_t lon_e4);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SUN_H */
