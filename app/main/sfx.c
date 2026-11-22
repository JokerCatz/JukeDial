// SPDX-License-Identifier: LGPL-2.1-or-later
/* Toy sound effects (sfx.h): the bank text (sfx_bank.def, flash) is parsed once at boot into PSRAM; the LVGL task posts entry ids with a time
 * stamp into a lock-free single-producer queue, the audio task takes them at the start of its next block (dropping the ones that waited too long)
 * and mixes the voices into the block before the gain (audio_svc.c). */
#include "sfx.h"
#include <stdio.h>
#include <string.h>
#include "cfg.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "pc/pc_tuner.h"

static const char BANK_TEXT[] =
#include "sfx_bank.def"
    ;

typedef struct {
    pc_sfx_q_t q;                 /* LVGL task -> audio task */
    pc_sfx_t syn;                 /* audio task only (the console dump reads it: a diagnostic, a torn value is harmless) */
    uint32_t posted, off, full;   /* LVGL task */
    uint32_t mixed, stale;        /* audio task: blocks the effects changed; posts dropped as too old */
    pc_tone_t tone;               /* audio task only: the tuner's reference tone (sfx_tone_set) */
    uint32_t tone_blocks;         /* audio task: blocks the tone was added to (console `tuner tonestate`) */
    uint32_t tone_want;           /* LVGL task -> audio task: bit 0 on, bits 1..3 the wave (PC_TONE_*) */
    uint32_t tone_hz_bits;        /* the pitch as float bits */
    bool ready;                   /* the bank is parsed (set before the audio task starts) */
} sfx_state_t;
EXT_RAM_BSS_ATTR static sfx_state_t S;   /* zero = an empty queue and silent voices without a bank */
EXT_RAM_BSS_ATTR static pc_sfx_bank_t s_bank;

static void bank_log(const char *m, void *u) { (void)u; printf("APP sfx bank refused %s\n", m); }

void sfx_init(void)
{
    if (S.ready) return;
    int n = pc_sfx_bank_parse(&s_bank, BANK_TEXT, bank_log, NULL);
    pc_sfx_q_init(&S.q);
    pc_sfx_init(&S.syn, &s_bank);
    S.ready = true;
    if (s_bank.dropped) printf("APP sfx bank n=%d dropped=%d notes=%d\n", n, s_bank.dropped, s_bank.nev);   /* quiet when all is well (`sfx bank` prints the counts) */
}

const pc_sfx_bank_t *sfx_bank(void) { return &s_bank; }

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool post(int id)
{
    if (!pc_sfx_q_post(&S.q, (uint8_t)id, now_ms())) { S.full++; return false; }
    S.posted++;
    return true;
}

bool sfx_play(const char *name)
{
    int id = pc_sfx_id(&s_bank, name);
    if (id < 0) { printf("APP sfx unknown %s\n", name ? name : "(null)"); return false; }
    if (!cfg_int("toysfx", 1)) { S.off++; return false; }
    return post(id);
}

bool sfx_audition(int id) { return id >= 0 && id < pc_sfx_count(&s_bank) && post(id); }

void sfx_tone_set(int wave, float hz, bool on)
{
    uint32_t b;
    memcpy(&b, &hz, sizeof b);
    __atomic_store_n(&S.tone_hz_bits, b, __ATOMIC_RELAXED);
    __atomic_store_n(&S.tone_want, (on ? 1u : 0u) | ((uint32_t)wave << 1), __ATOMIC_RELEASE);
}
bool sfx_tone_active(void) { return S.tone_want & 1u; }
uint32_t sfx_tone_blocks(void) { return S.tone_blocks; }

void sfx_mix(float *lr, int frames, uint32_t rate, float g)
{
    if (!S.ready) return;
    int id;
    uint32_t t = now_ms();
    while ((id = pc_sfx_q_take(&S.q, t, &S.stale)) >= 0) pc_sfx_start(&S.syn, id);
    if (pc_sfx_mix_gain(&S.syn, lr, frames, rate, g)) S.mixed++;
    const uint32_t w = __atomic_load_n(&S.tone_want, __ATOMIC_ACQUIRE);
    if (w || S.tone.amp > 0.0f) {                       /* the tuner's tone: the same mix point and gain as the effects above (nothing else changes) */
        const uint32_t hb = __atomic_load_n(&S.tone_hz_bits, __ATOMIC_RELAXED);
        float hz;
        memcpy(&hz, &hb, sizeof hz);
        pc_tone_set(&S.tone, (int)(w >> 1), hz, w & 1u);
        if (pc_tone_mix(&S.tone, lr, frames, rate, g)) S.tone_blocks++;
    }
}

void sfx_print(void)
{
    char b[PC_SFX_LAST * (PC_SFX_NAME + 1) + 1];
    int o = 0;
    uint32_t st = S.syn.started, n = st < PC_SFX_LAST ? st : PC_SFX_LAST;
    b[0] = 0;
    for (uint32_t k = n; k > 0; k--) {               /* oldest of the kept ones first, the newest last */
        const char *nm = pc_sfx_name(&s_bank, S.syn.last[(st - k) % PC_SFX_LAST]);
        o += snprintf(b + o, sizeof b - (size_t)o, "%s%s", o ? "," : "", nm ? nm : "?");
        if (o >= (int)sizeof b) break;
    }
    printf("APP sfx on=%d posted=%lu off=%lu full=%lu stale=%lu started=%lu stolen=%lu mixed=%lu last=%s\n", cfg_int("toysfx", 1) ? 1 : 0,
           (unsigned long)S.posted, (unsigned long)S.off, (unsigned long)S.full, (unsigned long)S.stale, (unsigned long)st,
           (unsigned long)S.syn.stolen, (unsigned long)S.mixed, n ? b : "-");
}

void sfx_print_bank(void)
{
    printf("APP sfx bank n=%d dropped=%d notes=%d waves=%d credits=%d\n", s_bank.n, s_bank.dropped, s_bank.nev, s_bank.nwave, s_bank.ncredit);
    for (int i = 0; i < s_bank.n; i++) {
        const pc_sfx_entry_t *e = &s_bank.e[i];
        printf("APP sfx entry %d %s group=%s tracks=%d ms=%lu by=%s\n", i, e->name, pc_sfx_group_key(e->group), e->ntr,
               (unsigned long)pc_sfx_len_ms(&s_bank, i), s_bank.ckey[e->credit]);
    }
}
