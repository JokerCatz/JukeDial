// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_splash.h"
#include "pc/pc_paths.h"

static void test_path(void)
{
    char b[96];
    CHECK(pc_splash_path("player", 0, b, sizeof b)); CHECK_STR(b, "/sdcard/_player/splash/player.jpg");
    CHECK(pc_splash_path("player", 1, b, sizeof b)); CHECK_STR(b, "/sdcard/_player/splash.jpg");
    CHECK(!pc_splash_path("player", 2, b, sizeof b));
    CHECK(!pc_splash_path("../x", 0, b, sizeof b));
    CHECK(!pc_splash_path("Usb", 0, b, sizeof b));
    CHECK(!pc_splash_path("", 0, b, sizeof b));
    CHECK(!pc_splash_path(NULL, 1, b, sizeof b));
    CHECK(!pc_splash_path("player", 0, b, 10));
    for (int i = 0; pc_splash_modes[i]; i++) CHECK(pc_splash_path(pc_splash_modes[i], 0, b, sizeof b));
}
static void test_segments(void)
{
    pc_splash_seg_t s;
    CHECK(pc_splash_seg(0, 60, &s)); CHECK_EQ_INT(s.to_pct, 60);
    CHECK(pc_splash_seg(1, 60, &s)); CHECK_EQ_INT(s.to_pct, 20);
    CHECK(pc_splash_seg(2, 60, &s)); CHECK_EQ_INT(s.to_pct, 60);
    CHECK(!pc_splash_seg(3, 60, &s));
    CHECK(pc_splash_seg(1, 2, &s)); CHECK_EQ_INT(s.to_pct, 1);    /* never 0 above 0 */
    CHECK(pc_splash_seg(1, 1, &s)); CHECK_EQ_INT(s.to_pct, 1);
    CHECK(pc_splash_seg(0, 250, &s)); CHECK_EQ_INT(s.to_pct, 100);
    uint32_t sum = 0;
    for (int i = 0; i < PC_SPLASH_NSEG; i++) { pc_splash_seg(i, 60, &s); sum += s.ms; }
    CHECK_EQ_INT(sum, PC_SPLASH_TOTAL_MS);
    CHECK(PC_SPLASH_TOTAL_MS <= 2000);
}
static void test_level(void)
{
    CHECK_NEAR(pc_splash_level(PC_SPLASH_UP1_MS / 2, 60), 15, 1);      /* half the time = half the PERCEIVED level: a quarter of the duty */
    CHECK(pc_splash_level(1, 60) >= 1);                                  /* after the first light never 0 */
    CHECK_EQ_INT(pc_splash_level(0, 60), 0);
    CHECK_EQ_INT(pc_splash_level(PC_SPLASH_UP1_MS, 60), 60);
    CHECK_EQ_INT(pc_splash_level(PC_SPLASH_UP1_MS + PC_SPLASH_DOWN_MS, 60), 20);
    CHECK_EQ_INT(pc_splash_level(PC_SPLASH_TOTAL_MS - 1, 60) >= 59, 1);
    CHECK_EQ_INT(pc_splash_level(PC_SPLASH_TOTAL_MS, 60), 60);
    CHECK_EQ_INT(pc_splash_level(99999, 60), 60);
    uint8_t prev = 0;
    for (uint32_t t = 0; t <= PC_SPLASH_UP1_MS; t += 10) { uint8_t v = pc_splash_level(t, 80); CHECK(v >= prev); prev = v; }
    for (uint32_t t = PC_SPLASH_UP1_MS; t <= PC_SPLASH_UP1_MS + PC_SPLASH_DOWN_MS; t += 10) { uint8_t v = pc_splash_level(t, 80); CHECK(v <= prev); prev = v; }
    for (uint32_t t = PC_SPLASH_UP1_MS; t < PC_SPLASH_TOTAL_MS; t++) CHECK(pc_splash_level(t, 1) == 1);   /* never 0 after the first light */
}
int main(void) { RUN(test_path); RUN(test_segments); RUN(test_level); return 0; }
