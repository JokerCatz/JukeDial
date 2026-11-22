// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include <stdbool.h>
#include "pc/pc_put.h"
#include "pc_test.h"

static void test_parse(void)
{
    pc_put_chunk_t c;
    CHECK_EQ_INT(pc_put_parse("@0 5 aGVsbG8=", &c), 0);
    CHECK_EQ_INT(c.off, 0);
    CHECK_EQ_INT(c.n, 5);
    CHECK_STR(c.b64, "aGVsbG8=");
    CHECK_EQ_INT(pc_put_parse("@4294966528 768 QUJD", &c), 0);            /* any 32-bit offset */
    CHECK_EQ_INT(c.off, 4294966528u);
    const char *bad[] = { "", "@", "@0", "@0 5", "@0 5 ", "0 5 aGVs", "@x 5 aGVs", "@0 x aGVs", "@0 0 aGVs", "@-1 5 aGVs",
                          "@0 -5 aGVs", "@4294967296 5 aGVs", "@0 5000 aGVs", "@ 0 5 aGVs", "@0  5 aGVs", "@01x 5 aGVs",
                          "@+1 5 aGVs", "@99999999999999999999 5 aGVs" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (pc_put_parse(bad[i], &c) != -EINVAL) { fprintf(stderr, "accepted \"%s\"\n", bad[i]); exit(1); }
    CHECK_EQ_INT(pc_put_parse(NULL, &c), -EINVAL);
}

static void test_decide(void)
{
    CHECK_EQ_INT(pc_put_decide(0, 0, 768), PC_PUT_WRITE);                 /* in order */
    CHECK_EQ_INT(pc_put_decide(768, 768, 768), PC_PUT_WRITE);
    CHECK_EQ_INT(pc_put_decide(1536, 768, 768), PC_PUT_HAVE);             /* the reply was lost, the host resent it */
    CHECK_EQ_INT(pc_put_decide(1536, 0, 768), PC_PUT_HAVE);               /* any chunk already on the card */
    CHECK_EQ_INT(pc_put_decide(1536, 2304, 768), PC_PUT_GAP);             /* a chunk is missing: tell the size */
    CHECK_EQ_INT(pc_put_decide(1000, 768, 768), PC_PUT_GAP);              /* overlaps the end: resume from the size */
    CHECK_EQ_INT(pc_put_decide(0xFFFFFF00u, 0xFFFFFF00u, 0x200), PC_PUT_GAP);   /* past 4 GB */
    CHECK_EQ_INT(pc_put_decide(5, 5, 0), PC_PUT_GAP);
}

/* the host's resend after lost replies, run against pc_put_decide: whatever is lost, the file ends up whole and
 * nothing is written twice */
static void test_lossy_session(void)
{
    enum { SIZE = 10000, CH = 768 };
    static uint8_t src[SIZE], dst[SIZE];
    for (int i = 0; i < SIZE; i++) src[i] = (uint8_t)(i * 7 + 3);
    uint32_t cur = 0, writes = 0, host = 0, seed = 1, sent = 0;
    while (host < SIZE) {
        uint32_t n = SIZE - host < CH ? SIZE - host : CH;
        sent++;
        if (pc_put_decide(cur, host, n) == PC_PUT_WRITE) { memcpy(dst + cur, src + host, n); cur += n; writes++; }
        seed = seed * 1103515245u + 12345u;
        bool lost = (seed >> 16) % 3 == 0;                      /* a third of the replies never arrive */
        if (!lost) host = cur;                                  /* each reply carries the size: go on from there */
    }                                                           /* lost: timeout, the same chunk again */
    CHECK_EQ_INT(cur, SIZE);
    CHECK_EQ_INT(writes, (SIZE + CH - 1) / CH);
    CHECK(sent > writes);
    CHECK(memcmp(src, dst, SIZE) == 0);
}

int main(void)
{
    RUN(test_parse);
    RUN(test_decide);
    RUN(test_lossy_session);
    return 0;
}
