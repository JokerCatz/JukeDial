// SPDX-License-Identifier: LGPL-2.1-or-later
#include <unistd.h>
#include "pc/pc_stats.h"
#include "pc_test.h"

static void test_stats_roundtrip(void)
{
    pc_stats_t *s = pc_stats_create();
    pc_stats_toggle_fav(s, "mp3:/sdcard/export/A/a=b.mp3");
    pc_stats_add_play(s, "mp3:/sdcard/export/A/a=b.mp3");
    pc_stats_add_play(s, "mp3:/sdcard/export/A/a=b.mp3");
    pc_stats_add_play(s, "chip:nes.zip|Mega Man 2.nsf|3");
    pc_stats_toggle_fav(s, "scene:editorial:rain");
    char path[] = "/tmp/pc_stats_XXXXXX";
    close(mkstemp(path));
    CHECK_EQ_INT(pc_stats_save(s, path), 0);
    pc_stats_destroy(s);
    s = pc_stats_create();
    CHECK_EQ_INT(pc_stats_load(s, path), 0);
    CHECK(pc_stats_is_fav(s, "mp3:/sdcard/export/A/a=b.mp3"));
    CHECK_EQ_INT(pc_stats_plays(s, "mp3:/sdcard/export/A/a=b.mp3"), 2);
    CHECK_EQ_INT(pc_stats_plays(s, "chip:nes.zip|Mega Man 2.nsf|3"), 1);
    CHECK(!pc_stats_is_fav(s, "chip:nes.zip|Mega Man 2.nsf|3"));
    CHECK(pc_stats_is_fav(s, "scene:editorial:rain"));
    CHECK_EQ_INT(pc_stats_plays(s, "unknown"), 0);
    const char *favs[8];
    CHECK_EQ_INT(pc_stats_list_favs(s, "mp3:", favs, 8), 1);
    CHECK_STR(favs[0], "mp3:/sdcard/export/A/a=b.mp3");
    CHECK_EQ_INT(pc_stats_list_favs(s, "scene:", favs, 8), 1);
    pc_stats_destroy(s);
    unlink(path);
}

static void test_stats_dirty(void)
{
    pc_stats_t *s = pc_stats_create();
    CHECK(!pc_stats_dirty(s));
    pc_stats_add_play(s, "x");
    CHECK(pc_stats_dirty(s));
    char path[] = "/tmp/pc_stats_XXXXXX";
    close(mkstemp(path));
    pc_stats_save(s, path);
    CHECK(!pc_stats_dirty(s));
    pc_stats_toggle_fav(s, "x");
    pc_stats_toggle_fav(s, "x");
    CHECK(pc_stats_dirty(s));
    CHECK(!pc_stats_is_fav(s, "x"));
    pc_stats_destroy(s);
    unlink(path);
}

static void test_stats_many(void)
{
    pc_stats_t *s = pc_stats_create();
    char id[32];
    for (int i = 0; i < 5000; i++) { sprintf(id, "mp3:/t%d", i); pc_stats_add_play(s, id); }
    for (int i = 0; i < 5000; i += 97) { sprintf(id, "mp3:/t%d", i); CHECK_EQ_INT(pc_stats_plays(s, id), 1); }
    CHECK_EQ_INT(pc_stats_load(s, "/nonexistent/stats.txt"), 0);    /* missing file: nothing loaded, no error */
    pc_stats_destroy(s);
}

int main(void)
{
    RUN(test_stats_roundtrip);
    RUN(test_stats_dirty);
    RUN(test_stats_many);
    return 0;
}
