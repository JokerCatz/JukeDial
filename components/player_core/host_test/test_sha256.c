// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdint.h>
#include <string.h>
#include "pc/pc_sha256.h"
#include "pc_test.h"

static void hex_of(const void *d, uint32_t n, uint32_t step, char hex[65])
{
    pc_sha256_t s; uint8_t o[32];
    pc_sha256_init(&s);
    for (uint32_t i = 0; i < n; i += step) pc_sha256_update(&s, (const uint8_t *)d + i, n - i < step ? n - i : step);
    pc_sha256_final(&s, o);
    pc_sha256_hex(o, hex);
}

static void test_vectors(void)
{
    char h[65];
    hex_of("", 0, 1, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    hex_of("abc", 3, 1, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* 56 bytes: the padding spills into a second block */
    hex_of(two, 56, 56, h);
    CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    hex_of(two, 56, 7, h);                                                            /* the same, fed in pieces */
    CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    static uint8_t a[1000000];
    memset(a, 'a', sizeof a);
    hex_of(a, sizeof a, 4093, h);
    CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

int main(void) { test_vectors(); return 0; }
