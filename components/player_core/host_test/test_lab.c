// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lab.h"
#include "pc_test.h"

static void test_table(void)
{
    for (int i = 0; i < PC_LAB_N; i++) {
        const pc_lab_def_t *d = pc_lab_def(i);
        CHECK(d && !strncmp(d->key, "lab_", 4));
        CHECK_EQ_INT(pc_lab_find(d->key), i);                 /* with the prefix */
        CHECK_EQ_INT(pc_lab_find(d->key + 4), i);             /* without */
        CHECK(strlen(d->key) < 32 - 1);                       /* the config key limit (pc_kv_t) */
        CHECK(pc_lab_ok(i, pc_lab_default(i, true)));         /* a default is always allowed, on both boards */
        CHECK(pc_lab_ok(i, pc_lab_default(i, false)));
        CHECK(pc_lab_ok(i, d->min)); CHECK(pc_lab_ok(i, d->max));
        CHECK(!pc_lab_ok(i, d->min - 1)); CHECK(!pc_lab_ok(i, d->max + 1));
        for (int j = i + 1; j < PC_LAB_N; j++) CHECK(strcmp(d->key, pc_lab_def(j)->key));   /* no key twice */
    }
    CHECK(pc_lab_def(-1) == NULL); CHECK(pc_lab_def(PC_LAB_N) == NULL);
    CHECK_EQ_INT(pc_lab_find("nope"), -1); CHECK_EQ_INT(pc_lab_find(NULL), -1); CHECK_EQ_INT(pc_lab_find("lab_"), -1);
}

static void test_defaults_are_todays(void)
{
    CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_EDGE, true), 50); CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_EDGE, false), 56);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_MS, true), 300); CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_START_MS, true), 150);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_PCT, true), 40); CHECK_EQ_INT(pc_lab_default(PC_LAB_FLICK_H_PCT, true), 30);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_NOWB_HIDE_MS, false), 4000);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_KEY_DBL_MS, false), 300); CHECK_EQ_INT(pc_lab_default(PC_LAB_KEY_HOLD_MS, false), 450); CHECK_EQ_INT(pc_lab_default(PC_LAB_KEY_REP_MS, false), 140);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_TOUCH_X_NUM, false), 29);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_CO5300_C4, false), 1); CHECK_EQ_INT(pc_lab_default(PC_LAB_CO5300_58, false), 1);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_DAC_PDN_PAIR, false), 0); CHECK_EQ_INT(pc_lab_default(PC_LAB_DAC_HP_INV, false), 0); CHECK_EQ_INT(pc_lab_default(PC_LAB_DAC_XTAL_IBIAS, false), 4);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_IMU_SCALE_PM, false), 1000); CHECK_EQ_INT(pc_lab_default(PC_LAB_USB_SUSPEND_MS, false), 2000);
}

static void test_ranges(void)
{
    CHECK(pc_lab_ok(PC_LAB_FLICK_EDGE, 100)); CHECK(!pc_lab_ok(PC_LAB_FLICK_EDGE, 19)); CHECK(!pc_lab_ok(PC_LAB_FLICK_EDGE, 121));
    CHECK(pc_lab_ok(PC_LAB_NOWB_HIDE_MS, 0)); CHECK(!pc_lab_ok(PC_LAB_NOWB_HIDE_MS, 500)); CHECK(pc_lab_ok(PC_LAB_NOWB_HIDE_MS, 1000));   /* 0 = never, nothing between 0 and 1000 */
    CHECK(pc_lab_ok(PC_LAB_USB_SUSPEND_MS, 0)); CHECK(!pc_lab_ok(PC_LAB_USB_SUSPEND_MS, 499)); CHECK(!pc_lab_ok(PC_LAB_USB_SUSPEND_MS, 30001));
    CHECK(pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 2)); CHECK(pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 4)); CHECK(pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 6));
    CHECK(!pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 3)); CHECK(!pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 5)); CHECK(!pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 0)); CHECK(!pc_lab_ok(PC_LAB_DAC_XTAL_IBIAS, 8));
    CHECK(pc_lab_ok(PC_LAB_TOUCH_X_NUM, 26)); CHECK(pc_lab_ok(PC_LAB_TOUCH_X_NUM, 32)); CHECK(!pc_lab_ok(PC_LAB_TOUCH_X_NUM, 33));
    CHECK(!pc_lab_ok(PC_LAB_CO5300_C4, 2)); CHECK(!pc_lab_ok(PC_LAB_DAC_PDN_PAIR, -1));
    CHECK(pc_lab_ok(PC_LAB_IMU_SCALE_PM, 800)); CHECK(pc_lab_ok(PC_LAB_IMU_SCALE_PM, 1250)); CHECK(!pc_lab_ok(PC_LAB_IMU_SCALE_PM, 799));
    CHECK(!pc_lab_ok(-1, 0)); CHECK(!pc_lab_ok(PC_LAB_N, 0));
    /* the key timings stay under the hold cap (2500 ms) and the double-click window never swallows the hold */
    CHECK(pc_lab_def(PC_LAB_KEY_HOLD_MS)->max < 2500);
}

static void test_parse_and_when(void)
{
    int v = 7;
    CHECK(pc_lab_parse_int("100", &v) && v == 100); CHECK(pc_lab_parse_int("-5", &v) && v == -5);
    CHECK(!pc_lab_parse_int("", &v)); CHECK(!pc_lab_parse_int("1x", &v)); CHECK(!pc_lab_parse_int(" 1", &v) || v == 1); CHECK(!pc_lab_parse_int("99999999999", &v)); CHECK(!pc_lab_parse_int(NULL, &v));
    CHECK_STR(pc_lab_when_name(PC_LAB_LIVE), "live"); CHECK_STR(pc_lab_when_name(PC_LAB_NEXT), "next track"); CHECK_STR(pc_lab_when_name(PC_LAB_REBOOT), "reboot");
    CHECK_EQ_INT(pc_lab_def(PC_LAB_CO5300_C4)->when, PC_LAB_REBOOT);
    CHECK_EQ_INT(pc_lab_def(PC_LAB_DAC_HP_INV)->when, PC_LAB_NEXT);
    CHECK_EQ_INT(pc_lab_def(PC_LAB_FLICK_EDGE)->when, PC_LAB_LIVE);
}

static void test_lcd(void)
{
    CHECK_EQ_INT(pc_lab_default(PC_LAB_LCD_PSRAM_DMA, false), 0);                              /* today's behaviour */
    CHECK_EQ_INT(pc_lab_default(PC_LAB_LCD_QUEUE, false), 2);
    CHECK_EQ_INT(pc_lab_default(PC_LAB_LCD_CHUNK_ROWS, false), 16);
    CHECK_EQ_INT(pc_lab_lcd_internal_bytes(0, 2, 16, 460), 44160);                             /* 3 bounce buffers of 14.7 KB */
    CHECK(pc_lab_lcd_ok(0, 2, 16, 460));                                                       /* the default is allowed */
    CHECK(!pc_lab_lcd_ok(0, 2, 32, 460));                                                      /* 88 KB of internal RAM: refused */
    CHECK(!pc_lab_lcd_ok(0, 6, 16, 460));
    CHECK_EQ_INT(pc_lab_lcd_internal_bytes(1, 6, 128, 460), 0);                                /* direct PSRAM DMA: no bounce at any size */
    CHECK(pc_lab_lcd_ok(1, 6, 128, 460));
    CHECK(pc_lab_ok(PC_LAB_LCD_CHUNK_ROWS, 48) && !pc_lab_ok(PC_LAB_LCD_CHUNK_ROWS, 20));
}

int main(void)
{
    RUN(test_lcd);
    RUN(test_table);
    RUN(test_defaults_are_todays);
    RUN(test_ranges);
    RUN(test_parse_and_when);
    return 0;
}
