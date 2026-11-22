// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_fortune.h"     /* pc_rng_t */
#include "pc/pc_glyphpack.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 單字 vocabulary quiz (spec an internal note). Pure logic: no statics, no
 * allocation (the caller owns every buffer), host-tested (test_vocab).
 *
 * Deck = glyph pack of kind PC_GP_KIND_VOCAB. The root's children are the cards (card index = child offset, the
 * deck order = the order new cards are taught); a card's text is the word (or root / affix), its weight the level
 * band (1 common .. 255 rare; 0 = left out), its flags the class (PC_VC_*). Child 0 = the meaning, child 1
 * (optional) = an extra line (example words). The device never reads the text: answers are glyph-ID sequences.
 *
 * Progress = Leitner boxes per card: box 0 new (never answered), 1 relearn (due today), 2..8 known with the
 * interval 1 2 4 8 16 32 64 days (pc_vocab_interval). Days = local days since 2000-01-01 (pc_vocab_day). */

enum { PC_VC_NONE = 0, PC_VC_NOUN = 1, PC_VC_VERB = 2, PC_VC_ADJ = 3, PC_VC_ADV = 4, PC_VC_OTHER = 5,
       PC_VC_PREFIX = 8, PC_VC_ROOT = 9, PC_VC_SUFFIX = 10 };   /* >= 8: word parts (roots / affixes) */

#define PC_VOCAB_BOX_MAX     8
#define PC_VOCAB_LEARNED_BOX 3        /* "learned" in the stats: box >= 3 (an interval of 2 days or more) */
#define PC_VOCAB_FILE_HDR    48
#define PC_VOCAB_FILE_VER    1
#define PC_VOCAB_MAX_CARDS   32000u   /* 2 nodes a card, pc_gp node limit 65535 */
#define PC_VOCAB_RECENT      4        /* not asked again within this many questions (when there is a choice) */
#define PC_VOCAB_RELEARN     8        /* missed cards asked again in the session ... */
#define PC_VOCAB_RELEARN_GAP 3        /* ... after this many other questions */

/* ---- deck ---- */
typedef struct {
    const uint16_t *word, *meaning, *extra;   /* inside the pack's block; extra NULL when there is none */
    uint16_t word_len, meaning_len, extra_len;
    uint8_t cls, band;                        /* band 0 = left out */
} pc_vocab_entry_t;

uint32_t pc_vocab_count(const pc_gp_t *gp);                  /* cards (root children); 0 when not a vocab pack */
int  pc_vocab_entry(const pc_gp_t *gp, uint32_t card, pc_vocab_entry_t *e);   /* -ERANGE, -EINVAL (no meaning) */
uint32_t pc_vocab_hash(const pc_gp_t *gp);                   /* FNV-1a over every node: progress belongs to it */
/* bit per card set when the card can never be asked (band 0, no meaning, empty word); returns the usable cards */
uint32_t pc_vocab_skipbits(const pc_gp_t *gp, uint8_t *bits, uint32_t nbytes);
static inline uint32_t pc_vocab_bitbytes(uint32_t n) { return (n + 7) / 8; }
static inline bool pc_vocab_bit(const uint8_t *b, uint32_t i) { return b && (b[i >> 3] >> (i & 7) & 1); }

/* 3 (want) wrong answers for card: other usable cards with another word AND another meaning (else a second right
 * answer), answer texts (field 0 = word, 1 = meaning) all different; same class and a near band preferred (decks over 256 cards: the best of 256
 * random ones). out gets card indexes; returns how many (fewer for a tiny deck). */
int pc_vocab_distractors(pc_rng_t *r, const pc_gp_t *gp, const uint8_t *skip, uint32_t card, int field,
                         uint32_t *out, int want);

/* ---- progress ---- */
typedef struct { uint8_t box, lapses; uint16_t due; } pc_vocab_card_t;
typedef struct {
    uint32_t deck_hash, ncards;
    uint16_t last_day;               /* the newest day a valid clock gave (0: never) */
    uint16_t streak, streak_day;     /* days in a row with an answer, the last one */
    uint16_t new_day, new_count;     /* new cards taught on new_day */
    uint16_t stat_day, stat_n, stat_ok;   /* answers on stat_day, right ones */
    uint32_t reviews, correct;       /* all time */
    pc_vocab_card_t *cards;          /* caller's array of ncards */
} pc_vocab_prog_t;

uint16_t pc_vocab_day(int64_t local_seconds);   /* local seconds since 1970 -> days since 2000-01-01 (>= 1) */
uint16_t pc_vocab_interval(int box);            /* days until due after a right answer into box */
void   pc_vocab_reset(pc_vocab_prog_t *p, uint32_t hash, uint32_t ncards);   /* p->cards must hold ncards */
size_t pc_vocab_file_bytes(uint32_t ncards);
/* File (LE): "JVPR", u16 version, u16 header size, u32 deck hash, u32 cards, 8 x u16 (last_day streak streak_day
 * new_day new_count stat_day stat_n stat_ok), u32 reviews, u32 correct, u32 0, u32 CRC-32 of everything else;
 * then per card u8 box, u8 lapses, u16 due. Load: 0, or the progress is reset and -EINVAL (damaged: size, magic,
 * CRC, a box > 8), -EPROTONOSUPPORT (other version), -ESTALE (another deck or another build of it). */
int    pc_vocab_load(pc_vocab_prog_t *p, const uint8_t *buf, size_t len, uint32_t hash, uint32_t ncards);
size_t pc_vocab_save(const pc_vocab_prog_t *p, uint8_t *buf, size_t cap);   /* bytes written, 0 if cap is short */

/* ---- session / scheduler ---- */
enum { PC_VN_NONE = 0, PC_VN_RELEARN = 1, PC_VN_DUE = 2, PC_VN_NEW = 3, PC_VN_PRACTICE = 4 };
typedef struct {
    uint32_t recent[PC_VOCAB_RECENT];
    uint32_t relearn[PC_VOCAB_RELEARN];
    uint8_t wait[PC_VOCAB_RELEARN];
    uint8_t nrecent, nrelearn;
    uint16_t new_limit, new_session; /* new cards a day (a session without a valid clock) */
    uint16_t answered, right, run, best_run;
} pc_vocab_session_t;

void pc_vocab_session_init(pc_vocab_session_t *s, uint16_t new_limit);
/* The next card: a missed one whose gap is over, else the most overdue due card (lowest box first), else a new
 * card in deck order while today's new limit allows, else practice (a random known card; a right answer then
 * changes nothing). Not one of the last PC_VOCAB_RECENT unless nothing else is left. -1 when the deck has no
 * usable card. *why = PC_VN_*. clock_ok false: the new limit counts this session instead of today. */
int32_t pc_vocab_next(pc_vocab_session_t *s, const pc_vocab_prog_t *p, const uint8_t *skip, uint16_t today,
                      bool clock_ok, pc_rng_t *r, int *why);
/* Records an answer: right -> next box (a new card straight to box 2) and due = today + interval, except practice;
 * wrong -> box 1, due today, lapses + 1, asked again after PC_VOCAB_RELEARN_GAP questions. Updates the counters
 * and, with clock_ok, last_day and the day streak. */
void pc_vocab_answer(pc_vocab_session_t *s, pc_vocab_prog_t *p, uint32_t card, int why, bool right, uint16_t today,
                     bool clock_ok);

typedef struct { uint32_t total, fresh, seen, learned, due; uint16_t streak, today_n, today_ok; } pc_vocab_stats_t;
void pc_vocab_stats(const pc_vocab_prog_t *p, const uint8_t *skip, uint16_t today, pc_vocab_stats_t *st);

#ifdef __cplusplus
}
#endif
