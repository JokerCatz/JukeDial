// SPDX-License-Identifier: LGPL-2.1-or-later
/* fx apps: the fixed chain order (owner point 3, spec §2). Built twice: with CONFIG_FX_VELVET_ROOM=1 and without
 * (test_fxchain_order_novroom); the list is 14 ids with it, 13 without. */
#include "pc/pc_fxchain.h"
#include "pc_test.h"

static void test_order(void)
{
    static const char *const WANT[] = { "gain", "balance", "widener", "crossfeed", "velvet_widener", "equalizer", "tone", "bass", "loudness", "tube",
                                        "compressor", "freeverb",
#ifdef CONFIG_FX_VELVET_ROOM
                                        "velvet_room",
#endif
                                        "limiter" };
    const int nwant = (int)(sizeof WANT / sizeof *WANT);
    const pc_fxapp_t *const *a = NULL;
    int n = pc_fxapps(&a);
    CHECK(a != NULL);
    CHECK_EQ_INT(n, nwant);
#ifdef CONFIG_FX_VELVET_ROOM
    CHECK_EQ_INT(n, 14);
#else
    CHECK_EQ_INT(n, 13);
    CHECK(pc_fxapp_find("velvet_room") < 0);
#endif
    for (int i = 0; i < n; i++) CHECK_STR(a[i]->id, WANT[i]);
    CHECK(a[n - 1]->flags & PC_FXA_ALWAYS_LAST);
    CHECK_STR(a[n - 1]->id, "limiter");
    CHECK_EQ_INT(pc_fxapp_find("compressor"), 10);
    CHECK_EQ_INT(pc_fxapp_find("gain"), 0);
    CHECK_EQ_INT(pc_fxapp_find("nope"), -1);
    CHECK_EQ_INT(pc_fxapp_find(""), -1);
    CHECK_EQ_INT(pc_fxapp_find(NULL), -1);
#ifdef CONFIG_FX_VELVET_ROOM
    printf("built with CONFIG_FX_VELVET_ROOM\n");
#else
    printf("built without CONFIG_FX_VELVET_ROOM\n");
#endif
}

int main(void)
{
    RUN(test_order);
    return 0;
}
