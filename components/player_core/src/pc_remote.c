// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_remote.h"
#include <string.h>

uint8_t pc_remote_abs_from_vol(int vol)
{
    if (vol <= 0) return 0;
    if (vol >= 100) return 127;
    return (uint8_t)((vol * 127 + 50) / 100);
}

int pc_remote_vol_from_abs(uint8_t abs, int cur)
{
    if (abs > 127) abs = 127;
    if (cur >= 0 && cur <= 100 && pc_remote_abs_from_vol(cur) == abs) return cur;
    return (abs * 100 + 63) / 127;
}

pc_remote_key_t pc_remote_key(uint8_t c)
{
    switch (c) {
    case 0x44: return PC_RK_PLAY;
    case 0x45: return PC_RK_STOP;
    case 0x46: return PC_RK_PAUSE;
    case 0x4B: return PC_RK_NEXT;
    case 0x4C: return PC_RK_PREV;
    default: return PC_RK_NONE;
    }
}

pc_remote_act_t pc_remote_action(pc_remote_key_t key, bool has_owner, bool playing, bool has_prev, bool alarm)
{
    if (alarm) return PC_RA_NONE;                 /* a ringing alarm: a headset never silences it (wear sensors send PAUSE) */
    if (!has_owner) return PC_RA_NONE;
    switch (key) {
    case PC_RK_PLAY:
    case PC_RK_PAUSE: return playing ? PC_RA_PAUSE : PC_RA_RESUME;
    case PC_RK_STOP: return PC_RA_STOP;
    case PC_RK_NEXT: return PC_RA_NEXT;
    case PC_RK_PREV: return has_prev ? PC_RA_PREV : PC_RA_NONE;
    default: return PC_RA_NONE;
    }
}

const char *pc_remote_key_name(pc_remote_key_t key)
{
    static const char *const n[] = { "none", "play", "pause", "stop", "next", "prev" };
    return (unsigned)key < sizeof n / sizeof n[0] ? n[key] : "none";
}

const char *pc_remote_act_name(pc_remote_act_t act)
{
    static const char *const n[] = { "none", "pause", "resume", "stop", "next", "prev" };
    return (unsigned)act < sizeof n / sizeof n[0] ? n[act] : "none";
}

bool pc_remote_press(pc_remote_press_t *p, pc_remote_key_t key, bool pressed, uint32_t now_ms)
{
    if (!pressed || key == PC_RK_NONE) return false;
    if (key == p->key && now_ms - p->at_ms < PC_REMOTE_REPEAT_MS) return false;
    p->key = key;
    p->at_ms = now_ms;
    return true;
}

void pc_rv_link(pc_rv_t *r, bool up)
{
    memset(r, 0, sizeof *r);
    r->st = up ? PC_RV_ASKING : PC_RV_OFF;
}

static int dist(int a, int b) { return a > b ? a - b : b - a; }

static bool send(pc_rv_t *r, uint8_t v, uint32_t now_ms, uint8_t *abs)
{
    r->busy = true;
    r->sent = v;
    r->far_seen = false;
    r->sent_v[r->sent_i] = v;                      /* remembered for the echo test */
    r->sent_t[r->sent_i] = now_ms;
    r->sent_i = (uint8_t)((r->sent_i + 1) % PC_RV_SENT_N);
    if (r->sent_n < PC_RV_SENT_N) r->sent_n++;
    *abs = v;
    return true;
}

/* the matching answer refreshes the time of the value in flight (the last one sent) */
static void answered(pc_rv_t *r, uint32_t now_ms) { r->sent_t[(r->sent_i + PC_RV_SENT_N - 1) % PC_RV_SENT_N] = now_ms; }

/* within one step of any value we sent, only those younger than max_ms (0: any age) */
static bool sent_near(const pc_rv_t *r, uint8_t abs, uint32_t now_ms, uint32_t max_ms)
{
    for (int i = 0; i < r->sent_n; i++)
        if (dist(abs, r->sent_v[i]) <= PC_RV_TOL && (!max_ms || now_ms - r->sent_t[i] < max_ms)) return true;
    return false;
}

static void far_answer(pc_rv_t *r, uint8_t ans)
{
    r->far_seen = true;
    r->far_ans = ans;
    if (r->far_n < UINT16_MAX) r->far_n++;
}

bool pc_rv_caps(pc_rv_t *r, bool vol_change, bool enabled, int cur, uint32_t now_ms, uint8_t *abs)
{
    if (r->st != PC_RV_ASKING) return false;
    if (!vol_change || !enabled) { r->st = PC_RV_NONE; return false; }
    r->st = PC_RV_TRYING;
    r->want = cur;
    return send(r, pc_remote_abs_from_vol(cur), now_ms, abs);
}

bool pc_rv_offer(pc_rv_t *r, int vol, uint32_t now_ms, uint8_t *abs)
{
    if (r->st != PC_RV_ON || vol == r->want) return false;
    r->want = vol;
    uint8_t v = pc_remote_abs_from_vol(vol);
    if (!r->busy) return send(r, v, now_ms, abs);
    r->has_next = v != r->sent;                    /* back at the value in flight: nothing more to send */
    r->next = v;
    return false;
}

static void stop(pc_rv_t *r)
{
    r->st = PC_RV_NONE;
    r->busy = r->has_next = r->has_kept = false;
    r->sent_n = r->sent_i = 0;                     /* far_seen / far_ans / far_n stay for the log and btrc */
}

pc_rv_done_t pc_rv_answer(pc_rv_t *r, bool ok, uint8_t ans, int cur, uint32_t now_ms, uint8_t *abs, int *vol)
{
    if (!r->busy || (r->st != PC_RV_TRYING && r->st != PC_RV_ON)) return PC_RV_IDLE;   /* stale */
    if (ok && r->st == PC_RV_TRYING) {             /* activation: never louder than asked */
        if (ans > r->sent + PC_RV_TOL) {
            if (sent_near(r, ans, now_ms, 0)) { far_answer(r, ans); return PC_RV_IDLE; }   /* an earlier copy's late answer: the wait goes on */
            r->ans = ans;
            stop(r);
            return PC_RV_MISMATCH;
        }
        r->ans = ans;
        answered(r, now_ms);
        r->st = PC_RV_ON;
        r->busy = false;
        r->fails = 0;
        return PC_RV_BECAME_ON;
    }
    if (ok) {
        if (dist(ans, r->sent) > PC_RV_TOL) { far_answer(r, ans); return PC_RV_IDLE; }   /* not the answer to the command in flight: it stays in flight */
        r->ans = ans;
        r->fails = 0;
        answered(r, now_ms);
        if (r->has_next) { r->has_next = r->has_kept = false; send(r, r->next, now_ms, abs); return PC_RV_SEND; }   /* the device moved: it wins */
        r->busy = false;
        if (r->has_kept) {                         /* not an echo (decided on arrival) */
            r->has_kept = false;
            int v = pc_remote_vol_from_abs(r->kept, r->want);
            if (v == r->want) return PC_RV_IDLE;
            r->want = v;
            *vol = v;
            return PC_RV_HEARD;                    /* what goes back depends on the device's clamp: pc_rv_heard_back */
        }
        return PC_RV_IDLE;
    }
    if (++r->fails >= PC_REMOTE_FAILS) { stop(r); return PC_RV_GAVE_UP; }
    if (r->has_next) { r->has_next = r->has_kept = false; send(r, r->next, now_ms, abs); }   /* the device's newer value wins */
    else if (r->st == PC_RV_TRYING) { r->want = cur; send(r, pc_remote_abs_from_vol(cur), now_ms, abs); }   /* the last value offered */
    else send(r, r->sent, now_ms, abs);            /* no answer: the same value again */
    return PC_RV_SEND;
}

bool pc_rv_heard(pc_rv_t *r, uint8_t abs, uint32_t now_ms, int *vol)
{
    if (r->st != PC_RV_ON) return false;
    if ((r->busy && dist(abs, r->sent) <= PC_RV_TOL) || sent_near(r, abs, now_ms, PC_RV_ECHO_MS)) return false;   /* our echo: dropped on arrival */
    if (r->busy || r->has_next) { r->kept = abs; r->has_kept = true; return false; }   /* a key during our command: applied at its answer */
    int v = pc_remote_vol_from_abs(abs, r->want);
    if (v == r->want) return false;
    r->want = v;
    *vol = v;
    return true;
}

bool pc_rv_heard_back(pc_rv_t *r, int dev, uint32_t now_ms, uint8_t *abs)
{
    if (r->st != PC_RV_ON || r->busy) return false;
    if (dev == r->want) return send(r, r->kept, now_ms, abs);   /* taken as it is: the report itself, raw */
    r->want = dev;                                 /* clamped: only the device's value */
    return send(r, pc_remote_abs_from_vol(dev), now_ms, abs);
}

void pc_rv_leave(pc_rv_t *r) { stop(r); }

bool pc_rv_unity(const pc_rv_t *r) { return r->st == PC_RV_ON; }

pc_rv_block_t pc_rv_block(bool sink, bool remote, bool paused, bool unity_prev, float gain, float device)
{
    pc_rv_block_t b;
    b.unity = sink && remote;                      /* only a block that goes to a sink: the headset does the volume */
    b.start = !sink && unity_prev ? 0.0f : gain;   /* leaving unity: an I2S block ramps up from silence, never down from full scale */
    b.target = paused ? 0.0f : b.unity ? 1.0f : device;
    return b;
}
