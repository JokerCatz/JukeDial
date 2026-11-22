// SPDX-License-Identifier: LGPL-2.1-or-later
#include "speed_svc.h"
#include <stdio.h>
#include "audio_svc.h"
#include "esp_attr.h"
#include "cfg.h"
#include "font.h"
#include "fx_state.h"
#include "i18n.h"
#include "pc/pc_fxapp.h"
#include "pc/pc_speed.h"

/* the chosen values, cached: fx_mode_set (any task) refreshes without touching cfg, which only the LVGL task may. PSRAM struct, 0 B internal. */
EXT_RAM_BSS_ATTR static struct { volatile int pct, xf; } V;   /* zero until speed_svc_init: pct 0 reads as 100 */
int speed_svc_pct(void) { return V.pct ? V.pct : 100; }
int speed_svc_xfade_s(void) { return V.xf; }
const char *speed_svc_why(void)
{
    uint8_t m = fx_mode_bits();
    return pc_speed_ok(m) ? NULL : pc_fx_mode_why((uint8_t)(m & PC_SPEED_OFF_MODES), i18n_zh());
}

void speed_svc_refresh(void)
{
    bool ok = pc_speed_ok(fx_mode_bits());
    audio_svc_set_speed(ok ? speed_svc_pct() : 100);
    audio_svc_set_xfade(ok ? speed_svc_xfade_s() * 1000 : 0);
}
void speed_svc_init(void)
{
    V.pct = pc_speed_norm(cfg_int("play_speed", 100));
    V.xf = pc_xfade_norm(cfg_int("xfade_s", 0));
    speed_svc_refresh();
}
void speed_svc_step(int d) { V.pct = pc_speed_step(speed_svc_pct(), d); cfg_set_int("play_speed", V.pct); speed_svc_refresh(); }
void speed_svc_xfade_step(int d) { V.xf = pc_xfade_step(V.xf, d); cfg_set_int("xfade_s", V.xf); speed_svc_refresh(); }

void speed_svc_label(char *out, size_t cap)
{
    int p = speed_svc_pct();
    snprintf(out, cap, "%s %d.%02d\xC3\x97", T(S_SPEED) /* 倍速 */, p / 100, p % 100);
    if (p != 100 && !pc_speed_ok(fx_mode_bits())) snprintf(out, cap, "%s %s", T(S_SPEED) /* 倍速 */, T(S_OFF) /* 關 */);
}
