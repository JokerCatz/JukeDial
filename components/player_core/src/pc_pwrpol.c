// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pwrpol.h"

static const int APO[] = { 0, 10, 30, 60 };
#define APO_N 4

int pc_apo_norm(int m)
{
    for (int i = 0; i < APO_N; i++) if (APO[i] == m) return m;
    return PC_APO_DEFAULT_MIN;
}
int pc_apo_step(int m, int d)
{
    int i = 0;
    while (i < APO_N && APO[i] != m) i++;
    if (i == APO_N) i = 2;
    return APO[(((i + d) % APO_N) + APO_N) % APO_N];
}

uint32_t pc_oled_idle_limit(uint32_t l) { return l == 0 || l > PC_OLED_IDLE_MAX_MS ? PC_OLED_IDLE_MAX_MS : l; }
bool pc_oled_press_stuck(uint32_t ms) { return ms >= PC_OLED_PRESS_MAX_MS; }
bool pc_apo_ext_screen_off(int minutes, bool external, uint32_t idle) { return external && idle >= (uint32_t)(minutes > 0 ? minutes : 10) * 60000u; }

bool pc_apo_tick(pc_apo_t *s, int minutes, uint32_t dt_ms, bool playing, bool external, uint32_t input_idle_ms)
{
    if (playing || external) { s->quiet_ms = 0; return false; }
    if (s->quiet_ms < 0xFFFFFFFFu - dt_ms) s->quiet_ms += dt_ms;
    if (minutes <= 0) return false;
    const uint32_t idle = s->quiet_ms < input_idle_ms ? s->quiet_ms : input_idle_ms;   /* an input restarts the count */
    return idle >= (uint32_t)minutes * 60000u;
}

bool pc_lowbat_boot_refuse(int pct, bool external) { return pct >= 0 && pct < PC_LOWBAT_BOOT_PCT && !external; }

int pc_lowbat_tick(pc_lowbat_t *s, int pct, bool external, uint32_t dt_ms)
{
    if (pct < 0 || external) { s->low_ms = 0; if (external) s->warned = false; return PC_LB_NONE; }
    if (pct > PC_LOWBAT_REARM_PCT) s->warned = false;
    if (pct >= PC_LOWBAT_RUN_PCT) s->low_ms = 0;
    else if (s->low_ms < PC_LOWBAT_RUN_MS) {
        s->low_ms += dt_ms;
        if (s->low_ms >= PC_LOWBAT_RUN_MS) return PC_LB_SHUTDOWN;      /* once: low_ms stays at the limit but the caller powers off */
    } else return PC_LB_NONE;
    if (pct <= PC_LOWBAT_WARN_PCT && !s->warned) { s->warned = true; return PC_LB_WARN; }
    return PC_LB_NONE;
}

bool pc_pwron_is_plug_only(uint8_t r) { return (r & 0x04) && !(r & 0x01); }
