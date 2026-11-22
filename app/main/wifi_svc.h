// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* Wi-Fi station for network radio and the web settings page. Credentials: /sdcard/_player/config/wifi.txt
 * (ssid=..., pass=...). Off by default; start/stop run on the wifi task (internal-RAM stack). */
void wifi_svc_init(void);            /* once at boot: creates the task, does not start the radio */
/* Wi-Fi is on while someone holds it (pc_wh_who_t: radio, clock sync, probe, console) and for 60 s after the last one lets go.
 * All of these are queued to the wifi task (thread-safe, any task); 0 = accepted. A hold is refused in broadcast mode. */
int  wifi_svc_hold(int who);
void wifi_svc_release(int who);
int  wifi_svc_set(bool on);          /* console: true = hold as PC_WH_MANUAL, false = drop every holder, off now */
void wifi_svc_reconnect(void);       /* restart with the saved network when it is on; holders unchanged */
int  wifi_svc_hold_mask(void);       /* the holders now, for wifi st */
bool wifi_svc_on(void);
bool wifi_svc_connected(void);
int  wifi_svc_rssi(void);            /* dBm, 0 when not connected */
const char *wifi_svc_ip(void);       /* "" when not connected */
/* test helper: HTTP GET url for `seconds`, print throughput every second (blocking, call from a worker) */
void wifi_svc_bench(const char *url, int seconds);
