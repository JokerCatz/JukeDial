// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/sun.h"

#include <math.h>

#include "time/clock.h"
#include "time/solar_longitude.h"

/* M_PI is POSIX, not C11 -- spelled out so -Wpedantic builds don't
 * depend on the libc's feature-test mood. */
#define SUN_PI 3.14159265358979323846
#define SUN_DEG2RAD (SUN_PI / 180.0)

/* Solar declination (radians) and the equation of time (minutes) at the
 * given instant, from the shared apparent-longitude series
 * (solar_longitude.h -- one epoch, one rate, used by both this file and
 * terms.c so they cannot drift apart).
 *
 * The declination follows from the longitude through the obliquity; the
 * equation of time is the gap between mean and apparent solar time,
 * 4 min per degree of (mean longitude - right ascension). */
static void sun_params(int64_t utc_s, double *decl_rad, double *eot_min)
{
    double n = solar_days_j2000(utc_s);
    double lambda = solar_lambda_deg(utc_s) * SUN_DEG2RAD;
    /* Mean longitude, NOT reduced mod 360 in step with lambda -- the
     * L - alpha difference below is re-wrapped into (-180, 180] anyway. */
    double lmean = SOLAR_L0_DEG + SOLAR_L_RATE_DEG_PER_DAY * n;
    double eps = (23.439 - 0.0000004 * n) * SUN_DEG2RAD;

    *decl_rad = asin(sin(eps) * sin(lambda));

    double alpha = atan2(cos(eps) * sin(lambda), cos(lambda)) / SUN_DEG2RAD;
    double dgap = fmod(lmean - alpha, 360.0);
    if (dgap > 180.0) {
        dgap -= 360.0;
    } else if (dgap < -180.0) {
        dgap += 360.0;
    }
    *eot_min = 4.0 * dgap;
}

/* Solar transit (local apparent noon) at longitude `lon_deg` nearest the
 * given civil date, as UTC seconds: 12:00 UTC minus the longitude's 4
 * min/deg, minus the equation of time -- evaluated twice so the EoT is
 * taken at roughly the transit itself rather than at Greenwich noon. */
static double transit_utc_s(int64_t day_utc0, double lon_deg)
{
    double decl, eot;
    double t = (double)day_utc0 + (720.0 - 4.0 * lon_deg) * 60.0;
    sun_params((int64_t)t, &decl, &eot);
    return (double)day_utc0 + (720.0 - 4.0 * lon_deg - eot) * 60.0;
}

double sun_altitude_deg(int64_t utc_s, int32_t lat_e4, int32_t lon_e4)
{
    double lat = (double)lat_e4 / 10000.0 * SUN_DEG2RAD;
    double lon_deg = (double)lon_e4 / 10000.0;
    double decl, eot;
    sun_params(utc_s, &decl, &eot);

    /* Apparent solar time at this longitude, as minutes of day: UTC
     * minutes, plus 4 min per degree east, plus the equation of time.
     * Its distance from solar noon (720) is the hour angle at 1/4 deg
     * per minute; cos() needs no wrapping. */
    int64_t sod = utc_s % 86400;
    if (sod < 0) {
        sod += 86400;
    }
    double solar_min = (double)sod / 60.0 + 4.0 * lon_deg + eot;
    double h = (solar_min - 720.0) / 4.0 * SUN_DEG2RAD;

    /* The standard altitude formula off declination and hour angle. */
    double s = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(h);
    if (s > 1.0) {
        s = 1.0; /* rounding guard at the zenith */
    } else if (s < -1.0) {
        s = -1.0;
    }
    return asin(s) / SUN_DEG2RAD;
}

int sun_level(int64_t utc_s, int32_t lat_e4, int32_t lon_e4)
{
    long lvl = lround(sun_altitude_deg(utc_s, lat_e4, lon_e4) / 6.0);
    if (lvl < SUN_LEVEL_MIN) {
        lvl = SUN_LEVEL_MIN; /* everything below -15 deg is equally night */
    } else if (lvl > SUN_LEVEL_MAX) {
        lvl = SUN_LEVEL_MAX; /* defensive: round() cannot exceed 15 for alt <= 90 */
    }
    return (int)lvl;
}

int sun_rise_set(int year, int month, int day, int32_t tz_min,
                 int32_t lat_e4, int32_t lon_e4, int *rise_min, int *set_min)
{
    double lat = (double)lat_e4 / 10000.0 * SUN_DEG2RAD;
    double lon_deg = (double)lon_e4 / 10000.0;
    /* UTC instant of the LOCAL date's Greenwich midnight: the transit
     * formula below lands within the requested local date because the
     * table guarantees |4*lon - tz| stays a few hours at most (the
     * generator's offset-vs-longitude assertion). */
    int64_t day_utc0 = clock_days_from_civil(year, month, day) * 86400;

    /* The standard rise/set zenith: 90.833 deg -- 34' of refraction plus
     * the 16' solar semidiameter, the definition published tables use. */
    double sin_h0 = sin(-0.833 * SUN_DEG2RAD);

    double transit = transit_utc_s(day_utc0, lon_deg);

    /* Two passes per event: hour angle from parameters at the transit,
     * then again at the first estimate -- the declination moves up to
     * ~0.4 deg between a solstice-adjacent midnight and evening, worth
     * about a minute at mid-latitudes. */
    double events[2]; /* 0 = rise, 1 = set */
    for (int ev = 0; ev < 2; ev++) {
        double t = transit;
        for (int pass = 0; pass < 2; pass++) {
            double decl, eot;
            sun_params((int64_t)t, &decl, &eot);
            double cos_h = (sin_h0 - sin(lat) * sin(decl)) / (cos(lat) * cos(decl));
            if (cos_h > 1.0) {
                return SUN_POLAR_NIGHT; /* never that high: no rise today */
            }
            if (cos_h < -1.0) {
                return SUN_POLAR_DAY; /* never that low: no set today */
            }
            double h_deg = acos(cos_h) / SUN_DEG2RAD;
            t = transit + (ev == 0 ? -1.0 : 1.0) * h_deg * 4.0 * 60.0;
        }
        events[ev] = t;
    }

    /* Local minutes of day, floor-wrapped like clock_civil_from_utc. */
    for (int ev = 0; ev < 2; ev++) {
        int64_t local = (int64_t)(events[ev] + 0.5) + (int64_t)tz_min * 60;
        int64_t sod = local % 86400;
        if (sod < 0) {
            sod += 86400;
        }
        *(ev == 0 ? rise_min : set_min) = (int)(sod / 60);
    }
    return SUN_OK;
}
