// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Bluetooth headphones remote (AVRCP, bt-remote task 1): the headset's keys and one shared volume through absolute
 * volume. Pure decisions; the stack glue (app/main/bt_remote.c) only moves events in and commands out. */

/* ---- volume mapping: device 0..100 <-> AVRCP absolute volume 0..127 ---- */
uint8_t pc_remote_abs_from_vol(int vol);           /* linear on position, rounded; 0 <-> 0, 100 <-> 127; out of range clamps */
int pc_remote_vol_from_abs(uint8_t abs, int cur);  /* cur when abs_from_vol(cur) == abs (a value we sent comes back unchanged) */

/* ---- keys ---- */
typedef enum { PC_RK_NONE, PC_RK_PLAY, PC_RK_PAUSE, PC_RK_STOP, PC_RK_NEXT, PC_RK_PREV } pc_remote_key_t;
typedef enum { PC_RA_NONE, PC_RA_PAUSE, PC_RA_RESUME, PC_RA_STOP, PC_RA_NEXT, PC_RA_PREV } pc_remote_act_t;
#define PC_REMOTE_REPEAT_MS 300                     /* the same key again within this of the last one taken: a held key repeating */
pc_remote_key_t pc_remote_key(uint8_t avrc_code);   /* AVRCP passthrough code (PLAY 0x44, STOP 0x45, PAUSE 0x46, FORWARD 0x4B, BACKWARD 0x4C); others NONE */
/* PLAY while playing pauses, PAUSE while paused resumes (headsets differ in what they send); no owner: NONE; PREV without a prev: NONE;
 * a ringing alarm owns playback: NONE (headsets with wear sensors send PAUSE by themselves when taken off) */
pc_remote_act_t pc_remote_action(pc_remote_key_t key, bool has_owner, bool playing, bool has_prev, bool alarm);
const char *pc_remote_key_name(pc_remote_key_t key);   /* play|pause|stop|next|prev|none */
const char *pc_remote_act_name(pc_remote_act_t act);   /* pause|resume|stop|next|prev|none */
typedef struct { pc_remote_key_t key; uint32_t at_ms; } pc_remote_press_t;   /* zero-initialise */
/* true: handle this key. A release is never handled; a press of the key taken last, less than PC_REMOTE_REPEAT_MS after it, is not. */
bool pc_remote_press(pc_remote_press_t *p, pc_remote_key_t key, bool pressed, uint32_t now_ms);

/* ---- absolute volume: one command at a time, the newest value wins, three failures in a row end it ----
 * OFF: no AVRCP link. ASKING: link up, the notification capabilities asked. TRYING: the device's volume sent, no answer yet.
 * ON: remote volume active (the Bluetooth path at unity, every change goes to the headset). NONE: not on this link (no support,
 * setting off, failures). A link going down or up again starts at OFF / ASKING. */
#define PC_REMOTE_ANSWER_MS 2000
#define PC_REMOTE_FAILS 3
#define PC_RV_TOL 8                /* of 127: one step of a 16-step headset. An answer this close to the value sent is its answer */
#define PC_RV_ECHO_MS 1000         /* a VOLUME_CHANGE within PC_RV_TOL of any value we sent (or had answered) this recently: our echo */
#define PC_RV_SENT_N 8             /* the values sent remembered for that (a small ring) */
typedef enum { PC_RV_OFF, PC_RV_ASKING, PC_RV_TRYING, PC_RV_ON, PC_RV_NONE } pc_rv_state_t;
typedef struct {
    uint8_t st;            /* pc_rv_state_t */
    bool busy;             /* a command waits for its answer: `sent` */
    bool has_next;         /* a newer device value waits for that answer: `next` */
    bool has_kept;         /* a headset report arrived while busy: `kept` (applied at the answer unless the device moved meanwhile) */
    uint8_t sent, next, kept, fails;
    uint8_t ans;           /* the last answered value (what the headset says it set) */
    int want;              /* the device volume the headset is at once everything sent is answered; -1: resend whatever is offered next */
    uint8_t sent_v[PC_RV_SENT_N];   /* the last values sent, oldest overwritten first */
    uint32_t sent_t[PC_RV_SENT_N];  /* when each was sent (refreshed by its matching answer) */
    uint8_t sent_n, sent_i;         /* entries used, next slot */
    bool far_seen;         /* the wait of the command in flight saw an answer far from it (a give-up then says reason=far) */
    uint8_t far_ans;       /* that answer */
    uint16_t far_n;        /* far or stale answers on this link (btrc) */
} pc_rv_t;                 /* zero = OFF */
typedef enum { PC_RV_IDLE, PC_RV_SEND, PC_RV_BECAME_ON, PC_RV_GAVE_UP, PC_RV_MISMATCH, PC_RV_HEARD } pc_rv_done_t;
void pc_rv_link(pc_rv_t *r, bool up);              /* every link change: state reset, OFF or ASKING */
/* the capabilities arrived (vol_change: VOLUME_CHANGE listed) or did not in time (false). true: send *abs (for cur, the last
 * value offered to the remote path) */
bool pc_rv_caps(pc_rv_t *r, bool vol_change, bool enabled, int cur, uint32_t now_ms, uint8_t *abs);
/* the device's volume changed. true: send *abs now; otherwise it waits (newest only) or the headset has it already */
bool pc_rv_offer(pc_rv_t *r, int vol, uint32_t now_ms, uint8_t *abs);
/* the answer arrived (ok, ans = the volume the headset says it set) or PC_REMOTE_ANSWER_MS passed (!ok); cur = the last value
 * offered to the remote path. An answer further than PC_RV_TOL from the value in flight is stale or not applied: IDLE with the
 * command still in flight (busy; the wait goes on and ends as a failure).
 * SEND: send *abs. BECAME_ON: remote volume starts (the first answer, not above sent + PC_RV_TOL; offer cur next).
 * During activation an answer above sent + PC_RV_TOL that matches an earlier copy we sent is that copy's late answer: ignored.
 * MISMATCH: the first answer is louder than asked and matches nothing we sent: no remote volume on this link. GAVE_UP:
 * PC_REMOTE_FAILS in a row, remote volume ends for this link (far_seen: the last wait saw a far answer). HEARD: a headset report
 * kept while busy: move the device to *vol, then call pc_rv_heard_back with the device's value after its clamp. */
pc_rv_done_t pc_rv_answer(pc_rv_t *r, bool ok, uint8_t ans, int cur, uint32_t now_ms, uint8_t *abs, int *vol);
/* after HEARD, once the device moved (dev: its volume after volume_max): true, send *abs: the headset's own report when the device
 * took it as it is (a coarse headset is not moved by rounding), the clamped value's abs when the device clamped it. One send. */
bool pc_rv_heard_back(pc_rv_t *r, int dev, uint32_t now_ms, uint8_t *abs);
/* the headset's VOLUME_CHANGE. true: move the device's volume to *vol. Only when ON. Decided on arrival: our echo (within PC_RV_TOL
 * of the value in flight, or of any value sent less than PC_RV_ECHO_MS ago) is dropped at once, busy or not; any other report is
 * kept while a command is in flight (see pc_rv_answer); a value that maps to the one the headset already stands for: nothing */
bool pc_rv_heard(pc_rv_t *r, uint8_t abs, uint32_t now_ms, int *vol);
void pc_rv_leave(pc_rv_t *r);                      /* -> NONE for this link (setting switched off) */
bool pc_rv_unity(const pc_rv_t *r);                /* ON */
/* The audio task's per-block gain (audio_svc.c, safety: no block to I2S at unity). sink: this block goes to a sink (the same
 * snapshot selects the output branch); remote: the remote-volume flag; unity_prev: the last block was at unity; gain: where the
 * last block's ramp ended; device: the device's volume gain. A block without a sink is never at unity, and after a unity block it
 * ramps up from 0. Pure, no allocation, no lock. */
typedef struct { float target, start; bool unity; } pc_rv_block_t;
pc_rv_block_t pc_rv_block(bool sink, bool remote, bool paused, bool unity_prev, float gain, float device);
#ifdef __cplusplus
}
#endif
