// SPDX-License-Identifier: LGPL-2.1-or-later
/* SIM_STATE: "key=value;key=value" test knobs for the fake services (batt, charging, bt, sd). Parsed from the environment on each call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"

const char *sim_state_str(const char *key, const char *def)
{
    static char val[64];
    const char *s = getenv("SIM_STATE");
    size_t kl = strlen(key);
    while (s && *s) {
        const char *end = strchr(s, ';');
        size_t n = end ? (size_t)(end - s) : strlen(s);
        if (n > kl && !strncmp(s, key, kl) && s[kl] == '=') {
            n -= kl + 1;
            if (n >= sizeof val) n = sizeof val - 1;
            memcpy(val, s + kl + 1, n);
            val[n] = 0;
            return val;
        }
        s = end ? end + 1 : NULL;
    }
    return def;
}

int sim_state_int(const char *key, int def)
{
    const char *v = sim_state_str(key, NULL);
    char *e;
    long x;
    if (!v) return def;
    x = strtol(v, &e, 10);
    return e == v || *e ? def : (int)x;
}
