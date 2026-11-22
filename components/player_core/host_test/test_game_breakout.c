// SPDX-License-Identifier: LGPL-2.1-or-later
/* 環形打磚塊 (ideas G1): the pure physics and rules (pc_game_breakout.h): reflection off the arc, brick faces, kinds, power-ups, rules, levels. */
#include "pc/pc_game_breakout.h"
#include "pc_test.h"

static float deg_of(float x, float y) { float d = atan2f(y, x) * 57.29578f; return d < 0 ? d + 360.0f : d; }
static float len(float x, float y) { return sqrtf(x * x + y * y); }
static int count_ev(const pc_bo_t *g, int kind) { int n = 0; for (int i = 0; i < g->n_ev; i++) n += g->ev[i].kind == kind; return n; }
static int find_ev(const pc_bo_t *g, int kind) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i].kind == kind) return i; return -1; }

/* an empty field in PLAY with one ball at (x, y) moving (vx, vy) */
static void field(pc_bo_t *g, int rule, float x, float y, float vx, float vy)
{
    pc_bo_init(g, rule, 1, 7);
    for (int i = 0; i < PC_BO_BRICKS; i++) g->brick[i] = (pc_bo_brick_t){ 0 };
    for (int k = 0; k < PC_BO_RINGS; k++) { g->rot[k] = 0; g->spin[k] = 0; }
    g->bricks_left = 0;
    g->state = PC_BO_PLAY;
    g->ball[0] = (pc_bo_ball_t){ .x = x, .y = y, .vx = vx, .vy = vy, .live = true };
    g->ball[1].live = false;
}
static void put(pc_bo_t *g, int i, int kind, int hp)
{
    g->brick[i] = (pc_bo_brick_t){ .kind = (uint8_t)kind, .hp = (uint8_t)hp, .hp0 = (uint8_t)hp };
    g->bricks_left++;
}
/* steps until an event of `kind` (true) or n steps */
static bool run_until(pc_bo_t *g, int kind, int n)
{
    for (int s = 0; s < n; s++) { pc_bo_step(g); if (find_ev(g, kind) >= 0) return true; }
    return false;
}

static void test_reflect(void)
{
    float ox, oy;
    pc_bo_reflect(1.0f, 1.0f, 0.0f, -1.0f, &ox, &oy);
    CHECK_NEAR(ox, 1.0, 1e-6); CHECK_NEAR(oy, -1.0, 1e-6);
    pc_bo_reflect(3.0f, -2.0f, 1.0f, 0.0f, &ox, &oy);
    CHECK_NEAR(ox, -3.0, 1e-6); CHECK_NEAR(oy, -2.0, 1e-6);
    CHECK_NEAR(pc_bo_angdiff(10, 350), 20, 1e-4);
    CHECK_NEAR(pc_bo_angdiff(350, 10), -20, 1e-4);
    CHECK_NEAR(pc_bo_angdiff(180, 0), 180, 1e-4);
}

/* the arc: at the paddle's centre the ball is mirrored about the radial normal (straight back when it came radially); speed kept */
static void test_paddle_mirror(void)
{
    float ox, oy;
    pc_bo_paddle_dir(0, 82.5f, 0, 1.5f, 90, 22, &ox, &oy);
    CHECK_NEAR(ox, 0, 1e-4); CHECK_NEAR(oy, -1.5, 1e-4);
    pc_bo_paddle_dir(0, 82.5f, 0.6f, 1.4f, 90, 22, &ox, &oy);
    CHECK_NEAR(ox, 0.6, 1e-4); CHECK_NEAR(oy, -1.4, 1e-4);
    /* the same on a paddle at 3 o'clock: the normal points left */
    pc_bo_paddle_dir(82.5f, 0, 1.4f, -0.6f, 0, 22, &ox, &oy);
    CHECK_NEAR(ox, -1.4, 1e-4); CHECK_NEAR(oy, -0.6, 1e-4);
    CHECK_NEAR(len(ox, oy), len(1.4f, -0.6f), 1e-4);
}

/* english: a hit at an end turns the ball PC_BO_ENGLISH_DEG towards that end's side; the result stays within PC_BO_MAX_OUT_DEG of the inward normal */
static void test_paddle_english(void)
{
    float ox, oy, a = 68.0f * 3.14159265f / 180;                    /* the paddle's counter-clockwise end (90 - 22): the right side at 6 o'clock */
    float x = 82.5f * cosf(a), y = 82.5f * sinf(a);
    pc_bo_paddle_dir(x, y, 1.5f * cosf(a), 1.5f * sinf(a), 90, 22, &ox, &oy);   /* came radially */
    float n = deg_of(-x, -y);
    CHECK_NEAR(pc_bo_angdiff(deg_of(ox, oy), n), PC_BO_ENGLISH_DEG, 0.05);
    CHECK(ox > 0);                                                   /* to the right, the side it hit */
    CHECK_NEAR(len(ox, oy), 1.5, 1e-4);
    /* the other end: the other way */
    a = 112.0f * 3.14159265f / 180; x = 82.5f * cosf(a); y = 82.5f * sinf(a);
    pc_bo_paddle_dir(x, y, 1.5f * cosf(a), 1.5f * sinf(a), 90, 22, &ox, &oy);
    CHECK_NEAR(pc_bo_angdiff(deg_of(ox, oy), deg_of(-x, -y)), -PC_BO_ENGLISH_DEG, 0.05);
    CHECK(ox < 0);
    /* a shallow ball plus the english would skim: held at the limit */
    a = 68.0f * 3.14159265f / 180; x = 82.5f * cosf(a); y = 82.5f * sinf(a);
    float t = 60.0f * 3.14159265f / 180, vx = 1.5f * cosf(a - t), vy = 1.5f * sinf(a - t);   /* 60 degrees off the radial, toward the same side */
    pc_bo_paddle_dir(x, y, vx, vy, 90, 22, &ox, &oy);
    CHECK(fabsf(pc_bo_angdiff(deg_of(ox, oy), deg_of(-x, -y))) <= PC_BO_MAX_OUT_DEG + 0.01f);
    CHECK((ox * -x + oy * -y) > 0);                                  /* always inward */
}

/* a step: the ball reaches the paddle and comes back; a combo ends on the paddle */
static void test_step_paddle(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 60, 0, 1.5f);
    g.combo = 4;
    CHECK(run_until(&g, PC_EV_PADDLE, 40));
    CHECK(g.ball[0].vy < 0);
    CHECK(len(g.ball[0].x, g.ball[0].y) + PC_BO_BALL_R <= PC_BO_PAD_IN + 0.01f);
    CHECK_EQ_INT(g.combo, 0);
    CHECK_EQ_INT(g.lives, PC_BO_LIVES);
}

/* no paddle under it: the ball leaves the field, a life goes, the next ball waits on the paddle */
static void test_step_lost(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, -60, 0, -1.5f);                      /* towards 12 o'clock, the paddle at 6 */
    CHECK(run_until(&g, PC_EV_LOST, 60));
    CHECK_EQ_INT(g.lives, PC_BO_LIVES - 1);
    CHECK_EQ_INT(g.state, PC_BO_SERVE);
    CHECK(g.ball[0].live);
    CHECK_NEAR(deg_of(g.ball[0].x, g.ball[0].y), 90, 0.5);
    /* the last life: game over */
    g.lives = 1;
    CHECK(pc_bo_serve(&g));
    pc_bo_move(&g, 180);                                             /* the paddle away from where the ball goes */
    CHECK(run_until(&g, PC_EV_OVER, 200));
    CHECK_EQ_INT(g.state, PC_BO_OVER);
    CHECK_EQ_INT(g.lives, 0);
    CHECK(!pc_bo_running(&g));
}

/* 雙拍: the second paddle, opposite, takes a ball the first one cannot */
static void test_twin(void)
{
    pc_bo_t g;
    field(&g, PC_BO_TWIN, 0, -60, 0, -1.5f);
    CHECK_EQ_INT(pc_bo_paddles(&g), 2);
    CHECK_NEAR(pc_bo_half(&g), PC_BO_HALF_TWIN, 1e-6);
    CHECK(run_until(&g, PC_EV_PADDLE, 40));
    CHECK(g.ball[0].vy > 0);
    field(&g, PC_BO_CLASSIC, 0, -60, 0, -1.5f);
    CHECK_EQ_INT(pc_bo_paddles(&g), 1);
    CHECK(!run_until(&g, PC_EV_PADDLE, 60));
}

/* the inner face of a brick: mirrored about the radial normal, the brick breaks, the level is clear */
static void test_brick_inner_face(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0.0f, 1.5f);
    g.rot[0] = -15.0f;                                               /* brick 3 of ring 0 centred on 90 degrees */
    put(&g, 3, PC_BK_NORMAL, 1);
    float r0, r1, a0, a1;
    CHECK(pc_bo_brick_sector(&g, 3, &r0, &r1, &a0, &a1));
    CHECK_NEAR(r0, PC_BO_RING_IN, 1e-4); CHECK_NEAR(r1, PC_BO_RING_IN + PC_BO_RING_T, 1e-4);
    CHECK_NEAR((a0 + a1) / 2, 90, 1e-3);
    CHECK_NEAR(a1 - a0, 30 - PC_BO_BRICK_GAP, 1e-3);
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK(g.ball[0].vy < 0);
    CHECK_NEAR(g.ball[0].vx, 0, 1e-4);
    CHECK_EQ_INT(g.score, 10 + 50);                                  /* the break and the level's bonus */
    CHECK_EQ_INT(g.bricks_left, 0);
    CHECK(!pc_bo_brick_sector(&g, 3, &r0, &r1, &a0, &a1));
    CHECK(find_ev(&g, PC_EV_CLEAR) >= 0);
    CHECK_EQ_INT(g.state, PC_BO_CLEAR);
}

/* the outer face, from outside: mirrored outward */
static void test_brick_outer_face(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 70, 0.0f, -1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_NORMAL, 1);
    put(&g, 4, PC_BK_NORMAL, 1);                                     /* another one, so the level is not clear */
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK(g.ball[0].vy > 0);
    CHECK(len(g.ball[0].x, g.ball[0].y) - PC_BO_BALL_R >= PC_BO_RING_IN + PC_BO_RING_T - 0.01f);
    CHECK_EQ_INT(g.state, PC_BO_PLAY);
}

/* a side face: a ball running along the ring into a brick's end turns back along the ring */
static void test_brick_side_face(void)
{
    pc_bo_t g;
    float a = 55.0f * 3.14159265f / 180, r = PC_BO_RING_IN + PC_BO_RING_T / 2;
    field(&g, PC_BO_CLASSIC, r * cosf(a), r * sinf(a), -1.2f * sinf(a), 1.2f * cosf(a));   /* clockwise along the ring */
    g.rot[0] = -15.0f;                                               /* brick 3 spans 76.5 .. 103.5 */
    put(&g, 3, PC_BK_NORMAL, 1);
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    const pc_bo_ball_t *b = &g.ball[0];
    float t = deg_of(b->x, b->y) * 3.14159265f / 180;
    CHECK(b->vx * -sinf(t) + b->vy * cosf(t) < 0);                   /* the tangential motion turned round */
}

/* tough: two hits; the first scores 5 and keeps the brick */
static void test_tough(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_TOUGH, 2);
    CHECK(run_until(&g, PC_EV_HIT, 40));
    CHECK_EQ_INT(g.brick[3].hp, 1);
    CHECK_EQ_INT(g.score, 5);
    CHECK_EQ_INT(count_ev(&g, PC_EV_BREAK), 0);
    g.ball[0] = (pc_bo_ball_t){ .x = 0, .y = 0, .vx = 0, .vy = 1.5f, .live = true };
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK_EQ_INT(g.bricks_left, 0);
}

/* boom: its neighbours on the ring and the ring next to it go; a boom neighbour carries the chain on; bricks further away stay */
static void test_boom_chain(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = g.rot[1] = -15.0f;
    put(&g, 3, PC_BK_BOOM, 1);
    put(&g, 4, PC_BK_BOOM, 1);
    put(&g, 5, PC_BK_NORMAL, 1);
    put(&g, 7, PC_BK_NORMAL, 1);
    put(&g, PC_BO_PER_RING + 3, PC_BK_TOUGH, 2);                     /* ring 1, over brick 3: a boom breaks it whatever its hits */
    put(&g, PC_BO_PER_RING + 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK_EQ_INT(count_ev(&g, PC_EV_BOOM), 2);
    CHECK(!g.brick[3].kind && !g.brick[4].kind && !g.brick[5].kind && !g.brick[PC_BO_PER_RING + 3].kind);
    CHECK(g.brick[7].kind && g.brick[PC_BO_PER_RING + 9].kind);
    CHECK_EQ_INT(g.bricks_left, 2);
    CHECK_EQ_INT(count_ev(&g, PC_EV_BREAK), 4);
    CHECK_EQ_INT(g.combo, 4);
    CHECK_EQ_INT(g.score, 10 + 20 + 30 + 40);                        /* the combo grows with every brick */
}

/* a power brick drops a capsule outward; the paddle under it catches it */
static void test_power_drop(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_POWER, 1);
    g.brick[3].power = PC_PW_WIDE;
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK(g.drop[0].live);
    CHECK_NEAR(g.drop[0].deg, 90, 0.5);
    g.ball[0] = (pc_bo_ball_t){ .x = 0, .y = -20, .live = true };      /* a still ball out of the way keeps the round on */
    CHECK(run_until(&g, PC_EV_POWER, 120));
    CHECK_EQ_INT(g.ev[find_ev(&g, PC_EV_POWER)].value, PC_PW_WIDE);
    CHECK_NEAR(pc_bo_half(&g), PC_BO_HALF_DEG * PC_BO_WIDE, 1e-4);
    CHECK(!g.drop[0].live);
    /* missed: a capsule beside the paddle goes out */
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_POWER, 1);
    g.brick[3].power = PC_PW_SLOW;
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    pc_bo_move(&g, 90);
    g.ball[0].live = false;
    g.ball[1] = (pc_bo_ball_t){ .x = 0, .y = -20, .vx = 0, .vy = 0.0f, .live = true };   /* a still ball keeps the round on */
    for (int s = 0; s < 120; s++) { pc_bo_step(&g); CHECK(find_ev(&g, PC_EV_POWER) < 0); }
    CHECK(!g.drop[0].live);
    CHECK(pc_bo_speed(&g) > 1.0f);
}

/* the multi-ball and slow power-ups */
static void test_power_multi_slow(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, -20, 0, -1.0f);
    float s0 = pc_bo_speed(&g);
    g.drop[0] = (pc_bo_drop_t){ .r = 80, .deg = 90, .power = PC_PW_MULTI, .live = true };
    CHECK(run_until(&g, PC_EV_POWER, 40));
    CHECK(g.ball[0].live && g.ball[1].live);
    CHECK_NEAR(len(g.ball[1].vx, g.ball[1].vy), len(g.ball[0].vx, g.ball[0].vy), 1e-3);
    g.drop[0] = (pc_bo_drop_t){ .r = 80, .deg = 90, .power = PC_PW_SLOW, .live = true };
    CHECK(run_until(&g, PC_EV_POWER, 40));
    CHECK(pc_bo_speed(&g) < s0 * 0.75f);
    /* one ball of two lost: no life goes */
    field(&g, PC_BO_CLASSIC, 0, -60, 0, -1.5f);
    g.ball[1] = (pc_bo_ball_t){ .x = 0, .y = -10, .vx = 0, .vy = 0.5f, .live = true };
    for (int s = 0; s < 40; s++) pc_bo_step(&g);
    CHECK(!g.ball[0].live);
    CHECK_EQ_INT(g.lives, PC_BO_LIVES);
    CHECK_EQ_INT(g.state, PC_BO_PLAY);
}

/* 黏球: the ball stays where it landed, moves with the paddle, a tap lets it go inward, aimed by where it sits; or it goes by itself */
static void test_catch(void)
{
    pc_bo_t g;
    float a = 80.0f * 3.14159265f / 180;
    field(&g, PC_BO_CATCH, 60 * cosf(a), 60 * sinf(a), 1.5f * cosf(a), 1.5f * sinf(a));
    CHECK(run_until(&g, PC_EV_PADDLE, 40));
    CHECK(g.ball[0].stuck);
    float d0 = deg_of(g.ball[0].x, g.ball[0].y);
    CHECK_NEAR(d0, 80, 0.5);
    for (int s = 0; s < 10; s++) pc_bo_step(&g);
    CHECK_NEAR(deg_of(g.ball[0].x, g.ball[0].y), d0, 1e-3);           /* it stays */
    pc_bo_move(&g, 30);
    CHECK_NEAR(deg_of(g.ball[0].x, g.ball[0].y), d0 + 30, 1e-3);      /* with the paddle */
    CHECK(pc_bo_serve(&g));
    CHECK(find_ev(&g, PC_EV_RELEASE) >= 0);
    CHECK(!g.ball[0].stuck);
    CHECK((g.ball[0].vx * -g.ball[0].x + g.ball[0].vy * -g.ball[0].y) > 0);   /* inward */
    float n = deg_of(-g.ball[0].x, -g.ball[0].y);
    CHECK(pc_bo_angdiff(deg_of(g.ball[0].vx, g.ball[0].vy), n) > 1.0f);       /* it sat on the counter-clockwise side of the paddle's centre: aimed that way */
    /* by itself */
    field(&g, PC_BO_CATCH, 0, 60, 0, 1.5f);
    CHECK(run_until(&g, PC_EV_PADDLE, 40));
    CHECK(run_until(&g, PC_EV_RELEASE, PC_BO_CATCH_TICKS + 2));
    CHECK(!g.ball[0].stuck);
}

/* serve: the ball rests on the paddle and moves with it; a tap starts it inward */
static void test_serve(void)
{
    pc_bo_t g;
    pc_bo_init(&g, PC_BO_CLASSIC, 1, 42);
    CHECK_EQ_INT(g.state, PC_BO_SERVE);
    CHECK(!pc_bo_running(&g));
    CHECK_NEAR(g.paddle_deg, 90, 1e-6);
    pc_bo_move(&g, -400);                                             /* wraps */
    CHECK_NEAR(g.paddle_deg, 50, 1e-4);
    CHECK_NEAR(deg_of(g.ball[0].x, g.ball[0].y), 50, 1e-3);
    float r = len(g.ball[0].x, g.ball[0].y);
    CHECK_NEAR(r, PC_BO_PAD_IN - PC_BO_BALL_R - 0.5f, 1e-3);
    float x = g.ball[0].x, y = g.ball[0].y;
    for (int s = 0; s < 5; s++) pc_bo_step(&g);
    CHECK(g.ball[0].x == x && g.ball[0].y == y);                      /* still before the serve */
    CHECK(pc_bo_serve(&g));
    CHECK_EQ_INT(g.state, PC_BO_PLAY);
    CHECK(pc_bo_running(&g));
    CHECK(!pc_bo_serve(&g));
    CHECK((g.ball[0].vx * -x + g.ball[0].vy * -y) > 0);
    CHECK_NEAR(len(g.ball[0].vx, g.ball[0].vy), pc_bo_speed(&g), 1e-4);
}

/* the levels: two rings first, then three; rotation from level 3; a boss every 5th; golden about one level in PC_BO_GOLD_N; speed grows */
static void test_levels(void)
{
    pc_bo_t g;
    pc_bo_init(&g, PC_BO_CLASSIC, 1, 3);
    int n = 0, power = 0;
    for (int i = 0; i < PC_BO_BRICKS; i++) { n += g.brick[i].kind != PC_BK_NONE; power += g.brick[i].kind == PC_BK_POWER; }
    CHECK_EQ_INT(n, 2 * PC_BO_PER_RING);
    CHECK_EQ_INT(g.bricks_left, n);
    CHECK(power >= 1);
    for (int i = 2 * PC_BO_PER_RING; i < PC_BO_BRICKS; i++) CHECK(g.brick[i].kind == PC_BK_NONE);
    CHECK(g.spin[0] == 0 && g.spin[1] == 0);
    float s1 = pc_bo_speed(&g);
    pc_bo_level(&g, 2);
    CHECK_EQ_INT(g.bricks_left, PC_BO_BRICKS);
    CHECK(g.spin[0] == 0);
    pc_bo_level(&g, 3);
    CHECK(g.spin[0] != 0 && g.spin[1] != 0 && (g.spin[0] > 0) != (g.spin[1] > 0));
    CHECK(pc_bo_speed(&g) > s1);
    pc_bo_level(&g, 5);
    for (int j = 0; j < PC_BO_PER_RING; j++) { CHECK_EQ_INT(g.brick[2 * PC_BO_PER_RING + j].kind, PC_BK_BOSS); CHECK_EQ_INT(g.brick[2 * PC_BO_PER_RING + j].hp, 3); CHECK_EQ_INT(g.brick[PC_BO_PER_RING + j].kind, PC_BK_NONE); }
    CHECK(fabsf(g.spin[2]) > fabsf(g.spin[0]));
    pc_bo_level(&g, 40);
    CHECK(pc_bo_speed(&g) <= s1 * 1.5f + 1e-4f);
    int gold = 0;
    for (int lv = 1; lv <= 120; lv++) {
        pc_bo_level(&g, lv);
        int k = 0;
        for (int i = 0; i < PC_BO_BRICKS; i++) k += g.brick[i].kind == PC_BK_GOLD;
        CHECK(k <= 1);
        gold += k;
    }
    CHECK(gold >= 8 && gold <= 40);
    /* the speed modes */
    pc_bo_t a, b;
    pc_bo_init(&a, 0, 0, 1); pc_bo_init(&b, 0, 2, 1);
    CHECK(pc_bo_speed(&a) < pc_bo_speed(&b));
}

/* the golden brick: +1 life (at most PC_BO_LIVES_MAX) and 100 */
static void test_gold(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_GOLD, 1);
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_GOLD, 40));
    CHECK_EQ_INT(g.lives, PC_BO_LIVES + 1);
    CHECK_EQ_INT(g.score, 10 + 100);
    CHECK(find_ev(&g, PC_EV_LIFE) >= 0);
}

/* a cleared level: CLEAR for PC_BO_CLEAR_TICKS, the bonus, then the next level waits for the serve */
static void test_clear_next(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_CLEAR, 40));
    CHECK_EQ_INT(g.score, 10 + 50);
    CHECK(pc_bo_running(&g));
    for (int s = 0; s < PC_BO_CLEAR_TICKS; s++) pc_bo_step(&g);
    CHECK_EQ_INT(g.level, 2);
    CHECK_EQ_INT(g.state, PC_BO_SERVE);
    CHECK_EQ_INT(g.bricks_left, PC_BO_BRICKS);
}

/* a combo milestone every 5 */
static void test_combo_event(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_NORMAL, 1);
    put(&g, 9, PC_BK_NORMAL, 1);
    g.combo = 4;
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    int i = find_ev(&g, PC_EV_COMBO);
    CHECK(i >= 0);
    CHECK_EQ_INT(g.ev[i].value, 5);
    CHECK_EQ_INT(g.combo_best, 5);
}

/* the auto-player: a paddle that follows the ball clears levels; nothing is NaN, no ball is ever inside a brick it did not hit, no ball outside the field while live;
 * the same seed and the same moves give the same game (determinism) */
static void play(pc_bo_t *g, int rule, uint32_t seed, int steps, int *max_level)
{
    pc_bo_init(g, rule, 2, seed);
    *max_level = 1;
    int bounces = 0;
    for (int s = 0; s < steps; s++) {
        if (g->ball[0].live && g->ball[0].stuck) pc_bo_move(g, (float)((bounces * 13) % 7 - 3) * 5.0f);   /* 黏球: carry it somewhere first (aiming), then let go */
        if (g->state == PC_BO_SERVE || (g->ball[0].live && g->ball[0].stuck)) pc_bo_serve(g);
        if (g->state == PC_BO_OVER) break;
        int lead = g->ball[0].live ? 0 : 1;
        const pc_bo_ball_t *b = &g->ball[lead];
        if (b->live) {
            float d = pc_bo_angdiff(deg_of(b->x, b->y) + (float)((int)(((uint32_t)bounces * 2654435761u) >> 28) % 7 - 3) * pc_bo_half(g) / 4.0f, g->paddle_deg);   /* a follower that meets the ball off-centre by a hashed amount each bounce (no short cycle): the english varies */
            if (pc_bo_paddles(g) == 2) { float d2 = pc_bo_angdiff(d, 180.0f); if (fabsf(d2) < fabsf(d)) d = d2; }   /* 雙拍: the nearer of the two paddles goes */
            pc_bo_move(g, d > 6 ? 6 : d < -6 ? -6 : d);
        }
        pc_bo_step(g);
        bounces += count_ev(g, PC_EV_PADDLE);
        for (int k = 0; k < PC_BO_BALLS; k++) {
            const pc_bo_ball_t *q = &g->ball[k];
            if (!q->live) continue;
            CHECK(!isnan(q->x) && !isnan(q->y) && !isnan(q->vx) && !isnan(q->vy));
            CHECK(len(q->x, q->y) <= PC_BO_FIELD + PC_BO_BALL_R + 3.0f);
        }
        if (g->level > *max_level) *max_level = g->level;
    }
}
static void test_auto_player(void)
{
    static pc_bo_t a, b;
    int lv_a, lv_b;
    for (int rule = 0; rule < PC_BO_RULES; rule++) {
        play(&a, rule, 11 + (uint32_t)rule, 40000, &lv_a);
        printf("auto-player rule %d: level %d score %u ticks %u lives %d combo_best %d\n", rule, lv_a, (unsigned)a.score, (unsigned)a.ticks, a.lives, a.combo_best);
        CHECK(lv_a >= 3);                                             /* it clears levels, the rotating ones too */
        play(&b, rule, 11 + (uint32_t)rule, 40000, &lv_b);
        CHECK_EQ_INT(lv_a, lv_b);
        CHECK_EQ_INT(a.score, b.score);
        CHECK_EQ_INT(a.ticks, b.ticks);
        CHECK(a.ball[0].x == b.ball[0].x && a.ball[0].y == b.ball[0].y);
    }
}

/* fast balls never pass through a ring (sub-steps): at the fastest speed and level the ball always breaks the brick in its way */
static void test_no_tunnel(void)
{
    for (int k = 0; k < 36; k++) {
        pc_bo_t g;
        float a = (float)k * 10.0f * 3.14159265f / 180, v = 1.95f * 1.5f;
        field(&g, PC_BO_CLASSIC, 0, 0, v * cosf(a), v * sinf(a));
        for (int i = 0; i < PC_BO_PER_RING; i++) put(&g, i, PC_BK_TOUGH, 9);   /* a full ring 0 that does not break */
        for (int s = 0; s < 60; s++) {
            pc_bo_step(&g);
            CHECK(len(g.ball[0].x, g.ball[0].y) < PC_BO_RING_IN + PC_BO_RING_T);   /* never past it: the gaps are narrower than the ball */
        }
    }
}

/* fix round 1 I2 (the reviewer's case): a turning boss ring sweeps into a ball that is leaving the brick's side: one contact is at most one hit,
 * never the 3-hit brick broken by one touch; also a ball wedged across a gap does not drain both neighbours */
static void test_sweep_one_hit(void)
{
    for (int off = -45; off <= 10; off += 5) {
        pc_bo_t g;
        pc_bo_init(&g, PC_BO_CLASSIC, 1, 5);
        pc_bo_level(&g, 5);
        CHECK(g.spin[2] > 0);                                         /* turning towards larger angles: brick 24's a1 side comes at slot 25 */
        g.brick[25] = (pc_bo_brick_t){ 0 };
        g.bricks_left--;
        g.state = PC_BO_PLAY;
        float r0, r1, a0, a1;
        CHECK(pc_bo_brick_sector(&g, 24, &r0, &r1, &a0, &a1));
        float r = r0 + 1.0f, at = a1 + PC_BO_BALL_R / r * 57.29578f + 0.3f, dir = (at + (float)off) * 3.14159265f / 180, sp = pc_bo_speed(&g);
        g.ball[0] = (pc_bo_ball_t){ .x = r * cosf(at * 3.14159265f / 180), .y = r * sinf(at * 3.14159265f / 180), .vx = sp * cosf(dir), .vy = sp * sinf(dir), .live = true };
        int hits = 0;
        for (int s = 0; s < 10; s++) {
            pc_bo_step(&g);
            for (int e = 0; e < g.n_ev; e++) if ((g.ev[e].kind == PC_EV_HIT || g.ev[e].kind == PC_EV_BREAK) && g.ev[e].brick == 24) hits++;
        }
        if (hits > 1) printf("offset %d: %d hits\n", off, hits);
        CHECK(hits <= 1);
        CHECK(g.brick[24].kind == PC_BK_BOSS && g.brick[24].hp >= 2);
    }
    /* wedged across a 3 degree gap, moving outward along it: neither neighbour is hit (the ball moves away from both sides) */
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 0);
    put(&g, 2, PC_BK_TOUGH, 2);
    put(&g, 3, PC_BK_TOUGH, 2);
    float a = 90.0f * 3.14159265f / 180, r = PC_BO_RING_IN + 2;                /* the gap between bricks 2 and 3 is at 90 degrees */
    g.ball[0] = (pc_bo_ball_t){ .x = r * cosf(a), .y = r * sinf(a), .vx = 1.5f * cosf(a), .vy = 1.5f * sinf(a), .live = true };
    for (int s = 0; s < 12; s++) { pc_bo_step(&g); CHECK(find_ev(&g, PC_EV_HIT) < 0); }
    CHECK(g.brick[2].hp == 2 && g.brick[3].hp == 2);
}

/* fix round 1 m6: a ball diagonally past a sector's corner (outside the radial and the angular range, within the ball's radius of each) does not touch it */
static void test_corner(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 0);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_NORMAL, 1);                                                  /* 76.5 .. 103.5, radii 30 .. 39 */
    put(&g, 9, PC_BK_NORMAL, 1);
    float c = 76.5f * 3.14159265f / 180, cx = 39.0f * cosf(c), cy = 39.0f * sinf(c);   /* the outer corner at a0 */
    float ox = (cosf(c) + sinf(c)) * 0.70711f, oy = (sinf(c) - cosf(c)) * 0.70711f;    /* diagonally out: radially out and towards smaller angles */
    g.ball[0] = (pc_bo_ball_t){ .x = cx + ox * (PC_BO_BALL_R + 0.3f), .y = cy + oy * (PC_BO_BALL_R + 0.3f), .vx = -ox * 0.0f, .vy = 0, .live = true };
    float x0 = g.ball[0].x, y0 = g.ball[0].y;
    pc_bo_step(&g);
    CHECK(find_ev(&g, PC_EV_BREAK) < 0);
    CHECK(g.ball[0].x == x0 && g.ball[0].y == y0);                                 /* not touched, not pushed */
    g.ball[0].vx = -ox * 1.0f; g.ball[0].vy = -oy * 1.0f;                        /* now into the corner: it is hit, bounced back out along the corner's normal */
    CHECK(run_until(&g, PC_EV_BREAK, 4));
    CHECK(g.ball[0].vx * ox + g.ball[0].vy * oy > 0.5f);
}

/* fix round 1 m5: the slow ball's end brings every moving ball back to the speed */
static void test_slow_ends(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, -20, 0, -0.3f);
    g.drop[0] = (pc_bo_drop_t){ .r = 85.5f, .deg = 90, .power = PC_PW_SLOW, .live = true };
    float s0 = pc_bo_speed(&g);
    g.ball[0].vy = -s0 * 0.01f;                                                   /* nearly still: it stays in the field */
    CHECK(run_until(&g, PC_EV_POWER, 3));
    for (int s = 0; s < PC_BO_POWER_TICKS + 1; s++) pc_bo_step(&g);
    CHECK_NEAR(pc_bo_speed(&g), s0, 1e-4);
    CHECK_NEAR(len(g.ball[0].vx, g.ball[0].vy), s0, 1e-3);
}

/* ruling (fix round 1): 黏球 catches a ball PC_BO_CATCHES times, then it bounces; a new ball catches again */
static void test_catch_limit(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CATCH, 0, 60, 0, 1.5f);
    for (int k = 0; k < PC_BO_CATCHES; k++) {
        CHECK(run_until(&g, PC_EV_PADDLE, 120));
        CHECK(g.ball[0].stuck);
        CHECK(pc_bo_serve(&g));
        g.ball[0].vx = 0; g.ball[0].vy = -1.5f;                                   /* straight up and back down: no paddle up there in classic, so */
        g.ball[0].x = 0; g.ball[0].y = 60; g.ball[0].vy = 1.5f;                   /* send it down again from the middle */
    }
    CHECK(run_until(&g, PC_EV_PADDLE, 120));
    CHECK(!g.ball[0].stuck);                                                      /* the fourth: a bounce */
    CHECK(g.ball[0].vy < 0);
    CHECK_EQ_INT(g.catches, PC_BO_CATCHES);
    pc_bo_level(&g, 2);
    CHECK_EQ_INT(g.catches, 0);
}

/* fix round 2 N3: an outer-ring brick hit from outside, the ball flies out of the band, comes back at the same brick: a second hit and a bounce
 * (the one-contact rule never lasts past leaving the band) */
static void test_return_same_brick(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 80, 0, -1.5f);
    g.rot[2] = -15.0f;                                                            /* brick 27 (ring 2, slot 3) centred on 90 degrees */
    put(&g, 2 * PC_BO_PER_RING + 3, PC_BK_TOUGH, 2);
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_HIT, 40));
    CHECK(g.ball[0].vy > 0);                                                      /* bounced back out */
    CHECK_EQ_INT(g.ball[0].touch, 2 * PC_BO_PER_RING + 3 + 1);
    for (int s = 0; s < 6; s++) pc_bo_step(&g);                                   /* out of the band: r > outer + R */
    CHECK(len(g.ball[0].x, g.ball[0].y) - PC_BO_BALL_R > PC_BO_RING_IN + PC_BO_RINGS * (PC_BO_RING_T + PC_BO_RING_GAP));
    g.ball[0].vx = 0; g.ball[0].vy = -1.5f;                                       /* sent back at the same brick */
    CHECK(run_until(&g, PC_EV_BREAK, 40));
    CHECK(g.ball[0].vy > 0);                                                      /* the second contact bounces and breaks it */
    CHECK_EQ_INT(g.bricks_left, 1);
}

/* fix round 2 ruling: the cooldown never removes the bounce. A ball sent back INTO the brick it just hit (still within TOUCH_CLEAR of it) is mirrored, not
 * held against the face; no second damage on that contact */
static void test_cooldown_still_bounces(void)
{
    pc_bo_t g;
    field(&g, PC_BO_CLASSIC, 0, 0, 0, 1.5f);
    g.rot[0] = -15.0f;
    put(&g, 3, PC_BK_TOUGH, 3);
    put(&g, 9, PC_BK_NORMAL, 1);
    CHECK(run_until(&g, PC_EV_HIT, 40));
    CHECK(g.ball[0].vy < 0);
    g.ball[0].vy = 1.5f;                                                          /* straight back in while it is still touching */
    for (int s = 0; s < 3; s++) { pc_bo_step(&g); CHECK(find_ev(&g, PC_EV_HIT) < 0); }
    CHECK(g.ball[0].vy < 0);                                                      /* bounced, not pinned */
    CHECK_EQ_INT(g.brick[3].hp, 2);                                               /* one hit for the contact */
}

/* fix round 2 N2: auto-play with a random paddle over 2880 games (levels 1..12, the 3 speeds, the 3 rules, 80 seeds, 3000 steps each): no ball in play
 * stays within 0.5 units of where it was for 10 steps or more (a ball pinned against a brick face); no ball centre ever inside a live brick */
static int pinned_games(int *max_run)
{
    static pc_bo_t g;
    int pinned = 0;
    *max_run = 0;
    for (int lv = 1; lv <= 12; lv++)
        for (int sp = 0; sp < PC_BO_SPEEDS; sp++)
            for (int seed = 0; seed < 80; seed++) {
                pc_bo_init(&g, seed % PC_BO_RULES, sp, 1000u * (uint32_t)lv + 7u * (uint32_t)seed + (uint32_t)sp);
                pc_bo_level(&g, lv);
                uint32_t rng = 0x12345u + (uint32_t)seed * 977u + (uint32_t)lv * 31u;
                float lx[PC_BO_BALLS] = { 0 }, ly[PC_BO_BALLS] = { 0 };
                int run[PC_BO_BALLS] = { 0 }, worst = 0;
                for (int s = 0; s < 3000 && g.state != PC_BO_OVER; s++) {
                    if (g.state == PC_BO_SERVE || (g.ball[0].live && g.ball[0].stuck)) pc_bo_serve(&g);
                    rng = rng * 1664525u + 1013904223u;
                    pc_bo_move(&g, (float)((int)(rng >> 28) - 8));                /* a paddle that wanders: balls are lost, levels are cleared */
                    pc_bo_step(&g);
                    for (int k = 0; k < PC_BO_BALLS; k++) {
                        const pc_bo_ball_t *b = &g.ball[k];
                        if (!b->live || b->stuck || g.state != PC_BO_PLAY) { run[k] = 0; continue; }
                        run[k] = len(b->x - lx[k], b->y - ly[k]) < 0.5f ? run[k] + 1 : 0;
                        lx[k] = b->x; ly[k] = b->y;
                        if (run[k] > worst) worst = run[k];
                        for (int i = 0; i < PC_BO_BRICKS; i++) {
                            float r0, r1, a0, a1, r = len(b->x, b->y);
                            if (!pc_bo_brick_sector(&g, i, &r0, &r1, &a0, &a1)) continue;
                            CHECK(!(r > r0 && r < r1 && fabsf(pc_bo_angdiff(atan2f(b->y, b->x) * 57.29578f, (a0 + a1) / 2)) < (a1 - a0) / 2));
                        }
                    }
                }
                if (worst >= 10) pinned++;
                if (worst > *max_run) *max_run = worst;
            }
    return pinned;
}
static void test_no_pinned_ball(void)
{
    int max_run, n = pinned_games(&max_run);
    printf("pinned: %d of 2880 games, longest still run %d steps\n", n, max_run);
    CHECK_EQ_INT(n, 0);
}

int main(void)
{
    RUN(test_reflect);
    RUN(test_paddle_mirror);
    RUN(test_paddle_english);
    RUN(test_step_paddle);
    RUN(test_step_lost);
    RUN(test_twin);
    RUN(test_brick_inner_face);
    RUN(test_brick_outer_face);
    RUN(test_brick_side_face);
    RUN(test_tough);
    RUN(test_boom_chain);
    RUN(test_power_drop);
    RUN(test_power_multi_slow);
    RUN(test_catch);
    RUN(test_serve);
    RUN(test_levels);
    RUN(test_gold);
    RUN(test_clear_next);
    RUN(test_combo_event);
    RUN(test_auto_player);
    RUN(test_no_tunnel);
    RUN(test_sweep_one_hit);
    RUN(test_corner);
    RUN(test_slow_ends);
    RUN(test_catch_limit);
    RUN(test_return_same_brick);
    RUN(test_cooldown_still_bounces);
    RUN(test_no_pinned_ball);
    return 0;
}
