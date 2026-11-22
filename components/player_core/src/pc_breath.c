// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_breath.h"

static const uint8_t PHASE_S[PC_BR_PATTERNS][PC_BR_PHASES] = { { 4, 7, 8, 0 }, { 4, 4, 4, 4 }, { 5, 0, 5, 0 } };
static const char *const ID[PC_BR_PATTERNS] = { "478", "box", "55" };

int pc_br_session_s(int s) { return s == 1 ? 300 : s == 2 ? 600 : 180; }
int pc_br_phase_s(int p, int ph)
{
    if (p < 0 || p >= PC_BR_PATTERNS) p = PC_BR_478;
    return ph < 0 || ph >= PC_BR_PHASES ? 0 : PHASE_S[p][ph];
}
const char *pc_br_pattern_id(int p) { return ID[p < 0 || p >= PC_BR_PATTERNS ? 0 : p]; }
void pc_br_init(pc_br_t *b, int pattern, int session)
{
    *b = (pc_br_t){ 0 };
    b->pattern = (uint8_t)(pattern < 0 || pattern >= PC_BR_PATTERNS ? PC_BR_478 : pattern);
    b->phase_ticks = pc_br_phase_s(b->pattern, PC_BR_IN) * PC_BR_FPS;
    b->total = pc_br_session_s(session) * PC_BR_FPS;
}
int pc_br_step(pc_br_t *b)
{
    if (b->done) return 0;
    b->elapsed++;
    b->phase_tick++;
    int ev = 0;
    if (b->elapsed >= b->total) { b->done = true; return PC_BR_EV_DONE; }
    if (b->phase_tick >= b->phase_ticks) {
        do {
            if (++b->phase >= PC_BR_PHASES) { b->phase = PC_BR_IN; b->cycles++; }
            b->phase_ticks = pc_br_phase_s(b->pattern, b->phase) * PC_BR_FPS;
        } while (b->phase_ticks == 0);
        b->phase_tick = 0;
        ev |= PC_BR_EV_PHASE;
    }
    return ev;
}
static int ease(int t, int n)                          /* smoothstep, permille */
{
    if (n <= 0) return 1000;
    if (t < 0) t = 0;
    if (t > n) t = n;
    const long x = 1000L * t / n;
    return (int)(x * x * (3000 - 2 * x) / 1000000);
}
int pc_br_level(const pc_br_t *b)
{
    switch (b->phase) {
    case PC_BR_IN: return ease(b->phase_tick, b->phase_ticks);
    case PC_BR_HOLD: return 1000;
    case PC_BR_OUT: return 1000 - ease(b->phase_tick, b->phase_ticks);
    default: return 0;
    }
}
int pc_br_phase_left_s(const pc_br_t *b) { return (b->phase_ticks - b->phase_tick + PC_BR_FPS - 1) / PC_BR_FPS; }
int pc_br_left_s(const pc_br_t *b) { return (b->total - b->elapsed + PC_BR_FPS - 1) / PC_BR_FPS; }
int pc_br_noise_pm(int level, int depth)
{
    if (level < 0) level = 0;
    if (level > 1000) level = 1000;
    if (depth < 0) depth = 0;
    if (depth > PC_BR_DEPTH_MAX) depth = PC_BR_DEPTH_MAX;
    return 1000 - depth * (1000 - level) / 1000;
}
bool pc_br_hold_on(pc_br_hold_t *h, uint32_t req, int64_t now_us, int64_t hold_us)
{
    if (req != h->seen) { h->seen = req; h->until = now_us + hold_us; }
    return h->until != 0 && now_us < h->until;
}
