// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include "pc/pc_upload.h"
#include "pc/pc_zip.h"
#include "pc_test.h"

static void test_names(void)
{
    char path[PC_LIST_PATH_MAX];
    CHECK_EQ_INT(pc_list_path("lunch.zip", path, sizeof path), 0);
    CHECK_STR(path, "/sdcard/lists/lunch.zip");
    CHECK_EQ_INT(pc_list_path("午餐 2.zip", path, sizeof path), 0);   /* UTF-8 and spaces are fine (FAT long names) */
    CHECK_STR(path, "/sdcard/lists/午餐 2.zip");
    const char *bad[] = { "", ".zip", "a", "a.txt", "a.zip.new", "../a.zip", "..zip", "a/../b.zip", "/a.zip", "a/b.zip", "a\\b.zip",
                          "..", "a..b.zip", ".hidden.zip", "a:b.zip", "a*.zip", "a?.zip", "a\".zip", "a<.zip", "a>.zip", "a|.zip",
                          "a\x01.zip", "a\n.zip", " a.zip", "a .zip", "a.zip ", "a\x7f.zip",
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.zip" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (pc_list_path(bad[i], path, sizeof path) != -EINVAL) { fprintf(stderr, "accepted \"%s\"\n", bad[i]); exit(1); }
    }
    CHECK_EQ_INT(pc_list_path(NULL, path, sizeof path), -EINVAL);
    CHECK_EQ_INT(pc_list_path("a.zip", path, 10), -EINVAL);   /* does not fit the buffer */
    CHECK_EQ_INT(pc_list_path("\xff\xfe.zip", path, sizeof path), -EINVAL);   /* not UTF-8 */
    CHECK_EQ_INT(pc_list_path("\xe5\x8d.zip", path, sizeof path), -EINVAL);   /* cut in the middle of a character */
}

static void test_b64(void)
{
    uint8_t out[16];
    CHECK_EQ_INT(pc_b64_decode("aGVsbG8=", out, sizeof out), 5);
    CHECK(memcmp(out, "hello", 5) == 0);
    CHECK_EQ_INT(pc_b64_decode("aGVsbG8h", out, sizeof out), 6);
    CHECK_EQ_INT(pc_b64_decode("", out, sizeof out), 0);
    CHECK_EQ_INT(pc_b64_decode("aGVs*G8=", out, sizeof out), -EINVAL);
    CHECK_EQ_INT(pc_b64_decode("aGVsbG8", out, sizeof out), -EINVAL);     /* not a multiple of 4 */
    CHECK_EQ_INT(pc_b64_decode("aGVsbG8haGVsbG8haGVsbG8h", out, 16), -ENOSPC);
    CHECK_EQ_INT(pc_b64_decode("a===", out, sizeof out), -EINVAL);
    CHECK_EQ_INT(pc_b64_decode("aG=s", out, sizeof out), -EINVAL);        /* padding only at the end */
    char enc[32];
    CHECK_EQ_INT(pc_b64_encode((const uint8_t *)"hello", 5, enc, sizeof enc), 8);
    CHECK_STR(enc, "aGVsbG8=");
    CHECK_EQ_INT(pc_b64_encode((const uint8_t *)"hello", 5, enc, 8), -ENOSPC);   /* room for the 0 too */
    uint8_t all[256], back[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    char big[400];
    for (int n = 0; n <= 256; n += 17) {
        CHECK(pc_b64_encode(all, (uint32_t)n, big, sizeof big) >= 0);
        CHECK_EQ_INT(pc_b64_decode(big, back, sizeof back), n);
        CHECK(memcmp(all, back, (size_t)n) == 0);
    }
}

static void test_session(void)
{
    pc_upload_t u;
    CHECK_EQ_INT(pc_upload_begin(&u, 0), -EINVAL);
    CHECK_EQ_INT(pc_upload_begin(&u, PC_LIST_MAX_BYTES + 1), -E2BIG);
    CHECK_EQ_INT(pc_upload_begin(&u, 10), 0);
    const uint8_t a[] = "hello", b[] = "world";
    CHECK_EQ_INT(pc_upload_chunk(&u, 0, a, 5), PC_UPLOAD_WRITE);
    CHECK_EQ_INT(pc_upload_chunk(&u, 0, a, 5), PC_UPLOAD_DUP);          /* the page resent the last chunk: no write */
    CHECK_EQ_INT(pc_upload_chunk(&u, 0, a, 3), PC_UPLOAD_DUP);          /* any chunk already written (replies lost) */
    CHECK_EQ_INT(pc_upload_chunk(&u, 3, a, 3), -EINVAL);                /* overlaps the end: the host resumes from got */
    CHECK_EQ_INT(pc_upload_chunk(&u, 7, b, 3), -EINVAL);                /* a gap */
    CHECK_EQ_INT(pc_upload_chunk(&u, 5, b, 6), -E2BIG);                 /* past the announced size */
    CHECK_EQ_INT(pc_upload_end(&u, pc_crc32(0, "helloworld", 10)), -EINVAL);   /* not complete */
    CHECK_EQ_INT(pc_upload_chunk(&u, 5, b, 5), PC_UPLOAD_WRITE);
    CHECK_EQ_INT(u.got, 10);
    CHECK_EQ_INT(pc_upload_end(&u, 1234), -EBADMSG);                    /* CRC */
    CHECK_EQ_INT(pc_upload_end(&u, pc_crc32(0, "helloworld", 10)), 0);
    CHECK_EQ_INT(pc_upload_chunk(&u, 10, b, 1), -EINVAL);               /* finished */
    CHECK_EQ_INT(pc_upload_chunk(&u, 0, NULL, 0), -EINVAL);
}

int main(void)
{
    RUN(test_names);
    RUN(test_b64);
    RUN(test_session);
    return 0;
}
