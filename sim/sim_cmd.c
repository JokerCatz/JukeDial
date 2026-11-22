// SPDX-License-Identifier: LGPL-2.1-or-later
/* One command line: the simulator's own commands (@, shot <file>, simres, quit); everything else goes to the registered handler. */
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio_svc.h"
#include "cfg.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "misc/lv_timer_private.h"
#include "sim.h"
#include "ui/ui_frame.h"
#include "ui.h"
#include "ui_geom.h"
#include "inst_pads.h"

#define MAX_ADVANCE_S 86400.0

static void (*g_handler)(const char *line);
static atomic_bool g_quit;   /* set from the SDL event filter, maybe another thread */

void sim_on_command(void (*fn)(const char *line)) { g_handler = fn; }
void sim_quit(void) { g_quit = true; }
bool sim_quitting(void) { return g_quit; }
size_t sim_alloc_fail_min, sim_alloc_fail_max;                       /* esp_heap_caps.h: PSRAM allocations of at least this size fail while it is non-zero */

/* the live LVGL timers: count, forever ones, and each one's period (sorted; "x<n>" = n runs left, a one-shot is "x1";
 * "p" = paused). tools/simtools/sim_timer_races.py checks with it that a race really had the timer it is about running. */
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void cmd_timers(void)
{
    enum { MAXT = 64 };
    int key[MAXT], n = 0, forever = 0, total = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        total++;
        if (t->repeat_count == -1) forever++;
        if (n < MAXT) key[n++] = (int)t->period * 1000 + (t->repeat_count > 0 ? t->repeat_count : 0) * 10 + (t->paused ? 1 : 0);
    }
    qsort(key, (size_t)n, sizeof key[0], cmp_int);
    printf("SIM timers n=%d forever=%d periods=", total, forever);
    for (int i = 0; i < n; i++) {
        int per = key[i] / 1000, rep = key[i] / 10 % 100, paused = key[i] % 10;
        printf("%s%d", i ? "," : "", per);
        if (rep) printf("x%d", rep);
        if (paused) printf("p");
    }
    printf("\n");
}

/* test hook for ui_rebuild_all's leak check (sim/test.sh): "simowner" pushes a screen that owns one forever timer, started
 * later by "simowner start" (e.g. while a child screen is open) and deleted by its own destroy; "simowner leak" makes the
 * destroy forget it (a real leak). */
static lv_timer_t *s_own;
static bool s_own_leak;
static void own_tick(lv_timer_t *t) { (void)t; }
static void own_create(lv_obj_t *p, void *arg) { (void)p; (void)arg; }
static void own_destroy(void) { if (s_own && !s_own_leak) lv_timer_delete(s_own); s_own = NULL; }
static const ui_screen_t scr_owner = { .name = "simowner", .create = own_create, .destroy = own_destroy };
static void cmd_owner(const char *a)
{
    if (!*a) ui_push(&scr_owner, NULL);
    else if (!strcmp(a, "start")) { if (!s_own) s_own = lv_timer_create(own_tick, 1000, NULL); }
    else if (!strcmp(a, "leak")) s_own_leak = true;
    printf("SIM owner timer=%d leak=%d\n", s_own != NULL, s_own_leak);
}

static void cmd_advance(const char *arg)
{
    char *end;
    double s = strtod(arg, &end);
    while (*end && isspace((unsigned char)*end)) end++;
    if (end == arg || *end || isnan(s) || s < 0) { printf("SIM bad time\n"); return; }
    if (s > MAX_ADVANCE_S) s = MAX_ADVANCE_S;
    sim_advance_ms((uint32_t)llround(s * 1000.0));
}

static void cmd_shot(const char *file)
{
    if (sim_shot_png(file)) {
        lv_display_t *d = lv_display_get_default();
        printf("SIM shot %s %dx%d\n", file, (int)lv_display_get_horizontal_resolution(d), (int)lv_display_get_vertical_resolution(d));
    } else printf("SIM shot failed %s\n", file);
}

/* simtime YYYY-MM-DD HH:MM:SS[.mmm] (UTC): the wall clock reads that instant after 1.1 s of virtual time have passed (so every UI timer ran at least
 * once and the clock pages show the last tick at or before it; tools/simtools/sim_diff.py) */
static void cmd_simtime(const char *arg)
{
    struct tm tm = { 0 };
    double sec;
    if (sscanf(arg, "%d-%d-%d %d:%d:%lf", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &sec) != 6 ||
        tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_min < 0 || tm.tm_min > 59 || !(sec >= 0 && sec < 60)) { printf("SIM bad time\n"); return; }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    sim_set_wall_us((int64_t)timegm(&tm) * 1000000 + llround(sec * 1e6) - 1100000);
    sim_advance_ms(1100);
}

/* simburst a|b|c: hands the lines to the console one after the other with no LVGL run in between (several serial lines that reach the board
 * before the UI task runs the first one; board batch A F2): the order they then run in is the order under test */
static void cmd_burst(char *a)
{
    char *save = NULL;
    for (char *l = strtok_r(a, "|", &save); l; l = strtok_r(NULL, "|", &save)) if (g_handler) g_handler(l);
}

/* simcfg: every setting (key=value, in store order) and the count; tools/simtools/sim_con.py compares it before and after a tool run */
static void cmd_cfg(void)
{
    for (int i = 0; i < g_cfg_n; i++) printf("SIM cfg %s=%s\n", g_cfg[i].key, g_cfg[i].val);
    printf("SIM cfg end n=%d\n", g_cfg_n);
}

static void leak_cb(void *arg) { (void)arg; }

static void leak_timer(void)
{
    esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = leak_cb, .name = "simleak" };
    esp_timer_create(&a, &t);
}

void sim_exec(const char *line)
{
    char buf[512];
    if (strlen(line) >= sizeof buf) {   /* never run a truncated command: it could be a different one */
        printf("SIM line too long (%zu bytes, max %zu): skipped\n", strlen(line), sizeof buf - 1);
        if (sim_headless()) printf("SIM ok\n");
        fflush(stdout);
        return;
    }
    snprintf(buf, sizeof buf, "%s", line);
    char *c = buf;
    while (isspace((unsigned char)*c)) c++;
    for (size_t n = strlen(c); n && isspace((unsigned char)c[n - 1]); ) c[--n] = 0;
    if (!*c) return;

    const char *crash = getenv("SIM_CRASH_ON");   /* test hook: the crash path of the drivers (tools/simtools/sim_con.py) */
    if (crash && !strcmp(c, crash)) abort();

    int a, b, d, e2, f2;
    if (c[0] == '@') cmd_advance(c + 1);
    else if (!strncmp(c, "shot ", 5) && c[5 + strspn(c + 5, " ")]) cmd_shot(c + 5 + strspn(c + 5, " "));   /* bare "shot" is the board's text protocol */
    else if (!strncmp(c, "simtime ", 8)) cmd_simtime(c + 8);
    else if (!strcmp(c, "simrtc") || !strncmp(c, "simrtc ", 7)) fake_rtc_cmd(c[6] ? c + 7 : "");
    else if (!strcmp(c, "simsntp")) printf("SIM sntp fired=%d\n", sim_sntp_fire());
    else if (!strcmp(c, "quit")) sim_quit();
    else if (!strcmp(c, "simres")) printf("SIM res esp_timers=%d\n", sim_esp_timer_count());   /* resources LSan cannot see (tools/simtools/leak_cycles.py) */
    else if (!strcmp(c, "simowner") || !strncmp(c, "simowner ", 9)) cmd_owner(c[8] ? c + 9 : "");   /* test hook (above) */
    else if (!strncmp(c, "simallocfail ", 13)) { char *e; sim_alloc_fail_min = (size_t)strtoul(c + 13, &e, 10); sim_alloc_fail_max = (size_t)strtoul(e, NULL, 10); printf("SIM allocfail %zu %zu\n", sim_alloc_fail_min, sim_alloc_fail_max); }   /* test hook: see esp_heap_caps.h */
    else if (!strncmp(c, "simburst ", 9)) cmd_burst(c + 9);                                  /* test hook (above) */
    else if (!strcmp(c, "simcfg")) cmd_cfg();                                                 /* test hook (above) */
    else if (!strcmp(c, "simtimers")) cmd_timers();                                             /* LVGL timers (tools/simtools/sim_timer_races.py) */
    else if (!strcmp(c, "simspec")) printf("SIM spec calls=%d\n", fake_audio_spec_calls());   /* how many times the spectrum (the FFT) was asked for */
    else if (!strncmp(c, "simvoices ", 10)) printf(fake_audio_voices_set(c + 10) ? "SIM voices %s\n" : "SIM voices unknown %s\n", c + 10);   /* fake per-voice levels: off|nes|gb|spc|one|max */
    else if (!strcmp(c, "simthumbgate")) fake_thumb_gate();   /* the cover write gate now (live / published generations) */
    else if (!strncmp(c, "simaudiorate ", 13)) { unsigned hz = 0, burst = 0; int n = -1; int k = sscanf(c + 13, "%u %d %u", &hz, &n, &burst); bool ok = k >= 2 && fake_audio_rate_next(hz, n, burst); if (ok) printf("SIM audiorate %u %d\n", hz, n); else printf("SIM audiorate bad\n"); }   /* the next n decoder sources report hz and the optional burst (frames of its largest read; default 0) */
    else if (!strncmp(c, "simsink ", 8)) { if (!strcmp(c + 8, "1") || !strcmp(c + 8, "0")) { fake_audio_sink(c[8] == '1'); printf("SIM sink %c\n", c[8]); } else printf("SIM sink bad\n"); }   /* the fake's sink flag: the plan for a sink (48k) */
    else if (!strncmp(c, "simhw ", 6)) printf(fake_audio_hw((uint32_t)strtoul(c + 6, NULL, 10)) ? "SIM hw %s\n" : "SIM hw bad %s\n", c + 6);   /* the fake DAC clock ceiling (hw_cap): the rate test faults when it drops */
    else if (!strncmp(c, "simdac ", 7)) printf(fake_audio_dac(c + 7) ? "SIM dac %s\n" : "SIM dac bad %s\n", c + 7);   /* the rate test's fake DAC: the listener's ceiling, the rate that underruns, off */
    else if (!strncmp(c, "simaudiolen ", 12)) { int ms = 0, n = -1; bool ok = sscanf(c + 12, "%d %d", &ms, &n) == 2 && fake_audio_len_next(ms, n); printf(ok ? "SIM audiolen %d %d\n" : "SIM audiolen bad\n", ms, n); }   /* the next n decoder sources are ms long */
    else if (!strncmp(c, "simaudiofail ", 13)) printf(fake_audio_fail_next(atoi(c + 13)) ? "SIM audiofail %s\n" : "SIM audiofail bad %s\n", c + 13);   /* the next n decoder sources fail */
    else if (!strncmp(c, "simradio ", 9)) printf(fake_radio_cmd(c + 9) ? "SIM radio %s\n" : "SIM radio refused %s\n", c + 9);   /* giveup: the board's give-up (stop + message) */
    else if (sscanf(c, "simtaskhold %d", &a) == 1) { sim_tasks_hold = a != 0; printf("SIM taskhold %d\n", sim_tasks_hold); }   /* queued task bodies wait while 1 */
    else if (!strncmp(c, "ext ", 4)) { if (!fake_ext_cmd(c + 4)) printf("SIM ext refused %s\n", c + 4); }   /* --mode ext: the external mode's keys and config */
    else if (!strncmp(c, "simusb ", 7)) { if (!fake_usb_cmd(c + 7)) printf("SIM usb refused %s\n", c + 7); }   /* --mode usb: the computer mounts / ejects the drive */
    else if (!strcmp(c, "simsd") || !strncmp(c, "simsd ", 6)) { if (!fake_sd_cmd(c[5] ? c + 6 : "")) printf("SIM sd refused %s\n", c); }   /* the card under the card format (fake_sd.c) */
    else if (!strcmp(c, "simunderrun")) { fake_audio_underrun(); printf("SIM underrun\n"); }
    else if (!strcmp(c, "simunderrun sink")) { fake_bt_sink_underflow(); printf("SIM underrun sink\n"); }   /* hires T8e fix 0: one dry block of the Bluetooth sink */   /* hires T8e: one underrun of the playing source */
    else if (!strncmp(c, "simthumbpack ", 13)) printf(fake_thumb_pack(c + 13) ? "SIM thumbpack %s\n" : "SIM thumbpack bad %s\n", c + 13);   /* hires T8e: clear (an empty pack: the pass builds) | fill <n> (n covers in, held) | hold 1|0 | tidy 1|0 | full */
    else if (!strncmp(c, "simthumblow ", 12)) { fake_thumb_low(atoi(c + 12)); printf("SIM thumblow %d\n", atoi(c + 12)); }   /* the decoder refuses the next n phone covers (low internal memory) */
    else if (!strncmp(c, "simbtmeta ", 10)) printf(fake_btmeta_cmd(c + 10) ? "SIM btmeta %s\n" : "SIM btmeta refused %s\n", c + 10);   /* the phone's AVRCP title, artist, album, cover */
    else if (!strncmp(c, "simbt ", 6)) printf(fake_bt_cmd(c + 6) ? "SIM bt %s\n" : "SIM bt refused %s\n", c + 6);   /* stream / stop: the phone's A2DP stream (an owner without prev, next, list, details) */
    else if (!strncmp(c, "simnoise ", 9)) printf(fake_noise_set(c + 9) ? "SIM noise %s\n" : "SIM noise bad %s\n", c + 9);   /* fake scene set: <scenes> <layers of scene 0> [custom] */
    else if (!strcmp(c, "simblock 0") || !strcmp(c, "simblock 1")) { fake_audio_blocks(c[9] == '1'); printf("SIM block %c\n", c[9]); }   /* BTVOL: the fake audio task stops / makes blocks (an output change stays held meanwhile) */
    else if (!strncmp(c, "simsetvol ", 10)) { audio_svc_set_volume(atoi(c + 10)); printf("SIM setvol %d\n", fake_audio_volume()); }   /* BTVOL: a value handed to the audio path without storing it (as an alarm's ramp or a duck does) */
    else if (!strcmp(c, "simaudiovol")) printf("SIM audiovol %d\n", fake_audio_volume());               /* the value the audio path was given (audio_svc_set_volume) */
    else if (!strcmp(c, "simvol")) printf("SIM vol %d\n", app_volume());                               /* what the knob changed (the board has no read-back command) */
    else if (!strcmp(c, "simleaktimer")) leak_timer();                                          /* test hook: a timer that is never deleted */
    else if (!strncmp(c, "simsdl ", 7) || !strcmp(c, "simsdl")) { if (sim_headless()) printf("SIM n/a %s\n", c); else sim_sdl_inject(c + strspn(c + 6, " ") + 6); }
    else if (sscanf(c, "siminst note %d %d", &a, &b) == 2) fake_inst_engine_note(a, b);       /* --mode inst: a note into the engine fake's queue (vel 0 = off) */
    else if (sscanf(c, "siminst paddown %d", &a) == 1) inst_pads_inject(a, true);              /* --mode inst: a touch-down on pad a of the pads page (a UI event on the pads object) */
    else if (sscanf(c, "siminst padup %d", &a) == 1) inst_pads_inject(a, false);               /* its release */
    else if (sscanf(c, "siminst ble %d", &a) == 1) { fake_inst_ble_set(a != 0); printf("SIM inst ble %d\n", a != 0); }   /* --mode inst: the BLE link up / down */
    else if (sscanf(c, "simimu %d %d", &a, &b) == 2) { fake_imu_set(a, b); printf("SIM imu %d %d\n", a, b); }   /* F1b: the tilt source's vector (percent of a full tilt, each -100..100); the first one makes it present */
    else if (sscanf(c, "simimu moving %d", &a) == 1) { fake_imu_moving(a != 0); printf("SIM imu moving %d\n", a != 0); }
    else if (!strcmp(c, "simimu off")) { fake_imu_off(); printf("SIM imu off\n"); }
    else if (!strncmp(c, "simtone", 7)) {                       /* the tuner's synthetic microphone: simtone <Hz> [cents] | noise [amp] | off */
        double hz = 0, ct = 0;
        if (!strncmp(c + 7, " noise", 6)) { hz = 0; ct = 0; double amp = 6000; sscanf(c + 13, "%lf", &amp); sim_mic_tone(0, 0, amp); }
        else if (sscanf(c + 7, "%lf %lf", &hz, &ct) >= 1 && hz > 0) sim_mic_tone(hz, ct, 0);
        else sim_mic_tone(0, 0, 0);
        printf("SIM tone hz=%.2f cents=%.1f\n", hz, ct);
    }
    else if (sscanf(c, "simknob %d", &a) == 1) sim_knob(a);
    else if (!strncmp(c, "simkey ", 7)) { char k[16]; a = 0; if (sscanf(c + 7, "%15s %d", k, &a) < 1 || !sim_key(k, a)) { printf("SIM err simkey top|bottom [hold ms]\n"); } }
    else if (sscanf(c, "simtap %d %d", &a, &b) == 2) sim_tap(a, b);
    else if (!strncmp(c, "simtaprow ", 10) || !strcmp(c, "simtapback")) {      /* a real touch on a popup item: the row's cell n, or 返回 (the card moves them) */
        int x, y, n = 0;
        if (c[6] == 'r') sscanf(c + 10, "%d", &n);
        if (ui_frame_item_at(c[6] == 'r' ? "row" : "back", n, &x, &y)) sim_tap(x, y); else printf("APP simtap none\n");
    }
    else if (sscanf(c, "simpress %d %d %d %d", &a, &b, &d, &e2) == 4) sim_press_off(a, b, d, e2);
    else if (sscanf(c, "simpress %d %d %d", &a, &b, &d) == 3) sim_press(a, b, d);
    else if (sscanf(c, "simframe %d", &a) == 1) { sim_set_frame_ms((uint32_t)a); printf("SIM frame=%d ms\n", (int)sim_frame_ms()); }   /* coarse virtual frames: a starved LVGL task (timers and the touch read once per frame) */
    else if (sscanf(c, "simdrag %d %d %d %d %d", &a, &b, &d, &e2, &f2) == 5) sim_drag(a, b, d, e2, f2);
    else if (!strncmp(c, "simflick ", 9)) {                    /* GS: a fast flick from the edge it comes from (UI_FLICK_EDGE, 2026-11-20): up | down | left | right (60 % of the panel in 150 ms, begun 12 px inside the edge) */
        int cx = UI_W / 2, cy = UI_H / 2, e = 12, dx = UI_W * 6 / 10, dy = UI_H * 6 / 10;
        const char *w = c + 9;
        if (!strcmp(w, "up")) sim_drag(cx, UI_H - e, cx, UI_H - e - dy, 150);
        else if (!strcmp(w, "down")) sim_drag(cx, e, cx, e + dy, 150);
        else if (!strcmp(w, "left")) sim_drag(UI_W - e, cy, UI_W - e - dx, cy, 150);
        else if (!strcmp(w, "right")) sim_drag(e, cy, e + dx, cy, 150);
        else printf("SIM err simflick up|down|left|right\n");
        sim_advance_ms(3 * LV_DEF_REFR_PERIOD);               /* the verdict is made at the release: let it run before the next command looks */
    }
    else if (sim_is_na(c)) printf("SIM n/a %s\n", c);
    else if (g_handler) g_handler(c);
    else printf("SIM unknown %s\n", c);
    if (c[0] != '@') sim_advance_ms(0);   /* let what the command queued (lv_async_call, new screens) run before the next command */
    if (sim_headless()) printf("SIM ok\n");   /* scripts sync on this line instead of sleeping */
    fflush(stdout);
}
