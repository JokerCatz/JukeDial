// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pc/pc_radio.h"
#include "pc_test.h"

static void test_parse(void)
{
    pc_station_t s[4];
    const char *t = "# my stations\n"
                    "Radio Paradise|http://stream.radioparadise.com/mp3-192\r\n"
                    "\n"
                    "  bad line without bar\n"
                    "Soma | Groove|https://ice1.somafm.com/groovesalad-128-mp3  \n"
                    "|http://no.name/\n"
                    "Empty url|\n";
    int n = pc_radio_parse(t, s, 4);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(s[0].name, "Radio Paradise");
    CHECK_STR(s[0].url, "http://stream.radioparadise.com/mp3-192");
    CHECK_STR(s[1].name, "Soma | Groove");                    /* the url starts after the last bar */
    CHECK_STR(s[1].url, "https://ice1.somafm.com/groovesalad-128-mp3");
    CHECK_STR(s[2].name, "http://no.name/");                  /* no name: the url is the name */
}

static void test_parse_max(void)
{
    pc_station_t s[1];
    CHECK_EQ_INT(pc_radio_parse("a|http://a\nb|http://b\n", s, 1), 1);
}

static void test_format_round_trip(void)
{
    pc_station_t s[2] = { { .name = "A", .url = "http://a/x" }, { .name = "B b", .url = "https://b/y", .alt = { "http://b/z" } } }, o[2];
    char buf[128];
    CHECK(pc_radio_format(s, 2, buf, sizeof buf) > 0);
    CHECK_EQ_INT(pc_radio_parse(buf, o, 2), 2);
    CHECK_STR(o[1].name, "B b");
    CHECK_STR(o[1].url, "https://b/y");
    CHECK_STR(o[1].alt[0], "http://b/z");
    CHECK_EQ_INT(pc_radio_format(s, 2, buf, 8), -1);
}

static void test_playlists(void)
{
    char u[256];
    CHECK(pc_playlist_first_url("[playlist]\nNumberOfEntries=2\nFile1=http://a.example/s1\nTitle1=x\nFile2=http://b/\n", u, sizeof u));
    CHECK_STR(u, "http://a.example/s1");
    CHECK(pc_playlist_first_url("#EXTM3U\r\n#EXTINF:-1,Name\r\nhttps://c.example/live.mp3\r\n", u, sizeof u));
    CHECK_STR(u, "https://c.example/live.mp3");
    CHECK(!pc_playlist_first_url("<html>nope</html>", u, sizeof u));
    CHECK(pc_playlist_first_url("File1=https://secure/s\nFile2=http://plain/s\n", u, sizeof u));
    CHECK_STR(u, "http://plain/s");                           /* https is not playable here: the first http one */
    CHECK(pc_playlist_first_url("https://only/secure\n", u, sizeof u));
    CHECK_STR(u, "https://only/secure");                      /* nothing better: the caller reports https */
}

static void test_guess(void)
{
    CHECK_EQ_INT(pc_radio_guess("audio/mpeg", NULL), PC_FMT_MP3);
    CHECK_EQ_INT(pc_radio_guess("audio/aacp", NULL), PC_FMT_AAC);
    CHECK_EQ_INT(pc_radio_guess("audio/aac; charset=x", NULL), PC_FMT_AAC);
    CHECK_EQ_INT(pc_radio_guess("audio/x-scpls", NULL), PC_FMT_PLAYLIST);
    CHECK_EQ_INT(pc_radio_guess("audio/x-mpegurl", NULL), PC_FMT_PLAYLIST);
    CHECK_EQ_INT(pc_radio_guess("application/octet-stream", "http://x/live.aac"), PC_FMT_AAC);
    CHECK_EQ_INT(pc_radio_guess(NULL, "http://x/list.pls"), PC_FMT_PLAYLIST);
    CHECK_EQ_INT(pc_radio_guess(NULL, "http://x/stream"), PC_FMT_UNKNOWN);
    CHECK_EQ_INT(pc_radio_guess(NULL, "http://x/live/index.m3u8?t=1"), PC_FMT_PLAYLIST);   /* HLS starts as a playlist */
}

static void test_prebuffer(void)
{
    CHECK_EQ_INT(pc_radio_prebuffer(128), 32000);             /* 2 s at icy-br */
    CHECK_EQ_INT(pc_radio_prebuffer(320), 80000);
    CHECK_EQ_INT(pc_radio_prebuffer(24), 8192);               /* clamped */
    CHECK_EQ_INT(pc_radio_prebuffer(1000), 98304);
    CHECK_EQ_INT(pc_radio_prebuffer(0), 32768);               /* unknown */
    CHECK_EQ_INT(pc_radio_prebuffer(5000), 32768);            /* nonsense */
}

/* ADTS frame: 7-byte header (no CRC), 48 kHz stereo, frame_length = len */
static size_t adts(unsigned char *b, size_t len)
{
    memset(b, 0x55, len);
    b[0] = 0xFF; b[1] = 0xF1; b[2] = 0x4C; b[3] = (unsigned char)(0x80 | ((len >> 11) & 3));
    b[4] = (unsigned char)(len >> 3); b[5] = (unsigned char)(((len & 7) << 5) | 0x1F); b[6] = 0xFC;
    return len;
}

static void test_sniff_adts(void)
{
    static unsigned char b[4096];
    size_t n = 5;                                             /* some junk first: we joined mid-frame */
    memset(b, 0, 5);
    n += adts(b + n, 371); n += adts(b + n, 402); n += adts(b + n, 388);
    CHECK_EQ_INT(pc_radio_sniff(b, n), PC_FMT_AAC);
    CHECK_EQ_INT(pc_radio_sniff(b, 5 + 371 + 402), PC_FMT_UNKNOWN);   /* two frames are not enough */
}

static void test_sniff_mp3(void)
{
    char p[512];
    snprintf(p, sizeof p, "%s/song_head.mp3", FIXTURES);
    FILE *f = fopen(p, "rb");
    CHECK(f);
    static unsigned char b[16384];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    CHECK_EQ_INT(pc_radio_sniff(b, n), PC_FMT_MP3);
    static unsigned char z[4096];
    CHECK_EQ_INT(pc_radio_sniff(z, sizeof z), PC_FMT_UNKNOWN);
}

static void test_ogg(void)
{
    static unsigned char b[4096];
    memset(b, 0x11, sizeof b);
    memcpy(b + 700, "OggS\0\2", 6);                         /* joined mid-page: the next page header */
    CHECK_EQ_INT(pc_radio_sniff(b, sizeof b), PC_FMT_OGG);
    CHECK_EQ_INT(pc_radio_guess("application/ogg", NULL), PC_FMT_OGG);
    CHECK_EQ_INT(pc_radio_guess("audio/ogg; codecs=opus", NULL), PC_FMT_OGG);
    CHECK_EQ_INT(pc_radio_guess(NULL, "http://x/live.opus"), PC_FMT_OGG);
    CHECK_EQ_INT(pc_radio_guess(NULL, "http://x/stream.ogg?id=1"), PC_FMT_OGG);
}

static void test_backups(void)
{
    pc_station_t s[4];
    const char *t = "A|http://a/1|http://a/2|http://a/3\n"
                    "B | x|http://b/1|https://b/2\n"
                    "C|http://c/1||http://c/3\n"
                    "D|http://d/1|http://d/2|http://d/3|http://d/4\n";
    CHECK_EQ_INT(pc_radio_parse(t, s, 4), 4);
    CHECK_STR(s[0].name, "A");
    CHECK_STR(s[0].url, "http://a/1");
    CHECK_STR(s[0].alt[0], "http://a/2");
    CHECK_STR(s[0].alt[1], "http://a/3");
    CHECK_EQ_INT(pc_station_urls(&s[0]), 3);
    CHECK_STR(pc_station_url(&s[0], 2), "http://a/3");
    CHECK(pc_station_url(&s[0], 3) == NULL);
    CHECK_STR(s[1].name, "B | x");                              /* a name with a bar still works next to backups */
    CHECK_STR(s[1].alt[0], "https://b/2");
    CHECK_EQ_INT(pc_station_urls(&s[1]), 2);
    CHECK_STR(s[2].alt[0], "http://c/3");                       /* an empty backup field is skipped */
    CHECK_EQ_INT(pc_station_urls(&s[2]), 2);
    CHECK_EQ_INT(pc_station_urls(&s[3]), 3);                    /* a 4th address is ignored */
    char buf[512], buf2[512];
    pc_station_t o[4];
    int len = pc_radio_format(s, 4, buf, sizeof buf);
    CHECK(len > 0);
    CHECK_EQ_INT(pc_radio_parse(buf, o, 4), 4);
    CHECK_STR(o[0].alt[1], "http://a/3");
    CHECK(pc_radio_format(o, 4, buf2, sizeof buf2) == len && !strcmp(buf, buf2));
    CHECK_EQ_INT(pc_radio_format(s, 4, buf, 30), -1);
    pc_station_t one[1];                                        /* the old files: one address, a name with a bar and no http after it */
    CHECK_EQ_INT(pc_radio_parse("x|y|z.example/stream\n", one, 1), 1);
    CHECK_STR(one[0].name, "x|y");
    CHECK_STR(one[0].url, "z.example/stream");
    CHECK_EQ_INT(pc_station_urls(&one[0]), 1);
}

static void test_https_policy(void)
{
    pc_station_t s = { 0 };
    snprintf(s.name, sizeof s.name, "a");
    snprintf(s.url, sizeof s.url, "http://h/a");                              /* http first: unchanged behaviour */
    CHECK_EQ_INT(pc_station_pick(&s, false), 0);
    CHECK_EQ_INT(pc_station_pick(&s, true), 0);
    snprintf(s.url, sizeof s.url, "https://h/a"); snprintf(s.alt[0], sizeof s.alt[0], "http://h/b");
    CHECK_EQ_INT(pc_station_pick(&s, true), 1);                               /* both: http wins even when it is the backup */
    CHECK_EQ_INT(pc_station_pick(&s, false), 1);
    s.alt[0][0] = 0;
    CHECK_EQ_INT(pc_station_pick(&s, true), 0);                               /* https only: used when TLS is allowed */
    CHECK_EQ_INT(pc_station_pick(&s, false), -1);                             /* refused in this mode */
    snprintf(s.url, sizeof s.url, "ftp://x"); CHECK_EQ_INT(pc_station_pick(&s, true), -1);
    CHECK(pc_url_is_https("HTTPS://x") && !pc_url_is_https("http://x") && pc_url_is_web("http://x") && !pc_url_is_web("ftp://x") && !pc_url_is_web(NULL));
    char o[64];
    CHECK_EQ_INT(pc_url_http_twin("https://a.b/c?d=1", o, sizeof o), -1);          /* a query string may hold a token: no cleartext twin (FIX3 M5) */
    CHECK_EQ_INT(pc_url_http_twin("https://user:pw@a.b/c", o, sizeof o), -1);       /* userinfo: never in the clear */
    CHECK_EQ_INT(pc_url_http_twin("https://a.b/c@d", o, sizeof o) > 0, 1);          /* an '@' in the path is not userinfo */
    CHECK(pc_url_redact("https://user:pw@a.b:8443/c/d.mp3?token=x#f", o, sizeof o) > 0 && !strcmp(o, "https://a.b:8443/c/d.mp3"));
    CHECK(pc_url_redact("http://a.b/c", o, sizeof o) > 0 && !strcmp(o, "http://a.b/c"));
    CHECK(pc_url_redact("http://a.b?k=1", o, sizeof o) > 0 && !strcmp(o, "http://a.b"));
    CHECK_EQ_INT(pc_url_redact("http://a.b/c", o, 5), -1);
    CHECK(pc_url_http_twin("https://a.b:443/c", o, sizeof o) > 0 && !strcmp(o, "http://a.b/c"));
    CHECK(pc_url_http_twin("https://a.b:8443/c", o, sizeof o) > 0 && !strcmp(o, "http://a.b:8443/c"));
    CHECK(pc_url_http_twin("https://a.b", o, sizeof o) > 0 && !strcmp(o, "http://a.b"));
    CHECK_EQ_INT(pc_url_http_twin("http://a.b/", o, sizeof o), -1);
    CHECK_EQ_INT(pc_url_http_twin("https://a.b/c", o, 8), -1);
}

typedef struct { int calls; char seen[4][64]; bool http_ok, https_ok; } fk_t;
static int fk_try(void *c, const char *u)     /* a fake client: no network */
{
    fk_t *f = c;
    if (f->calls < 4) snprintf(f->seen[f->calls], 64, "%s", u);
    f->calls++;
    return pc_url_is_https(u) ? f->https_ok : f->http_ok;
}
static int stop_try(void *c, const char *u) { (void)u; ((int *)c)[0]++; return PC_TRY_STOP; }
static void test_twin_play(void)
{
    char used[256];
    uint8_t cache = 0;
    fk_t f = { .http_ok = true, .https_ok = true };
    CHECK_EQ_INT(pc_https_play(&cache, "http://h/a", used, sizeof used, fk_try, &f), 1);   /* plain http: untouched, one try, cache unused */
    CHECK(f.calls == 1 && cache == PC_TWIN_UNKNOWN && !strcmp(used, "http://h/a"));
    f = (fk_t){ .http_ok = true, .https_ok = true };
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, fk_try, &f), 1);  /* https-only listing: the twin first */
    CHECK(f.calls == 1 && !strcmp(used, "http://h/a") && cache == PC_TWIN_OK);
    f = (fk_t){ .http_ok = false, .https_ok = true };                                      /* the twin dies later: cache cleared, https this time */
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, fk_try, &f), 1);
    CHECK(f.calls == 2 && !strcmp(used, "https://h/a") && cache == PC_TWIN_UNKNOWN);
    cache = 0; f.calls = 0;  /* first time the twin fails: remembered */
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, fk_try, &f), 1);
    CHECK(f.calls == 2 && cache == PC_TWIN_BAD);
    f = (fk_t){ .http_ok = true, .https_ok = true };                                       /* next play: no needless retry of the twin */
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, fk_try, &f), 1);
    CHECK(f.calls == 1 && !strcmp(used, "https://h/a") && cache == PC_TWIN_BAD);
    f = (fk_t){ 0 }; cache = 0;                                                            /* both fail */
    CHECK_EQ_INT(pc_https_play(&cache, "https://h:8443/a", used, sizeof used, fk_try, &f), 0);
    CHECK(f.calls == 2 && !strcmp(f.seen[0], "http://h:8443/a") && !strcmp(f.seen[1], "https://h:8443/a"));
    /* FIX3 I4: the twin's attempt ended without a verdict (a station switch, a finished list): no https try, the cache unchanged */
    int calls = 0;
    cache = PC_TWIN_UNKNOWN;
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, stop_try, &calls), PC_TRY_STOP);
    CHECK(calls == 1 && cache == PC_TWIN_UNKNOWN);
    calls = 0; cache = PC_TWIN_OK;
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a", used, sizeof used, stop_try, &calls), PC_TRY_STOP);
    CHECK(calls == 1 && cache == PC_TWIN_OK);
    /* an address that has no twin (a query string) goes straight to https: one try, no cache */
    f = (fk_t){ .http_ok = true, .https_ok = true }; cache = 0;
    CHECK_EQ_INT(pc_https_play(&cache, "https://h/a?t=1", used, sizeof used, fk_try, &f), 1);
    CHECK(f.calls == 1 && !strcmp(used, "https://h/a?t=1") && cache == PC_TWIN_UNKNOWN);
}

static void test_diag(void)
{
    CHECK_EQ_INT(pc_radio_diag("https://x/", true, 200, 0, false), PC_RDIAG_OK);          /* https is judged like http now */
    CHECK_EQ_INT(pc_radio_diag("https://x/", true, 404, 0, false), PC_RDIAG_STATUS);
    CHECK_EQ_INT(pc_radio_diag("http://x/", false, 0, ECONNREFUSED, false), PC_RDIAG_REFUSED);
    CHECK_EQ_INT(pc_radio_diag("http://x/", false, 0, ETIMEDOUT, false), PC_RDIAG_TIMEOUT);
    CHECK_EQ_INT(pc_radio_diag("http://x/", false, 0, EAGAIN, false), PC_RDIAG_TIMEOUT);
    CHECK_EQ_INT(pc_radio_diag("http://x/", false, 0, 0, false), PC_RDIAG_FAILED);      /* no errno: a name that does not resolve, no route ...: not claimed as a timeout */
    CHECK_EQ_INT(pc_radio_diag("http://x/", false, 0, EHOSTUNREACH, false), PC_RDIAG_FAILED);
    CHECK_EQ_INT(pc_radio_diag("http://x/", true, 404, 0, false), PC_RDIAG_STATUS);
    CHECK_EQ_INT(pc_radio_diag("http://x/", true, 200, 0, true), PC_RDIAG_UNSUPPORTED);
    CHECK_EQ_INT(pc_radio_diag("http://x/", true, 200, 0, false), PC_RDIAG_OK);
}

static void test_backoff(void)
{
    CHECK_EQ_INT(pc_radio_backoff_ms(1, 1, false), 1000);                       /* one address: 1 2 4 ... 30 s as before */
    CHECK_EQ_INT(pc_radio_backoff_ms(2, 1, false), 2000);
    CHECK_EQ_INT(pc_radio_backoff_ms(6, 1, false), 30000);
    CHECK_EQ_INT(pc_radio_backoff_ms(40, 1, false), 30000);
    CHECK_EQ_INT(pc_radio_backoff_ms(1, 3, true), 1000);                        /* mid-round: at once */
    CHECK_EQ_INT(pc_radio_backoff_ms(3, 3, false), 1000);                       /* the first full round */
    CHECK_EQ_INT(pc_radio_backoff_ms(6, 3, false), 2000);
    CHECK_EQ_INT(pc_radio_backoff_ms(300, 3, false), 30000);                    /* never retries every second forever */
    int prev = 0;
    for (int f = 3; f <= 60; f += 3) { int b = pc_radio_backoff_ms(f, 3, false); CHECK(b >= prev && b <= 30000); prev = b; }
}

static void test_lists(void)
{
    CHECK(pc_radio_stem_ok("") && pc_radio_stem_ok("cn") && pc_radio_stem_ok("my-list_2"));
    CHECK(!pc_radio_stem_ok("CN") && !pc_radio_stem_ok("a/b") && !pc_radio_stem_ok("..") && !pc_radio_stem_ok("0123456789012345678901234") && !pc_radio_stem_ok(NULL));
    char p[64];
    CHECK(pc_radio_list_path("", p, sizeof p) > 0);
    CHECK_STR(p, "/sdcard/_player/config/radio.txt");
    CHECK(pc_radio_list_path("cn", p, sizeof p) > 0);
    CHECK_STR(p, "/sdcard/_player/config/radio_cn.txt");
    CHECK_EQ_INT(pc_radio_list_path("../x", p, sizeof p), -1);
    CHECK_EQ_INT(pc_radio_list_path("cn", p, 8), -1);
    CHECK_STR(pc_radio_default_stem(0), "");
    CHECK_STR(pc_radio_default_stem(1), "cn");
    CHECK_STR(pc_radio_default_stem(2), "");
    char dir[] = "/tmp/pcradioXXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char f[96];
    const char *names[] = { "radio_jazz.txt", "radio.txt", "radio_cn.txt", "radio_Bad.txt", "radio_x.bak", "app.txt", "radio_.txt" };
    for (int i = 0; i < 7; i++) { snprintf(f, sizeof f, "%s/%s", dir, names[i]); FILE *fp = fopen(f, "w"); if (fp) fclose(fp); }
    char st[8][PC_RADIO_STEM_MAX];
    CHECK_EQ_INT(pc_radio_scan_lists(dir, st, 8), 3);
    CHECK_STR(st[0], "");                                       /* the main list first, then by name */
    CHECK_STR(st[1], "cn");
    CHECK_STR(st[2], "jazz");
    CHECK_EQ_INT(pc_radio_scan_lists("/nonexistent-dir", st, 8), 0);
    for (int mx = 1; mx <= 3; mx++) { CHECK_EQ_INT(pc_radio_scan_lists(dir, st, mx), mx); CHECK_STR(st[0], ""); }   /* full slots: the main list is still found and first */
    for (int i = 0; i < 7; i++) { snprintf(f, sizeof f, "%s/%s", dir, names[i]); unlink(f); }
    rmdir(dir);
}

int main(void)
{
    RUN(test_backups);
    RUN(test_diag);
    RUN(test_backoff);
    RUN(test_lists);
    RUN(test_ogg);
    RUN(test_sniff_adts);
    RUN(test_sniff_mp3);
    RUN(test_prebuffer);
    RUN(test_parse);
    RUN(test_parse_max);
    RUN(test_format_round_trip);
    RUN(test_playlists);
    RUN(test_guess);
    RUN(test_https_policy);
    RUN(test_twin_play);
    return 0;
}
