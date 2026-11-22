// SPDX-License-Identifier: LGPL-2.1-or-later
#include "keys_svc.h"
#include "instrument.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_knob.h"
#include "bsp/bsp_sdcard.h"
#include "bsp/bsp_touch.h"
#include "cfg.h"
#include "lcd_clock.h"
#include "console.h"
#include "crashlog.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guard.h"
#include "inst_beat.h"
#include "inst_ble.h"
#include "inst_chip.h"
#include "inst_engine.h"
#include "inst_fm.h"
#include "inst_kit.h"
#include "inst_loopfile.h"
#include "inst_ui.h"
#include "lowmem.h"
#include "pc/pc_ledfx.h"
#include "memlog.h"
#include "sysmode.h"
#include "ui/font.h"
#include "ui/ui.h"

#define MAGIC 0x494E5354u                     /* "INST" */
static RTC_NOINIT_ATTR uint32_t s_req;
static pc_instr_cfg_t s_cfg;

bool instrument_peek_request(void) { return s_req == MAGIC; }   /* the boot front (boot_front.c): the same flag, not consumed */

bool instrument_take_request(void)
{
    bool r = s_req == MAGIC;
    s_req = 0;
    return r;
}

void instrument_request(void)
{
    s_req = MAGIC;
    sysmode_reboot();                        /* stores what is playing: the player resumes it afterwards */
}

static void print_stat(const char *tag)
{
    inst_stats_t s;
    inst_engine_stats(&s);
    int iv = inst_ble_interval_x100();
    printf("INST stat %s chip=%s ok=%d cpu=%.2f%% block_max=%luus underruns=%lu blocks=%lu events=%lu wait_avg=%luus wait_max=%luus "
           "ble=%d interval=%d.%02dms packets=%lu peak=%d\n", tag, inst_chip(inst_engine_chip())->id, s.chip_ok, s.cpu_pct,
           (unsigned long)s.block_max_us, (unsigned long)s.underruns, (unsigned long)s.blocks, (unsigned long)s.events,
           (unsigned long)s.wait_avg_us, (unsigned long)s.wait_max_us, inst_ble_connected(), iv / 100, iv % 100,
           (unsigned long)inst_ble_packets(), s.peak);
    printf("INST types on=%lu off=%lu prog=%lu cc=%lu bend=%lu\n", (unsigned long)s.by_type[PC_MIDI_NOTE_ON], (unsigned long)s.by_type[PC_MIDI_NOTE_OFF],
           (unsigned long)s.by_type[PC_MIDI_PROGRAM], (unsigned long)s.by_type[PC_MIDI_CC], (unsigned long)s.by_type[PC_MIDI_BEND]);
    memlog("inst");
}

static void ui_event(int type, int ch, int a, int b)
{
    pc_midi_ev_t e = { (uint8_t)type, (uint8_t)ch, (uint8_t)a, (uint8_t)b, 0, 0 };
    inst_engine_post_ui(&e);
}

/* console "inst bench": every chip idle, then every voice sounding, 1.5 s each (CPU of the render) */
static void bench_post(void *arg)
{
    int on = (int)(intptr_t)arg;
    static const uint8_t chord[] = { 48, 55, 60, 64, 67, 72, 76, 79 };
    for (unsigned i = 0; i < sizeof chord; i++) ui_event(on ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF, 0, chord[i], 100);
    ui_event(on ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF, 9, 45, 100);
    if (!on) ui_event(PC_MIDI_CC, 0, 123, 0);
}
static void bench_task(void *arg)
{
    int only = (int)(intptr_t)arg - 1;       /* -1: every chip */
    int keep = inst_engine_chip();
    for (int i = 0; i < inst_chip_count(); i++) {
        if (only >= 0 && i != only) continue;
        inst_engine_set_chip(i);
        vTaskDelay(pdMS_TO_TICKS(1300));
        inst_stats_t idle, full, after;
        inst_engine_stats(&idle);
        inst_engine_peak_reset();
        ui_async(bench_post, (void *)1);      /* posted from the LVGL task: the UI ring's one producer */
        vTaskDelay(pdMS_TO_TICKS(2100));
        inst_engine_stats(&full);
        ui_async(bench_post, (void *)0);
        vTaskDelay(pdMS_TO_TICKS(600));
        inst_engine_peak_reset();
        vTaskDelay(pdMS_TO_TICKS(300));
        inst_engine_stats(&after);
        printf("INST bench chip=%-6s voices=%d idle_cpu=%.2f%% full_cpu=%.2f%% block_max=%luus peak=%d after_off_peak=%d underruns=%lu\n",
               inst_chip(i)->id, inst_chip(i)->voice_count(), idle.cpu_pct, full.cpu_pct, (unsigned long)full.block_max_us,
               full.peak, after.peak, (unsigned long)after.underruns);
    }
    inst_engine_set_chip(keep);
    printf("INST bench done\n");
    vTaskDelete(NULL);
}

/* console metronome / kit commands change the engine and the in-memory settings, never the saved cfg
 * (test values must not stick; the screen saves when it changes something itself) */
static bool metro_cmd(const char *c)
{
    int a;
    if (!strcmp(c, "inst metro on")) inst_engine_metro_run(1);
    else if (!strcmp(c, "inst metro off")) inst_engine_metro_run(0);
    else if (sscanf(c, "inst bpb %d", &a) == 1) { s_cfg.bpb = a < 2 ? 2 : a > 7 ? 7 : a; inst_engine_set_bpb(s_cfg.bpb); }
    else if (!strcmp(c, "inst click on") || !strcmp(c, "inst click off")) { s_cfg.click = c[12] == 'n'; inst_engine_set_click(s_cfg.click); }
    else if (!strcmp(c, "inst beathap on") || !strcmp(c, "inst beathap off")) { s_cfg.beat_haptic = c[14] == 'n'; inst_beat_set_haptic(s_cfg.beat_haptic); }
    else if (sscanf(c, "inst hlead %d", &a) == 1) { inst_beat_set_lead(a); printf("INST hlead=%dus\n", inst_beat_lead()); }
    else if (!strcmp(c, "inst metrostat")) {
        inst_metro_t m;
        inst_engine_metro(&m);
        int n;
        float p50, p95, mx, known;
        uint32_t ticks;
        inst_beat_stats(&n, &p50, &p95, &mx, &known, &ticks);
        printf("INST metro running=%d ext=%d locked=%d bpm=%.1f ext_bpm=%.2f bpb=%d beats=%lu | haptic n=%d late_p50=%.2fms p95=%.2fms max=%.2fms "
               "lead=%dus known_min=%.1fms clock_ticks_sent=%lu ble_sent=%lu\n", m.running, m.ext, m.locked, m.bpm, m.ext_bpm, m.bpb,
               (unsigned long)m.beats, n, p50, p95, mx, inst_beat_lead(), known, (unsigned long)ticks, (unsigned long)inst_ble_sent());
    }
    else if (!strcmp(c, "inst clkstat")) {
        int n;
        float p50, p95, mx, lo, hi;
        inst_engine_clkstat(&n, &p50, &p95, &mx, &lo, &hi);
        printf("INST clkstat beats=%d spacing_err p50=%.2fms p95=%.2fms max=%.2fms bpm_min=%.2f bpm_max=%.2f\n", n, p50, p95, mx, lo, hi);
    }
    else if (!strcmp(c, "inst fm")) {
        for (int i = 0; i < inst_fm_patch_count(); i++) printf("INST fm %d %s\n", i, inst_fm_patch_name(i));
        printf("INST fm n=%d program=%d\n", inst_fm_patch_count(), inst_fm_program(0));
    }
    else if (sscanf(c, "inst fmfast %d", &a) == 1) printf("INST fmfast=%d\n", inst_fm_fast(a));
    else if (sscanf(c, "inst kvoices %d", &a) == 1) { inst_engine_set_kit_voices(a); printf("INST kvoices=%d (0 = by chip)\n", a); }
    else if (sscanf(c, "inst fm %d", &a) == 1) { s_cfg.fm = a & 127; inst_fm_set_program_all(s_cfg.fm); printf("INST fm program=%d %s\n", s_cfg.fm, inst_fm_patch_name(s_cfg.fm)); }
    else if (!strcmp(c, "inst layer auto")) inst_engine_set_layer(PC_LAYER_AUTO);
    else if (!strcmp(c, "inst layer chip")) inst_engine_set_layer(PC_LAYER_CHIP);
    else if (!strcmp(c, "inst layer kit")) inst_engine_set_layer(PC_LAYER_KIT);
    else if (!strcmp(c, "inst layer both")) inst_engine_set_layer(PC_LAYER_BOTH);
    else return false;
    return true;
}

/* console "inst kbench": all 16 kit voices kept busy (retriggered every 300 ms), CPU of the render */
static void kbench_post(void *arg)
{
    (void)arg;
    for (int i = 0; i < 16; i++) ui_event(PC_MIDI_NOTE_ON, INST_CH_PAD, 36 + i, 100);
}
static void kbench_task(void *arg)
{
    (void)arg;
    inst_stats_t a, b;
    inst_engine_stats(&a);
    int vmin = 99;
    for (int r = 0; r < 12; r++) {
        ui_async(kbench_post, NULL);
        vTaskDelay(pdMS_TO_TICKS(300));
        if (r >= 4) { int v = inst_engine_kit_voices(); if (v < vmin) vmin = v; }
    }
    inst_engine_stats(&b);
    printf("INST kbench voices_min=%d cpu=%.2f%% block_max=%luus underruns=%lu peak=%d\n", vmin, b.cpu_pct,
           (unsigned long)b.block_max_us, (unsigned long)(b.underruns - a.underruns), b.peak);
    vTaskDelete(NULL);
}

/* console "inst fmbench": the heaviest stage-5 load: FM with all 6 voices, 16 kit voices kept busy, the metronome
 * clicking; the worst block over 8 s */
static void fmbench_post(void *arg)
{
    int on = (int)(intptr_t)arg;
    static const uint8_t chord[] = { 48, 55, 60, 64, 67 };
    for (unsigned i = 0; i < sizeof chord; i++) ui_event(on ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF, 0, chord[i], 110);
    if (on) {
        ui_event(PC_MIDI_NOTE_ON, 9, 36, 110);
        for (int i = 0; i < 16; i++) ui_event(PC_MIDI_NOTE_ON, INST_CH_PAD, 36 + i, 100);
    }
}
/* console "inst burst <mask>": one burst of events in one block (1: 5 chip notes, 2: a ch 10 drum, 4: 16 pads,
 * 8: the 5 notes off first), to see what a burst costs (`inst worst`) */
static void burst_post(void *arg)
{
    int m = (int)(intptr_t)arg;
    static const uint8_t chord[] = { 48, 55, 60, 64, 67 };
    if (m & 8) for (unsigned i = 0; i < sizeof chord; i++) ui_event(PC_MIDI_NOTE_OFF, 0, chord[i], 0);
    if (m & 1) for (unsigned i = 0; i < sizeof chord; i++) ui_event(PC_MIDI_NOTE_ON, 0, chord[i], 110);
    if (m & 2) ui_event(PC_MIDI_NOTE_ON, 9, 36, 110);
    if (m & 4) for (int i = 0; i < 16; i++) ui_event(PC_MIDI_NOTE_ON, INST_CH_PAD, 36 + i, 100);
}

static void fmbench_task(void *arg)
{
    (void)arg;
    int keep = inst_engine_chip();
    inst_engine_set_chip(inst_chip_find("fm"));
    vTaskDelay(pdMS_TO_TICKS(400));
    inst_engine_metro_run(1);
    inst_stats_t a, b;
    inst_engine_stats(&a);
    uint32_t worst = 0;
    float cpu_max = 0;
    int vmin = 99, fm_min = 0;
    inst_engine_peak_reset();
    for (int r = 0; r < 26; r++) {
        if (r % 8 == 0) ui_async(fmbench_post, (void *)0);   /* release and strike again: envelopes restart */
        ui_async(fmbench_post, (void *)1);
        vTaskDelay(pdMS_TO_TICKS(300));
        inst_engine_stats(&b);
        if (r >= 4) {
            if (b.block_max_us > worst) worst = b.block_max_us;
            if (b.cpu_pct > cpu_max) cpu_max = b.cpu_pct;
            int v = inst_engine_kit_voices();
            if (v < vmin) vmin = v;
            const pc_synth_t *s = inst_engine_synth();
            int act = 0;
            for (int i = 0; i < s->voice_count(); i++) { uint8_t lv; int on; s->voice_state(i, &lv, &on); act += on; }
            if (act > fm_min) fm_min = act;
        }
    }
    inst_engine_stats(&b);
    ui_async(fmbench_post, (void *)0);
    inst_engine_metro_run(0);
    printf("INST fmbench fm_voices_max=%d kit_voices_min=%d cpu_max=%.2f%% block_max=%luus deferred=%lu slow>500/750/1000us=%lu/%lu/%lu of %lu underruns=%lu peak=%d\n",
           fm_min, vmin, cpu_max, (unsigned long)worst, (unsigned long)(b.deferred - a.deferred), (unsigned long)(b.slow[0] - a.slow[0]), (unsigned long)(b.slow[1] - a.slow[1]),
           (unsigned long)(b.slow[2] - a.slow[2]), (unsigned long)(b.blocks - a.blocks), (unsigned long)(b.underruns - a.underruns), b.peak);
    vTaskDelay(pdMS_TO_TICKS(500));
    inst_engine_set_chip(keep);
    vTaskDelete(NULL);
}

static bool inst_cmd(const char *c)
{
    int a, b, ch;
    char w[16];
    if (inst_cmd_ui(c) || metro_cmd(c)) ;
    else if (!strcmp(c, "inst stat")) print_stat("cmd");
    else if (sscanf(c, "inst burst %d", &a) == 1) ui_async(burst_post, (void *)(intptr_t)a);
    else if (!strcmp(c, "inst worst")) {
        inst_stats_t s;
        inst_engine_stats(&s);
        printf("INST worst block=%luus events+metro+loop=%u (ui=%u ble=%u metro=%u loop=%u) chip=%u kit=%u rest=%u\n", (unsigned long)s.worst_us,
               s.worst_part[0], s.worst_ev[0], s.worst_ev[1], s.worst_ev[2], s.worst_ev[3], s.worst_part[1], s.worst_part[2], s.worst_part[3]);
        inst_engine_worst_reset();
    }
    else if (!strcmp(c, "inst kbench")) xTaskCreatePinnedToCore(kbench_task, "inst_kbench", 3072, NULL, 3, NULL, 0);
    else if (!strcmp(c, "inst fmbench")) xTaskCreatePinnedToCore(fmbench_task, "inst_fmbench", 3072, NULL, 3, NULL, 0);
    else if (!strcmp(c, "inst bench")) xTaskCreatePinnedToCore(bench_task, "inst_bench", 4096, NULL, 3, NULL, 0);
    else if (!strncmp(c, "inst bench ", 11) && sscanf(c + 11, "%15s", w) == 1) xTaskCreatePinnedToCore(bench_task, "inst_bench", 4096, (void *)(intptr_t)(inst_chip_find(w) + 1), 3, NULL, 0);
    else if (!strcmp(c, "inst alloff")) inst_engine_all_off();
    else if (!strcmp(c, "inst cfgreset")) { cfg_del("inst"); cfg_save_now(); printf("INST cfg removed\n"); }
    else if (!strcmp(c, "inst voices")) {
        const pc_synth_t *s = inst_chip(inst_engine_chip());
        for (int i = 0; i < s->voice_count(); i++) {
            uint8_t lv = 0;
            int act = 0;
            s->voice_state(i, &lv, &act);
            printf("INST voice %d %s note=%d level=%u\n", i, s->voice_name(i), i < s->voice_count() - 1 ? s->voice_note(i) : -1, lv);
        }
    }
    else if (!strcmp(c, "inst jitter")) {
        float p50, p95, p99, mx;
        inst_engine_jitter(&a, &p50, &p95, &p99, &mx);
        printf("INST jitter n=%d p50=%.1fms p95=%.1fms p99=%.1fms max=%.1fms (0.5 ms bins, 1.33 ms blocks)\n", a, p50, p95, p99, mx);
    }
    else if (!strncmp(c, "inst chip ", 10) && sscanf(c + 10, "%15s", w) == 1) {   /* the space first: a longer word is not a chip name (A-I1) */
        int i = atoi(w);
        if (w[0] < '0' || w[0] > '9') i = inst_chip_find(w);
        inst_engine_set_chip(i);
    }
    else if (sscanf(c, "inst note %d %d %d", &a, &b, &ch) >= 1) {
        if (sscanf(c, "inst note %d %d %d", &a, &b, &ch) < 3) ch = 0;
        if (sscanf(c, "inst note %d %d", &a, &b) < 2) b = 100;
        ui_event(b ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF, ch, a, b);
    }
    else if (sscanf(c, "inst cc %d %d %d", &a, &b, &ch) >= 2) ui_event(PC_MIDI_CC, sscanf(c, "inst cc %d %d %d", &a, &b, &ch) == 3 ? ch : 0, a, b);
    else if (sscanf(c, "inst bend %d", &a) == 1) { pc_midi_ev_t e = { PC_MIDI_BEND, 0, 0, 0, 0, (int16_t)a }; inst_engine_post_ui(&e); }
    else if (sscanf(c, "inst vol %d", &a) == 1) inst_engine_set_volume(a);
    else if (!strcmp(c, "inst map auto")) inst_engine_set_map(PC_MAP_AUTO);
    else if (!strcmp(c, "inst map direct")) inst_engine_set_map(PC_MAP_DIRECT);
    else if (!strcmp(c, "inst map gm")) inst_engine_set_map(PC_MAP_GM);
    else if (!strcmp(c, "inst timing stable")) inst_engine_set_stable(true);
    else if (!strcmp(c, "inst timing now")) inst_engine_set_stable(false);
    else return false;
    return true;
}

void instrument_run(void)
{
    printf("APP instrument mode\n");
    memlog("inst_boot");
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;
    crashlog_flush(g_sd_ok);
    cfg_load();
    const char *saved = cfg_str("inst", NULL);
    pc_instr_cfg_parse(saved, &s_cfg);
    inst_fm_set_program_all(s_cfg.fm);
    if (!saved) s_cfg.volume = app_volume();  /* first time: as loud as the player */
    esp_err_t ei = bsp_i2c_init();
    lv_display_t *disp = NULL;
    bsp_display_set_task_stack(16 * 1024, true);
    bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(false);   /* experimental QSPI clock (lcd_clock.h); the line above: screen rotation from the card (0 without one); the panel, the touch and the keys follow */
    esp_err_t ed = bsp_display_start_ex(&disp, (uint8_t)cfg_brightness());   /* the instrument is its screen: always on */
    uint8_t hst = 0;
    esp_err_t eh = ei == ESP_OK ? bsp_haptic_init(&hst) : ei;
    printf("INST hw sd=%d i2c=%s display=%s haptic=%s cfg='%s'\n", g_sd_ok, esp_err_to_name(ei), esp_err_to_name(ed), esp_err_to_name(eh), saved ? saved : "");
    memlog("inst_display");
    int er = inst_engine_start(&s_cfg, inst_chip_find(s_cfg.chip));
    printf("INST engine=%d\n", er);
    inst_beat_start(eh == ESP_OK && s_cfg.beat_haptic, cfg_haptic_mv());
    if (g_sd_ok && s_cfg.kit[0]) inst_kit_load(s_cfg.kit);   /* in the background: TF -> PSRAM */
    memlog("inst_engine");
    bool ui_ok = false;
    if (ed == ESP_OK && bsp_display_lock(-1)) {
        lv_indev_t *touch = NULL;
        bsp_touch_start(disp, &touch);
        bsp_knob_start(disp, false, NULL);
        keys_svc_start_host();               /* the FakePod two keys = the knob detents (the instrument volume, a pad ring cursor); nothing on a board with a knob */
        font_init();
        inst_ui_start(disp, touch, &s_cfg);
        bsp_display_unlock();
        ui_ok = true;
    }
    if (!ui_ok) {                            /* no screen: no way back from here; the player boots instead */
        printf("INST no display (%s): back to the player in 5 s\n", esp_err_to_name(ed));
        vTaskDelay(pdMS_TO_TICKS(5000));
        inst_engine_stop();
        esp_restart();
    }
    memlog("inst_ui");
    lowmem_start();                          /* level >= still: the ring's effects and spectrum stop (spec 18) */
    int eb = inst_ble_start();
    printf("INST ble=%d\n", eb);
    memlog("inst_ble");
    guard_arm_healthy_timer();
    console_app_cmd = inst_cmd;
    console_start();
    for (int i = 0;; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        inst_engine_log_poll();
        if (i % 100 == 99) print_stat("periodic");
    }
}
