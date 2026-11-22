// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes wifi_svc (the Wi-Fi station task). */
/* Behaviour: the holders and the 60 s grace run through the real pc_wifihold on the virtual clock, so `wifi st` and the tests see on/hold; the link never comes up (connected stays false), so SNTP and radio stay idle. */
/* Empty: everything (the credentials file is never read). */
#include <stdio.h>
#include "esp_timer.h"
#include "sim.h"
#include "pc/pc_wifihold.h"
#include "wifi_svc.h"
#include "cfg.h"

static pc_wifihold_t s_h;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void log_change(uint8_t m, bool on) { if (m != s_h.holders || on != s_h.on) printf("APP wifi hold=%d on=%d\n", s_h.holders, s_h.on); }

void wifi_svc_init(void) {}
int wifi_svc_hold(int who) { if (!cfg_int("wifi_allow", 1)) return -1; uint8_t m = s_h.holders; bool on = s_h.on; pc_wh_hold(&s_h, (pc_wh_who_t)who); log_change(m, on); return 0; }
void wifi_svc_release(int who) { uint8_t m = s_h.holders; bool on = s_h.on; pc_wh_release(&s_h, (pc_wh_who_t)who, now_ms()); log_change(m, on); }
int wifi_svc_set(bool on)
{
    if (on) return wifi_svc_hold(PC_WH_MANUAL);
    uint8_t m = s_h.holders; bool o = s_h.on;
    pc_wh_force_off(&s_h);
    log_change(m, o);
    return 0;
}
void wifi_svc_reconnect(void) {}
int wifi_svc_hold_mask(void) { return s_h.holders; }
bool wifi_svc_on(void)
{
    uint8_t m = s_h.holders; bool on = s_h.on;
    pc_wh_tick(&s_h, now_ms());
    log_change(m, on);
    return s_h.on;
}
bool wifi_svc_connected(void) { return sim_state_int("wifi_up", 0) == 1; }   /* SIM_STATE wifi_up=1: the link is up (SNTP starts, tests/43_rtc.sh); default never */
int wifi_svc_rssi(void) { return 0; }
const char *wifi_svc_ip(void) { return ""; }
void wifi_svc_bench(const char *url, int seconds) { (void)url; (void)seconds; }
