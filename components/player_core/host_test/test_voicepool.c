// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_voicepool.h"

static int trig[16][3], ntrig, sil[16], nsil;
static void on_trig(int slot, int note, int vel, int timbre) { trig[ntrig][0] = slot; trig[ntrig][1] = note; trig[ntrig][2] = timbre; (void)vel; ntrig++; }
static void on_sil(int slot) { sil[nsil++] = slot; }
static void clear(void) { ntrig = nsil = 0; }

static void test_bass_and_harmony(void)
{
    pc_vp_t p;
    pc_vp_init(&p, on_trig, on_sil);           /* 2 harmony + 1 bass */
    clear();
    pc_vp_note_on(&p, 0, 48, 100, 0);          /* lowest of an empty pool -> bass */
    CHECK_EQ_INT(trig[0][0], 2);
    pc_vp_note_on(&p, 0, 60, 100, 0);
    pc_vp_note_on(&p, 0, 64, 100, 0);
    CHECK_EQ_INT(trig[1][0], 0);
    CHECK_EQ_INT(trig[2][0], 1);
    pc_vp_note_on(&p, 0, 67, 100, 3);          /* pool full: steals the oldest harmony (60 on slot 0) */
    CHECK_EQ_INT(trig[3][0], 0);
    CHECK_EQ_INT(pc_vp_voice_note(&p, 0), 67);
    CHECK_EQ_INT(pc_vp_voice_timbre(&p, 0), 3);
    CHECK_EQ_INT(pc_vp_voice_note(&p, 2), 48); /* bass never stolen */
    pc_vp_note_off(&p, 0, 60);                 /* stolen already: no-op */
    CHECK_EQ_INT(nsil, 0);
    pc_vp_note_off(&p, 0, 48);
    CHECK_EQ_INT(nsil, 1); CHECK_EQ_INT(sil[0], 2);
    pc_vp_note_on(&p, 1, 70, 100, 0);          /* higher than held harmony: not bass */
    CHECK(trig[4][0] != 2);
    pc_vp_note_off(&p, 0, 70);                 /* wrong channel: stays */
    CHECK_EQ_INT(nsil, 1);
    pc_vp_all_off(&p);
    CHECK_EQ_INT(nsil, 3);
    CHECK_EQ_INT(pc_vp_voice_note(&p, 0), -1);
    CHECK_EQ_INT(pc_vp_harmony_count(&p), 2);
}

static void test_gm_groups(void)
{
    pc_vp_t p;
    pc_vp_init_n(&p, 2, 1, on_trig, on_sil);
    clear();
    pc_vp_note_on(&p, 0, 40, 100, 0);          /* bass */
    pc_vp_note_on(&p, 0, 70, 100, 0);          /* harmony slot 0 */
    pc_vp_note_off(&p, 0, 40);                 /* bass free again */
    clear();
    pc_vp_note_on_gm(&p, 1, 80, 100, PC_GM_BASS, 1, 5);      /* bass group: bass slot even if higher */
    CHECK_EQ_INT(trig[0][0], 2); CHECK_EQ_INT(trig[0][2], 5);
    pc_vp_note_on_gm(&p, 2, 30, 100, PC_GM_HARMONY, 1, 5);   /* never bass */
    CHECK_EQ_INT(trig[1][0], 1); CHECK_EQ_INT(trig[1][2], 1);
    pc_vp_all_off(&p);
    clear();
    pc_vp_note_on(&p, 0, 40, 100, 0);          /* bass */
    pc_vp_note_on(&p, 0, 60, 100, 0);          /* harmony 0 */
    pc_vp_note_off(&p, 0, 40);
    pc_vp_note_on_gm(&p, 3, 65, 100, PC_GM_EITHER, 2, 4);    /* higher than harmony: not bass */
    CHECK_EQ_INT(trig[2][0], 1);
    pc_vp_note_on_gm(&p, 3, 50, 100, PC_GM_EITHER, 2, 4);    /* lowest: bass */
    CHECK_EQ_INT(trig[3][0], 2); CHECK_EQ_INT(trig[3][2], 4);
}

static void test_direct_and_shapes(void)
{
    pc_vp_t p;
    pc_vp_init_n(&p, 4, 0, on_trig, on_sil);   /* pure harmony pool */
    clear();
    for (int i = 0; i < 5; i++) pc_vp_note_on(&p, 0, 40 + i, 100, 0);
    CHECK_EQ_INT(trig[0][0], 0);
    CHECK_EQ_INT(trig[4][0], 0);               /* the fifth steals the oldest */
    pc_vp_all_off(&p);
    clear();
    pc_vp_note_on_direct(&p, 3, 50, 100, 0);
    pc_vp_note_on_direct(&p, 3, 52, 100, 0);   /* mono: retriggers */
    pc_vp_note_off_direct(&p, 3, 50);          /* stale */
    CHECK_EQ_INT(nsil, 0);
    pc_vp_note_off_direct(&p, 3, 52);
    CHECK_EQ_INT(nsil, 1);
    pc_vp_note_on_direct(&p, 9, 50, 100, 0);   /* out of range */
    CHECK_EQ_INT(ntrig, 2);
    /* channel of each slot (pitch bend follows it) */
    pc_vp_note_on(&p, 5, 60, 100, 0);
    CHECK_EQ_INT(pc_vp_voice_channel(&p, 0), 5);
    CHECK_EQ_INT(pc_vp_voice_channel(&p, 1), -1);
}

int main(void)
{
    RUN(test_bass_and_harmony);
    RUN(test_gm_groups);
    RUN(test_direct_and_shapes);
    return 0;
}
