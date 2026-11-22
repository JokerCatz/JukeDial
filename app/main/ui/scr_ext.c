// SPDX-License-Identifier: LGPL-2.1-or-later
/* The external mode's screen (external mode T2, spec 2026-11-15-external-mode.md section 6; grown from the USB drive page): the UI half of usb_mode_run, compiled by the
 * firmware and the simulator. Fonts from the flash pack only (the card goes to the computer), the shared host with the external mode's rules (k_ext_host), one page at
 * the bottom of its stack that is one menu ring (a tap runs a cell at once; more than 7 cells turn by dragging): the keys of the current layer, the two layer cells,
 * Settings (the error list page), Reread settings, Enter URL, Back to player (armed) and 說明 last. The centre says the layer, the USB state and the knob's job. The model is ext_mode.c,
 * the hardware usb_mode.c or sim/fake_usb_mode.c. Pages only compose shared pieces (ui_ring, ui_list, ui_help). LVGL task only. */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "pc/pc_power.h"
#include "ext_audio.h"
#include "ext_ble.h"
#include "ext_mode.h"
#include "ext_xport.h"
#include "pc/pc_extdesc.h"
#include "pc/pc_xport.h"
#include "pc/pc_sdguide.h"
#include "font.h"
#include "screens.h"
#include "ui.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "usb_mode.h"

#define NOTE_US 4000000                       /* a result stays 4 s in the centre */

/* the knob is the layer's job (ext_knob: the HID send queue); the screen times out as the player's on USB power (60 s): this mode always runs on USB power and the power
 * service is not started in it; the card belongs to the computer: nothing saved. FakePod (no knob): its two keys arrive as knob detents (ui_knob_inject), so they do the same. */
static bool host_knob(int d) { return ext_knob(d); }
static uint32_t host_idle_ms(void) { return pc_screen_timeout_ms(PC_PWR_USB); }
static const ui_host_t k_ext_host = { .name = "ext", .knob_default = host_knob, .idle_ms = host_idle_ms, .cfg_writes = false, .haptics = true };

static const ui_screen_t scr_ext;                /* defined below */
static lv_obj_t *s_ring;
static char s_note[48], s_centre[200];
static int64_t s_note_at;
static int s_gen_shown, s_reload_w, s_rel_shown;           /* set by page_create: no initial value, so they stay .bss (PSRAM) */
/* cell layout of the ring made by page_create: keys first, then the fixed cells */
static int c_nk, c_prev, c_next, c_cfg, c_reload, c_url, c_func, c_bt, c_unpair, c_exit, c_help, c_n, c_reconn;

static bool in_use(void) { return usb_mode_has_msc() && usb_mode_where() == 1 && usb_mode_mounted(); }
static const char *where_text(int w)
{
    if (!usb_mode_has_msc()) return w == -1 ? T(S_WAITING) /* Waiting for the computer */ : T(S_CONNECTED) /* Connected */;   /* sound card without the drive: no eject, no card in use */
    if (usb_mode_link() == PC_USB_LINK_LOST) return T(S_USB_LOST_CARD_RELEASED) /* USB disconnected, card released */;
    if (usb_mode_link() == PC_USB_LINK_SLEEP) return T(S_USB_SLEEP_CARD_RELEASED) /* Computer asleep or suspended, card released */;
    return w == 1 ? T(S_IN_USE) /* In use by the computer */ : w == 0 ? T(S_EJECTED_UNPLUG) /* Ejected, safe to unplug */ : T(S_WAITING) /* Waiting for the computer */;
}
/* the centre's second and third line: who carries the keys and its state (USB: the computer's side of the plug; Bluetooth: waiting / connected, the pairings) */
static const char *xport_line(char *b, size_t n)
{
    int sd = ext_xport_sender();
    if (sd == PC_XS_BLE) snprintf(b, n, "%s", ext_ble_connected() ? T(S_BT_CONNECTED) /* Bluetooth: connected */ : T(S_BT_WAITING) /* Bluetooth: waiting */);
    else if (sd == PC_XS_NONE) snprintf(b, n, "%s", T(S_USB_UNPLUGGED) /* USB unplugged */);
    else snprintf(b, n, "%s", where_text(usb_mode_where()));
    return b;
}
static bool link_ok(void) { return ext_xport_link_ok(); }
static void note(const char *s)
{
    snprintf(s_note, sizeof s_note, "%s", s);
    s_note_at = esp_timer_get_time();
}

/* ---- the error list page (Settings): what the card's _player/config/keys.txt said ---- */
static lv_obj_t *s_elist;
static void err_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const pc_extkeys_res_t *r = ext_res();
    ui_obj_guard(ui_list_titled(p, T(S_CONFIG_FILE) /* Config file */), &s_elist);
    char b[96];
    if (r->ok && r->from_file) snprintf(b, sizeof b, T(S_EXT_CFG_OK) /* %d layers applied */, ext_cfg()->nlayers);
    else if (r->ok) snprintf(b, sizeof b, "%s", T(S_EXT_CFG_NONE) /* No config file */);
    else snprintf(b, sizeof b, T(S_EXT_CFG_BAD) /* Whole file rejected: %d errors */, r->total);
    ui_list_note(s_elist, b);
    for (int i = 0; i < r->nerr; i++) {
        pc_extkeys_err_fmt(&r->err[i], b, sizeof b);
        ui_list_row_wrap(s_elist, b, UI_PANEL2, NULL, NULL);
    }
    if (r->total > r->nerr) {
        snprintf(b, sizeof b, T(S_D_MORE_NOT_LISTED) /* %d more not listed */, r->total - r->nerr);
        ui_list_note(s_elist, b);
    }
    ui_list_help_row(s_elist, &HELP_EXT);
    ui_back_button(p);
}
static void err_destroy(void) { s_elist = NULL; }
static bool err_knob(int d) { return ui_list_knob(s_elist, d); }
static const ui_screen_t scr_ext_errors = { .name = "ext_errors", .create = err_create, .destroy = err_destroy, .on_knob = err_knob };

/* ---- the question: unplugged -> Bluetooth?, plugged in -> back to USB? (pc_xport asks; one tap answers, no answer is no after 30 s) ---- */
static lv_obj_t *s_ask_ring;
static void ask_pick(int i)
{
    if (i < 0) return;
    ext_xport_answer(i == 0);
    ui_pop();
}
static void ask_tick(lv_timer_t *t)
{
    (void)t;
    ext_xport_poll();
    if (!ext_xport_ask() && ui_top() && !strcmp(ui_top()->name, "ext_ask")) ui_pop();   /* the cable moved, or the time ran out: the question is gone */
}
static void ask_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    bool to_bt = ext_xport_ask() == 1;
    const char *labels[2] = { to_bt ? T(S_USE_BLUETOOTH) /* Switch to Bluetooth */ : T(S_BACK_TO_USB) /* Back to USB */, T(S_NO) /* No */ };
    s_ask_ring = ui_ring_create(p, labels, 2, ask_pick, NULL);
    ui_ring_set_system(s_ask_ring, 0);
    ui_ring_set_center_text(s_ask_ring, to_bt ? T(S_USB_UNPLUGGED_SEND_KEYS_BY_BLUETOOTH) /* USB unplugged: send keys over Bluetooth instead? */
                                               : T(S_USB_PLUGGED_IN_BACK_TO_USB_KEYS) /* USB plugged in: send keys over USB again? */);
    ui_timer_bind(p, ask_tick, 300, NULL);
}
static void ask_destroy(void) { s_ask_ring = NULL; }
static const ui_screen_t scr_ext_ask = { .name = "ext_ask", .create = ask_create, .destroy = ask_destroy };

/* ---- the page ---- */
static void rebuild_cb(void *arg) { (void)arg; ui_rebuild_all(); }
static void rebuild(void) { ui_async(rebuild_cb, NULL); }   /* never from inside the ring's own callback or the page's own timer */

static void refresh_centre(void)
{
    const pc_extkeys_cfg_t *c = ext_cfg();
    char b[200];
    bool noted = s_note_at && esp_timer_get_time() - s_note_at < NOTE_US;
    char knob[48];
    snprintf(knob, sizeof knob, T(S_S_S) /* %s = %s */, ui_word_knob(), ext_knob_word());
    char st[48], l3[72];
    if (ext_xport_sender() == PC_XS_BLE && ext_ble_connected()) snprintf(l3, sizeof l3, "%s", ext_ble_peer());
    else if (ext_xport_sender() == PC_XS_BLE) snprintf(l3, sizeof l3, T(S_D_PAIRED) /* %d paired */, ext_ble_bonded());
    else if (usb_mode_has_msc() && usb_mode_link() == PC_USB_LINK_LOST) snprintf(l3, sizeof l3, "%s", T(S_USB_LOST_GUIDE) /* Go back, then enter drive mode again */);
    else if (usb_mode_has_msc() && usb_mode_link() == PC_USB_LINK_SLEEP) snprintf(l3, sizeof l3, "%s", T(S_USB_SLEEP_GUIDE) /* When the computer is awake, tap Reconnect */);
    else snprintf(l3, sizeof l3, "%s", knob);
    char snd[96] = "";
    ext_audio_status_t as;
    if (usb_mode_has_uac() && ext_audio_started()) ext_audio_status(&as); else memset(&as, 0, sizeof as);
    if (as.alt && !noted) {                               /* the sound card is streaming (or its rate is refused): the third line says the format, a fourth the counts (honest degrade) */
        char rs[16], l[48];
        ext_audio_rate_str(as.rate, rs, sizeof rs);
        if (as.refused) snprintf(snd, sizeof snd, T(S_EXT_UAC_NO_RATE) /* This device cannot output %s kHz */, rs);
        else {
            snprintf(l, sizeof l, T(S_EXT_UAC_STREAM) /* %s kHz %d bit */, rs, as.bits);
            snprintf(snd, sizeof snd, "%s\n", l);
            snprintf(snd + strlen(snd), sizeof snd - strlen(snd), T(S_EXT_UAC_XRUNS) /* underrun %u overflow %u */, (unsigned)as.underruns, (unsigned)as.overruns);
        }
    }
    snprintf(b, sizeof b, "%s (%d/%d)\n%s\n%s", c->layers[ext_layer()].name, ext_layer() + 1, c->nlayers, xport_line(st, sizeof st), noted ? s_note : snd[0] ? snd : l3);
    if (strcmp(b, s_centre)) { snprintf(s_centre, sizeof s_centre, "%s", b); ui_ring_set_center_text(s_ring, s_centre); }
    int w = usb_mode_where();
    if (w != s_reload_w) {                                  /* Reread settings says why it cannot run while the computer has the card */
        s_reload_w = w;
        ui_ring_set_label(s_ring, c_reload, w == 1 ? T(S_EJECT_FIRST) /* Eject first */ : T(S_RELOAD) /* Reread settings */);
    }
}

/* the function mix by name: all-in-one (drive + sound card + keys), drive + keys, sound card + keys (spec section 2: the exclusive fallbacks) */
static const char *combo_name(int c)
{
    return c == PC_EXTCOMBO_DRIVE ? T(S_DRIVE_KEYS) /* Drive + keyboard */ : c == PC_EXTCOMBO_SOUND ? T(S_SOUND_CARD_KEYS) /* Sound card + keyboard */ : T(S_ALL_THREE) /* All-in-one */;
}
static char s_unpair_armed[48], s_func_armed[96];
static const char *armed_text(int i)
{
    if (i == c_func) { snprintf(s_func_armed, sizeof s_func_armed, T(S_EXT_FUNC_ARMED) /* Tap again to change to %s */, combo_name(pc_extdesc_combo_next(usb_mode_combo()))); return s_func_armed; }
    if (i == c_unpair) { snprintf(s_unpair_armed, sizeof s_unpair_armed, T(S_TAP_AGAIN_FORGET_D_PAIRINGS) /* Tap again: clear %d pairings */, ext_ble_bonded()); return s_unpair_armed; }
    return in_use() ? T(S_EXT_ARMED_IN_USE) /* The card is on the computer; eject it there first, a tap again = force back */
                    : T(S_TAP_AGAIN_REBOOT) /* Tap again: reboot */;
}
static void pick(int i)
{
    if (i < 0) return;
    if (i < c_nk) {
        if (!link_ok()) { note(ext_xport_sender() == PC_XS_BLE ? T(S_WAITING_FOR_A_BLUETOOTH_LINK) /* Waiting for the computer's Bluetooth */ : T(S_WAITING_FOR_THE_COMPUTER) /* Waiting for the computer */); refresh_centre(); return; }
        if (ext_key(i)) ui_haptic_tick();
        else { note(T(S_NOT_SENT_QUEUE_FULL) /* Cannot send (queue full) */); refresh_centre(); }
        return;
    }
    if (i == c_prev || i == c_next) { ext_layer_step(i == c_next ? 1 : -1); rebuild(); return; }
    if (i == c_cfg) { ui_push(&scr_ext_errors, NULL); return; }
    if (i == c_reload) {
        int r = ext_reload();
        note(r == EXT_RL_OK ? T(S_CONFIG_RELOADED) /* Settings reread */ : r == EXT_RL_IN_USE ? T(S_THE_COMPUTER_HAS_THE_CARD_EJECT_IT_FIRST) /* The card is on the computer, eject first */
             : r == EXT_RL_NOT_OURS ? T(S_THE_CARD_IS_NOT_BACK_YET) /* The card is not back yet */ : T(S_THE_COMPUTER_TOOK_IT_BACK_NOT_READ) /* The computer took it again, not read */);
        if (r == EXT_RL_OK) rebuild(); else refresh_centre();
        return;
    }
    if (i == c_reconn && c_reconn >= 0) {                  /* only while the card is released: the user's way back to the computer (never automatic) */
        int r = usb_mode_reconnect();
        note(r == 0 ? T(S_CARD_ATTACHED_AGAIN) /* Card attached again */ : r == 1 ? T(S_PLUG_THE_CABLE_IN_FIRST) /* Plug the cable in first */ : T(S_COULD_NOT_REOPEN_THE_CARD) /* Could not reopen the card */);
        if (r == 0) rebuild(); else refresh_centre();
        return;
    }
    if (i == c_func) { usb_mode_set_combo(pc_extdesc_combo_next(usb_mode_combo())); rebuild(); return; }   /* the board restarts here; the simulator applies the mix at once */
    if (i == c_bt) {
        if (!ext_xport_ble_ok()) note(T(S_BLUETOOTH_IS_NOT_AVAILABLE) /* Bluetooth unavailable */);
        else if (!ext_xport_request()) note(ext_xport_sender() == PC_XS_BLE ? T(S_NO_USB_CABLE_TO_GO_BACK_TO) /* No USB cable to switch back to */ : T(S_THE_USB_CABLE_IS_STILL_IN) /* The USB cable is still plugged in */);
        refresh_centre();
        return;
    }
    if (i == c_unpair) { ext_ble_unpair(); note(T(S_PAIRINGS_FORGOTTEN) /* Pairings cleared */); refresh_centre(); return; }
    if (i == c_url) {
        if (!link_ok()) { note(T(S_WAITING_FOR_THE_COMPUTER) /* Waiting for the computer */); refresh_centre(); return; }
        usb_mode_type_url();
        note(T(S_ADDRESS_TYPED_TURN_THE_PLUG_OVER_FOR) /* URL entered; flip the plug when configuring */);
        refresh_centre();
        return;
    }
    if (i == c_exit) { usb_mode_exit(true); return; }      /* the armed cell's second tap is the force; on the board it never returns */
    if (i == c_help) ui_help_open(&HELP_EXT);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    ext_poll();
    ext_xport_poll();
    if (ext_xport_ask() && ui_top() == &scr_ext) { ui_wake(); ui_push(&scr_ext_ask, NULL); return; }
    if (ext_generation() != s_gen_shown || (int)usb_mode_released() != s_rel_shown) { rebuild(); return; }   /* a new config, or the card was released / attached: the Reconnect cell comes or goes */   /* a new config: the cells are other */
    refresh_centre();
}

static void page_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const pc_extkeys_cfg_t *c = ext_cfg();
    const pc_extkeys_layer_t *ly = &c->layers[ext_layer()];
    const char *labels[PC_EXTKEYS_KEYS + 10];
    int n = 0;
    for (int i = 0; i < ly->nkeys; i++) labels[n++] = ly->keys[i].name;
    c_nk = n;
    c_prev = c_next = -1;
    if (c->nlayers > 1) { c_prev = n; labels[n++] = T(S_PREV_LAYER) /* Previous layer */; c_next = n; labels[n++] = T(S_NEXT_LAYER) /* Next layer */; }
    c_cfg = n; labels[n++] = ext_res()->ok ? T(S_CONFIG) /* Settings */ : T(S_CONFIG_ERRORS) /* Settings errors */;
    c_reload = n; labels[n++] = T(S_RELOAD) /* Reread settings */;
    c_url = n; labels[n++] = T(S_TYPE_URL) /* Enter URL */;
    c_func = n; labels[n++] = T(S_USB_MIX) /* USB functions */;
    c_bt = n; labels[n++] = T(S_BLUETOOTH_2) /* Bluetooth */;
    c_unpair = n; labels[n++] = T(S_UNPAIR) /* Clear pairings */;
    c_reconn = -1;
    if (usb_mode_has_msc() && usb_mode_released()) { c_reconn = n; labels[n++] = T(S_RECONNECT) /* Reconnect */; }
    c_exit = n; labels[n++] = T(S_PLAYER) /* Back to player */;
    c_help = n; labels[n++] = ui_help_label();
    c_n = n;
    s_ring = ui_ring_create_menu(p, labels, n, pick, NULL);
    ui_ring_set_knob_volume(s_ring);                       /* the ring never takes the knob (a page turn): it is the layer's job */
    ui_ring_set_system(s_ring, c_exit);
    ui_ring_set_item_arm(s_ring, c_exit, armed_text(c_exit));
    ui_ring_set_item_arm(s_ring, c_unpair, armed_text(c_unpair));
    ui_ring_set_item_arm(s_ring, c_func, armed_text(c_func));
    ui_ring_set_system(s_ring, c_func);
    ui_ring_set_system(s_ring, c_unpair);
    ui_ring_set_arm_text_fn(s_ring, armed_text);
    s_gen_shown = ext_generation();
    s_rel_shown = usb_mode_released();
    s_reload_w = -2;
    s_centre[0] = 0;
    ui_timer_bind(p, tick, 300, NULL);
    refresh_centre();
}
static void page_destroy(void) { s_ring = NULL; }
static const ui_screen_t scr_ext = { .name = "ext", .create = page_create, .destroy = page_destroy };

void ext_screen_start(lv_display_t *d, lv_indev_t *touch)
{
    font_init_flash();
    ui_start_host(d, touch, &k_ext_host);
    ui_push(&scr_ext, NULL);
}
void ext_screen_tap(int idx) { if (ui_top() == &scr_ext && s_ring) pick(idx); }
int ext_screen_cell(const char *n)
{
    return !strcmp(n, "func") ? c_func : !strcmp(n, "bt") ? c_bt : !strcmp(n, "unpair") ? c_unpair : !strcmp(n, "cfg") ? c_cfg : !strcmp(n, "reload") ? c_reload : !strcmp(n, "url") ? c_url : !strcmp(n, "reconnect") ? c_reconn : !strcmp(n, "exit") ? c_exit : !strcmp(n, "help") ? c_help : -1;
}
void ext_screen_page(int delta) { if (ui_top() == &scr_ext && s_ring) ui_ring_page(s_ring, delta); }
void ext_screen_arm_cell(const char *name) { if (ui_top() == &scr_ext && s_ring) ui_ring_tap(s_ring, ext_screen_cell(name)); }
void ext_screen_arm(bool on) { if (ui_top() == &scr_ext && s_ring) ui_ring_tap(s_ring, on ? c_exit : -1); }
void ext_screen_ask(bool yes) { if (ui_top() == &scr_ext_ask) ask_pick(yes ? 0 : 1); }
