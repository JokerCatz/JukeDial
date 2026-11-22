// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_layerset.h"
#include "pc_test.h"

static void test_default_first_three_on(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 6, 3);
    CHECK(s.on[0] && s.on[1] && s.on[2]);
    CHECK(!s.on[3] && !s.on[4] && !s.on[5]);
    CHECK_EQ_INT(pc_layerset_count(&s), 3);
}

static void test_default_fewer_layers(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 2, 3);
    CHECK_EQ_INT(pc_layerset_count(&s), 2);
}

static void test_fourth_refused(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 6, 3);
    CHECK_EQ_INT(pc_layerset_toggle(&s, 4), PC_LS_FULL);
    CHECK(!s.on[4]);
    CHECK_EQ_INT(pc_layerset_count(&s), 3);
}

static void test_swap_after_off(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 6, 3);
    CHECK_EQ_INT(pc_layerset_toggle(&s, 1), PC_LS_OFF);
    CHECK_EQ_INT(pc_layerset_toggle(&s, 4), PC_LS_ON);
    CHECK(s.on[0] && !s.on[1] && s.on[2] && s.on[4]);
    CHECK_EQ_INT(pc_layerset_count(&s), 3);
}

static void test_bad_index(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 2, 3);
    CHECK(pc_layerset_toggle(&s, 2) < 0);
    CHECK(pc_layerset_toggle(&s, -1) < 0);
}

/* a layer that failed to open is turned off and frees its place */
static void test_failed_layer_off(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 4, 3);
    pc_layerset_set(&s, 0, false);
    CHECK_EQ_INT(pc_layerset_count(&s), 2);
    CHECK_EQ_INT(pc_layerset_toggle(&s, 3), PC_LS_ON);
}

static void test_add_layer(void)
{
    pc_layerset_t s;
    pc_layerset_init(&s, 0, 3);                            /* blank scene */
    for (int i = 0; i < 3; i++) CHECK_EQ_INT(pc_layerset_add(&s), i);
    CHECK(s.on[0] && s.on[1] && s.on[2]);
    CHECK_EQ_INT(pc_layerset_add(&s), 3);
    CHECK(!s.on[3]);                                       /* fourth comes in off */
    CHECK_EQ_INT(pc_layerset_count(&s), 3);
    while (s.n < PC_SCENE_MAX_LAYERS) pc_layerset_add(&s);
    CHECK_EQ_INT(pc_layerset_add(&s), -1);                 /* full */
    CHECK_EQ_INT(s.n, PC_SCENE_MAX_LAYERS);
}

int main(void)
{
    RUN(test_add_layer);
    RUN(test_default_first_three_on);
    RUN(test_default_fewer_layers);
    RUN(test_fourth_refused);
    RUN(test_swap_after_off);
    RUN(test_bad_index);
    RUN(test_failed_layer_off);
    return 0;
}
