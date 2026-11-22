// SPDX-License-Identifier: LGPL-2.1-or-later
#include <time.h>
#include "pc_test.h"
#include "pc/pc_vmute.h"
#include "pc/pc_voice.h"

static void test_cycle_and_mask(void)
{
    pc_vmute_reset();
    pc_vmute_sync();
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    pc_vmute_cycle(2, 5);                                   /* on -> muted */
    CHECK_EQ_INT(pc_vmute_get(2), PC_VM_MUTED);
    CHECK_EQ_INT((int)pc_vmute_mask(), 1 << 2);
    pc_vmute_cycle(0, 5);
    CHECK_EQ_INT((int)pc_vmute_mask(), (1 << 2) | 1);
    pc_vmute_cycle(2, 5);                                   /* muted -> solo: every other voice is muted */
    CHECK_EQ_INT(pc_vmute_solo_voice(), 2);
    CHECK_EQ_INT((int)pc_vmute_mask(), 0x1F & ~(1 << 2));
    pc_vmute_cycle(4, 5); pc_vmute_cycle(4, 5);             /* voice 4 muted then solo: the solo moves, voice 2 is on again */
    CHECK_EQ_INT(pc_vmute_solo_voice(), 4);
    CHECK_EQ_INT(pc_vmute_get(2), PC_VM_ON);
    CHECK_EQ_INT((int)pc_vmute_mask(), 0x1F & ~(1 << 4));
    pc_vmute_cycle(4, 5);                                   /* solo -> on: the earlier mute of voice 0 is back */
    CHECK_EQ_INT(pc_vmute_solo_voice(), -1);
    CHECK_EQ_INT((int)pc_vmute_mask(), 1);
    pc_vmute_clear(5);
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
}

static void test_reset_epoch(void)
{
    pc_vmute_reset(); pc_vmute_sync();
    pc_vmute_cycle(1, 3);
    CHECK_EQ_INT((int)pc_vmute_mask(), 2);
    CHECK(!pc_vmute_sync());                                /* nothing moved */
    pc_vmute_reset();                                       /* a new source took the voices */
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    pc_vmute_cycle(2, 3);                                   /* a UI post before its next sync: the old copy goes first, the change applies to the new source */
    CHECK_EQ_INT((int)pc_vmute_mask(), 4);
    CHECK_EQ_INT(pc_vmute_get(1), PC_VM_ON);                /* voice 1 (muted above, for the old source) is on again */
    CHECK(pc_vmute_sync());                                 /* the drop is still reported once (the page refreshes its copy) */
    CHECK_EQ_INT((int)pc_vmute_mask(), 4);
    CHECK_EQ_INT(pc_vmute_get(2), PC_VM_MUTED);
    CHECK(!pc_vmute_sync());
}

static void test_bounds(void)
{
    pc_vmute_reset(); pc_vmute_sync();
    pc_vmute_cycle(-1, 4); pc_vmute_cycle(PC_VOICES_MAX, 4); pc_vmute_set(0, (pc_vm_state_t)9, 4);
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    pc_vmute_set(31, PC_VM_MUTED, 99);                      /* n beyond the max is clamped */
    CHECK_EQ_INT((int)pc_vmute_mask(), (int)(1u << 31));
    pc_vmute_clear(32);
}

static void test_cents_and_names(void)
{
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(44000), 6900);   /* A4 */
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(88000), 8100);
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(22000), 5700);
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(26163), 6000);   /* C4 261.63 Hz */
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(0), PC_VOICE_NOPITCH);
    CHECK_EQ_INT(pc_voice_cents_of_hz_x100(-5), PC_VOICE_NOPITCH);
    CHECK(pc_voice_cents_of_hz_x100(1000000) > 9000);       /* 10 kHz: still a number below the cap */
    char n[4];
    pc_voice_note_name(6900, n); CHECK_STR(n, "A4");
    pc_voice_note_name(6100, n); CHECK_STR(n, "C#4");
    pc_voice_note_name(2400, n); CHECK_STR(n, "C1");
    pc_voice_note_name(2149, n); CHECK_STR(n, "A0");        /* rounds to the nearest semitone (21.49 -> 21) */
    pc_voice_note_name(-1, n); CHECK_STR(n, "--");
    uint8_t armed = 1;
    CHECK(pc_voice_level_strike(200, &armed));              /* rises: strike */
    CHECK(!pc_voice_level_strike(200, &armed));             /* held: no second strike */
    CHECK(!pc_voice_level_strike(40, &armed));              /* between the thresholds: still disarmed */
    CHECK(!pc_voice_level_strike(10, &armed));              /* below: armed again */
    CHECK(pc_voice_level_strike(64, &armed));
}

/* wave-6 review #21 (FIX7): the UI task, the console task and the audio task's reset at once. Every thread checks the invariants it can
 * (the mask never has a bit at or above the voice count, a solo's mask is exactly the other voices); at the end a sync and one mute leave exactly that mute. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
static atomic_int g_stop, g_bad;
static void *ui_thread(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    while (!atomic_load(&g_stop)) {
        int i = rand_r(&seed) % 8, n = 8;
        switch (rand_r(&seed) % 4) {
        case 0: pc_vmute_cycle(i, n); break;
        case 1: pc_vmute_set(i, (pc_vm_state_t)(rand_r(&seed) % 3), n); break;
        case 2: pc_vmute_clear(n); break;
        default: pc_vmute_sync(); break;
        }
        if (pc_vmute_mask() & ~0xFFu) atomic_store(&g_bad, 1);
        (void)pc_vmute_get(i); (void)pc_vmute_solo_voice();
    }
    return NULL;
}
static void *audio_thread(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) { pc_vmute_reset(); (void)pc_vmute_mask(); (void)pc_vmute_extra_muted(); }
    return NULL;
}
static void test_threads(void)
{
    pthread_t t[3];
    atomic_store(&g_stop, 0);
    pthread_create(&t[0], NULL, ui_thread, (void *)1);
    pthread_create(&t[1], NULL, ui_thread, (void *)2);        /* the console task */
    pthread_create(&t[2], NULL, audio_thread, NULL);
    struct timespec ts = { 0, 300 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
    CHECK_EQ_INT(atomic_load(&g_bad), 0);
    pc_vmute_reset(); pc_vmute_sync();                        /* the quiet end state: a mute is exactly that mute */
    pc_vmute_set(1, PC_VM_MUTED, 8);
    CHECK_EQ_INT((int)pc_vmute_mask(), 2);
    CHECK(!pc_vmute_extra_muted());
    pc_vmute_set(3, PC_VM_SOLO, 8);
    CHECK_EQ_INT((int)pc_vmute_mask(), 0xFF & ~(1 << 3));
    CHECK(pc_vmute_extra_muted());
    pc_vmute_clear(8);
}
/* the race itself, step by step: a reset lands between the UI's sync and its post -> the posted mask must not survive */
static void test_post_after_reset(void)
{
    pc_vmute_reset(); pc_vmute_sync();
    pc_vmute_set(2, PC_VM_MUTED, 4);
    CHECK_EQ_INT((int)pc_vmute_mask(), 4);
    pc_vmute_reset();                                         /* audio: new track */
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    pc_vmute_set(1, PC_VM_MUTED, 4);                          /* a UI post before its next sync: the old source's voice 2 mute is gone, this one applies */
    CHECK_EQ_INT((int)pc_vmute_mask(), 2);
    CHECK_EQ_INT(pc_vmute_get(2), PC_VM_ON);
    pc_vmute_reset();                                         /* and another reset between two posts wipes that one too */
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    pc_vmute_cycle(3, 4);
    CHECK_EQ_INT((int)pc_vmute_mask(), 8);
    CHECK_EQ_INT(pc_vmute_get(1), PC_VM_ON);
    pc_vmute_clear(4);
}

int main(void)
{
    RUN(test_cycle_and_mask); RUN(test_reset_epoch); RUN(test_bounds); RUN(test_post_after_reset); RUN(test_threads); RUN(test_cents_and_names);
    return 0;
}
