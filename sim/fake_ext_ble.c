// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes ext_ble (BLE HID of the external mode): the hardware half of app/main/ext_ble.c. */
/* Behaviour: the state the screen reads (ready / advertising / connected / bonded) moves with the console's `ext ble adv|conn|disc|bond <n>|fail`; a connection needs
 * advertising on (like a computer finding the device), a bond is made by the first connection; sink prints "SIM ext ble hid ..." for each report. */
/* Empty: the controller, Bluedroid, pairing, the GATT table; every call is the whole of it. */
#include <stdio.h>
#include <string.h>
#include "ext_ble.h"
#include "sim.h"

static bool g_ready, g_adv, g_conn;
static int g_bonded;
static const char *g_peer = "A1:B2:C3";

int ext_ble_init(void) { g_ready = sim_state_int("extble", 1) != 0;   /* SIM_STATE extble=0: the stack does not start */ printf("SIM ext ble init ready=%d\n", g_ready); return g_ready ? 0 : -1; }
bool ext_ble_ready(void) { return g_ready; }
void ext_ble_advertise(bool on)
{
    if (!g_ready || on == g_adv || (on && g_conn)) return;
    g_adv = on;
    printf("SIM ext ble adv=%d\n", on);
}
bool ext_ble_advertising(void) { return g_adv; }
bool ext_ble_connected(void) { return g_conn; }
const char *ext_ble_peer(void) { return g_conn ? g_peer : ""; }
const char *ext_serial(void) { return "JD-R-000000"; }
int ext_ble_bonded(void) { return g_bonded; }
void ext_ble_disconnect(void)
{
    if (!g_conn) return;
    g_conn = false;
    printf("SIM ext ble disconnected\n");
}
void ext_ble_unpair(void) { g_bonded = 0; ext_ble_disconnect(); printf("SIM ext ble unpaired\n"); }
bool ext_ble_sink(const pc_extsend_ev *e)
{
    if (e->kind == PC_XEV_WAIT) { printf("SIM ext ble hid wait %d\n", e->after_ms); return true; }
    if (!g_conn) return false;
    if (e->kind == PC_XEV_CONS) printf("SIM ext ble hid cons %04X\n", e->code);
    else if (e->kind == PC_XEV_WHEEL) printf("SIM ext ble hid wheel %d\n", (int)(int16_t)e->code);
    else printf("SIM ext ble hid kbd mod=%02X code=%02X\n", e->mod, e->code);
    return true;
}

/* console ext ble: adv (a computer finds it: only while advertising, nothing happens otherwise) is not a command; conn | disc | bond <n>  */
bool fake_ext_ble_cmd(const char *a)
{
    int n;
    if (!strcmp(a, "conn")) {
        if (g_ready && g_adv && !g_conn) { g_conn = true; g_adv = false; if (!g_bonded) g_bonded = 1; printf("SIM ext ble connected %s\n", g_peer); }
        else printf("SIM ext ble conn refused adv=%d conn=%d\n", g_adv, g_conn);
    }
    else if (!strcmp(a, "disc")) { ext_ble_disconnect(); }
    else if (!strcmp(a, "adv")) printf("SIM ext ble adv=%d conn=%d bonded=%d ready=%d\n", g_adv, g_conn, g_bonded, g_ready);
    else if (sscanf(a, "bond %d", &n) == 1) g_bonded = n;
    else return false;
    return true;
}
