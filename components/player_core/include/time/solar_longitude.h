// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef TIME_SOLAR_LONGITUDE_H
#define TIME_SOLAR_LONGITUDE_H

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The sun's apparent ecliptic longitude -- the ONE piece of solar-
 * position arithmetic sunrise (sun.c) and the 24 solar terms (terms.c)
 * share, kept in one header so the two can never disagree about the
 * epoch or the rate. The series is the Astronomical Almanac's
 * low-accuracy apparent place (mean longitude + equation of center,
 * aberration folded into the constants), good to ~0.01 deg 1950-2050:
 *
 *   n      = days since J2000.0 (2000-01-01T12:00:00Z)
 *   L      = 280.460 + 0.9856474 n         mean longitude, deg
 *   g      = 357.528 + 0.9856003 n         mean anomaly, deg
 *   lambda = L + 1.915 sin g + 0.020 sin 2g
 *
 * The unit suite anchors these constants to documented equinox and
 * solstice instants -- authorities OUTSIDE this code -- so a broken
 * epoch here fails tests rather than shifting every date quietly. */

#define SOLAR_J2000_UTC_S INT64_C(946728000) /* 2000-01-01T12:00:00Z */
#define SOLAR_L0_DEG 280.460
#define SOLAR_L_RATE_DEG_PER_DAY 0.9856474
#define SOLAR_G0_DEG 357.528
#define SOLAR_G_RATE_DEG_PER_DAY 0.9856003

static inline double solar_days_j2000(int64_t utc_s)
{
    return (double)(utc_s - SOLAR_J2000_UTC_S) / 86400.0;
}

/* Apparent longitude in [0, 360). */
static inline double solar_lambda_deg(int64_t utc_s)
{
    double n = solar_days_j2000(utc_s);
    double g = (SOLAR_G0_DEG + SOLAR_G_RATE_DEG_PER_DAY * n) *
               (3.14159265358979323846 / 180.0);
    double lam = SOLAR_L0_DEG + SOLAR_L_RATE_DEG_PER_DAY * n +
                 1.915 * sin(g) + 0.020 * sin(2.0 * g);
    lam = fmod(lam, 360.0);
    if (lam < 0.0) {
        lam += 360.0;
    }
    return lam;
}

#ifdef __cplusplus
}
#endif

#endif /* TIME_SOLAR_LONGITUDE_H */
