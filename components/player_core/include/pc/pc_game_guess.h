// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 猜歌 / Guess the song (ideas wave 1, G4): the rules, no UI. A clip of a library track is played, four answers are shown (titles, or artists in the
 * 藝人 rule), one is the track the clip came from. The page asks pc_guess_ask for a question (target and three other tracks with different keys, in a
 * random order), plays the clip, and feeds the tapped answer to pc_guess_answer. The random stream is the game's own (seed from the page: games_seed).
 * Rules (game-feel.md: a variant changes a rule): CLASSIC a 5 s clip, titles; SHORT 2 s, titles; ARTIST 5 s, guess the artist.
 * Score: a right answer is worth 100 x the combo (streak 1-2: x1, 3-4: x2, 5-6: x3, 7+: x4); a golden round (1 in PC_GUESS_GOLD_N, drawn with the question)
 * doubles it; a wrong answer ends the streak (the game goes on: it is a fixed number of questions). */
enum { PC_GUESS_CLASSIC = 0, PC_GUESS_SHORT, PC_GUESS_ARTIST, PC_GUESS_RULES };
enum { PC_GUESS_IGNORED = 0, PC_GUESS_RIGHT, PC_GUESS_WRONG };
#define PC_GUESS_CHOICES   4
#define PC_GUESS_GOLD_N    10
#define PC_GUESS_MAX_ROUNDS 99
#define PC_GUESS_LABEL     16      /* pc_guess_label's buffer: 15 bytes (what ui_sectors keeps) and the NUL */
typedef const char *(*pc_guess_key_fn)(void *u, int i);   /* the title (artist in the 藝人 rule) of track i; "" or NULL: it cannot be asked */
typedef struct {
    uint8_t rule, rounds;          /* PC_GUESS_*, questions in a game */
    uint8_t asked, correct;        /* questions asked so far / right answers */
    uint8_t streak, best_streak;   /* right in a row now / the most in a game */
    uint8_t golden, answer, clip_pct, open;   /* this question is golden / the right sector / where in the file its clip starts (10..59 %) / a question waits for its answer */
    uint8_t over;                  /* the last question was answered */
    int track[PC_GUESS_CHOICES];   /* the track of each answer */
    int target, last_target;       /* the track the clip is from; the previous question's (never asked twice in a row) */
    uint32_t points, rng;
} pc_guess_t;

void pc_guess_init(pc_guess_t *g, int rule, int rounds, uint32_t seed);   /* rule out of range = CLASSIC; rounds clamped to 1..PC_GUESS_MAX_ROUNDS */
/* How many tracks with distinct keys there are, counted to PC_GUESS_CHOICES (so a result < PC_GUESS_CHOICES means the page cannot ask) */
int  pc_guess_ready(int n, pc_guess_key_fn key, void *u);
/* The next question over a library of n tracks; false when over, when one is still open, or when there are not PC_GUESS_CHOICES distinct keys */
bool pc_guess_ask(pc_guess_t *g, int n, pc_guess_key_fn key, void *u);
void pc_guess_skip(pc_guess_t *g);                 /* the open question's clip could not be played: it is taken back (not counted), the next ask picks another track */
int  pc_guess_answer(pc_guess_t *g, int sector);   /* PC_GUESS_RIGHT / WRONG; IGNORED when no question is open or the sector is out of range */
int  pc_guess_mult(int streak);                    /* the combo: 1..4 */
uint32_t pc_guess_points(int streak, bool golden); /* a right answer that makes streak: 100 x combo (x2 golden) */
uint32_t pc_guess_clip_ms(int rule);
/* Where a clip starts, in the unit the song's backend takes as its start (the player converts, mp3_ctl_play_clip(idx, pct)): PC_CLIP_BYTES (MP3, WAV: a byte position,
 * pct % of the file; 0 below 64 KB), PC_CLIP_FLAC (the backend's token is a PCM frame index: pct % of the total samples of STREAMINFO, read from `head`, the
 * first PC_CLIP_FLAC_HEAD bytes of the file; 0 when it is not a plain fLaC header), PC_CLIP_NONE (the others: OGG/Opus, M4A, tracker files: from their start). */
enum { PC_CLIP_NONE = 0, PC_CLIP_BYTES, PC_CLIP_FLAC };
#define PC_CLIP_FLAC_HEAD 26
uint32_t pc_guess_clip_token(int kind, uint32_t size, const uint8_t *head, int pct);
/* A candidate as the sector shows it: at most 15 bytes; a longer text is cut at a character and ends with an ellipsis */
void pc_guess_label(char out[PC_GUESS_LABEL], const char *s);
