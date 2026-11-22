// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_resume.h"
#include "pc_test.h"

static pc_resume_t round_trip(pc_resume_t r)
{
    char b[PC_RESUME_MAX];
    pc_resume_t o;
    CHECK(pc_resume_format(&r, b, sizeof b) > 0);
    CHECK(pc_resume_parse(b, &o));
    return o;
}

static void test_mp3_whole_library(void)
{
    pc_resume_t o = round_trip((pc_resume_t){ .kind = PC_RES_MP3, .a = -1, .b = 412 });
    CHECK_EQ_INT(o.kind, PC_RES_MP3);
    CHECK_EQ_INT(o.a, -1);
    CHECK_EQ_INT(o.b, 412);
}

static void test_mp3_position(void)
{
    pc_resume_t o = round_trip((pc_resume_t){ .kind = PC_RES_MP3, .a = 3, .b = 17, .pos = 1234567 });
    CHECK_EQ_INT(o.b, 17);
    CHECK_EQ_INT(o.pos, 1234567);
    pc_resume_t old;
    CHECK(pc_resume_parse("mp3 3 17", &old));                    /* written before positions existed: from the start */
    CHECK_EQ_INT(old.pos, 0);
}

static void test_chip_name_with_spaces(void)
{
    pc_resume_t r = { .kind = PC_RES_CHIP, .a = 7, .b = 3 };
    strcpy(r.name, "Game Boy (GBS) v2");
    pc_resume_t o = round_trip(r);
    CHECK_EQ_INT(o.kind, PC_RES_CHIP);
    CHECK_EQ_INT(o.a, 7);
    CHECK_EQ_INT(o.b, 3);
    CHECK(!strcmp(o.name, "Game Boy (GBS) v2"));
}

static void test_noise(void)
{
    pc_resume_t o = round_trip((pc_resume_t){ .kind = PC_RES_NOISE, .a = 5 });
    CHECK_EQ_INT(o.kind, PC_RES_NOISE);
    CHECK_EQ_INT(o.a, 5);
}

static void test_radio(void)
{
    pc_resume_t o = round_trip((pc_resume_t){ .kind = PC_RES_RADIO, .a = 2 });
    CHECK_EQ_INT(o.kind, PC_RES_RADIO);
    CHECK_EQ_INT(o.a, 2);
}

static void test_color(void)
{
    pc_resume_t o = round_trip((pc_resume_t){ .kind = PC_RES_COLOR, .a = 7 });
    CHECK_EQ_INT(o.kind, PC_RES_COLOR);
    CHECK_EQ_INT(o.a, 7);
}

static void test_nothing_playing_writes_nothing(void)
{
    char b[PC_RESUME_MAX];
    CHECK_EQ_INT(pc_resume_format(&(pc_resume_t){ .kind = PC_RES_NONE }, b, sizeof b), 0);
    CHECK_EQ_INT(b[0], 0);
}

static void test_garbage_rejected(void)
{
    pc_resume_t o;
    CHECK(!pc_resume_parse("", &o));
    CHECK(!pc_resume_parse("mp3", &o));
    CHECK(!pc_resume_parse("mp3 0 -2", &o));        /* track index must be >= 0 */
    CHECK(!pc_resume_parse("mp3 -2 4", &o));        /* folder -1 = library, nothing lower */
    CHECK(!pc_resume_parse("chip 1 0", &o));        /* no system name */
    CHECK(!pc_resume_parse("noise x", &o));
    CHECK(!pc_resume_parse("radio x", &o));
    CHECK(!pc_resume_parse("tv 1", &o));
    CHECK_EQ_INT(o.kind, PC_RES_NONE);              /* a failed parse leaves nothing to resume */
}

static void test_too_small_buffer(void)
{
    char b[6];
    pc_resume_t r = { .kind = PC_RES_CHIP, .a = 1, .b = 2 };
    strcpy(r.name, "NES");
    CHECK_EQ_INT(pc_resume_format(&r, b, sizeof b), -1);
}

int main(void)
{
    RUN(test_mp3_whole_library);
    RUN(test_mp3_position);
    RUN(test_chip_name_with_spaces);
    RUN(test_noise);
    RUN(test_radio);
    RUN(test_color);
    RUN(test_nothing_playing_writes_nothing);
    RUN(test_garbage_rejected);
    RUN(test_too_small_buffer);
    return 0;
}
