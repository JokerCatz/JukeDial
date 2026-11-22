// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_pack.h"
#include <ctype.h>
#include <string.h>

int pc_pack_parse(const char *txt, size_t n, pc_pack_t *p)
{
    p->rev = -1; p->version[0] = 0;
    for (size_t i = 0; i < n;) {
        size_t e = i; while (e < n && txt[e] != '\n') e++;
        size_t l = e - i;
        if (l > 5 && !memcmp(txt + i, "pack=", 5)) {
            long v = 0; size_t k = i + 5; int d = 0;
            while (k < e && isdigit((unsigned char)txt[k]) && v < 100000) { v = v * 10 + (txt[k] - '0'); k++; d++; }
            if (d) p->rev = (int)v;
        } else if (l > 8 && !memcmp(txt + i, "version=", 8)) {
            size_t m = l - 8; if (m >= sizeof p->version) m = sizeof p->version - 1;
            memcpy(p->version, txt + i + 8, m); p->version[m] = 0;
            while (m && (p->version[m - 1] == '\r' || p->version[m - 1] == ' ')) p->version[--m] = 0;
        }
        i = e + 1;
    }
    return p->rev < 0 ? -1 : 0;
}

pc_pack_state_t pc_pack_state(int rev) { return rev < 0 ? PC_PACK_MISSING : rev >= PC_PACK_MIN ? PC_PACK_OK : PC_PACK_OLD; }
