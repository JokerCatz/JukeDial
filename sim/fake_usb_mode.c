// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes usb_mode (the external mode, entered by reboot): the hardware half of app/main/usb_mode.c; the screen is the real ui/scr_ext.c, the model ext_mode.c, the send queue ext_hid_send.c. */
/* Behaviour: the state the screen reads (where / mounted) moves with the console's simusb enum|mount|eject|unplug in the board's order of events (below),
 * and where comes from the board's rule (pc_usb_where); typing the address and each knob detent print a line;
 * exit prints the board's line and calls esp_restart (the shim's SIM restart, which returns). The player's request is ignored but printed. */
/* The HID reports are printed ("SIM ext hid ...") by the sink the 5 ms timer of ext_start drains the queue with, on the virtual clock. Empty: the MSC drive, the RTC memory note (usb_mode_mem_last: none), the plug watch of the normal mode. */
#include <stdio.h>
#include "pc/pc_paths.h"
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "sim.h"
#include "usb_mode.h"
#include "ext_hid_send.h"
#include "ext_mode.h"
#include "ext_xport.h"
#include "ext_usbdesc.h"
#include "pc/pc_extdesc.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ui/ui.h"
#include "pc/pc_plug.h"
#include "pc/pc_sdguide.h"

static bool g_mounted;                   /* tud_mounted() */
static int g_event = -1;                 /* the last storage event as usb_mode.c's storage_cb gets it: -1 none yet, 0 the card went to our side, 1 to USB */
static int g_typed;
static bool g_was, g_released, g_ejected, g_slept;
static int g_lost_n;           /* cardsafe: the computer configured the device once; the card was released (lost / exit) */

bool usb_mode_take_request(void) { return false; }
void usb_mode_request(void) { printf("APP usb_mode request\n"); }
void usb_mode_run(void) {}
bool usb_mode_active(void) { return !strcmp(g_sim_mode, "ext"); }   /* sim --mode ext (alias usb) is the external mode's boot */
void usb_mode_watch_start(void) {}
bool usb_mode_mem_last(uint32_t *free_b, uint32_t *min_b) { (void)free_b; (void)min_b; return false; }

static int g_combo = -1;                 /* the function mix (pc_extcombo_t); SIM_STATE extusb=<0..2> sets it at the first call */
static int g_alt;
int usb_mode_combo(void) { if (g_combo < 0) g_combo = ext_usbdesc_combo_norm(sim_state_int("extusb", 0)); return g_combo; }
bool usb_mode_has_msc(void) { return pc_extdesc_combo_msc(usb_mode_combo()); }
bool usb_mode_has_uac(void) { return pc_extdesc_combo_uac(usb_mode_combo()); }
void usb_mode_set_combo(int c) { g_combo = ext_usbdesc_combo_norm(c); g_alt = 0; printf("APP usb combo=%d: restart into the mode\n", g_combo); esp_restart(); }   /* the simulator applies it at once, as the restart would */
int usb_mode_where(void) { return g_released ? -1 : pc_usb_where(g_mounted, !usb_mode_has_msc() || g_event == 0); }
int usb_mode_reconnect(void)
{
    if (!g_released || !usb_mode_has_msc()) return 2;
    if (!g_mounted) { printf("APP usb reconnect refused: no host\n"); return 1; }
    g_released = false; g_slept = false; g_ejected = false; g_event = 1; g_was = true;
    printf("APP usb card attached by=user\n");
    return 0;
}
bool usb_mode_released(void) { return g_released; }
int usb_mode_link(void) { return pc_usb_link(g_mounted, g_was, g_ejected, g_released, g_slept); }
bool usb_mode_mounted(void) { return g_mounted; }
void usb_mode_type_url(void) { g_typed++; printf("SIM usb typed queued=%d\n", ext_send_text("https://github.com/JokerCatz/JukeDial\n")); }
static int g_lock;                      /* the card lock depth (ext_reload holds it; the console `ext lock` holds it too, to stand for a reload in progress) */
void usb_mode_card_lock(void) { g_lock++; }
void usb_mode_card_unlock(void) { if (g_lock > 0) g_lock--; }
bool usb_mode_card_ours(void) { return !usb_mode_has_msc() || (g_event == 0 && !g_released); }
static char g_keys_path[160] = PC_CFG_KEYS;   /* simulator: the card is <root>; ext cfg <file> points it at another file of the root */
const char *usb_mode_keys_path(void) { return g_keys_path; }
bool usb_mode_exit(bool force)
{
    bool in_use = usb_mode_has_msc() && usb_mode_where() == 1;
    if (in_use && !force) return false;
    printf("APP usb mode exit in_use=%d\n", in_use);
    if (usb_mode_has_msc() && !g_released) { g_released = true; printf("APP usb card released why=exit storage=ESP_OK drain_ms=0\n"); }
    esp_restart();
    return true;
}

/* console simusb, in what the board sees (esp_tinyusb 2.4: the storage is created at USB; tud_mount_cb moves it to USB, the SCSI eject and
 * tud_umount_cb move it to our side, each move with a MOUNT_COMPLETE event, a move to where it already is with none):
 * enum | mount (the same on the board: the computer configures the device = tud_mount_cb; the first one sends no storage event, one after an
 * eject or an unplug moves the card back to USB) | eject (the computer ejects the drive, the cable stays in) | unplug (the cable goes: tud_umount_cb)
 * | typed (how often the address was typed) */
bool fake_usb_cmd(const char *a)
{
    if (!strcmp(a, "enum") || !strcmp(a, "mount")) { if (!g_mounted && g_event == 0) { g_event = 1; g_ejected = false; } g_mounted = true; g_was = true; }
    else if (!strcmp(a, "eject")) { if (g_mounted && g_event != 0) { g_event = 0; g_ejected = true; } }
    else if (!strcmp(a, "unplug") || !strcmp(a, "lost")) {   /* lost = the same: the cable / VBUS / a bus reset / a suspend that did not end; the board releases the card at once unless it was ejected (cardsafe) */
        if (g_mounted && g_event != 0) { g_event = 0; g_ejected = false; }
        g_mounted = false;
        if (usb_mode_has_msc() && g_was && !g_ejected && !g_released) { g_released = true; g_slept = false; g_lost_n++; printf("APP usb lost n=%d why=lost\n", g_lost_n); printf("APP usb card released why=lost storage=ESP_OK drain_ms=0\n"); }
    }
    else if (!strcmp(a, "sleep") || !strcmp(a, "sleepio")) {   /* the bus is suspended for 2 s (the computer sleeps): the cable stays configured; sleepio = a command was open at the moment (a fault: why=lost), sleep = none (harmless: why=suspend) */
        const char *why = pc_usb_release_why(g_mounted, g_was, g_ejected, g_released, true, !strcmp(a, "sleepio"), false);
        if (usb_mode_has_msc() && why) {
            g_released = true; g_slept = !strcmp(why, "suspend"); g_lost_n++;
            if (g_event != 0) g_event = 0;
            printf("APP usb lost n=%d why=%s\n", g_lost_n, why);
            printf("APP usb card released why=%s storage=ESP_OK drain_ms=0\n", why);
        }
    }
    else if (!strcmp(a, "mountstart")) {   /* the computer takes the card back: MOUNT_START waits for the card lock, then the card is not ours (the board's storage_cb) */
        if (g_lock > 0) { printf("SIM usb mountstart blocked\n"); return true; }
        g_event = 1; g_ejected = false;
    }
    else if (!strcmp(a, "typed")) { printf("SIM usb typed n=%d\n", g_typed); return true; }
    else return false;
    printf("SIM usb where=%d mounted=%d\n", usb_mode_where(), g_mounted);
    return true;
}

/* ---- the HID side of the external mode ---- */
static bool sink(const pc_extsend_ev *e)
{
    if (e->kind == PC_XEV_KBD) printf("SIM ext hid kbd mod=%02X code=%02X\n", e->mod, e->code);
    else if (e->kind == PC_XEV_CONS) printf("SIM ext hid cons %04X\n", e->code);
    else if (e->kind == PC_XEV_WHEEL) printf("SIM ext hid wheel %d\n", (int)(int16_t)e->code);
    else printf("SIM ext hid wait %d\n", e->after_ms);
    return true;
}
static void pump(lv_timer_t *t)
{
    (void)t;
    ext_xport_pump((uint32_t)(esp_timer_get_time() / 1000), sink, g_mounted);   /* the board's usb_keys task: the active transport's sink; nothing goes to a computer that is not there */
}
void fake_ext_start(void)
{
    ext_mode_start(PC_CFG_KEYS);
    ext_xport_start();
    lv_timer_create(pump, 5, NULL);
}

#define SAMPLE_LAYERS "layer media\nknob volume\nkey 播放 consumer 0xCD\nkey 下首 consumer 0xB5\nkey 上首 consumer 0xB6\nkey 靜音 consumer 0xE2\n" \
    "layer edit\nknob scroll\nkey 複製 combo ctrl+c\nkey 貼上 combo ctrl+v\nkey 復原 combo ctrl+z\nkey 全選 combo ctrl+a\nkey 簽名 text \"Hello\\n\"\nkey 巨集 macro ctrl+a, wait 50, ctrl+c\n" \
    "layer arrows\nknob arrows\nkey 上 combo up\nkey 下 combo down\n"
#define SAMPLE_BAD "layer media\nknob volume\nkey 播放 consumer 0xCD\nkey 壞了 consumer zz\nkey 中文 text \"你好\"\nlayer x\nknob spin\n"
/* console ext usb: dump (the configuration descriptor of this mix, built by ext_usbdesc_build as the board does, its FIFO words) | plug (the computer walks the
 * descriptor: every length, the interface count, the endpoints once; then mounts) | unplug | alt <n> (the AudioStreaming alternate setting: accepted for 0..n_alt,
 * data discarded) | combo <n> (keep the mix, "restart") */
static void usb_dump(bool enumerate)
{
    static uint8_t d[PC_EXTDESC_MAX_LEN];
    pc_extdesc_info_t in;
    pc_extdesc_fifo_t f;
    int n = ext_usbdesc_build(usb_mode_combo(), d, sizeof d, &in, &f);
    if (n <= 0) { printf("SIM ext usb dump failed %d\n", n); return; }
    int off = 0, itf = 0, eps = 0, dup = 0, seen[256] = { 0 };
    while (off < n && d[off] >= 2 && off + d[off] <= n) {
        if (d[off + 1] == 4) itf++;
        if (d[off + 1] == 5) { eps++; if (seen[d[off + 2]]++ && d[off + 2] != PC_EXTDESC_EP_AUD_OUT && d[off + 2] != PC_EXTDESC_EP_AUD_FB) dup++; }   /* the audio endpoints repeat once per alternate setting */
        off += d[off];
    }
    bool ok = off == n && (d[2] | d[3] << 8) == n && !dup;
    char rates[96] = "";
    for (int i = 0; i < in.n_rates; i++) snprintf(rates + strlen(rates), sizeof rates - strlen(rates), "%s%u", i ? "," : "", (unsigned)in.rates[i]);
    if (enumerate) { printf("SIM ext usb enum %s\n", ok ? "ok" : "bad"); if (ok) fake_usb_cmd("mount"); return; }
    printf("SIM ext usb dump combo=%d pid=%04x len=%d itf=%d desc_itf=%d eps=%d msc=%d hid=%d ac=%d as=%d alts=%d rates=%s fifo rx=%u total=%u of %u\n", usb_mode_combo(),
           pc_extdesc_combo_pid(usb_mode_combo()), n, in.n_itf, itf, eps, in.msc_itf != 0xFF, in.hid_itf != 0xFF, in.ac_itf != 0xFF, in.as_itf != 0xFF, in.n_alt, rates, f.rx, f.total, PC_EXTDESC_FIFO_WORDS);
}
static bool usb_cmd(const char *a)
{
    int n;
    if (!strcmp(a, "dump")) usb_dump(false);
    else if (!strcmp(a, "plug")) usb_dump(true);
    else if (!strcmp(a, "unplug")) { fake_usb_cmd("unplug"); g_alt = 0; }
    else if (sscanf(a, "alt %d", &n) == 1) {
        static uint8_t d[PC_EXTDESC_MAX_LEN];
        pc_extdesc_info_t in;
        if (!usb_mode_has_uac() || !g_mounted || ext_usbdesc_build(usb_mode_combo(), d, sizeof d, &in, NULL) <= 0 || n < 0 || n > in.n_alt) printf("SIM ext usb alt refused %d\n", n);
        else { g_alt = n; printf("SIM ext usb alt=%d pkt=%d bits=%d\n", n, n ? in.alt_max_pkt[n - 1] : 0, n ? in.alt_bits[n - 1] : 0); }
    }
    else if (sscanf(a, "combo %d", &n) == 1) usb_mode_set_combo(n);
    else return false;
    return true;
}
/* console ext: key <i> (a tap on cell i of the ring: the layer's key i) | layer <n> | cfg <file> (the card's keys.txt is <root>/<file> from now on) | eject (the computer
 * mounts if it has not, then ejects: the model reloads at the next tick) | reload (the 重讀設定 cell) | arm | disarm (the exit cell's first tap / its end) | errors (the 設定 page) | url (type the address) | xport | ble conn|disc|adv|bond <n>|fail | state */
bool fake_ext_cmd(const char *a)
{
    int n;
    char f[120];
    if (!strncmp(a, "ble ", 4)) return fake_ext_ble_cmd(a + 4);
    if (!strncmp(a, "usb ", 4)) return usb_cmd(a + 4);
    if (!strncmp(a, "uac ", 4)) return fake_ext_uac_cmd(a + 4);
    if (sscanf(a, "key %d", &n) == 1) ext_screen_tap(n);
    else if (sscanf(a, "layer %d", &n) == 1) { ext_layer_step(n - ext_layer()); ui_rebuild_all(); }
    else if (sscanf(a, "cfg %119s", f) == 1) {
        const char *body = !strcmp(f, "@layers") ? SAMPLE_LAYERS : !strcmp(f, "@bad") ? SAMPLE_BAD : NULL;   /* built-in samples: written to <root>/ext_sample.txt (pictures need no fixture file) */
        if (body) {
            FILE *fp = fopen("/sdcard/ext_sample.txt", "wb");
            if (fp) { fputs(body, fp); fclose(fp); }
            snprintf(f, sizeof f, "ext_sample.txt");
        }
        snprintf(g_keys_path, sizeof g_keys_path, "/sdcard/%s", f);
    }
    else if (!strcmp(a, "eject")) { fake_usb_cmd("mount"); fake_usb_cmd("eject"); }
    else if (!strcmp(a, "arm") || !strcmp(a, "disarm")) ext_screen_arm(a[0] == 'a');
    else if (!strcmp(a, "bt") || !strcmp(a, "unpair") || !strcmp(a, "func") || !strcmp(a, "reconnect")) ext_screen_tap(ext_screen_cell(a));
    else if (!strcmp(a, "page")) ext_screen_page(1);
    else if (!strcmp(a, "funcarm")) ext_screen_arm_cell("func");
    else if (!strcmp(a, "ask yes") || !strcmp(a, "ask no")) ext_screen_ask(a[4] == 'y');
    else if (!strcmp(a, "errors") || !strcmp(a, "url")) ext_screen_tap(ext_screen_cell(!strcmp(a, "url") ? "url" : "cfg"));
    else if (!strcmp(a, "lock")) usb_mode_card_lock();
    else if (!strcmp(a, "unlock")) usb_mode_card_unlock();
    else if (!strcmp(a, "reload")) printf("SIM ext reload=%d\n", ext_reload());
    else if (!strcmp(a, "state")) printf("SIM ext layer=%d/%d where=%d pending=%d drops=%u gen=%d link=%d released=%d\n", ext_layer(), ext_cfg()->nlayers, usb_mode_where(), ext_send_pending(), (unsigned)ext_send_drops(), ext_generation(), usb_mode_link(), usb_mode_released());
    else if (!strcmp(a, "xport")) printf("SIM ext xport sender=%d ask=%d ble_ok=%d link=%d\n", ext_xport_sender(), ext_xport_ask(), ext_xport_ble_ok(), ext_xport_link_ok());
    else return false;
    return true;
}
