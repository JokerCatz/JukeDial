// SPDX-License-Identifier: LGPL-2.1-or-later
/* Coloured noise as a playback owner: the generator runs on the audio task as a mixer source. */
#include "color_noise.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "pc/pc_noisegen.h"
#include "audio_svc.h"
#include "player_ctl.h"
#include "ui/font.h"
#include "ui/screens.h"

static int s_cur = -1;
static uint32_t s_gen;
static const now_ops_t s_ops;

static int render(float *out, int frames, void *u) { pc_noisegen_render(u, out, frames); return frames; }
static void release(void *u) { free(u); }               /* audio task: done with the generator */
static void forget(void) { s_cur = -1; }
static void on_event(const aud_event_t *e)
{
    if (e->gen != s_gen || s_cur < 0) return;
    s_cur = -1;
    player_ctl_release();
}

static void redo_play(int a, int b) { (void)b; color_noise_play(a); }
int color_noise_play(int c)
{
    if (c < 0 || c >= PC_NOISE_COLORS) return -1;
    if (spk_excl_gate(redo_play, c, 0)) return -1;           /* speaker mode is an exclusive source: asked first (player_ctl.h); nothing started */
    pc_noisegen_t *g = heap_caps_malloc(sizeof *g, MALLOC_CAP_SPIRAM);
    if (!g) return -1;
    pc_noisegen_start(g, c);
    player_ctl_take(&s_ops, forget, on_event);
    s_gen = audio_svc_play_mixer(render, g, PC_NOISE_RATE, release, g);
    if (!s_gen) { player_ctl_release(); player_ctl_changed(); return -1; }   /* release() already freed g */
    s_cur = c;
    printf("APP color noise=%d\n", c);
    player_ctl_changed();
    return 0;
}
int color_noise_current(void) { return s_cur; }

static void title(char *out, size_t cap) { snprintf(out, cap, "%s", pc_noise_name(s_cur, i18n_zh())); }
static void next(void) { color_noise_play((s_cur + 1) % PC_NOISE_COLORS); }
static void stop(void) { audio_svc_stop(false); s_cur = -1; player_ctl_release(); player_ctl_changed(); }
static bool playing(void) { return s_cur >= 0; }
static void open_list(void) { ui_open_once(&scr_colors, NULL); }   /* the colours ring is often right under the cover page */
static const char *list_label(void) { return T(S_COLOURS) /* 噪音顏色 */; }
static const now_ops_t s_ops = { .src = NOW_SRC_COLOR, .title = title, .next = next, .stop = stop, .playing = playing,
                                 .open_list = open_list, .list_label = list_label, .no_art = true };
