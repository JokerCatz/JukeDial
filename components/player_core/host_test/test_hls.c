// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdint.h>
#include "pc/pc_hls.h"
#include "pc/pc_radio.h"
#include "pc_test.h"

static void test_resolve(void)
{
    char o[256];
    pc_url_resolve("http://h.net/a/b/list.m3u8?x=1", "seg1.ts", o, sizeof o);   CHECK_STR(o, "http://h.net/a/b/seg1.ts");
    pc_url_resolve("http://h.net/a/b/list.m3u8", "../c/s.ts?t=2", o, sizeof o);  CHECK_STR(o, "http://h.net/a/c/s.ts?t=2");
    pc_url_resolve("http://h.net:8080/a/list.m3u8", "/root/s.ts", o, sizeof o);  CHECK_STR(o, "http://h.net:8080/root/s.ts");
    pc_url_resolve("http://h.net/a/list.m3u8", "http://other/x.ts", o, sizeof o); CHECK_STR(o, "http://other/x.ts");
    pc_url_resolve("http://h.net/a/list.m3u8", "//cdn.net/x.ts", o, sizeof o);   CHECK_STR(o, "http://cdn.net/x.ts");
}

static void test_is_hls(void)
{
    CHECK(pc_hls_is_hls("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n"));
    CHECK(!pc_hls_is_hls("#EXTM3U\n#EXTINF:-1,Radio\nhttp://a/stream.mp3\n"));   /* a plain playlist */
    CHECK(!pc_hls_is_hls("[playlist]\nFile1=http://a\n"));
}

static void test_master_picks_variant(void)
{
    static pc_hls_t h;
    const char *m = "#EXTM3U\n"
                    "#EXT-X-STREAM-INF:BANDWIDTH=48000,CODECS=\"mp4a.40.5\"\nlow/index.m3u8\n"
                    "#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\nmid/index.m3u8\n"
                    "#EXT-X-STREAM-INF:BANDWIDTH=320000\nhigh/index.m3u8\n";
    CHECK_EQ_INT(pc_hls_parse(m, "http://r.tw/live/master.m3u8", 200000, &h), 0);
    CHECK(h.master);
    CHECK_STR(h.variant, "http://r.tw/live/mid/index.m3u8");
    CHECK_EQ_INT(pc_hls_parse(m, "http://r.tw/live/master.m3u8", 20000, &h), 0);
    CHECK_STR(h.variant, "http://r.tw/live/low/index.m3u8");         /* nothing small enough: the lowest */
}

static void test_media_and_next(void)
{
    static pc_hls_t h;
    const char *m = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:280350520\n#EXT-X-TARGETDURATION:6\n"
                    "#EXTINF:6.4, no desc\nbbc-280350520.ts\n#EXTINF:6.4,\nbbc-280350521.ts\n"
                    "#EXTINF:6.4,\nbbc-280350522.ts\n#EXTINF:6.4,\nbbc-280350523.ts\n#EXTINF:6.4,\nbbc-280350524.ts\n";
    CHECK_EQ_INT(pc_hls_parse(m, "http://as-hls.net/live/bbc.m3u8", 200000, &h), 0);
    CHECK(!h.master);
    CHECK_EQ_INT(h.target_s, 6);
    CHECK_EQ_INT(h.nseg, 5);
    CHECK_STR(h.seg[0], "http://as-hls.net/live/bbc-280350520.ts");
    CHECK(h.seg_seq[4] == 280350524ULL);
    CHECK_EQ_INT(pc_hls_next(&h, UINT64_MAX), 2);                    /* start 3 segments back from live */
    CHECK_EQ_INT(pc_hls_next(&h, 280350522ULL), 3);
    CHECK_EQ_INT(pc_hls_next(&h, 280350524ULL), -1);                 /* caught up: wait for the list to grow */
    CHECK_EQ_INT(pc_hls_next(&h, 280350000ULL), 0);                  /* fell behind the window: oldest still listed */
    CHECK_EQ_INT(pc_hls_next(&h, 999999999ULL), 2);                  /* the encoder restarted (numbers went back): start again */
}

static void test_sniff_ts(void)
{
    static unsigned char b[188 * 4];
    memset(b, 0xFF, sizeof b);
    for (int i = 0; i < 4; i++) b[i * 188] = 0x47;
    CHECK_EQ_INT(pc_radio_sniff(b, sizeof b), PC_FMT_TS);
}

int main(void)
{
    RUN(test_resolve);
    RUN(test_is_hls);
    RUN(test_master_picks_variant);
    RUN(test_media_and_next);
    RUN(test_sniff_ts);
    return 0;
}
