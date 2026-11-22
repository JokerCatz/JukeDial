// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes cast_mode (the broadcast radio boot, entered by reboot): the hardware half of app/main/cast_mode.c; the screen is the real ui/scr_cast.c. */
/* Behaviour: the state the screen reads comes from the console's simcast (phase N | wifi 0|1 | rssi N); sim --mode cast starts in phase ON with Wi-Fi up, playing the player's current station
 * (cfg radio_last, else the first: the same rule as the board); the exit prints the board's line and calls esp_restart (the shim's restart, which returns); the request is printed and ignored. */
/* Empty: Wi-Fi, the Bluetooth stack, the radio's network, the reboot itself. */
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "cast_mode.h"
#include "cfg.h"
#include "radio_svc.h"
#include "player_ctl.h"
#include "sim.h"

static int g_phase = CAST_PH_ON, g_rssi = -52;
static bool g_wifi = true;

bool cast_mode_take_request(void) { return false; }
void cast_mode_request(void) { printf("APP cast_mode request\n"); }
void cast_mode_run(void) {}
bool cast_mode_active(void) { return !strcmp(g_sim_mode, "cast"); }
void cast_mode_status(cast_status_t *o)
{
    memset(o, 0, sizeof *o);
    o->phase = g_phase;
    o->wifi = g_wifi;
    o->rssi = g_wifi ? g_rssi : 0;
    o->n_stations = radio_count();
    o->station = radio_current();
    const now_ops_t *n = player_ctl_now();
    if (n && n->title) n->title(o->now, sizeof o->now);
    o->volume = app_volume();
    snprintf(o->cast_name, sizeof o->cast_name, "JukeDial");
    radio_stats(&o->bytes, &o->reconnects, &o->underruns);
}
void cast_mode_exit(void) { printf("APP cast mode exit\n"); esp_restart(); }

/* boot (sim_boot.c boot_cast): what cast_mode_run does after the screen is up, in the board's order */
void fake_cast_start(void)
{
    g_phase = sim_state_int("castphase", CAST_PH_ON);
    g_wifi = sim_state_int("castwifi", 1) != 0;
    int n = radio_load();
    int i = cfg_int("radio_last", 0);
    if (i < 0 || i >= n) i = 0;
    if (g_phase == CAST_PH_ON && n > 0) radio_play(i);
}
bool fake_cast_cmd(const char *c)
{
    int n = 0;
    if (sscanf(c, "simcast phase %d", &n) == 1) { g_phase = n; printf("APP simcast phase=%d\n", n); return true; }
    if (sscanf(c, "simcast wifi %d", &n) == 1) { g_wifi = n != 0; return true; }
    if (sscanf(c, "simcast rssi %d", &n) == 1) { g_rssi = n; return true; }
    return cast_mode_cmd(c);
}
