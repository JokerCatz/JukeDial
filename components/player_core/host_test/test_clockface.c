// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clockface.h"
#include "pc_test.h"

static void test_names_round_trip(void)
{
    CHECK_EQ_INT(pc_cf_count(), 8);
    CHECK_STR(pc_cf_name(6), "moon");
    CHECK_STR(pc_cf_name(7), "hud");
    CHECK_EQ_INT(pc_tf_count(), 3);
    CHECK_STR(pc_tf_name(PC_TF_HUD), "hud");
    CHECK_EQ_INT(pc_tf_parse("hud"), PC_TF_HUD);
    CHECK_EQ_INT(pc_tf_parse("classic"), 0);
    CHECK_EQ_INT(pc_hc_count(), 4);
    CHECK_STR(pc_hc_name(PC_HC_TRI), "tri");
    CHECK_EQ_INT(pc_hc_parse("cyan"), PC_HC_CYAN);
    CHECK_EQ_INT(pc_hc_parse("x"), 0);
    CHECK_EQ_INT(pc_cf_next(5), 6);
    for (int i = 0; i < pc_cf_count(); i++) CHECK_EQ_INT(pc_cf_parse(pc_cf_name(i)), i);
    CHECK_EQ_INT(pc_cf_parse("nonsense"), 0);
    CHECK_EQ_INT(pc_cf_parse(NULL), 0);
    CHECK_STR(pc_cf_name(0), "info");
    CHECK_STR(pc_cf_name(99), "info");
    CHECK_EQ_INT(pc_cf_next(pc_cf_count() - 1), 0);
    CHECK_EQ_INT(pc_cf_next(0), 1);
}

static void test_hands(void)
{
    pc_cf_hands_t h;
    pc_cf_hands(0, 0, 0, 0, &h);
    CHECK_EQ_INT(h.hour, 0); CHECK_EQ_INT(h.minute, 0); CHECK_EQ_INT(h.second, 0);
    pc_cf_hands(3, 0, 0, 0, &h);
    CHECK_EQ_INT(h.hour, 900);                       /* deci-degrees clockwise from 12 */
    pc_cf_hands(15, 30, 0, 0, &h);
    CHECK_EQ_INT(h.hour, 1050);                      /* 3:30 -> 105 deg */
    CHECK_EQ_INT(h.minute, 1800);
    pc_cf_hands(10, 10, 30, 500, &h);
    CHECK_EQ_INT(h.second, 1830);                    /* 30.5 s -> 183 deg */
    CHECK_EQ_INT(h.minute, 630);                     /* 10.5 min -> 63 deg (minute hand creeps with seconds) */
    pc_cf_hands(23, 59, 59, 999, &h);
    CHECK(h.hour < 3600 && h.minute < 3600 && h.second < 3600);
}

static void test_point(void)
{
    int x, y;
    pc_cf_point(180, 180, 0, 100, &x, &y);    CHECK_EQ_INT(x, 180); CHECK_EQ_INT(y, 80);
    pc_cf_point(180, 180, 900, 100, &x, &y);  CHECK_EQ_INT(x, 280); CHECK_EQ_INT(y, 180);
    pc_cf_point(180, 180, 1800, 100, &x, &y); CHECK_EQ_INT(x, 180); CHECK_EQ_INT(y, 280);
    pc_cf_point(180, 180, 2700, 100, &x, &y); CHECK_EQ_INT(x, 80);  CHECK_EQ_INT(y, 180);
}

static int area(const pc_cf_box_t *b) { return (b->x2 - b->x1 + 1) * (b->y2 - b->y1 + 1); }
static void test_hand_boxes_cover_and_are_small(void)
{
    pc_cf_box_t b[8];
    int n = pc_cf_hand_boxes(180, 180, 450, -20, 150, 2, 4, b, 8);
    CHECK_EQ_INT(n, 4);
    int tot = 0;
    for (int i = 0; i < n; i++) tot += area(&b[i]);
    CHECK(tot < 120 * 120 / 2);                 /* far less than one box round the whole diagonal hand */
    for (int r = -20; r <= 150; r++) {          /* every point along the hand, with its width, lies in some box */
        int x, y, in = 0;
        pc_cf_point(180, 180, 450, r, &x, &y);
        for (int i = 0; i < n; i++) in |= x - 2 >= b[i].x1 && x + 2 <= b[i].x2 && y - 2 >= b[i].y1 && y + 2 <= b[i].y2;
        CHECK(in);
    }
    CHECK_EQ_INT(pc_cf_hand_boxes(180, 180, 0, 0, 100, 2, 9, b, 8), 8);   /* capped by the array */
}

static void test_sun_dial(void)
{
    /* rise 06:00, set 18:00: rise at 9 o'clock (270), noon at the top, set at 3 o'clock, midnight at the bottom */
    CHECK_EQ_INT(pc_cf_sun_deg(360, 360, 1080, 0), 2700);
    CHECK_EQ_INT(pc_cf_sun_deg(720, 360, 1080, 0), 0);
    CHECK_EQ_INT(pc_cf_sun_deg(1080, 360, 1080, 0), 900);
    CHECK_EQ_INT(pc_cf_sun_deg(0, 360, 1080, 0), 1800);
    CHECK_EQ_INT(pc_cf_sun_deg(1439, 360, 1080, 0) / 10, 179);
    /* uneven day: rise 05:00 set 19:00 -> 12:00 at the top */
    CHECK_EQ_INT(pc_cf_sun_deg(720, 300, 1140, 0), 0);
    CHECK(pc_cf_is_day(720, 300, 1140, 0));
    CHECK(!pc_cf_is_day(1200, 300, 1140, 0));
    CHECK(!pc_cf_is_day(200, 300, 1140, 0));
    /* polar day: always up, the sun runs the top half by the clock; polar night: the bottom half */
    CHECK(pc_cf_is_day(0, 0, 0, 1));
    CHECK(!pc_cf_is_day(720, 0, 0, -1));
    CHECK_EQ_INT(pc_cf_sun_deg(720, 0, 0, 1), 0);
    CHECK_EQ_INT(pc_cf_sun_deg(0, 0, 0, -1), 1800);
}

static void test_ring_fractions(void)
{
    pc_cf_rings_t r;
    pc_cf_rings(6, 30, 15, 0, &r);
    CHECK_EQ_INT(r.hour, 542);                  /* 6:30:15 of 12 h -> permille */
    CHECK_EQ_INT(r.minute, 504);                /* 30.25 min */
    CHECK_EQ_INT(r.second, 250);
    pc_cf_rings(12, 0, 0, 0, &r);
    CHECK_EQ_INT(r.hour, 0);
    pc_cf_rings(0, 0, 59, 999, &r);
    CHECK(r.second <= 1000);
}

int main(void)
{
    RUN(test_names_round_trip);
    RUN(test_hands);
    RUN(test_point);
    RUN(test_hand_boxes_cover_and_are_small);
    RUN(test_sun_dial);
    RUN(test_ring_fractions);
    return 0;
}
