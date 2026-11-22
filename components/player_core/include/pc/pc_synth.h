// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One live chip voice set (after RP2350_player src/player/chip_synth.h). Everything but voice_* runs on the
 * audio task only; voice_* are reads for the UI (benign races). */
typedef struct pc_synth {
    const char *id;                                  /* "nes": cfg value */
    const char *name;                                /* "NES 2A03": screen */
    int  (*reset)(uint32_t rate);                    /* 0 ok, <0 out of memory (chip stays silent) */
    void (*note_on)(int ch, int note, int vel);      /* Auto (ch 9 = percussion) */
    void (*note_off)(int ch, int note);
    void (*program_change)(int ch, int prog);
    void (*all_off)(void);
    void (*note_on_direct)(int voice, int note, int vel);
    void (*note_off_direct)(int voice, int note);
    void (*note_on_gm)(int ch, int note, int vel);
    void (*program_change_gm)(int ch, int prog);
    void (*pitch_bend)(int ch, int cents);           /* may be NULL; the chip adapters bend the whole chip */
    void (*render)(int16_t *stereo, size_t frames);  /* writes frames stereo pairs */
    int  (*voice_count)(void);                       /* melodic voices, then percussion last */
    const char *(*voice_name)(int i);
    void (*voice_state)(int i, uint8_t *level, int *active);   /* level 0..255 */
    int  (*voice_note)(int i);                       /* note held by melodic voice i, -1 */
    int  (*harmony_count)(void);                     /* width of the same-timbre voice group (looper) */
} pc_synth_t;
#ifdef __cplusplus
}
#endif
