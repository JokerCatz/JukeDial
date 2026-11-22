// SPDX-License-Identifier: LGPL-2.1-or-later
/* Desktop simulator: shared declarations. Simulator code lives only under sim/. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SIM_STATE (env, "key=value;key=value"): batt, charging, bt, sd. Read at each call so tests can change it. */
int         sim_state_int(const char *key, int def);
const char *sim_state_str(const char *key, const char *def);   /* valid until the next call */

/* Headless default wall clock: 2026-11-11 13:30:00 UTC (Taipei 21:30). */
#define SIM_DEFAULT_WALL_S 1794403800LL

void    sim_clock_init(int64_t wall_utc_s, bool realtime);   /* realtime: windowed mode follows the host clock */
void    sim_set_wall_us(int64_t wall_us);                    /* the wall clock reads wall_us right now (the monotonic clock is untouched) */
bool    fake_audio_voices_set(const char *set);              /* fake_audio_svc.c: simvoices off|nes|gb|spc|one|max */
bool    fake_audio_rate_next(uint32_t hz, int n, uint32_t burst);   /* fake_audio_svc.c: simaudiorate <hz> <n> [burst] */
void    fake_thumb_gate(void);                               /* fake_thumb_svc.c: simthumbgate (hires T8c fix round 3) */
bool    fake_audio_len_next(int ms, int n);                  /* fake_audio_svc.c: simaudiolen <ms> <n>: the next n decoder sources are ms long */
bool    fake_audio_fail_next(int n);                         /* fake_audio_svc.c: simaudiofail <n> */
bool    fake_audio_dac(const char *args);                    /* fake_audio_svc.c: simdac <hz> | fault <hz> | off (the rate test's fake DAC) */
uint32_t fake_audio_mixer_rate(void);                        /* fake_audio_svc.c: the playing mixer's rate (0 none): the external mode's sound card simulation pulls its blocks */
int     fake_audio_pull(float *buf, int frames, uint32_t rate);   /* fake_audio_svc.c: one block: the mixer's render, then the post hook */
bool    fake_audio_hw(uint32_t hz);                          /* fake_audio_svc.c: simhw <hz> (the fake DAC clock ceiling) */
bool    fake_audio_sink(bool on);                            /* fake_audio_svc.c: simsink 1|0 (and audio_svc_set_sink) */
void    fake_audio_blocks(bool run);                         /* fake_audio_svc.c: simblock 0|1 (BTVOL: the audio task makes no block / makes blocks again) */
bool    fake_radio_cmd(const char *arg);                     /* fake_radio_svc.c: simradio giveup */
bool    fake_noise_set(const char *arg);                     /* fake_noise_engine.c: simnoise <scenes> <layers of scene 0> [custom] | own | ? */
bool fake_cast_cmd(const char *c);   /* fake_cast_mode.c: simcast ... then the cast console */
void fake_cast_start(void);
extern const char *g_sim_mode;                               /* main.c: --mode, "player" (default), "inst", "ext" (alias "usb"), "cast" or "safe" */
bool fake_cast_cmd(const char *c);                              /* fake_cast_mode.c: simcast ... then the cast console (--mode cast) */
void fake_cast_start(void);
void    fake_inst_engine_note(int note, int vel);            /* fake_inst_engine.c: siminst note <n> <vel> (vel 0 = off) */
int     fake_inst_engine_held(int voice);                    /* fake_inst_engine.c: the note a voice of the current chip holds, -1 */
uint32_t fake_inst_engine_beat(uint8_t *beat, uint8_t *accent);   /* fake_inst_engine.c: the last beat of the metronome on the virtual clock (sequence number, 0 = none yet) */
int     fake_inst_engine_loop_ready(void);                   /* fake_inst_engine.c: 0 when the loop can be saved / exported (stopped or playing, not recording, a length), else INST_LF_BUSY / INST_LF_EMPTY as the real inst_loopfile.c */
bool    fake_inst_engine_loop_content(void);                 /* fake_inst_engine.c: some track has events */
void    fake_inst_engine_loop_loaded(void);                  /* fake_inst_engine.c: a loop file was loaded: the looper stops with a 2-bar loop of one event on track 1 */
void    fake_inst_ble_set(bool on);                          /* fake_inst_ble.c: siminst ble 0|1 */
int     fake_audio_volume(void);                             /* fake_audio_svc.c: the last audio_svc_set_volume value (simaudiovol: the gain the audio path would use) */
int     fake_audio_spec_calls(void);                         /* fake_audio_svc.c: how often audio_svc_spectrum_db was called (simspec) */
void    sim_mic_tone(double hz, double cents, double noise);    /* shim_bsp.c: the tuner's synthetic microphone (hz 0 = silence; noise = amplitude of white noise, 0 = a faint floor) */
int64_t sim_now_us(void);                                    /* monotonic virtual microseconds since start */
int64_t sim_wall_us(void);                                   /* virtual UTC, microseconds since the epoch */
void    sim_set_frame_ms(uint32_t ms);                       /* console simframe: coarse virtual frames (a starved LVGL task); 0 = the normal LV_DEF_REFR_PERIOD */
uint32_t sim_frame_ms(void);
void    sim_advance_ms(uint32_t ms);                         /* steps of <= CONFIG_LV_DEF_REFR_PERIOD */
void    sim_tasks_run(void);                                 /* shim_esp.c: run the task bodies queued by xTaskCreatePinnedToCore */
extern bool sim_tasks_hold;                                  /* shim_esp.c: "simtaskhold 1|0": while set, queued task bodies do not run */
bool    sim_sntp_fire(void);                                 /* shim_esp.c: run the SNTP sync callback if SNTP was started (simsntp) */
bool    fake_rtc_cmd(const char *arg);                       /* fake_rtc.c: simrtc absent|invalid|ISO (the FakePod RTC chip) */
bool    fake_ext_ble_cmd(const char *arg);                   /* fake_ext_ble.c: ext ble conn|disc|adv|bond <n>|fail (the BLE side of the external mode) */
bool    fake_ext_uac_cmd(const char *arg);                   /* fake_ext_uac.c: ext uac play|stop|status|fu (the sound card's stream: a fake host through the shared ext_audio glue) */
bool    fake_ext_cmd(const char *arg);                       /* fake_usb_mode.c: ext key|layer|cfg|eject|reload|state (--mode ext) */
void    fake_ext_start(void);                                /* fake_usb_mode.c: the mode start of --mode ext: config read, the HID sender timer */
bool    fake_usb_cmd(const char *arg);                       /* fake_usb_mode.c: simusb mount|eject|unplug|typed (--mode usb: the computer's side) */
void    fake_imu_set(int px, int py);                        /* fake_imu_svc.c (F1b): the tilt source answers with this vector (percent each, clamped to the unit circle); the first call makes it present */
void    fake_imu_moving(bool on);                            /* the simulated device is moving: imu_svc_zero fails (the "hold still" message) */
void    fake_imu_off(void);                                  /* absent again (the 傾斜 rule is hidden for the next page, the page keeps what it offered) */
bool    fake_sd_cmd(const char *arg);                        /* fake_sd.c: simsd fail gpt|nofs|nocard|stuck|err | ok | size <bytes> | formatfail 1|0 (the card under the card format) */
bool    fake_bt_cmd(const char *arg);                        /* fake_bt_svc.c: simbt stream|stop (the phone starts / stops streaming) */
void    fake_bt_sink_underflow(void);                        /* fake_bt_svc.c: simunderrun sink (hires T8e fix 0) */
void    fake_audio_underrun(void);                           /* fake_audio_svc.c: simunderrun (hires T8e: one underrun of the playing source) */
bool    fake_thumb_pack(const char *args);                   /* fake_thumb_svc.c: simthumbpack clear|hold 1|0 (hires T8e: the fake pack, the fake pass) */
void    fake_thumb_low(int n);                               /* fake_thumb_svc.c: simthumblow <n> (the next n phone covers refused for low memory) */
bool    fake_btmeta_cmd(const char *arg);                    /* fake_bt_svc.c: simbtmeta title|artist|album <text>, art <file>|-, clear (the phone's AVRCP track) */
int     sim_esp_timer_count(void);                           /* shim_esp_timer.c: esp_timers created and not yet deleted (the census "simres" prints) */
void    sim_timers_run(void);                                /* shim_esp_timer.c: fire the esp_timers due at sim_now_us() */

void        sim_set_root(const char *dir);
const char *sim_path(const char *p, char *buf, size_t n);    /* "/sdcard" or "/sdcard/..." -> under the root; else p itself; NULL for a path with a ".." component */

void     sim_cover_draw(const char *key, int size, uint16_t *px);   /* sim_cover.c: size x size RGB565 test cover for key */
bool     sim_shot_png(const char *file);
bool     sim_headless(void);
uint32_t sim_seed(void);

void sim_exec(const char *line);                             /* one command line */
void sim_on_command(void (*fn)(const char *line));           /* where non-sim commands go (console_exec) */
void sim_quit(void);                                         /* ask the main loop to end */
bool sim_quitting(void);
void sim_font_shutdown(void);                                 /* shim_font.c: delete every FreeType font (before lv_deinit) */
bool sim_is_na(const char *cmd);                             /* sim_na.c: a command that exists only on the board */

void sim_ui_close(void);                                     /* sim_boot.c: pop every screen, clean the rest (before the fonts are freed) */
bool sim_key(const char *which, int ms);                      /* "top" (knob +) / "bottom" (knob -): a click, or held for ms with repeat; false for another name */
void sim_knob(int detents);                                  /* the UI's knob hook first, else LVGL's encoder */
void sim_drag(int x0, int y0, int x1, int y1, int ms);          /* press at (x0,y0), move in a straight line to (x1,y1) over `ms` of virtual time, release */
void sim_press_off(int x, int y, int ms, int off);              /* headless: the screen goes off `off` ms into the press */
void sim_press(int x, int y, int ms);                         /* press, `ms` of virtual time, release (ms > 400: a long press) */
/* Screen rotation (pc_rotate.h): `--rotate 0|1` (1 = turned 180 degrees) or, without it, the card's "rotate" key latched at boot. It turns the pictures (sim_shot_png) and makes every
 * coordinate the commands take (simtap / simpress / simdrag) a point on the SCREEN as the user sees it; the touch mapping turns it back to the UI's own. The window (non-headless)
 * keeps showing the UI unrotated. On the FakePod's build the two keys trade places at 1 (sim_key). */
void sim_rotation_override(int rotate);
void sim_rotation_latch(void);     /* after cfg_load: the card's value unless --rotate was given */
int  sim_rotation(void);
void sim_tap(int x, int y);                                  /* press, one step, release, one step */
struct _lv_display_t;
struct _lv_indev_t;
struct _lv_indev_t *sim_input_start(struct _lv_display_t *disp);   /* default group, encoder and pointer indevs; returns the pointer (touch) */
void sim_input_window(struct _lv_display_t *disp);           /* window mode only: SDL event filter and the circle overlay */
void sim_sdl_inject(const char *arg);                        /* TEST ONLY, window mode: push synthetic SDL events (simsdl click|wheel|key|close) */
void sim_input_poll(void);                                   /* window mode: drain wheel and key events */

int sim_selftest(void);                                      /* selftest.c: prints "SELFTEST ok <n>" or "FAIL <name>"; 0 = all passed */
