// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 疊塔 / Stack tower (ideas G3): the rules, pure C (no UI, host-tested: test_game_tower).
 * A block slides across the field (or swings on an arc); one tap drops it on the tower. What overhangs the block below is cut off and falls (debris);
 * what stays is the next block's width. A drop within the tolerance of the block below is PERFECT: nothing is cut, the width is kept, and from the
 * second perfect in a row (the combo) the block grows back PC_TW_GROW a floor up to its full width. A drop that touches nothing ends the game.
 * Units: the field is 100 units from the centre to the rim (the page scales to the arena's px radius); x right, y DOWN (screen axes). The tower is built
 * on floors of PC_TW_H units; the camera follows so the tower's top block always sits at the same screen height (pc_tw_floor_y). The block's centre
 * slides within +-PC_TW_RANGE, so a block of the full width PC_TW_W0 stays inside the round field at every height it is shown.
 * One pc_tw_step() is one fixed step of 1/30 s (ui_arena's tick).
 * Rules (規則, classic first; a variant changes a rule):
 *   0 經典 Classic  the block slides, as wide as what is left of the tower
 *   1 窄塔 Narrow   as classic, but no block starts wider than the cap, which halves every PC_TW_MILE floors (PC_TW_MINW at the least)
 *   2 搖擺 Swing    the block swings on an arc (a pendulum: the ends are higher than the middle); the tap drops it where it hangs
 * Surprise: the golden block (1 in PC_TW_GOLD_N, from the game's own stream; shown gold while it slides): a successful drop restores the full width
 * (the rule's cap), no cut. A milestone every PC_TW_MILE floors. Score = the height (floors above the base); level = height / PC_TW_MILE + 1.
 * The first tap only starts the slide (state WAIT -> PLAY). All random choices come from the seed given at init. */
enum { PC_TW_CLASSIC = 0, PC_TW_NARROW, PC_TW_SWING, PC_TW_RULES };
enum { PC_TW_WAIT = 0, PC_TW_PLAY, PC_TW_OVER };
enum { PC_TW_K_NORMAL = 0, PC_TW_K_PERFECT, PC_TW_K_GOLD };                       /* how a floor was landed */
enum { PC_TW_EV_DROP = 1, PC_TW_EV_PERFECT, PC_TW_EV_COMBO, PC_TW_EV_GOLD, PC_TW_EV_MILE, PC_TW_EV_OVER };
#define PC_TW_FIELD   100.0f    /* the field's radius in units */
#define PC_TW_H       10.0f     /* a floor's height */
#define PC_TW_W0      60.0f     /* the base's and the full width */
#define PC_TW_RANGE   45.0f     /* the block's centre moves within +-this */
#define PC_TW_MINW    6.0f      /* narrow: the cap's floor */
#define PC_TW_GROW    2.0f      /* a perfect in a combo gives the block this much back */
#define PC_TW_MILE    10        /* floors */
#define PC_TW_GOLD_N  12
#define PC_TW_VIS     16        /* floors kept (the ring of the last floors: more than the screen shows) */
#define PC_TW_DEBRIS  3
#define PC_TW_EVENTS  8
#define PC_TW_BASE_Y  60.0f     /* the screen y of the ground (the base block's bottom) */
#define PC_TW_TOP_Y   (-30.0f)  /* the screen y of the tower's top once the camera follows */
typedef struct { uint8_t kind; float x, y; int value; } pc_tw_ev_t;   /* x, y: a field point (the landing's top edge) */
typedef struct { bool live; float x, w, wy, vy; } pc_tw_deb_t;        /* a piece falling: centre x, width, world y (UP) of its bottom, speed */
typedef struct {
    uint8_t rule, state;
    bool gold;                          /* the block that slides now is golden */
    int n;                              /* floors landed, the base included: the height is n - 1 */
    int combo, combo_best, perfects, golds;
    float cx[PC_TW_VIS], w[PC_TW_VIS];  /* floor f: its centre and width, at index f % PC_TW_VIS */
    uint8_t kind[PC_TW_VIS];
    float sx, sw, dy;                   /* the sliding block: centre, width, the swing's lift (<= 0: up) */
    float speed;                        /* the page's speed setting (1 normal): the slide and the swing are this much faster (set after init) */
    float dir, phase, vx;               /* slide direction, swing phase, the last step's speed (for the tolerance) */
    float cam, cam_to;                  /* the camera: world y (up) of the screen's ground line */
    pc_tw_deb_t deb[PC_TW_DEBRIS];
    uint32_t rng;
    pc_tw_ev_t ev[PC_TW_EVENTS];
    int n_ev;                           /* the events of the last step / tap (cleared at the start of each) */
} pc_tw_t;
void pc_tw_init(pc_tw_t *g, int rule, uint32_t seed);
void pc_tw_step(pc_tw_t *g);                         /* the slide / swing, the camera, the debris; events: none */
bool pc_tw_tap(pc_tw_t *g);                          /* WAIT: starts; PLAY: drops (events); OVER: false */
bool pc_tw_running(const pc_tw_t *g);                /* a step changes something: playing, debris falling or the camera moving */
int  pc_tw_height(const pc_tw_t *g);                 /* floors above the base */
int  pc_tw_level(const pc_tw_t *g);                  /* height / PC_TW_MILE + 1 */
float pc_tw_cap(const pc_tw_t *g, int height);       /* the widest a block may start at that height (narrow: halves every PC_TW_MILE) */
float pc_tw_tol(const pc_tw_t *g);                   /* how far off the centre a drop is still PERFECT (units; grows with the block's speed) */
float pc_tw_floor_y(const pc_tw_t *g, int f);        /* the screen y of floor f's TOP edge */
float pc_tw_slider_y(const pc_tw_t *g);              /* the screen y of the sliding block's top edge (the swing's lift included) */
