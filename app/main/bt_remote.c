// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth headphones remote (bt-remote task 1, headphones mode only): AVRCP target for the headset's keys, AVRCP
 * controller for one shared volume through absolute volume. Every decision is in pc_remote (host-tested); this file
 * moves events in and commands out.
 * Tasks: the Bluedroid callbacks (BTC task) copy a word and post to the LVGL task (ui_async); the state (s_rv), every
 * esp_avrc_ct_send_* call and the answer timeout (an LVGL timer) are LVGL-task only. ui_async runs pending calls newest
 * first, so a value that must end at the newest (the volume) travels in a variable and the posted call only says "look".
 * Safety (full scale must never reach the 3.5 mm out): audio_svc applies unity only to blocks that go to a sink, and its
 * gain target is always the device's volume (audio_svc.c, s_remote). */
#include "bt_remote.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_AVRCP_ENABLED
#include "audio_svc.h"
#include "cfg.h"
#include "clock_svc.h"
#include "esp_attr.h"
#include "esp_avrc_api.h"
#include "esp_timer.h"
#include "pc/pc_remote.h"
#include "player_ctl.h"
#include "ui/ui.h"
#include "vol_out.h"

#define TL_CAPS 0                       /* transaction labels: one per kind, at most one of each in flight */
#define TL_NOTE 1
#define TL_SET 2

static pc_rv_t s_rv;                    /* LVGL task */
static lv_timer_t *s_wait;              /* LVGL task: PC_REMOTE_ANSWER_MS for the caps or the volume command in flight */
static bool s_noted;                    /* LVGL task: VOLUME_CHANGE registered on this link (re-registered after each one) */
static uint32_t s_link_seen;            /* LVGL task: the s_link_gen applied */
static pc_remote_press_t s_press;       /* BTC task (and the console's btkey on the LVGL task: a test aid) */
static bool s_ct, s_tg;                 /* mode task: what was initialised */
/* s_up, s_link, s_link_gen: written by the mode and BTC tasks, read by the LVGL task: only through LD / ST / GEN_BUMP (seq_cst atomics) */
static bool s_up;                       /* headphones mode has AVRCP (the controller half) */
static bool s_link;                     /* the controller link (BTC task) */
static uint32_t s_link_gen;             /* bumped with every change of s_link / s_up */
#define LD(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)
#define ST(x, v) __atomic_store_n(&(x), (v), __ATOMIC_SEQ_CST)
#define GEN_BUMP() ((void)__atomic_fetch_add(&s_link_gen, 1u, __ATOMIC_SEQ_CST))
static volatile uint32_t s_feat;        /* the headset's AVRC feature mask, and its target feature flags (CT remote features) */
static volatile uint16_t s_tg_flag;
static volatile int s_vol_req;          /* the newest value audio_svc_set_volume handed over (any task) */
static volatile uint8_t s_abs;          /* the last absolute volume sent or heard (btrc) */
static EXT_RAM_BSS_ATTR uint8_t s_rc_bda[6];   /* BTVOL: the headset of the controller link (BTC task writes at the connection, LVGL reads): whose volume is offered and stored */

static void ui_sync(void *arg);
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void ui_vol(void *arg);

/* ---------- LVGL task ---------- */
static void wait_stop(void) { if (s_wait) lv_timer_pause(s_wait); }
static void wait_cb(lv_timer_t *t);
static void wait_start(void)
{
    if (!s_wait) s_wait = lv_timer_create(wait_cb, PC_REMOTE_ANSWER_MS, NULL);
    if (!s_wait) return;
    lv_timer_reset(s_wait);
    lv_timer_resume(s_wait);
}

static void send_abs(uint8_t abs)
{
    s_abs = abs;
    esp_err_t e = esp_avrc_ct_send_set_absolute_volume_cmd(TL_SET, abs);
    if (e != ESP_OK) printf("APP bt hp absvol set %u rc=%s\n", abs, esp_err_to_name(e));   /* the timeout counts it as a failure */
    wait_start();
}

/* BTVOL: the headset's volume as the link should have it: the value the audio path was last given while the headset is the output (an alarm
 * ramp's step), else (the stream has not started, or it is suspended and the wired output plays) the headset's own stored value or the default */
static int hs_vol(void) { return vol_out_is_bt() ? audio_svc_volume_last() : vol_out_headset(s_rc_bda); }

static void remote_vol(int vol)         /* any task: audio_svc_set_volume while remote volume is on */
{
    if (!vol_out_is_bt()) return;       /* BTVOL: a value of another output (the wired volume while the stream is suspended) never becomes the headset's volume */
    s_vol_req = vol;
    ui_async(ui_vol, NULL);
}

static void off(const char *why)        /* remote volume ends: software gain again (audio_svc keeps it at the device's volume) */
{
    bool was = pc_rv_unity(&s_rv);
    audio_svc_set_remote_volume(NULL);
    wait_stop();
    if (was) printf("APP bt hp absvol off reason=%s\n", why);
}

static void offer(int vol)
{
    uint8_t abs;
    if (pc_rv_offer(&s_rv, vol, now_ms(), &abs)) send_abs(abs);
}

static bool live(void) { return LD(s_up) && LD(s_link) && LD(s_link_gen) == s_link_seen; }   /* the link the state belongs to is the current one */

/* the headset moved its volume (a key): the device follows (volume_max clamps; a clamped value goes back through remote_vol) */
static void follow(uint8_t abs, int v, bool back)
{
    s_abs = abs;
    int c = vol_out_set_headset(s_rc_bda, v);             /* BTVOL: stored for this headset, never for the wired output */
    uint8_t b;
    if (back && pc_rv_heard_back(&s_rv, c, now_ms(), &b)) send_abs(b);   /* kept during a command: the report back raw, or only the clamped value */
    printf("APP bt hp absvol remote %u -> vol=%d\n", abs, c);
    if (!vol_out_is_bt()) return;                         /* the wired output plays (stream suspended): its volume is not this one */
    audio_svc_set_volume(c);                              /* the gain target, and through remote_vol the value back when needed */
    if (ui_screen_on()) ui_volume_shown(c);               /* never wakes the screen */
}

/* a link change the BTC task saw (or the mode going): every LVGL handler starts here. true: the link changed (state reset) */
static bool sync(void)
{
    uint32_t g = LD(s_link_gen);
    if (g == s_link_seen) return false;
    s_link_seen = g;
    bool su = LD(s_up), sl = LD(s_link);
    bool up = su && sl;
    off(!su ? "mode" : !sl ? "disconnect" : "relink");
    pc_rv_link(&s_rv, up);
    s_noted = false;
    if (up) {
        esp_err_t e = esp_avrc_ct_send_get_rn_capabilities_cmd(TL_CAPS);
        if (e != ESP_OK) printf("APP bt hp avrc caps rc=%s\n", esp_err_to_name(e));
        wait_start();
    }
    return true;
}
static void ui_sync(void *arg) { (void)arg; sync(); }
static void ui_vol(void *arg) { (void)arg; sync(); offer(s_vol_req); }   /* the newest value, however many posts are pending */

static void caps_done(bool answered, bool vol_change)
{
    if (s_rv.st != PC_RV_ASKING) return;                 /* a late answer after the timeout */
    wait_stop();
    bool en = cfg_int("hp_absvol", 1) != 0;
    uint8_t abs;
    bool go = pc_rv_caps(&s_rv, vol_change, en, hs_vol(), now_ms(), &abs);   /* BTVOL: this headset's own value (the last value forwarded, an alarm ramp's step, once it is the output) */
    printf("APP bt hp avrc up feat=%lx absvol=%d tg=%x caps=%d vc=%d\n", (unsigned long)s_feat, go, (unsigned)s_tg_flag, answered, vol_change);
    if (vol_change && en) {
        esp_avrc_ct_send_register_notification_cmd(TL_NOTE, ESP_AVRC_RN_VOLUME_CHANGE, 0);
        s_noted = true;
    }
    if (go) send_abs(abs);
}
static void ui_caps(void *arg) { sync(); uintptr_t a = (uintptr_t)arg; caps_done(true, a & 2); }

static void done(bool ok, uint8_t ans)
{
    bool was_busy = s_rv.busy;
    uint8_t abs = 0, sent = s_rv.sent;
    int v = 0;
    switch (pc_rv_answer(&s_rv, ok, ans, hs_vol(), now_ms(), &abs, &v)) {
    case PC_RV_SEND: send_abs(abs); break;
    case PC_RV_BECAME_ON:
        wait_stop();
        audio_svc_set_remote_volume(remote_vol);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);          /* the store before the check; bt_remote_stop: s_up before its NULL store (two cores) */
        if (!live()) { audio_svc_set_remote_volume(NULL); break; }   /* the link went meanwhile: undo (the posted ui_sync resets the state) */
        s_abs = ans;
        printf("APP bt hp absvol on vol=%d abs=%u ans=%u\n", s_rv.want, sent, ans);   /* ans below sent: the headset is quieter than asked (the safe side) */
        offer(hs_vol());                                  /* the volume may have moved while the first one was out */
        break;
    case PC_RV_MISMATCH:
        wait_stop();
        printf("APP bt hp absvol off reason=mismatch sent=%u ans=%u\n", sent, ans);   /* louder than asked: software gain on this link */
        break;
    case PC_RV_GAVE_UP:
        audio_svc_set_remote_volume(NULL);
        wait_stop();
        if (s_rv.far_seen) printf("APP bt hp absvol off reason=far sent=%u ans=%u\n", sent, s_rv.far_ans);   /* it answered, but not what we sent */
        else printf("APP bt hp absvol off reason=noanswer\n");
        break;
    case PC_RV_HEARD:
        follow(s_rv.kept, v, true);                       /* a headset key during our command; one value goes back */
        break;
    default:
        if (was_busy && !s_rv.busy) wait_stop();          /* a stale or far answer leaves the command in flight and its wait running */
        break;
    }
}
static void ui_answer(void *arg) { sync(); done(true, (uint8_t)(uintptr_t)arg); }

static void wait_cb(lv_timer_t *t)
{
    lv_timer_pause(t);
    if (sync()) return;                                   /* the link changed: the wait was for the old one (sync started a new one) */
    if (s_rv.st == PC_RV_ASKING) caps_done(false, false);   /* no capabilities in time: no absolute volume on this link */
    else done(false, 0);
}

static void ui_heard(void *arg)
{
    sync();
    uint8_t abs = (uint8_t)(uintptr_t)arg;
    if (s_noted && LD(s_link)) esp_avrc_ct_send_register_notification_cmd(TL_NOTE, ESP_AVRC_RN_VOLUME_CHANGE, 0);   /* one-shot: again */
    int v;
    if (pc_rv_heard(&s_rv, abs, now_ms(), &v)) follow(abs, v, false);
}

static void ui_key(void *arg)
{
    pc_remote_key_t k = (pc_remote_key_t)(uintptr_t)arg;
    const now_ops_t *o = player_ctl_now();
    bool alarm = clock_ringing();                         /* an alarm is never silenced from the headset (wear sensors send PAUSE) */
    pc_remote_act_t a = pc_remote_action(k, o != NULL, !audio_svc_paused(), o && o->prev, alarm);
    switch (a) {
    case PC_RA_PAUSE: audio_svc_pause(true); break;      /* the playing page's own pause (now_menu.c, the centre) */
    case PC_RA_RESUME: audio_svc_pause(false); break;
    case PC_RA_STOP: if (o->stop) o->stop(); else a = PC_RA_NONE; break;
    case PC_RA_NEXT: if (o->next) o->next(); else a = PC_RA_NONE; break;
    case PC_RA_PREV: o->prev(); break;
    default: break;
    }
    printf("APP bt hp key=%s act=%s%s\n", pc_remote_key_name(k), pc_remote_act_name(a), alarm ? " why=alarm" : "");
}

/* ---------- BTC task: copy, post ---------- */
void bt_remote_key(uint8_t code, bool pressed)
{
    pc_remote_key_t k = pc_remote_key(code);
    if (!pc_remote_press(&s_press, k, pressed, (uint32_t)(esp_timer_get_time() / 1000))) return;
    ui_async(ui_key, (void *)(uintptr_t)k);              /* no ui_activity: a key never wakes the screen */
}

void bt_remote_heard(uint8_t abs) { ui_async(ui_heard, (void *)(uintptr_t)abs); }

static void ct_cb(esp_avrc_ct_cb_event_t ev, esp_avrc_ct_cb_param_t *p)
{
    switch (ev) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        s_feat = 0;                                    /* this headset's words come with its features event */
        s_tg_flag = 0;
        if (p->conn_stat.connected) memcpy(s_rc_bda, p->conn_stat.remote_bda, 6);   /* before the link is published */
        ST(s_link, p->conn_stat.connected);
        GEN_BUMP();
        printf("APP bt hp avrc link=%d\n", p->conn_stat.connected);
        ui_async(ui_sync, NULL);
        break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        s_feat = p->rmt_feats.feat_mask;
        s_tg_flag = p->rmt_feats.tg_feat_flag;
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        esp_avrc_rn_evt_cap_mask_t m = p->get_rn_caps_rsp.evt_set;
        bool vc = esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &m, ESP_AVRC_RN_VOLUME_CHANGE);
        ui_async(ui_caps, (void *)(uintptr_t)(1 | (vc ? 2 : 0)));
        break;
    }
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:      /* the stack reports only an accepted command (a reject times out) */
        ui_async(ui_answer, (void *)(uintptr_t)(p->set_volume_rsp.volume & 0x7F));   /* bit 7 is reserved */
        break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:                /* CHANGED only (the stack drops the INTERIM one) */
        if (p->change_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) bt_remote_heard(p->change_ntf.event_parameter.volume & 0x7F);
        break;
    case ESP_AVRC_CT_PROF_STATE_EVT:
        if (p->avrc_ct_init_stat.state != ESP_AVRC_INIT_SUCCESS && p->avrc_ct_init_stat.state != ESP_AVRC_DEINIT_SUCCESS)
            printf("APP bt hp avrc init failed ct_state=%d\n", p->avrc_ct_init_stat.state);
        break;
    default:
        break;
    }
}

static void tg_cb(esp_avrc_tg_cb_event_t ev, esp_avrc_tg_cb_param_t *p)
{
    if (ev == ESP_AVRC_TG_PASSTHROUGH_CMD_EVT) bt_remote_key(p->psth_cmd.key_code, p->psth_cmd.key_state == ESP_AVRC_PT_CMD_STATE_PRESSED);
    else if (ev == ESP_AVRC_TG_PROF_STATE_EVT && p->avrc_tg_init_stat.state != ESP_AVRC_INIT_SUCCESS &&
             p->avrc_tg_init_stat.state != ESP_AVRC_DEINIT_SUCCESS)
        printf("APP bt hp avrc init failed tg_state=%d\n", p->avrc_tg_init_stat.state);
}

/* ---------- mode task ---------- */
void bt_remote_start(void)
{
    esp_err_t e;
    s_press = (pc_remote_press_t){ 0 };
    /* the controller half (volume) and the target half (keys) come up on their own: one failing leaves the other */
    if ((e = esp_avrc_ct_register_callback(ct_cb)) != ESP_OK || (e = esp_avrc_ct_init()) != ESP_OK)
        printf("APP bt hp avrc init failed ct %s\n", esp_err_to_name(e));   /* no volume sync */
    else s_ct = true;
    if ((e = esp_avrc_tg_register_callback(tg_cb)) != ESP_OK || (e = esp_avrc_tg_init()) != ESP_OK) {
        printf("APP bt hp avrc init failed tg %s\n", esp_err_to_name(e));   /* no keys */
    } else {
        s_tg = true;
        static const esp_avrc_pt_cmd_t keys[] = { ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_FORWARD,
                                                  ESP_AVRC_PT_CMD_BACKWARD };
        esp_avrc_psth_bit_mask_t m = { 0 };
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &m, keys[i]);
        if ((e = esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &m)) != ESP_OK)   /* only these are answered ACCEPT */
            printf("APP bt hp avrc init failed tg filter %s\n", esp_err_to_name(e));
    }
    ST(s_up, s_ct);                                      /* the volume part needs the controller only */
    GEN_BUMP();
}

void bt_remote_stop(void)
{
    ST(s_up, false);                                     /* first: a done() on the LVGL task that stores remote_vol after this sees it and undoes */
    ST(s_link, false);
    GEN_BUMP();
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    audio_svc_set_remote_volume(NULL);                   /* at once, from this task: one pointer store (the LVGL side follows in sync()) */
    ui_async(ui_sync, NULL);
    if (s_tg) esp_avrc_tg_deinit();                      /* before the A2DP deinit (esp_avrc_api.h); queued in order on the BTC task */
    if (s_ct) esp_avrc_ct_deinit();
    s_tg = s_ct = false;
}

void bt_remote_print(void)
{
    printf("APP bt hp rc avrc=%d absvol=%d abs=%u ans=%u pending=%d fails=%u far=%u\n", LD(s_up) && LD(s_link), pc_rv_unity(&s_rv), (unsigned)s_abs,
           (unsigned)s_rv.ans, s_rv.busy || s_rv.has_next, (unsigned)s_rv.fails, (unsigned)s_rv.far_n);
}

#else
void bt_remote_start(void) {}
void bt_remote_stop(void) {}
void bt_remote_key(uint8_t avrc_code, bool pressed) { (void)avrc_code; (void)pressed; }
void bt_remote_heard(uint8_t abs) { (void)abs; }
void bt_remote_print(void) { printf("APP bt hp rc avrc=0 absvol=0 abs=0 ans=0 pending=0 fails=0 far=0\n"); }
#endif
