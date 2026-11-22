// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T5: ext_audio.h. Statics in PSRAM (this file is built with -msmall-data-limit=0). */
#include "ext_audio.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "audio_svc.h"
#include "cfg.h"
#include "pc/pc_extpath.h"
#include "pc/pc_rate.h"
#include "pc/pc_volume.h"

#define MIXER_CHECK_US 400000       /* after the mixer started: has the DAC taken the clock? (audio_svc_info().hw_cap) */

EXT_RAM_BSS_ATTR static pc_extstream_t s_stream;
EXT_RAM_BSS_ATTR static pc_extuac_t s_uac;
EXT_RAM_BSS_ATTR static pc_extpath_t *s_path;
EXT_RAM_BSS_ATTR static pc_extfb_t s_fb;
EXT_RAM_BSS_ATTR static uint8_t s_alt_bits[PC_EXTDESC_MAX_ALTS + 1];
EXT_RAM_BSS_ATTR static volatile int s_ev_alt;              /* the pending event (any task writes, the pump takes) */
EXT_RAM_BSS_ATTR static volatile uint32_t s_ev_rate;
EXT_RAM_BSS_ATTR static volatile uint32_t s_ev_seq, s_ev_done;
EXT_RAM_BSS_ATTR static int s_alt;                          /* applied (pump only) */
EXT_RAM_BSS_ATTR static uint32_t s_rate, s_mix_rate;
EXT_RAM_BSS_ATTR static int64_t s_mix_at;                   /* esp_timer us when the mixer was started; 0 = the clock was already checked */
EXT_RAM_BSS_ATTR static volatile bool s_refused;
EXT_RAM_BSS_ATTR static uint32_t s_bad_rate[PC_EXTDESC_MAX_RATES];   /* rates the DAC clock refused this boot: silent until the next boot (the descriptor drops them next time) */
EXT_RAM_BSS_ATTR static bool s_started;
EXT_RAM_BSS_ATTR static uint32_t s_pend_gen, s_pend_rate;   /* a rate change in flight: the new format waits until the restarted mixer is the audio task's current source (0 = none) */
EXT_RAM_BSS_ATTR static int s_pend_bytes;
EXT_RAM_BSS_ATTR static int64_t s_pend_at;
#define MIXER_PEND_MAX_US 400000    /* give up waiting for the mixer's start: set the format anyway */

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

const pc_extstream_t *ext_audio_stream(void) { return &s_stream; }
pc_extuac_t *ext_audio_uac(void) { return &s_uac; }
bool ext_audio_started(void) { return s_started; }
void ext_audio_set_alts(const pc_extdesc_info_t *info)
{
    memset(s_alt_bits, 0, sizeof s_alt_bits);
    for (int i = 0; i < info->n_alt && i < PC_EXTDESC_MAX_ALTS; i++) s_alt_bits[i + 1] = info->alt_bits[i];
}

void ext_audio_start(void)
{
    if (s_started) return;
    audio_svc_start();                                                              /* 1: the service (it plays nothing without a source) */
    int vol = pc_extstream_start_volume(app_volume());
    audio_svc_set_volume(vol);                                                      /* 2: the device volume, min(saved, 20), BEFORE any block of a source */
    uint8_t *ring = psram(PC_EXTSTREAM_RING_BYTES);
    if (ring) pc_extstream_init(&s_stream, ring, PC_EXTSTREAM_RING_BYTES);
    s_path = ring ? pc_extpath_create(psram, &s_stream, &s_uac) : NULL;
    if (!s_path) { printf("APP ext audio alloc failed: no sound\n"); return; }       /* the sound card enumerates but never plays: no mixer is ever started without the path */
    audio_svc_set_post(pc_extpath_post, s_path);                                    /* 3: the fixed limiter chain, before the first mixer block */
    s_started = true;
    printf("APP ext audio start vol=%d (saved %d) ring=%u limiter fixed\n", vol, app_volume(), (unsigned)PC_EXTSTREAM_RING_BYTES);
}

void ext_audio_event(int alt, uint32_t rate)
{
    s_ev_alt = alt;
    s_ev_rate = rate;
    __atomic_fetch_add(&s_ev_seq, 1, __ATOMIC_RELEASE);
}

/* the drop mask of the rates this board could not read back (the DAC clock refused or read it back wrong): RTC memory, tag + mask + check, read by the next entry into the mode */
static RTC_NOINIT_ATTR uint32_t s_drop;
#define DROP_TAG 0xD5u
static uint32_t drop_check(uint32_t m) { return (0xA7u ^ m ^ (m >> 8)) & 0xFFu; }
uint16_t ext_audio_drop_mask(void)
{
    uint32_t v = s_drop;
    if (v >> 24 != DROP_TAG || ((v >> 16) & 0xFFu) != drop_check(v & 0xFFFFu)) return 0;
    return (uint16_t)(v & 0xFFFFu);
}
static void note_drop(uint32_t rate)
{
    for (int i = 0; i < PC_EXTDESC_MAX_RATES; i++) {
        if (pc_extdesc_candidate(i) != rate || rate <= 48000) continue;             /* the crystal rates are never dropped (they cannot fail the way an APLL tier can) */
        uint32_t m = (uint32_t)ext_audio_drop_mask() | 1u << i;
        s_drop = DROP_TAG << 24 | drop_check(m) << 16 | m;
    }
}

static void stream_off(void) { pc_extstream_set_format(&s_stream, 0, 0); }

static void apply_event(void)
{
    uint32_t seq = __atomic_load_n(&s_ev_seq, __ATOMIC_ACQUIRE);
    int alt = s_ev_alt;
    uint32_t rate = s_ev_rate;
    if (seq == s_ev_done) return;
    s_ev_done = seq;
    s_refused = false;
    s_pend_gen = 0;
    s_alt = alt;
    s_rate = rate;
    if (alt <= 0 || alt > PC_EXTDESC_MAX_ALTS || !s_alt_bits[alt]) { s_alt = 0; stream_off(); printf("APP ext audio stop\n"); return; }
    if (!s_started) { stream_off(); return; }
    bool bad = !pc_mixer_rate_ok(rate);
    for (int i = 0; i < PC_EXTDESC_MAX_RATES; i++) if (s_bad_rate[i] == rate) bad = true;
    if (bad) { s_refused = true; stream_off(); printf("APP ext audio refused rate=%u (not an output rate, or refused earlier)\n", (unsigned)rate); return; }
    pc_extfb_init(&s_fb, rate);                                                      /* the feedback starts at the nominal value of the new rate */
    if (s_mix_rate != rate) {                                                        /* another rate: mute -> reclock -> prefill -> unmute. The stream stays OFF (the pump discards what the host sends, the old mixer renders silence, never new-rate data at the old clock) until the restarted mixer is current */
        stream_off();
        uint32_t gen = audio_svc_play_mixer(pc_extpath_render, s_path, rate, NULL, NULL);   /* CMD_MIXER mutes, restarts the clock, ramps up from 0 */
        printf("APP ext audio mixer rate=%u gen=%u\n", (unsigned)rate, (unsigned)gen);
        if (gen) { s_mix_rate = rate; s_mix_at = esp_timer_get_time(); s_pend_gen = gen; s_pend_rate = rate; s_pend_bytes = s_alt_bits[alt] / 8; s_pend_at = s_mix_at; }
        else s_mix_rate = 0;                                                         /* the queue was full: no sound until the host starts again (a rate or alt change) */
        return;
    }
    pc_extstream_set_format(&s_stream, rate, s_alt_bits[alt] / 8);                  /* flushes the ring: the reader skips the old format's data and waits for the water level */
    printf("APP ext audio stream alt=%d bits=%d rate=%u\n", alt, s_alt_bits[alt], (unsigned)rate);
}

static void apply_pending(void)
{
    if (!s_pend_gen) return;
    aud_status_t st;
    audio_svc_status(&st);
    if (st.gen != s_pend_gen && esp_timer_get_time() - s_pend_at < MIXER_PEND_MAX_US) return;
    s_pend_gen = 0;
    pc_extstream_set_format(&s_stream, s_pend_rate, s_pend_bytes);                  /* prefill starts here: the reader waits for the water level, then ramps up from silence */
    printf("APP ext audio stream alt=%d bits=%d rate=%u\n", s_alt, s_pend_bytes * 8, (unsigned)s_pend_rate);
}

void ext_audio_pump(void)
{
    apply_event();
    apply_pending();
    if (s_alt && s_mix_at && !s_refused && esp_timer_get_time() - s_mix_at >= MIXER_CHECK_US) {   /* has the clock been taken? hires_off lowers hw_cap when it was refused or read back wrong */
        s_mix_at = 0;
        aud_info_t in;
        audio_svc_info(&in);
        if (in.hw_cap && in.hw_cap < s_rate) {
            s_refused = true;
            stream_off();
            note_drop(s_rate);
            for (int i = 0; i < PC_EXTDESC_MAX_RATES; i++) if (!s_bad_rate[i]) { s_bad_rate[i] = s_rate; break; }
            printf("APP ext audio refused rate=%u (the DAC clock: cap %u)\n", (unsigned)s_rate, (unsigned)in.hw_cap);
        }
    }
}

bool ext_audio_event_pending(void) { return __atomic_load_n(&s_ev_seq, __ATOMIC_ACQUIRE) != s_ev_done; }
uint32_t ext_audio_feed(const uint8_t *data, uint32_t bytes)
{
    if (ext_audio_event_pending()) return 0;     /* the host changed alt / rate: these bytes are of the old format, the pump applies the event first */
    return pc_extstream_write(&s_stream, data, bytes);
}
int ext_audio_frame_bytes(void) { return 2 * pc_extstream_bytes(&s_stream); }
uint32_t ext_audio_fb(uint64_t now_us, uint32_t extra_frames)
{
    if (!s_alt || s_refused || s_pend_gen) return pc_extfb_nominal(s_rate ? s_rate : 48000);
    return pc_extfb_update(&s_fb, now_us, pc_extstream_fill_frames(&s_stream) + extra_frames, pc_extstream_playing(&s_stream));
}

void ext_audio_status(ext_audio_status_t *o)
{
    memset(o, 0, sizeof *o);
    o->alt = s_alt;
    o->bits = s_alt ? s_alt_bits[s_alt] : 0;
    o->rate = s_rate;
    o->mixer = s_mix_rate != 0 && s_mix_rate == s_rate;
    o->refused = s_refused;
    o->fill_frames = pc_extstream_fill_frames(&s_stream);
    o->target_frames = pc_extstream_target_frames(&s_stream);
    o->underruns = s_stream.underruns;
    o->overruns = s_stream.overruns;
    o->drops = s_stream.drops;
    o->limited = s_path ? pc_extpath_limited(s_path) : 0;
    o->fb = s_fb.value;
}

bool ext_audio_volume_step(int d)
{
    if (!s_started || !s_alt) return false;                   /* only while the computer streams: otherwise the layer's volume role stays the computer's (HID volume keys) */
    int v = pc_volume_step(audio_svc_volume_last(), d, cfg_int("volume_max", 100));
    audio_svc_set_volume(v);
    printf("APP ext audio volume=%d\n", v);
    return true;
}

const char *ext_audio_rate_str(uint32_t rate, char *buf, int n)
{
    if (rate % 1000 == 0) snprintf(buf, (size_t)n, "%u", (unsigned)(rate / 1000));
    else snprintf(buf, (size_t)n, "%u.%u", (unsigned)(rate / 1000), (unsigned)((rate % 1000) / 100));
    return buf;
}
