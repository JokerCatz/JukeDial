// SPDX-License-Identifier: LGPL-2.1-or-later
#include "time/terms.h"

#include <math.h>
#include <stddef.h>

#include "time/clock.h"
#include "time/solar_longitude.h"

int terms_index(int64_t utc_s)
{
    int idx = (int)floor(solar_lambda_deg(utc_s) / 15.0);
    /* lambda is [0,360) so idx is 0..23 already; the clamp only guards
     * the floating-point edge where lambda rounds to exactly 360. */
    return idx >= TERMS_COUNT ? 0 : idx;
}

const char *terms_name(int idx)
{
    /* Longitude order from 0 deg: the March equinox first. */
    static const char *const k_names[TERMS_COUNT] = {
        "Chunfen", "Qingming", "Guyu",    "Lixia",  "Xiaoman",     "Mangzhong",
        "Xiazhi",  "Xiaoshu",  "Dashu",   "Liqiu",  "Chushu",      "Bailu",
        "Qiufen",  "Hanlu",    "Shuangjiang", "Lidong", "Xiaoxue", "Daxue",
        "Dongzhi", "Xiaohan",  "Dahan",   "Lichun", "Yushui",      "Jingzhe",
    };
    if (idx < 0 || idx >= TERMS_COUNT) {
        return "?";
    }
    return k_names[idx];
}

int64_t terms_next_utc(int64_t utc_s, int *next_idx)
{
    double lam = solar_lambda_deg(utc_s);
    double target = (floor(lam / 15.0) + 1.0) * 15.0; /* may be 360 == 0 */

    /* Newton iteration on the boundary: the longitude advances at very
     * nearly the mean rate (the equation of center bends it by at most
     * ~2 deg total), so stepping by remaining-angle / mean-rate
     * converges in a few rounds -- each halves-or-better the error, and
     * five rounds land within the series' own accuracy. */
    double t = (double)utc_s;
    for (int i = 0; i < 5; i++) {
        double diff = fmod(target - solar_lambda_deg((int64_t)t), 360.0);
        if (diff > 180.0) {
            diff -= 360.0; /* overshot the boundary: step back */
        } else if (diff < -180.0) {
            diff += 360.0;
        }
        t += diff / SOLAR_L_RATE_DEG_PER_DAY * 86400.0;
    }

    if (next_idx != NULL) {
        int idx = (int)(target / 15.0);
        *next_idx = idx >= TERMS_COUNT ? 0 : idx;
    }
    return (int64_t)t;
}

/* JC player: Traditional Chinese names, same order (index 0 = 春分) */
const char *terms_name_zh(int idx)
{
    static const char *const k_zh[TERMS_COUNT] = {
        "春分", "清明", "穀雨", "立夏", "小滿", "芒種", "夏至", "小暑", "大暑", "立秋", "處暑", "白露",
        "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至", "小寒", "大寒", "立春", "雨水", "驚蟄",
    };
    return idx < 0 || idx >= TERMS_COUNT ? "?" : k_zh[idx];
}

int terms_days_until(int64_t now_utc, int64_t at_utc, int32_t tz_min)
{
    clock_civil_t a, b;
    clock_civil_from_utc(now_utc, tz_min, &a);
    clock_civil_from_utc(at_utc, tz_min, &b);
    return (int)(clock_days_from_civil(b.year, b.month, b.day) - clock_days_from_civil(a.year, a.month, a.day));
}
