// Two-stage R2 for smoke, after Ratchet & Clank: Rift Apart (user's request, 2026-11-08).
// The game tells a tap shot from a charge by how long R2 is held (a tap starts a charge and
// cancels it by ~70 ms). This rewrites what the game sees from how DEEP the trigger is pressed:
//   - up to the wall (half travel): one short press is sent = a normal shot;
//   - through the wall: R2 is held for the game = a charge, fired on release.
// Reaching the wall waits TWO_STAGE_DECIDE_US to see if the press keeps going (user's choice B:
// a slightly later normal shot, but a clean charge with no extra shot); letting go inside the
// wait fires at once. Pure C: the plugin feeds it each pad sample.
#pragma once

#include <stdint.h>

/* v2.9 (user's spec): 0-60 % free = normal shot, 61-100 % = the wall and the charge */
#define TWO_STAGE_HALF_ON    115    /* analog r2 (0-255), 45 %: arms a normal shot */
#define TWO_STAGE_HALF_OFF   77     /* 30 %: below this the trigger counts as let go (hysteresis) */
#define TWO_STAGE_FULL       199    /* 78 %: into the zone-7 wall, a charge. A zone-6 wall was felt
                                       at 68 % (user, v2.9.1), so the wall sits ~8 % past its zone */
#define TWO_STAGE_CHARGE_OFF 183    /* backing out of the wall fires the charged shot */
#define TWO_STAGE_DECIDE_US  80000ULL
#define TWO_STAGE_TAP_US     50000ULL   /* the normal shot's press; under the ~70 ms charge cutoff */

enum { TWO_STAGE_IDLE, TWO_STAGE_DECIDING, TWO_STAGE_TAP, TWO_STAGE_TAPPED, TWO_STAGE_CHARGING };

typedef struct {
    int state;
    uint64_t t0;            /* DECIDING: when the wall was reached; TAP: end of the press */
    uint32_t taps, charges; /* research counters for the log */
} TwoStage;

typedef struct { int pressed; uint8_t r2; } TwoStageOut;

static inline void two_stage_init(TwoStage* s) { s->state = TWO_STAGE_IDLE; s->t0 = 0; s->taps = s->charges = 0; }

static inline TwoStageOut two_stage_pass(int pressed, uint8_t r2) { TwoStageOut o = { pressed != 0, r2 }; return o; }

static inline void two_stage_tap(TwoStage* s, uint64_t now_us) {
    s->state = TWO_STAGE_TAP; s->t0 = now_us + TWO_STAGE_TAP_US; s->taps++;
}

static inline void two_stage_charge(TwoStage* s) { s->state = TWO_STAGE_CHARGING; s->charges++; }

/* `enabled`: smoke is the active power, in game, with energy. Returns what the game must see. */
static inline TwoStageOut two_stage_update(TwoStage* s, int enabled, uint8_t r2, int pressed, uint64_t now_us) {
    if (!enabled || (pressed && r2 == 0)) {     /* off, or a pad without analog data: untouched */
        s->state = TWO_STAGE_IDLE;
        return two_stage_pass(pressed, r2);
    }
    switch (s->state) {
    case TWO_STAGE_IDLE:
        if (r2 >= TWO_STAGE_HALF_ON) { s->state = TWO_STAGE_DECIDING; s->t0 = now_us; }
        if (s->state == TWO_STAGE_DECIDING && r2 >= TWO_STAGE_FULL) two_stage_charge(s);
        break;
    case TWO_STAGE_DECIDING:
        if (r2 >= TWO_STAGE_FULL) two_stage_charge(s);
        else if (r2 < TWO_STAGE_HALF_OFF || now_us - s->t0 >= TWO_STAGE_DECIDE_US) two_stage_tap(s, now_us);
        break;
    case TWO_STAGE_TAP:
        if (r2 >= TWO_STAGE_FULL) { s->state = TWO_STAGE_CHARGING; s->charges++; }
        else if (now_us >= s->t0) s->state = r2 < TWO_STAGE_HALF_OFF ? TWO_STAGE_IDLE : TWO_STAGE_TAPPED;
        break;
    case TWO_STAGE_TAPPED:
        if (r2 >= TWO_STAGE_FULL) two_stage_charge(s);
        else if (r2 < TWO_STAGE_HALF_OFF) s->state = TWO_STAGE_IDLE;
        break;
    case TWO_STAGE_CHARGING:
        if (r2 < TWO_STAGE_CHARGE_OFF) s->state = r2 < TWO_STAGE_HALF_OFF ? TWO_STAGE_IDLE : TWO_STAGE_TAPPED;
        break;
    }
    int out = s->state == TWO_STAGE_TAP || s->state == TWO_STAGE_CHARGING;
    return two_stage_pass(out, out ? 255 : 0);
}
