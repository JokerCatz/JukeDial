// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes bt_svc (Bluedroid: speaker, headphones, broadcast, listen). */
/* Behaviour: the mode (initial one from SIM_STATE bt=off|on|connected|broadcast|headphones; "on" = speaker mode, waiting; "connected" = speaker mode with a phone), a status line per mode (the real wording), two scan results in headphones mode, connect picks one;
 * "simbt stream" / "simbt stop": the phone starts / stops its A2DP stream, as bt_svc.c ui_stream does (takes the player with the same now_ops: title, stop, playing; no page is pushed). */
/* "simbtmeta title|artist|album <text>", "simbtmeta art <file>|-", "simbtmeta clear": the fake phone changes its track and sends TRACK_CHANGED (the first
 * one brings the AVRCP link up, with cover art); "simbtmeta link 0|1": the link goes / comes (a new phone); "simbtmeta hold 1|0": cover GETs are kept, then
 * answered late. The fake phone answers the SAME core as the board (bt_meta.c driving pc_bma_*): asks, the cover-art link, GETs in 1000-byte pieces; a new
 * picture gets a new handle (0000001, 0000002, ...); the fake thumb_svc draws a test cover from the bytes (nothing is decoded). */
/* "simbt pair off": the prompt ends without a reply. "simbt pair 123456": a pairing prompt is pending with these digits (bt_svc_pair_pending true until bt_svc_pair_reply, which prints "SIM bt pair reply <0|1>"). Empty: forget, codec, LDAC rate, any real radio. */
#include <stdio.h>
#include <stdlib.h>
#include "pc/pc_btcodec.h"
#include "wifi_svc.h"
#include <string.h>
#include "audio_svc.h"
#include "bt_meta.h"
#include "bt_svc.h"
#include "cfg.h"
#include "esp_heap_caps.h"
#include "player_ctl.h"
#include "sim.h"
#include "vol_out.h"
#include "ui/font.h"
#include "ui/ui.h"

static bt_mode_t s_mode;
static bool s_connected;
static bool s_scanned;
static char s_peer[64];
static const bt_dev_t k_devs[2] = { { "Sim Buds", { 0x02, 0, 0, 0, 0, 1 } }, { "Sim Speaker", { 0x02, 0, 0, 0, 0, 2 } } };

bool bt_svc_available(void) { return true; }
int bt_svc_set_mode(bt_mode_t m)
{
    if (m >= BT_MODE_COUNT) return -1;
    s_mode = m;
    vol_out_mode_changed();                              /* the real one does it when the mode task finished (ui_saved) */
    s_connected = false;
    s_scanned = false;
    s_peer[0] = 0;
    return 0;
}
bt_mode_t bt_svc_mode(void) { return s_mode; }
const char *bt_svc_mode_label(bt_mode_t m)
{
    static const char *const zh[] = { "關閉", "藍牙喇叭", "藍牙耳機", "Auracast 廣播", "Auracast 收聽" };   /* the real wording (bt_svc.c) */
    static const char *const en[] = { "Off", "Speaker", "Headphones", "Auracast", "Listen" };
    return m < BT_MODE_COUNT ? T_RAW(zh[m], en[m]) : "";
}
const char *bt_svc_status(void)      /* the real bt_svc.c wording where it has one; broadcast/listen are the fake's own */
{
    static char line[96];
    switch (s_mode) {
    case BT_MODE_OFF: return "";
    case BT_MODE_SPEAKER: return s_connected ? T(S_CONNECTED) /* 已連線 */ : T(S_WAITING_FOR_A_PHONE_JC_PLAYER) /* 等待手機連線（JukeDial） */;
    case BT_MODE_HEADPHONES:
        if (s_peer[0]) { snprintf(line, sizeof line, "%s %s", T(S_CONNECTED_2) /* 已連線： */, s_peer); return line; }
        return s_scanned ? T(S_SCAN_DONE_PICK_HEADPHONES) /* 掃描完成：點選耳機 */ : "";
    case BT_MODE_BROADCAST: return "broadcasting";
    default: return "listening";
    }
}
void bt_svc_start(void)
{
    const char *s = sim_state_str("bt", "off");
    if (!strcmp(s, "on")) { s_mode = BT_MODE_SPEAKER; s_connected = false; }
    else if (!strcmp(s, "connected")) { s_mode = BT_MODE_SPEAKER; s_connected = true; }
    else if (!strcmp(s, "broadcast")) { s_mode = BT_MODE_BROADCAST; s_connected = false; }
    else if (!strcmp(s, "headphones")) { s_mode = BT_MODE_HEADPHONES; s_connected = false; }
    else { s_mode = BT_MODE_OFF; s_connected = false; }
}
static bool s_pair;                                   /* "simbt pair <6 digits>": a phone asks to pair */
static char s_pair_digits[7] = "000000";
bool bt_svc_pair_pending(char digits[7]) { if (digits) strcpy(digits, s_pair ? s_pair_digits : "000000"); return s_pair; }
void bt_svc_pair_reply(bool accept) { s_pair = false; printf("SIM bt pair reply %d\n", accept ? 1 : 0); }
bool bt_svc_pair_check_timeout(void) { return false; }
int bt_svc_paired_count(void) { return s_mode == BT_MODE_OFF ? -1 : 2; }   /* the fake always holds two pairings */
int bt_svc_forget_all(void) { return s_mode == BT_MODE_OFF ? -1 : 2; }
void bt_svc_scan(void) { if (s_mode == BT_MODE_HEADPHONES) s_scanned = true; }
int bt_svc_devices(bt_dev_t *out, int max)
{
    if (s_mode != BT_MODE_HEADPHONES || !s_scanned) return 0;
    int n = max < 2 ? max : 2;
    memcpy(out, k_devs, (size_t)n * sizeof *out);
    return n;
}
void bt_svc_connect(const uint8_t bda[6])
{
    for (int i = 0; i < 2; i++) if (!memcmp(k_devs[i].bda, bda, 6)) snprintf(s_peer, sizeof s_peer, "%s", k_devs[i].name);
}
int bt_svc_ldac_kbps(int kbps) { (void)kbps; return -1; }
const char *bt_svc_codec(void) { return s_peer[0] ? "SBC" : ""; }
/* BTAUDIO: the fake sink's capabilities ("simbt caps ldac,aac,sbc|aac,sbc|sbc|none"), the build ("simbt build 0|1": LDAC stack or not), the live adaptive LDAC step ("simbt ldac <kbps>"),
 * Wi-Fi is the fake wifi's. bt_svc_apply_prefs prints the reconnect request the board would make. */
static unsigned s_caps = (1u << PC_BTC_LDAC) | (1u << PC_BTC_APTX_HD) | (1u << PC_BTC_APTX) | (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC);
static bool s_build_ldac = true;
static int s_live_kbps;
static int s_apply_n;
void bt_svc_out_in(pc_os_bt_in_t *in)
{
    in->connected = s_mode == BT_MODE_HEADPHONES && s_peer[0];
    in->build_ldac = s_build_ldac;
    in->sink_codecs = s_caps;
    for (int c = 0; c < PC_BTC_N; c++) in->sink_rate[c] = c == PC_BTC_LDAC ? 96000 : 48000;
    in->live_kbps = s_live_kbps;
    in->wifi_on = wifi_svc_on();
}
/* FIX4: the applied-preferences snapshot as bt_svc.c keeps it (taken at the first ask and at every apply), so the "next connection" line and the one-reconnect rule are the board's */
static struct { int codec, rate, bits, chan; bool set; } s_f;
static void f_take(void) { s_f.codec = cfg_int("bt_codec", PC_BT_PREF_LDAC); s_f.rate = cfg_int("bt_rate", 96000); s_f.bits = cfg_int("bt_bits", 24); s_f.chan = cfg_int("bt_chan", 0); s_f.set = true; }
static bool f_need(void)
{
    if (!s_f.set) f_take();
    return cfg_int("bt_codec", PC_BT_PREF_LDAC) != s_f.codec || (cfg_int("bt_rate", 96000) == 44100) != (s_f.rate == 44100) || cfg_int("bt_chan", 0) != s_f.chan;
}
void bt_svc_applied_prefs(pc_os_bt_in_t *in)
{
    if (!s_f.set) f_take();
    in->pref_codec = s_f.codec; in->pref_rate = (uint32_t)s_f.rate; in->pref_bits = s_f.bits; in->pref_chan = s_f.chan;
}
bool bt_svc_prefs_pending(void) { return s_mode == BT_MODE_HEADPHONES && f_need(); }
void bt_svc_apply_prefs(void)
{
    const bool need = f_need();
    f_take();
    if (need && s_mode == BT_MODE_HEADPHONES && s_peer[0]) { s_apply_n++; printf("APP bt apply prefs reconnect=%d\n", s_apply_n); }
}
static uint32_t s_sink_under;                             /* simunderrun sink (hires T8e): the sink paths' dry blocks */
uint32_t bt_svc_sink_underflows(void) { return s_sink_under; }
void fake_bt_sink_underflow(void) { s_sink_under++; }

/* the phone's stream (bt_svc.c "player ownership while the phone streams", same ops and order) */
static bool s_streaming;
static void title(char *out, size_t cap)
{
    if (!bt_meta_title(out, cap)) snprintf(out, cap, "%s %s", T(S_BLUETOOTH) /* 藍牙： */, s_peer[0] ? s_peer : T(S_PHONE) /* 手機 */);
}
static bool playing(void) { return s_streaming; }
static void stop_stream(void) { audio_svc_stop(false); s_streaming = false; player_ctl_release(); }
static now_ops_t s_ops;
static void forget(void) { s_streaming = false; }
static void on_event(const aud_event_t *e) { (void)e; }
static uint8_t s_hp[6];                                 /* BTVOL: the fake headset that is (or was last) the output */
static void fake_sink(const float *lr, int frames, uint32_t rate) { (void)lr; (void)frames; (void)rate; }
bool fake_bt_cmd(const char *arg)
{
    if (!strcmp(arg, "stream") && !s_streaming) {
        s_streaming = true;
        s_ops.title = title;
        s_ops.stop = stop_stream;
        s_ops.playing = playing;
        s_ops.src = NOW_SRC_BT;
        bt_meta_bind(&s_ops);
        player_ctl_take(&s_ops, forget, on_event);
        audio_svc_play_mixer(NULL, NULL, 44100, NULL, NULL);
        player_ctl_changed();
        printf("APP bt stream start rate=44100 gen=0\n");
        return true;
    }
    if (!strncmp(arg, "caps ", 5)) {                       /* BTAUDIO: the sink's codecs (ldac, aptxhd, aptx, aac, sbc; "none" = nothing reported) */
        static const char *const N[PC_BTC_N] = { "ldac", "aptxhd", "aptx", "aac", "sbc" };
        char t[64];
        snprintf(t, sizeof t, "%s", arg + 5);
        unsigned m = 0;
        for (char *tok = strtok(t, ","); tok; tok = strtok(NULL, ","))
            for (int c = 0; c < PC_BTC_N; c++) if (!strcmp(tok, N[c])) m |= 1u << c;
        s_caps = m;
        return true;
    }
    if (!strncmp(arg, "mode ", 5) && arg[5] >= '0' && arg[5] < '0' + BT_MODE_COUNT) { bt_svc_set_mode((bt_mode_t)(arg[5] - '0')); return true; }   /* BTAUDIO: the mode without the page (2 = headphones) */
    if (!strcmp(arg, "connect") && s_mode == BT_MODE_HEADPHONES) { s_scanned = true; bt_svc_connect(k_devs[0].bda); return true; }   /* BTAUDIO: the first fake headset connects (headphones mode) */
    if (!strncmp(arg, "build ", 6)) { s_build_ldac = arg[6] != '0'; return true; }
    if (!strncmp(arg, "ldac ", 5)) { s_live_kbps = atoi(arg + 5); return true; }
    if (!strncmp(arg, "pair ", 5) && strlen(arg + 5) == 6 && strspn(arg + 5, "0123456789") == 6) { strcpy(s_pair_digits, arg + 5); s_pair = true; return true; }
    if (!strcmp(arg, "pair off")) { s_pair = false; return true; }   /* the prompt ends elsewhere (the phone gave up, a second screen answered) */
    /* BTVOL: "simbt hp a|b": that fake headset is connected and its stream runs (the board: ui_hp_stream(1) -> vol_out_switch); "simbt hp off": the
     * stream stops / the headset goes (ui_hp_stream(0)); "simbt hpkey <n>": the headset's own volume key reports n (bt_remote.c follow());
     * "simbt hpforget a|b": unpaired (bt_svc_forget_all per device); "simbt cast 1|0": the Auracast broadcast takes / gives back the output */
    if (!strncmp(arg, "hp ", 3) && (!strcmp(arg + 3, "a") || !strcmp(arg + 3, "b"))) {
        const bt_dev_t *d = &k_devs[arg[3] - 'a'];
        s_mode = BT_MODE_HEADPHONES; s_scanned = true;
        snprintf(s_peer, sizeof s_peer, "%s", d->name);
        memcpy(s_hp, d->bda, 6);
        vol_out_switch(PC_VOUT_BT, s_hp, fake_sink);
        return true;
    }
    if (!strcmp(arg, "hp off")) { s_peer[0] = 0; vol_out_switch(PC_VOUT_WIRED, NULL, NULL); return true; }
    if (!strncmp(arg, "hpkey ", 6)) {
        const int c = vol_out_set_headset(s_hp, atoi(arg + 6));
        if (vol_out_is_bt()) { audio_svc_set_volume(c); if (ui_screen_on()) ui_volume_shown(c); }
        return true;
    }
    if (!strncmp(arg, "hpforget ", 9) && (!strcmp(arg + 9, "a") || !strcmp(arg + 9, "b"))) { vol_out_forget(k_devs[arg[9] - 'a'].bda); return true; }
    if (!strcmp(arg, "cast 1") || !strcmp(arg, "cast 0")) { vol_out_switch(arg[5] == '1' ? PC_VOUT_CAST : PC_VOUT_WIRED, NULL, arg[5] == '1' ? fake_sink : NULL); return true; }
    if (!strcmp(arg, "stop") && s_streaming) { stop_stream(); printf("APP bt stream stop\n"); return true; }
    return false;
}

/* the phone (AVRCP 1.6, speaker mode): a fake AVRCP target + cover-art server that answers the SAME core as the board (bt_meta.h, pc_bma_*).
 * Its answers are queued and handed over at the next tick (bt_meta's poll), as the board's events come from another task. */
#define PH_Q 256
static struct { pc_bma_ev_t ev; uint8_t *copy; } s_q[PH_Q];
static int s_qn;
static bool s_qlost;                     /* the fake's queue was full: the core is told (as the board's ring does) */
static struct {
    bool link, ca;                       /* the AVRCP link, the cover-art link */
    char title[160], artist[160], album[160];
    uint8_t *art; uint32_t art_len; unsigned hdl;   /* the track's cover and its handle number (a new one per picture) */
    bool hold;                           /* GETs are kept (simbtmeta hold 1) and answered by hold 0 */
    uint8_t *held; uint32_t held_len; bool held_ok;
} s_ph;

static void push(uint8_t kind, bool on, uint8_t id, const void *data, int len, bool final, int status)
{
    if (s_qn >= PH_Q) { s_qlost = true; return; }
    if (!s_qn) bt_meta_wake();                           /* as the board's glue: the tick sleeps while no request is open */
    uint8_t *c = NULL;
    if (data && len > 0 && (c = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM))) memcpy(c, data, (size_t)len);
    s_q[s_qn].ev = (pc_bma_ev_t){ .kind = kind, .on = on, .id = id, .data = c, .len = c ? len : 0, .final = final, .status = status };
    s_q[s_qn++].copy = c;
}
static void ph_poll(void)                /* what was queued before this tick (answers to what the core asks now come at the next one) */
{
    int n = s_qn;
    for (int i = 0; i < n; i++) { bt_meta_event(&s_q[i].ev); heap_caps_free(s_q[i].copy); }
    if (s_qlost) { s_qlost = false; pc_bma_ev_t l = { .kind = PC_BMA_LOST }; bt_meta_event(&l); }
    memmove(s_q, s_q + n, (size_t)(s_qn - n) * sizeof s_q[0]);
    s_qn -= n;
    if (s_qn) bt_meta_wake();                            /* answered during this tick: the board's answers would wake it the same way */
}
static void ph_caps(void) { push(PC_BMA_CAPS, true, 0, NULL, 0, false, 0); }
static void ph_reg(void) {}
static void hdl_str(char *b) { snprintf(b, 8, "%07u", s_ph.hdl % 10000000u); }
static void ph_ask(uint8_t mask)
{
    push(PC_BMA_ATTR, false, PC_BTMETA_TITLE, s_ph.title, (int)strlen(s_ph.title), false, 0);
    push(PC_BMA_ATTR, false, PC_BTMETA_ARTIST, s_ph.artist, (int)strlen(s_ph.artist), false, 0);
    push(PC_BMA_ATTR, false, PC_BTMETA_ALBUM, s_ph.album, (int)strlen(s_ph.album), false, 0);
    if ((mask & PC_BTMETA_ART) && s_ph.ca && s_ph.art) { char h[8]; hdl_str(h); push(PC_BMA_ATTR, false, PC_BTMETA_ART, h, 7, false, 0); }
}
static void ph_ca_connect(void) { s_ph.ca = true; push(PC_BMA_CA, true, 0, NULL, 0, false, 0); }
static void ph_ca_drop(void) { s_ph.ca = false; push(PC_BMA_CA, false, 0, NULL, 0, false, 0); }
static void send_picture(const uint8_t *b, uint32_t n, bool ok)
{
    if (!ok) { push(PC_BMA_DATA, false, 0, NULL, 0, true, 1); return; }
    if (!n) { push(PC_BMA_DATA, false, 0, NULL, 0, true, 0); return; }
    for (uint32_t o = 0, k; o < n; o += k) {             /* 1000-byte pieces, the last one final */
        k = n - o < 1000 ? n - o : 1000;
        push(PC_BMA_DATA, false, 0, b + o, (int)k, o + k >= n, 0);
    }
}
static void ph_get(const char *h)
{
    char cur[8];
    hdl_str(cur);
    bool ok = s_ph.ca && s_ph.art && !strcmp(h, cur);
    if (!s_ph.hold) { send_picture(s_ph.art, s_ph.art_len, ok); return; }
    heap_caps_free(s_ph.held);                           /* held: the picture as it was at the GET (late data for the core) */
    s_ph.held = NULL;
    s_ph.held_ok = ok && (s_ph.held = heap_caps_malloc(s_ph.art_len, MALLOC_CAP_SPIRAM));
    if (s_ph.held_ok) { memcpy(s_ph.held, s_ph.art, s_ph.art_len); s_ph.held_len = s_ph.art_len; }
}
static const bt_meta_phone_t k_phone = { .poll = ph_poll, .ask_caps = ph_caps, .reg_track = ph_reg, .ask_meta = ph_ask, .ca_connect = ph_ca_connect,
                                         .ca_drop = ph_ca_drop, .get = ph_get };
static void ph_link(bool on)
{
    static bool attached;
    if (!attached) { attached = true; bt_meta_attach(&k_phone); }
    s_ph.link = on;
    s_ph.ca = false;
    push(PC_BMA_LINK, on, 0, NULL, 0, false, 0);
    if (on) push(PC_BMA_FEAT, true, 0, NULL, 0, false, 0);   /* this phone's target has cover art */
}
static void ph_track(void) { if (!s_ph.link) ph_link(true); push(PC_BMA_TRACK, false, 0, NULL, 0, false, 0); }

bool fake_btmeta_cmd(const char *arg)
{
    const char *k[] = { "title", "artist", "album" };
    char *fld[] = { s_ph.title, s_ph.artist, s_ph.album };
    for (unsigned i = 0; i < 3; i++) {
        size_t n = strlen(k[i]);                         /* the word; the text after its space (none: an empty text, the console drops a trailing space) */
        if (strncmp(arg, k[i], n) || (arg[n] && arg[n] != ' ')) continue;
        snprintf(fld[i], sizeof s_ph.title, "%s", arg[n] ? arg + n + 1 : "");
        ph_track();
        return true;
    }
    if (!strcmp(arg, "clear")) {
        s_ph.title[0] = s_ph.artist[0] = s_ph.album[0] = 0;
        heap_caps_free(s_ph.art); s_ph.art = NULL; s_ph.art_len = 0;
        if (s_ph.link) push(PC_BMA_TRACK, false, 0, NULL, 0, false, 0);
        return true;
    }
    if (!strcmp(arg, "link 0") || !strcmp(arg, "link 1")) { ph_link(arg[5] == '1'); return true; }
    if (!strcmp(arg, "hold 1")) { s_ph.hold = true; return true; }
    if (!strcmp(arg, "hold 0")) {                        /* the kept GET is answered now */
        s_ph.hold = false;
        if (s_ph.held || s_ph.held_ok) send_picture(s_ph.held, s_ph.held_len, s_ph.held_ok);
        heap_caps_free(s_ph.held); s_ph.held = NULL; s_ph.held_ok = false;
        return true;
    }
    if (!strcmp(arg, "art -")) { heap_caps_free(s_ph.art); s_ph.art = NULL; s_ph.art_len = 0; ph_track(); return true; }
    if (strncmp(arg, "art ", 4)) return false;
    FILE *fp = fopen(arg + 4, "rb");
    if (!fp) return false;
    uint8_t *b = heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
    size_t n = b ? fread(b, 1, 64 * 1024, fp) : 0;
    fclose(fp);
    if (!n) { heap_caps_free(b); return false; }
    heap_caps_free(s_ph.art);
    s_ph.art = b;
    s_ph.art_len = (uint32_t)n;
    s_ph.hdl++;                                          /* a new picture, a new handle */
    ph_track();
    return true;
}

/* the broadcast-radio boot (cast_mode.c): the simulator's Auracast is "running" once the boot asked for it */
static bool s_lean_on;
void bt_svc_lean_begin(void) {}
bool bt_svc_lean(void) { return false; }
int bt_svc_lean_cast(void) { s_lean_on = true; return 0; }
bool bt_svc_lean_cast_on(void) { return s_lean_on; }
const char *bt_svc_cast_name(void) { return "JukeDial"; }
void bt_svc_test_bypass(bool on) { (void)on; }
bool bt_svc_wifi_ok(void) { return false; }
