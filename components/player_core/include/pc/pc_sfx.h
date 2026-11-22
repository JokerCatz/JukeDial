// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Toy sound effects (no sound files, no PCM): a tiny chiptune synth and its sound bank.
 *
 * Voices (the cheapest chips; no FM): NES pulse (duty 12.5/25/50/75 %, pitch sweep), NES triangle (4-bit stepped, 32 steps), noise (the NES 15-bit
 * LFSR in its long and short modes, the AY 17-bit one), the GB wave channel (32 x 4-bit tables defined in the bank), the AY tone (a square with the
 * AY's log volume steps), AY noise, the AY tone AND noise mixer, and the AY hardware envelope shapes. Every track has its own volume envelope
 * (attack, decay to a sustain level, release), pitch program (exponential and linear slides, a ZzFX-style jump and repeat, an arpeggio, vibrato).
 * Pitch and envelope run at a 1 ms control rate; only the phase, the wave and an amplitude ramp run per sample, at the block's rate up to 48 kHz
 * and at rate / ceil(rate / 48000) above it (88.2k, 96k: half; 176.4k, 192k: a quarter; interpolated: the cost stays about the 48 kHz cost). Pulse edges are band-limited with
 * a 2-sample polyBLEP; a one-pole low-pass after the sum is available (PC_SFX_BAND_*: measured in test_sfx, the default is the cheaper one that
 * removes most of the aliasing).
 *
 * The bank (pc_sfx_bank_t) is TEXT parsed once (pc_sfx_bank_parse; firmware: app/main/sfx_bank.def, into PSRAM). An entry is a one-shot recipe
 * (a ZzFX parameter list, or native notes with Hz and ms) or a jingle (up to PC_SFX_TRACKS tracks of MML: notes, tempo, per-note volume, gate),
 * each line naming its voice. A recipe needing anything the voices cannot do is refused at parse time (dropped, with the reason).
 *
 * Level: one entry peaks at most at PC_SFX_PEAK (-18 dBFS): the parser scales an entry whose tracks' loudest levels sum above 1 so that they sum
 * to 1 (every voice sample is within -1..1, envelopes within 0..1, so the entry's sum is within the peak; test_sfx measures it). Entries that
 * overlap are clamped to PC_SFX_CLAMP (-12 dBFS). Mixed into music the toy sound yields at full scale (PC_SFX_CEIL): it never pushes a sample
 * beyond +-1, nor beyond the music's own value when the music is already there, so it cannot make the music clip (Task 1 review m3).
 *
 * pc_sfx_mix ADDS the voices into a stereo block at any rate and block size; nothing is allocated, the state is pc_sfx_t (the caller places it:
 * PSRAM on the board). At most PC_SFX_VOICES tracks sound at once: a start that finds every voice busy takes the oldest one. Deterministic: the
 * same starts and blocks give the same samples.
 * The queue (pc_sfx_q_t) is the lock-free single-producer / single-consumer hand-off: the LVGL task posts ids with a time stamp, the audio task
 * takes them; a post older than PC_SFX_STALE_MS when it is taken is dropped, so a held-up audio task never starts a burst of late sounds (m5). */

#define PC_SFX_PEAK   0.125892541f   /* -18 dBFS: one entry's peak */
#define PC_SFX_CLAMP  0.251188643f   /* -12 dBFS: the sum of overlapping effects never goes beyond this */
#define PC_SFX_CEIL   1.0f           /* full scale: music + toy sound is held here (the toy sound yields) */
#define PC_SFX_VOICES 4              /* tracks sounding at once (the cap); more: the oldest is dropped */
#define PC_SFX_TRACKS 4              /* tracks of one entry */
#define PC_SFX_LAST   8              /* the newest started ids kept for the console dump */
#define PC_SFX_QLEN   16             /* power of two */
#define PC_SFX_STALE_MS 150          /* a post older than this when the audio task takes it is dropped */
#define PC_SFX_ENTRIES 56            /* HUD task: two entries added (timer_warn, timer_end) to a bank that was full at 48 */
#define PC_SFX_EVENTS  768           /* notes of every entry together */
#define PC_SFX_WAVES   8             /* GB wave tables */
#define PC_SFX_CREDITS 8
#define PC_SFX_NAME    16
#define PC_SFX_LABEL   32            /* UTF-8 bytes */
#define PC_SFX_CREDIT  160
#define PC_SFX_MAX_MS  4000          /* an entry's length */

enum { PC_SFX_PULSE, PC_SFX_TRI, PC_SFX_NOISE, PC_SFX_WAVE, PC_SFX_AYMIX };     /* voice kinds */
enum { PC_SFX_NZ_LONG, PC_SFX_NZ_SHORT, PC_SFX_NZ_AY };                          /* noise LFSRs: NES 15-bit tap 1 / tap 6, AY 17-bit tap 3 */
enum { PC_SFX_G_UI, PC_SFX_G_TOY, PC_SFX_G_GAME, PC_SFX_G_MORE, PC_SFX_GROUPS };  /* entry groups (the audition page's headers) */
enum { PC_SFX_BAND_NAIVE, PC_SFX_BAND_BLEP, PC_SFX_BAND_BLEP_LP };              /* band-limiting (test_sfx measures each) */

typedef struct {
    float hz;                        /* the note's frequency (noise: the LFSR clock) */
    float t_on, t_off;               /* ms from the entry's start: sounding from t_on, released (end of the release) at t_off */
    uint8_t vol;                     /* 0..15 */
} pc_sfx_ev_t;

typedef struct {
    uint8_t wave;                    /* PC_SFX_PULSE ... */
    uint8_t duty;                    /* pulse: the high part in 1/16 (2, 4, 8, 12 = 12.5, 25, 50, 75 %) */
    uint8_t sub;                     /* noise: PC_SFX_NZ_*; wave: the table index */
    uint8_t ay;                      /* 1: AY log volume steps, else linear (NES / GB) */
    uint8_t env;                     /* AY hardware envelope: 0 off, else shape + 1 (shapes 0..15) */
    uint8_t arp_n;                   /* arpeggio steps (0: off) */
    int8_t arp[4];                   /* semitones */
    float env_hz;                    /* AY envelope: cycles per second */
    float nz_hz;                     /* aymix: the noise clock (the tone is the note) */
    float att, dec, sus, rel;        /* ms, ms, level 0..1, ms (the release ends at the note's t_off) */
    float oct, lin, acc;             /* pitch: octaves / s, Hz / s, Hz / s^2 (from the note's start; ZzFX slide and delta slide) */
    float jump_hz, jump_ms, rep_ms;  /* ZzFX pitch jump (Hz added at jump_ms) and repeat (the pitch program restarts every rep_ms, jumps add up) */
    float arp_ms;                    /* one arpeggio step */
    float vib, vib_hz, vib_ms;       /* vibrato: depth (fraction of the pitch), rate, delay after the note's start */
    float level;                     /* 0..1 of PC_SFX_PEAK (after the entry's scaling) */
    uint16_t ev0, nev;               /* the track's notes in pc_sfx_bank_t.ev */
} pc_sfx_track_t;

typedef struct {
    char name[PC_SFX_NAME];          /* what sfx_play() takes */
    char zh[PC_SFX_LABEL], en[PC_SFX_LABEL];
    uint8_t group;                   /* PC_SFX_G_* */
    uint8_t credit;                  /* index into pc_sfx_bank_t.credit */
    uint8_t ntr;
    float len_ms;                    /* the latest t_off */
    pc_sfx_track_t tr[PC_SFX_TRACKS];
} pc_sfx_entry_t;

typedef struct {
    int n, nev, nwave, ncredit;
    int dropped;                     /* entries refused (the log callback says why) */
    pc_sfx_entry_t e[PC_SFX_ENTRIES];
    pc_sfx_ev_t ev[PC_SFX_EVENTS];
    float wave[PC_SFX_WAVES][32];    /* -1..1 */
    char wname[PC_SFX_WAVES][PC_SFX_NAME];
    char ckey[PC_SFX_CREDITS][PC_SFX_NAME];
    char credit[PC_SFX_CREDITS][PC_SFX_CREDIT];
} pc_sfx_bank_t;

/* parses the bank text (format: app/main/sfx_bank.def's head comment) into b (cleared first); returns the number of entries kept. Every refused
 * line or entry is reported through log (may be NULL) as "line N <name>: <why>". */
int pc_sfx_bank_parse(pc_sfx_bank_t *b, const char *text, void (*log)(const char *msg, void *u), void *u);

int pc_sfx_id(const pc_sfx_bank_t *b, const char *name);       /* entry id, -1 unknown (or no bank) */
const char *pc_sfx_name(const pc_sfx_bank_t *b, int id);       /* NULL: out of range */
int pc_sfx_count(const pc_sfx_bank_t *b);
uint32_t pc_sfx_len_ms(const pc_sfx_bank_t *b, int id);        /* the entry's length; 0 unknown */
const pc_sfx_entry_t *pc_sfx_entry(const pc_sfx_bank_t *b, int id);
const char *pc_sfx_group_key(int g);                           /* "ui" "toy" "game" "more"; NULL out of range */

typedef struct {
    const pc_sfx_track_t *tr;        /* NULL: free */
    const pc_sfx_ev_t *ev;           /* the current note */
    uint16_t left;                   /* notes after the current one */
    uint16_t ctl;                    /* samples to the next control tick */
    uint32_t pos;                    /* samples since the entry's start, at rate */
    uint32_t rate;
    uint32_t on_at, off_at;          /* the current note, in samples */
    uint32_t lfsr;
    uint32_t order;                  /* start order (the oldest is taken when all are busy) */
    float ph, inc, iinc;             /* 0..1, per sample, 1 / inc (the polyBLEP) */
    float nph, ninc;                 /* the AY mixer's noise clock */
    float amp, damp;                 /* amplitude now and its per-sample step to the next tick */
    float nz;                        /* noise: the current level */
    float eph, einc;                 /* AY envelope phase (cycles) and its per-sample step */
    float vamp;                      /* the note's volume as amplitude, times the track level */
} pc_sfx_voice_t;

typedef struct {
    const pc_sfx_bank_t *bank;
    pc_sfx_voice_t v[PC_SFX_VOICES];
    uint32_t order;
    uint32_t started;                /* entries started */
    uint32_t stolen;                 /* voices taken from a sounding track (the cap) */
    uint8_t last[PC_SFX_LAST];       /* the newest started ids, last[(started - 1) % PC_SFX_LAST] the newest */
    uint8_t band;                    /* PC_SFX_BAND_* */
    uint32_t lp_rate;
    float lp, lp_a;                  /* the one-pole after the sum */
    float prev, cur;                 /* the last two synth samples (above 48 kHz the synth runs at rate / m: interpolated between them) */
    uint32_t sub;                    /* output samples since cur */
    bool on;                         /* cur (or the ramp to it) sounds */
} pc_sfx_t;

#define PC_SFX_BAND_DEFAULT PC_SFX_BAND_BLEP

void pc_sfx_init(pc_sfx_t *s, const pc_sfx_bank_t *b);
bool pc_sfx_start(pc_sfx_t *s, int id);   /* false: unknown id; all voices busy: the oldest are taken */
bool pc_sfx_active(const pc_sfx_t *s);
/* adds the sounding voices into lr (frames interleaved stereo frames at rate); returns true when anything was added (the block changed) */
bool pc_sfx_mix(pc_sfx_t *s, float *lr, int frames, uint32_t rate);
bool pc_sfx_mix_gain(pc_sfx_t *s, float *lr, int frames, uint32_t rate, float g);   /* the same, every added sample times g (before the CEIL rule) */

/* Where a block's toy sound goes (the audio task). Not paused: before the block's gain (post false, gain 1), so it gets exactly the music's gain
 * (the sleep fade included). Paused (the block's gain target is 0, pc_rv_block): after the gain, scaled by the wired device-volume gain times the
 * sleep fade (device and fade each clamped to 0..1, NaN -> 0; m1): never above the device volume, never unity on its own. */
typedef struct { bool post; float gain; } pc_sfx_route_t;
pc_sfx_route_t pc_sfx_route(bool paused, float device, float fade);

typedef struct {
    uint8_t id[PC_SFX_QLEN];
    uint32_t ms[PC_SFX_QLEN];        /* when it was posted */
    uint32_t w, r;                   /* w: written by the producer only, r: by the consumer only */
} pc_sfx_q_t;

void pc_sfx_q_init(pc_sfx_q_t *q);
bool pc_sfx_q_post(pc_sfx_q_t *q, uint8_t id, uint32_t now_ms);   /* producer; false: full (dropped) */
int pc_sfx_q_take(pc_sfx_q_t *q, uint32_t now_ms, uint32_t *stale);   /* consumer; -1: empty; posts older than PC_SFX_STALE_MS are skipped (*stale counts them) */
