// SPDX-License-Identifier: LGPL-2.1-or-later
/* --selftest: checks of the host skeleton. Prints "FAIL <name>" per failure, "SELFTEST ok <n>" if all pass. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "audio_svc.h"
#include "esp_timer.h"
#include "library_svc.h"
#include "noise_engine.h"
#include "thumb_svc.h"
#include "album_svc.h"
#include "pc/pc_spectrum.h"
#include "lvgl.h"
#include "sim.h"

static char g_root[128];
static bool g_ok;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); g_ok = false; } } while (0)

/* ---- clock / timers ---- */
static int g_order[8], g_n;
static int64_t g_at[8];
static void rec(int id) { if (g_n < 8) { g_order[g_n] = id; g_at[g_n] = sim_now_us(); } g_n++; }
static void cb_esp(void *arg) { rec((int)(intptr_t)arg); }
static void cb_lv(lv_timer_t *t) { (void)t; rec(30); }

static bool t_clock_steps(void)
{
    sim_clock_init(1794375000, false);
    int64_t w0 = sim_wall_us();
    sim_advance_ms(100);
    CHECK(sim_now_us() == 100000);
    CHECK(lv_tick_get() == 100);
    CHECK(sim_wall_us() == w0 + 100000);
    return g_ok;
}

static bool t_timer_order(void)
{
    esp_timer_handle_t a, b;
    esp_timer_create_args_t aa = { .callback = cb_esp, .arg = (void *)(intptr_t)50, .name = "t50" };
    esp_timer_create_args_t bb = { .callback = cb_esp, .arg = (void *)(intptr_t)20, .name = "t20" };
    CHECK(esp_timer_create(&aa, &a) == ESP_OK && esp_timer_create(&bb, &b) == ESP_OK);
    g_n = 0;
    esp_timer_start_once(a, 50000);
    esp_timer_start_once(b, 20000);
    lv_timer_t *lt = lv_timer_create(cb_lv, 30, NULL);
    lv_timer_set_repeat_count(lt, 1);
    int64_t t0 = sim_now_us();
    sim_advance_ms(100);
    CHECK(g_n == 3);
    CHECK(g_order[0] == 20 && g_order[1] == 30 && g_order[2] == 50);
    /* callbacks see the time of the step boundary (33 ms steps), not their own due time */
    CHECK(g_at[0] == t0 + 33000 && g_at[1] == t0 + 33000 && g_at[2] == t0 + 66000);
    esp_timer_delete(a);
    esp_timer_delete(b);
    return g_ok;
}

/* the census behind "simres" (tools/simtools/leak_cycles.py reads it every cycle): a created timer counts until it is deleted */
static bool t_timer_census(void)
{
    esp_timer_handle_t a, b;
    esp_timer_create_args_t aa = { .callback = cb_esp, .arg = (void *)(intptr_t)1, .name = "census" };
    int n0 = sim_esp_timer_count();
    CHECK(esp_timer_create(&aa, &a) == ESP_OK && sim_esp_timer_count() == n0 + 1);
    CHECK(esp_timer_delete(a) == ESP_OK && sim_esp_timer_count() == n0);                  /* created and deleted: back where it was */
    CHECK(esp_timer_create(&aa, &a) == ESP_OK && esp_timer_create(&aa, &b) == ESP_OK);
    CHECK(esp_timer_delete(a) == ESP_OK && sim_esp_timer_count() == n0 + 1);              /* b is not deleted: the count stays up */
    CHECK(esp_timer_delete(b) == ESP_OK && sim_esp_timer_count() == n0);
    return g_ok;
}

static int g_ticks, g_self_ticks;
static esp_timer_handle_t g_self;
static void cb_tick(void *arg) { (void)arg; g_ticks++; }
static void cb_self_stop(void *arg) { (void)arg; if (++g_self_ticks == 3) esp_timer_stop(g_self); }

static bool t_timer_periodic_stop(void)
{
    esp_timer_handle_t p;
    esp_timer_create_args_t a = { .callback = cb_tick, .name = "per" };
    CHECK(esp_timer_create(&a, &p) == ESP_OK);
    g_ticks = 0;
    esp_timer_start_periodic(p, 10000);
    sim_advance_ms(95);
    CHECK(g_ticks == 9);
    esp_timer_stop(p);
    sim_advance_ms(50);
    CHECK(g_ticks == 9);
    esp_timer_delete(p);

    esp_timer_create_args_t s = { .callback = cb_self_stop, .name = "self" };
    CHECK(esp_timer_create(&s, &g_self) == ESP_OK);
    g_self_ticks = 0;
    esp_timer_start_periodic(g_self, 10000);
    sim_advance_ms(100);
    CHECK(g_self_ticks == 3);
    esp_timer_delete(g_self);
    return g_ok;
}

static bool t_wall_wrap(void)
{
    sim_clock_init(1794375000, false);   /* the value only needs to be non-trivial */
    CHECK(time(NULL) == 1794375000);
    struct timeval tv;
    CHECK(gettimeofday(&tv, NULL) == 0 && tv.tv_sec == 1794375000);
    sim_advance_ms(1500);
    CHECK(time(NULL) == 1794375001);
    return g_ok;
}

/* ---- paths ---- */
static bool t_path_map(void)
{
    char buf[256];
    sim_set_root("/tmp/r");
    CHECK(!strcmp(sim_path("/sdcard", buf, sizeof buf), "/tmp/r"));
    CHECK(!strcmp(sim_path("/sdcard/a/b", buf, sizeof buf), "/tmp/r/a/b"));
    CHECK(sim_path("/sdcard/../etc", buf, sizeof buf) == NULL);                      /* leaving the root is refused */
    CHECK(sim_path("/sdcard/a/../../etc", buf, sizeof buf) == NULL && sim_path("/sdcard/..", buf, sizeof buf) == NULL);
    CHECK(sim_path("/sdcard/a/..", buf, sizeof buf) == NULL && sim_path("/sdcard//..//x", buf, sizeof buf) == NULL);
    CHECK(!strcmp(sim_path("/sdcard/a..b/..c/d..", buf, sizeof buf), "/tmp/r/a..b/..c/d.."));   /* only a whole ".." component counts */
    CHECK(sim_path("/flashfont/../x", buf, sizeof buf) != NULL);                    /* not ours: passed through */
    {   /* every wrapped entry point refuses it the same way: -1 / NULL with ENOENT, and nothing is created outside */
        struct stat st;
        errno = 0; CHECK(fopen("/sdcard/../sim-escape", "w") == NULL && errno == ENOENT);
        errno = 0; CHECK(open("/sdcard/../sim-escape", O_CREAT | O_WRONLY, 0600) == -1 && errno == ENOENT);
        errno = 0; CHECK(stat("/sdcard/../x", &st) == -1 && errno == ENOENT);
        errno = 0; CHECK(opendir("/sdcard/../x") == NULL && errno == ENOENT);
        errno = 0; CHECK(remove("/sdcard/../x") == -1 && errno == ENOENT);
        errno = 0; CHECK(rename("/sdcard/../x", "/sdcard/y") == -1 && errno == ENOENT);
        errno = 0; CHECK(rename("/sdcard/y", "/sdcard/../x") == -1 && errno == ENOENT);
        errno = 0; CHECK(mkdir("/sdcard/../x", 0777) == -1 && errno == ENOENT);
        errno = 0; CHECK(unlink("/sdcard/../x") == -1 && errno == ENOENT);
        errno = 0; CHECK(rmdir("/sdcard/../x") == -1 && errno == ENOENT);
        errno = 0; CHECK(access("/sdcard/../x", F_OK) == -1 && errno == ENOENT);
        errno = 0; CHECK(truncate("/sdcard/../x", 0) == -1 && errno == ENOENT);
    }
    const char *same[] = { "/sdcardx", "/flashfont/x", "rel/p", "" };
    for (int i = 0; i < 4; i++) CHECK(sim_path(same[i], buf, sizeof buf) == same[i]);
    sim_set_root(g_root);
    return g_ok;
}

static bool t_fs_wrap(void)
{
    char real[256];
    struct stat st;
    CHECK(mkdir("/sdcard/t", 0777) == 0);
    snprintf(real, sizeof real, "%s/t", g_root);
    CHECK(stat(real, &st) == 0 && S_ISDIR(st.st_mode));
    int dfd = open("/sdcard/t", O_RDONLY | O_DIRECTORY);   /* O_TMPFILE contains the O_DIRECTORY bit: no mode argument here */
    CHECK(dfd >= 0);
    if (dfd >= 0) close(dfd);
    int ofd = open("/sdcard/t/o.txt", O_CREAT | O_WRONLY, 0600);
    CHECK(ofd >= 0);
    if (ofd >= 0) close(ofd);
    snprintf(real, sizeof real, "%s/t/o.txt", g_root);
    CHECK(stat(real, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(remove("/sdcard/t/o.txt") == 0);
    FILE *f = fopen("/sdcard/t/a.txt", "w");
    CHECK(f != NULL);
    if (f) { fputs("hello world", f); fclose(f); }
    snprintf(real, sizeof real, "%s/t/a.txt", g_root);
    CHECK(access(real, F_OK) == 0);                     /* it really landed under the root */
    CHECK(stat("/sdcard/t/a.txt", &st) == 0 && st.st_size == 11);
    CHECK(rename("/sdcard/t/a.txt", "/sdcard/t/b.txt") == 0);
    CHECK(access("/sdcard/t/a.txt", F_OK) != 0 && access("/sdcard/t/b.txt", F_OK) == 0);
    bool seen = false;
    DIR *d = opendir("/sdcard/t");
    CHECK(d != NULL);
    for (struct dirent *e; d && (e = readdir(d));) if (!strcmp(e->d_name, "b.txt")) seen = true;
    if (d) closedir(d);
    CHECK(seen);
    CHECK(truncate("/sdcard/t/b.txt", 4) == 0);
    CHECK(stat("/sdcard/t/b.txt", &st) == 0 && st.st_size == 4);
    CHECK(remove("/sdcard/t/b.txt") == 0);
    CHECK(rmdir("/sdcard/t") == 0);
    CHECK(access("/sdcard/t", F_OK) != 0);
    return g_ok;
}

/* ---- screenshots ---- */
static bool slurp(const char *file, unsigned char **out, long *n)
{
    FILE *f = fopen(file, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc((size_t)*n);
    bool ok = fread(*out, 1, (size_t)*n, f) == (size_t)*n;
    fclose(f);
    return ok;
}

/* a fixed page for the shot tests: text in two sizes, an arc, a bar */
static void test_page(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "SIM selftest");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_text(s, "360 x 360 RGB565");
    lv_obj_set_style_text_font(s, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(0x80C0FF), 0);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_t *a = lv_arc_create(scr);
    lv_obj_set_size(a, 160, 160);
    lv_arc_set_value(a, 65);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, 30);
    lv_obj_t *b = lv_bar_create(scr);
    lv_obj_set_size(b, 200, 14);
    lv_bar_set_value(b, 40, LV_ANIM_OFF);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static bool t_shot_same(void)
{
    char f1[200], f2[200];
    snprintf(f1, sizeof f1, "%s/s1.png", g_root);
    snprintf(f2, sizeof f2, "%s/s2.png", g_root);
    test_page();
    CHECK(sim_shot_png(f1));
    CHECK(sim_shot_png(f2));
    unsigned char *a = NULL, *b = NULL;
    long na = 0, nb = 0;
    CHECK(slurp(f1, &a, &na) && slurp(f2, &b, &nb));
    CHECK(na > 33 && na == nb && memcmp(a, b, (size_t)na) == 0);
    if (na > 33) {   /* IHDR: width, height, bit depth, colour type */
        uint32_t w = (uint32_t)a[16] << 24 | a[17] << 16 | a[18] << 8 | a[19];
        uint32_t h = (uint32_t)a[20] << 24 | a[21] << 16 | a[22] << 8 | a[23];
        CHECK(w == 360 && h == 360 && a[24] == 8 && a[25] == 2);
    }
    free(a);
    free(b);
    return g_ok;
}

static bool t_shot_convert(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    const uint32_t col[3] = { 0xFF0000, 0x00FF00, 0x808080 };   /* -> RGB565 0xF800, 0x07E0, 0x8410 */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *o = lv_obj_create(scr);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, 360, 120);
        lv_obj_set_pos(o, 0, i * 120);
        lv_obj_set_style_bg_color(o, lv_color_hex(col[i]), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    char f[200];
    snprintf(f, sizeof f, "%s/c.png", g_root);
    CHECK(sim_shot_png(f));
    png_image im;
    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&im, f)) {
        im.format = PNG_FORMAT_RGB;
        png_bytep px = malloc(PNG_IMAGE_SIZE(im));
        if (png_image_finish_read(&im, NULL, px, 0, NULL)) {
            const unsigned char want[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 131, 129, 131 } };
            for (int i = 0; i < 3; i++) {
                const unsigned char *p = px + ((size_t)(i * 120 + 60) * 360 + 180) * 3;
                CHECK(!memcmp(p, want[i], 3));
            }
        } else CHECK(!"png read");
        free(px);
        png_image_free(&im);
    } else CHECK(!"png open");
    lv_obj_clean(scr);
    return g_ok;
}


/* ---- audio: the fake play state machine ---- */
static int32_t g_tb_len = 200000;
static int tb_open(void **h, const pc_reader_t *r, uint32_t off) { (void)r; (void)off; *h = &g_tb_len; return 0; }
static int tb_render(void *h, float *s, int n) { (void)h; (void)s; (void)n; return 0; }
static uint32_t tb_rate(void *h) { (void)h; return 44100; }
static int32_t tb_length(void *h) { (void)h; return g_tb_len; }
static void tb_close(void *h) { (void)h; }
static const pc_backend_t g_tb = { .open = tb_open, .render = tb_render, .rate = tb_rate, .length_ms = tb_length, .close = tb_close };
static int g_released;
static void on_release(void *arg) { (void)arg; g_released++; }

static uint32_t tb_play(int32_t len_ms, int32_t near_ms)
{
    static unsigned char dummy[16];
    pc_reader_t r;
    pc_reader_mem(&r, dummy, sizeof dummy);
    g_tb_len = len_ms;
    aud_opts_t o = { .gain = 1.0f, .near_ms = near_ms };
    return audio_svc_play_ex(&g_tb, &r, 0, 0, &o, on_release, NULL);
}
static void drain_events(void)
{
    aud_event_t e;
    while (xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE) {}
}
static int g_pos_ms(void) { aud_status_t st; audio_svc_status(&st); return st.pos_ms; }

static bool t_play_advances(void)
{
    sim_clock_init(SIM_DEFAULT_WALL_S, false);
    audio_svc_start();
    drain_events();
    g_released = 0;
    uint32_t gen = tb_play(200000, 0);
    CHECK(gen != 0);
    aud_status_t st;
    audio_svc_status(&st);
    CHECK(st.state == AUD_PLAYING && st.len_ms == 200000 && st.gen == gen && st.rate == 44100);
    sim_advance_ms(5000);
    CHECK(g_pos_ms() >= 4900 && g_pos_ms() <= 5100);
    audio_svc_pause(true);
    audio_svc_status(&st);
    CHECK(st.state == AUD_PAUSED);
    int held = st.pos_ms;
    sim_advance_ms(5000);
    CHECK(g_pos_ms() == held);
    audio_svc_pause(false);
    sim_advance_ms(2000);
    CHECK(g_pos_ms() >= held + 1900 && g_pos_ms() <= held + 2100);
    CHECK(g_released == 0);
    audio_svc_stop(false);
    audio_svc_status(&st);
    CHECK(st.state == AUD_IDLE && g_released == 1);
    return g_ok;
}

static bool t_track_ends(void)
{
    sim_clock_init(SIM_DEFAULT_WALL_S, false);              /* its own clock; audio_svc_start re-arms the tick on it */
    audio_svc_start();
    drain_events();
    g_released = 0;
    uint32_t gen = tb_play(3000, 1000);
    aud_event_t e;
    sim_advance_ms(1500);                                   /* 1500 ms left: not near yet */
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdFALSE);
    sim_advance_ms(700);                                    /* 800 ms left: NEAR */
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE && e.type == AUD_NEAR && e.gen == gen);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdFALSE);
    sim_advance_ms(1000);                                   /* past the end: ENDED, once */
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE && e.type == AUD_ENDED && e.gen == gen);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdFALSE);
    aud_status_t st;
    audio_svc_status(&st);
    CHECK(st.state == AUD_ENDED && g_released == 1);
    sim_advance_ms(500);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdFALSE);

    /* gapless: a source queued after NEAR takes over at the end (AUD_NEXT carries its gen, no ENDED) */
    g_released = 0;
    gen = tb_play(3000, 1000);
    sim_advance_ms(2200);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE && e.type == AUD_NEAR);
    static unsigned char dummy[16];
    pc_reader_t r;
    pc_reader_mem(&r, dummy, sizeof dummy);
    g_tb_len = 5000;
    uint32_t g2 = audio_svc_queue_next(gen, &g_tb, &r, NULL, on_release, NULL);
    CHECK(g2 != 0 && g2 != gen);
    sim_advance_ms(1000);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdTRUE && e.type == AUD_NEXT && e.gen == g2);
    CHECK(xQueueReceive(audio_svc_events(), &e, 0) == pdFALSE);
    audio_svc_status(&st);
    CHECK(st.state == AUD_PLAYING && st.gen == g2 && st.len_ms == 5000 && st.pos_ms < 1000 && g_released == 1);
    audio_svc_stop(false);
    CHECK(g_released == 2);
    return g_ok;
}

static bool same_bars(const float *a, const float *b) { return memcmp(a, b, 64 * sizeof(float)) == 0; }
static bool t_spectrum_synth(void)
{
    sim_clock_init(SIM_DEFAULT_WALL_S, false);              /* its own clock; audio_svc_start re-arms the tick on it */
    audio_svc_start();
    drain_events();
    float a[64], b[64], c[64], z[64];
    memset(z, 0, sizeof z);
    audio_svc_stop(false);
    audio_svc_spectrum(a);
    CHECK(same_bars(a, z));                                  /* stopped: silence */
    tb_play(200000, 0);
    sim_advance_ms(1000);
    audio_svc_spectrum(a);
    bool in_range = true, all_same = true;
    for (int i = 0; i < 64; i++) { if (!(a[i] >= 0.0f && a[i] <= 1.0f)) in_range = false; if (a[i] != a[0]) all_same = false; }
    CHECK(in_range && !all_same);
    audio_svc_spectrum(c);
    CHECK(same_bars(a, c));                                  /* same instant, same bars */
    sim_advance_ms(33);
    audio_svc_spectrum(b);
    CHECK(!same_bars(a, b));                                 /* one refresh later: moved */
    audio_svc_pause(true);
    audio_svc_spectrum(b);
    CHECK(same_bars(b, z));                                  /* paused: silence */
    audio_svc_pause(false);
    audio_svc_stop(false);
    tb_play(200000, 0);                                      /* the same song again, the same instant: the same bars */
    sim_advance_ms(1000);
    audio_svc_spectrum(b);
    CHECK(same_bars(a, b));
    audio_svc_stop(false);
    audio_svc_spectrum(b);
    CHECK(same_bars(b, z));
    return g_ok;
}

/* ---- covers: drawn from the album name ---- */
static int g_thumb_n;
static uint16_t *g_thumb_px[2];
static void thumb_got(uint32_t req, uint16_t *px, void *u) { (void)req; g_thumb_px[(intptr_t)u] = px; g_thumb_n++; }

static bool t_cover(void)
{
    static pc_track_t tr[2];
    static char pool[] = "p0\0t\0a\0Album One\0p1\0t\0a\0Album Two";
    pc_library_t saved = g_lib;
    g_lib = (pc_library_t){ .tracks = tr, .n = 2, .pool = pool, .pool_len = sizeof pool };
    tr[0] = (pc_track_t){ .path = 0, .title = 3, .artist = 5, .album = 7, .apic_len = 8 };
    tr[1] = (pc_track_t){ .path = 17, .title = 20, .artist = 22, .album = 24, .apic_len = 8 };
    g_thumb_n = 0;
    uint16_t *keep[3] = { 0 };
    for (int i = 0; i < 3; i++) {                            /* album one twice, then album two */
        g_thumb_px[0] = NULL;
        CHECK(thumb_svc_request(i == 2 ? "p1" : "p0", 0, 8, thumb_got, NULL) != 0);
        sim_advance_ms(100);
        CHECK(g_thumb_px[0] != NULL);
        keep[i] = g_thumb_px[0];
    }
    CHECK(g_thumb_n == 3);
    if (keep[0] && keep[1] && keep[2]) {
        size_t n = 240 * 240 * sizeof(uint16_t);
        CHECK(memcmp(keep[0], keep[1], n) == 0);             /* same album: the same pixels */
        CHECK(memcmp(keep[0], keep[2], n) != 0);             /* another album: different */
        bool flat = true;
        for (int i = 1; i < 240 * 240; i++) if (keep[0][i] != keep[0][0]) flat = false;
        CHECK(!flat);
    }
    for (int i = 0; i < 3; i++) free(keep[i]);
    g_thumb_px[0] = (uint16_t *)1;                           /* a path that is not in the library: no cover (NULL) */
    CHECK(thumb_svc_request("/nope", 0, 8, thumb_got, NULL) != 0);
    sim_advance_ms(100);
    CHECK(g_thumb_px[0] == NULL);
    g_lib = saved;
    /* the album photos: three frames of 360x360, deterministic */
    CHECK(album_svc_total() == 3 && album_svc_ready() == 3);
    return g_ok;
}

/* ---- noise scenes: bounds as the header says ---- */
static bool t_noise_bounds(void)
{
    int n = noise_scene_count();
    CHECK(n > 0 && noise_load() == n);
    for (int i = 0; i < n; i++) CHECK(noise_scene(i) != NULL && noise_scene(i)->nlayers > 0 && noise_scene(i)->name[0]);
    CHECK(noise_scene(-1) == NULL && noise_scene(n) == NULL && noise_scene(1000) == NULL);
    CHECK(noise_engine_start(n) != 0 && noise_current() == -1);
    CHECK(noise_engine_start(0) == 0 && noise_current() == 0);
    for (int l = 0; l < 3; l++) CHECK(noise_engine_toggle_layer(0, l) >= 0);
    noise_engine_stop(false);
    CHECK(noise_current() == -1);
    return g_ok;
}

static int rm_cb(const char *p, const struct stat *s, int t, struct FTW *w) { (void)s; (void)t; (void)w; return remove(p); }

int sim_selftest(void)
{
    static const struct { const char *name; bool (*fn)(void); } T[] = {
        { "clock_steps", t_clock_steps }, { "timer_order", t_timer_order }, { "timer_periodic_stop", t_timer_periodic_stop },
        { "wall_wrap", t_wall_wrap }, { "path_map", t_path_map }, { "fs_wrap", t_fs_wrap },
        { "shot_same", t_shot_same }, { "shot_convert", t_shot_convert },
        { "play_advances", t_play_advances }, { "track_ends", t_track_ends }, { "spectrum_synth", t_spectrum_synth }, { "cover", t_cover }, { "noise_bounds", t_noise_bounds }, { "timer_census", t_timer_census },
    };
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof g_root, "%s/sim-selftest-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(g_root)) { printf("FAIL tmpdir\n"); return 1; }
    sim_set_root(g_root);
    int n = (int)(sizeof T / sizeof T[0]), bad = 0;
    for (int i = 0; i < n; i++) {
        g_ok = true;
        if (!T[i].fn()) { printf("FAIL %s\n", T[i].name); bad++; }
    }
    nftw(g_root, rm_cb, 8, FTW_DEPTH | FTW_PHYS);
    if (bad) return 1;
    printf("SELFTEST ok %d\n", n);
    return 0;
}
