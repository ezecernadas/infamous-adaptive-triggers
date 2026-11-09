// Maps inFAMOUS Second Son (v01.00) power state + rumble events to DualSense trigger effects.
// Pure C, no game access: the plugin feeds it what its hooks observe.
//
// What the logs showed (infamous-re/rumble_run*.log):
//   - Only rumbles started by the script native StartRumble (return address ISS_CALLER_SCRIPT)
//     belong to powers; the engine's own (landings, absorbing) come from elsewhere.
//   - Video (power 3): R2 burst = id 2 with infinite duration, stopped when the burst ends.
//   - Smoke (power 1): tap shot = id 8; charging = id 1 infinite, stopped on release (a tap also
//     starts one and cancels it ~70 ms later); charged shot = id 7 (finite) right before that stop.
//   - Neon (0): one shot per tap = id 9 (finite, 0.3 s), five of five shots in
//     infamous-re/triggers_v1_7_neon.log. Concrete (4): not recorded yet, nothing mapped.
//
// Tuning from the console tests (2026-11-08): the smoke tap's kick arrived after the trigger was
// already down and felt late, so smoke instead keeps a resting effect while it is the active
// power (first a light even resistance, then, at the user's request, a soft early click), and
// the firm charge resistance only starts once a charge outlasts a tap. Video's burst was made
// softer three times (0xa0 -> 0x70 -> 0x50 -> 0x38) and the smoke click went 0x90 -> 0x60 with
// no real change in feel, which is why the official 1-8 effects replaced the simple ones; on
// those, 2 of 8 was too soft for both, so they sit at 3.
#pragma once

#include <stdint.h>
#include <string.h>

#define ISS_CALLER_SCRIPT 0x0063f23eULL   /* return address inside the StartRumble native */

enum { POWER_NEON = 0, POWER_SMOKE = 1, POWER_VIDEO = 3, POWER_CONCRETE = 4 };

#define DS_TRIGGER_EFFECT_SIZE    11
/* Official effects (documented 1-8 strengths over 10 zones of the travel). The "simple" modes
 * 0x01/0x02/0x06 used first did not soften in proportion to their 0-255 values on the user's
 * controller, so everything moved to these. */
#define DS_TRIGGER_MODE_OFF       0x05
#define DS_TRIGGER_MODE_FEEDBACK  0x21
#define DS_TRIGGER_MODE_WEAPON    0x25
#define DS_TRIGGER_MODE_VIBRATION 0x26

/* Strengths are 1 (softest) to 8; positions are zones 0-9 of the trigger travel. */
#define VIDEO_BURST_HZ          20
#define VIDEO_BURST_AMPLITUDE   3
/* Video's burst speeds up while it runs (the game cuts it at ~4 s). */
#define VIDEO_RAMP_STEP_US      750000ULL
#define VIDEO_RAMP_HZ           5
#define VIDEO_BURST_MAX_HZ      40
/* Video at rest: an even, soft resistance, like an automatic's trigger. */
/* v2.9.3: strength 1 (the Feedback floor) still felt firm across zones 1-9, so it only covers the
 * second half of the travel now. */
#define VIDEO_IDLE_POSITION     5
#define VIDEO_IDLE_STRENGTH     1
/* Out of energy: the trigger "jams", a hard wall from early in the travel. Energy at or under
 * EMPTY_ENERGY counts as empty (hero +0x1a1e8; to be confirmed against the game's own limit). */
#define EMPTY_POSITION          1
#define EMPTY_STRENGTH          8
#define EMPTY_ENERGY            0.5f
/* v2.9 (user's spec): 0-60 % of the travel free (the normal shot, two_stage.h), 61-100 % a
 * hard, even wall (the charge). Earlier: a click (v2.6 at zones 4-6 fired before the click), a
 * Feedback wall from zone 4 (felt before it, v2.8), a Weapon wall 4-8 (gave way at its end, and
 * the busy rule freed it on every shot, so it loosened and stiffened again, v2.8.1). */
#define SMOKE_WALL_POSITION     7   /* v2.9.2: user moved it one zone deeper */
#define SMOKE_WALL_STRENGTH     7
/* Smoke charge: the same wall, then a short "ready" pulse (resistance steps were not felt on a
 * trigger held still). Charged shots came 0.755-0.776 s after the charge started
 * (rumble_run8_v4_smoke.log), so it is full by ~0.75 s. */
#define SMOKE_CHARGE_FULL_US    740000ULL
#define SMOKE_READY_PULSE_US    60000ULL
#define SMOKE_READY_AMPLITUDE   5
#define SMOKE_READY_HZ          80
/* v2.6 tried a vibration tick on every step; the user could not tell them apart, so v2.7 is
 * back to the v2.5 charge (resistance steps, one "ready" pulse). */
/* Neon at rest: one shot per tap, so a firmer, deeper click like a semi-automatic's. */
#define NEON_IDLE_START         3
#define NEON_IDLE_END           5
#define NEON_IDLE_STRENGTH      4
/* Neon shot (id 9): a short, crisp kick (a laser "tac", not a rumble); the game reports it
 * once the trigger is down. */
#define NEON_KICK_AMPLITUDE     5
#define NEON_KICK_HZ            70
#define NEON_KICK_US            70000ULL
/* "Can I fire?" comes from the game, not from timers: the hero's busy flag (hero +0x19c0,
 * game_state.h) is set during every shot, the recovery after a neon burst and dashes
 * (infamous-re/triggers_v22_neon.log), so the resting click is replaced by a free trigger while
 * it is set. Timers measured earlier (0.36 s cadence, bursts of 4) would break with upgrades. */
#define SMOKE_KICK_HZ           40
#define SMOKE_KICK_AMPLITUDE    6
#define SMOKE_CHARGE_DELAY_US   150000ULL
#define TRIGGER_KICK_STRONG_US  220000ULL

enum { EVENT_NONE, EVENT_PENDING_CHARGE, EVENT_HOLD, EVENT_TIMED, EVENT_CHARGE, EVENT_BURST };

typedef struct {
    int active;                          /* 0: let OneFamily use the user's setting */
    uint8_t r2[DS_TRIGGER_EFFECT_SIZE];
    uint8_t l2[DS_TRIGGER_EFFECT_SIZE];  /* reserved: always "off" for now */
    uint32_t seq;                        /* bumps whenever the requested effect changes */
    int32_t power;                       /* active power, -1 unknown */
    int event;                           /* EVENT_* currently shaping r2 */
    int32_t handle;                      /* rumble whose STOP ends a hold or a pending charge */
    uint64_t until_us;                   /* end of a timed effect / start of a pending charge */
    uint64_t now_us;                     /* time of the last call that passed one */
    int busy;                            /* the game says the hero cannot act (shot, recovery, dash) */
    int empty;                           /* no power energy left */
    int paused;                          /* the game is paused or loading: hands R2 back to the user */
    int locked;                          /* the game locks powers (dash): free trigger, smoke too */
    uint64_t event_start_us;             /* start of a charge (smoke) or a burst (video) */
    int stage;                           /* charge step / burst ramp step currently shown */
} TriggerMap;

static inline void trigger_effect_off(uint8_t e[DS_TRIGGER_EFFECT_SIZE]) {
    memset(e, 0, DS_TRIGGER_EFFECT_SIZE);
    e[0] = DS_TRIGGER_MODE_OFF;
}

/* Official Feedback (0x21): resistance of `strength` (1-8) from zone `position` (0-9) on. */
static inline void trigger_effect_feedback_zones(uint8_t e[DS_TRIGGER_EFFECT_SIZE], uint8_t position, uint8_t strength) {
    if (position > 9 || strength < 1 || strength > 8) { trigger_effect_off(e); return; }
    uint32_t force = 0;
    uint16_t active = 0;
    for (int i = position; i < 10; i++) { force |= (uint32_t)(strength - 1) << (3 * i); active |= (uint16_t)(1u << i); }
    memset(e, 0, DS_TRIGGER_EFFECT_SIZE);
    e[0] = DS_TRIGGER_MODE_FEEDBACK;
    e[1] = (uint8_t)active; e[2] = (uint8_t)(active >> 8);
    e[3] = (uint8_t)force; e[4] = (uint8_t)(force >> 8); e[5] = (uint8_t)(force >> 16); e[6] = (uint8_t)(force >> 24);
}

/* Official Weapon (0x25): resistance from zone `start` (2-7) to `end` (up to 8), then a release. */
static inline void trigger_effect_weapon_zones(uint8_t e[DS_TRIGGER_EFFECT_SIZE], uint8_t start, uint8_t end, uint8_t strength) {
    if (start < 2 || start > 7 || end > 8 || end <= start || strength < 1 || strength > 8) { trigger_effect_off(e); return; }
    uint16_t zones = (uint16_t)((1u << start) | (1u << end));
    memset(e, 0, DS_TRIGGER_EFFECT_SIZE);
    e[0] = DS_TRIGGER_MODE_WEAPON;
    e[1] = (uint8_t)zones; e[2] = (uint8_t)(zones >> 8);
    e[3] = (uint8_t)(strength - 1);
}

/* Official Vibration (0x26): `amplitude` (1-8) at `hz` from zone `position` (0-9) on. */
static inline void trigger_effect_vibration_zones(uint8_t e[DS_TRIGGER_EFFECT_SIZE], uint8_t position, uint8_t amplitude, uint8_t hz) {
    if (position > 9 || amplitude < 1 || amplitude > 8 || hz == 0) { trigger_effect_off(e); return; }
    uint32_t amp = 0;
    uint16_t active = 0;
    for (int i = position; i < 10; i++) { amp |= (uint32_t)(amplitude - 1) << (3 * i); active |= (uint16_t)(1u << i); }
    memset(e, 0, DS_TRIGGER_EFFECT_SIZE);
    e[0] = DS_TRIGGER_MODE_VIBRATION;
    e[1] = (uint8_t)active; e[2] = (uint8_t)(active >> 8);
    e[3] = (uint8_t)amp; e[4] = (uint8_t)(amp >> 8); e[5] = (uint8_t)(amp >> 16); e[6] = (uint8_t)(amp >> 24);
    e[9] = hz;
}

static inline void trigger_map_init(TriggerMap* m) {
    memset(m, 0, sizeof(*m));
    trigger_effect_off(m->r2);
    trigger_effect_off(m->l2);
    m->power = -1;
    m->handle = -1;
    m->event = EVENT_NONE;
}

/* The resting effect of the active power. */
/* Powers with their own resting effect; the others leave R2 to the user's setting. */
static inline int trigger_map_has_idle_click(int32_t power) {
    return power == POWER_SMOKE || power == POWER_NEON || power == POWER_VIDEO;
}

/* Smoke's wall stays while busy: its own normal shots set busy (see SMOKE_WALL_POSITION). */
static inline int trigger_map_frees_when_busy(int32_t power) {
    return power == POWER_NEON || power == POWER_VIDEO;
}

static inline void trigger_map_apply_idle(TriggerMap* m) {
    /* Empty wins over busy: a dry fire makes the hero busy too (triggers_v24.log), and the jam
     * must not give way on every press. */
    if (m->paused) {
        trigger_effect_off(m->r2);
        m->active = 0;
    } else if (trigger_map_has_idle_click(m->power) && m->empty) {
        trigger_effect_feedback_zones(m->r2, EMPTY_POSITION, EMPTY_STRENGTH);   /* jammed */
        m->active = 1;
    } else if (trigger_map_has_idle_click(m->power) && m->locked) {
        trigger_effect_off(m->r2);      /* dashing: no shot possible, for smoke as well */
        m->active = 1;
    } else if (trigger_map_frees_when_busy(m->power) && m->busy) {
        trigger_effect_off(m->r2);      /* no shot possible: a free trigger, not the user's preset */
        m->active = 1;
    } else if (m->power == POWER_VIDEO) {
        trigger_effect_feedback_zones(m->r2, VIDEO_IDLE_POSITION, VIDEO_IDLE_STRENGTH);
        m->active = 1;
    } else if (m->power == POWER_SMOKE) {
        trigger_effect_feedback_zones(m->r2, SMOKE_WALL_POSITION, SMOKE_WALL_STRENGTH);
        m->active = 1;
    } else if (m->power == POWER_NEON) {
        trigger_effect_weapon_zones(m->r2, NEON_IDLE_START, NEON_IDLE_END, NEON_IDLE_STRENGTH);
        m->active = 1;
    } else {
        trigger_effect_off(m->r2);
        m->active = 0;
    }
    m->seq++;
}

static inline void trigger_map_end_event(TriggerMap* m) {
    m->event = EVENT_NONE;
    m->handle = -1;
    m->until_us = 0;
    trigger_map_apply_idle(m);
}

/* Call with the current power on every tick; idle effects follow power switches. */
static inline void trigger_map_set_power(TriggerMap* m, int32_t power, uint64_t now_us) {
    m->now_us = now_us;
    if (power == m->power) return;
    m->power = power;
    if (m->event == EVENT_NONE || m->event == EVENT_PENDING_CHARGE) {
        m->event = EVENT_NONE;
        m->handle = -1;
        trigger_map_apply_idle(m);
    }
}

/* Call with the hero's busy flag on every tick; running effects (kicks, charge, burst) win. */
static inline void trigger_map_set_busy(TriggerMap* m, int busy, uint64_t now_us) {
    m->now_us = now_us;
    busy = busy != 0;
    if (busy == m->busy) return;
    m->busy = busy;
    if (m->event == EVENT_NONE && trigger_map_frees_when_busy(m->power)) trigger_map_apply_idle(m);
}

/* Call with "no energy left" on every tick; like busy, it only changes the resting effect. */
static inline void trigger_map_set_empty(TriggerMap* m, int empty, uint64_t now_us) {
    m->now_us = now_us;
    empty = empty != 0;
    if (empty == m->empty) return;
    m->empty = empty;
    if (m->event == EVENT_NONE && trigger_map_has_idle_click(m->power)) trigger_map_apply_idle(m);
}

/* "No energy" from the hero's energy plus the game's own dry-fire rumble: a dry fire with
 * some energy left (too little for a shot) counts as empty until the energy rises again. */
typedef struct { float dry_at; } EnergyGate;   /* energy at the last dry fire, < 0: none */

static inline void energy_gate_init(EnergyGate* g) { g->dry_at = -1.0f; }

static inline void energy_gate_dry_fire(EnergyGate* g, float energy) { g->dry_at = energy; }

static inline int energy_gate_empty(EnergyGate* g, float energy) {
    if (g->dry_at >= 0.0f && energy > g->dry_at + 0.01f) g->dry_at = -1.0f;   /* absorbed */
    return energy <= EMPTY_ENERGY || g->dry_at >= 0.0f;
}

/* Call with "paused" on every tick; pause cuts any running effect and wins over everything. */
static inline void trigger_map_set_paused(TriggerMap* m, int paused, uint64_t now_us) {
    m->now_us = now_us;
    paused = paused != 0;
    if (paused == m->paused) return;
    m->paused = paused;
    m->event = EVENT_NONE; m->handle = -1; m->until_us = 0;
    trigger_map_apply_idle(m);
}

/* Call with powers_locked(mask) on every tick; like busy, it only changes the resting effect. */
static inline void trigger_map_set_locked(TriggerMap* m, int locked, uint64_t now_us) {
    m->now_us = now_us;
    locked = locked != 0;
    if (locked == m->locked) return;
    m->locked = locked;
    if (m->event == EVENT_NONE && trigger_map_has_idle_click(m->power)) trigger_map_apply_idle(m);
}

static inline uint8_t video_burst_hz(int stage) {
    int hz = VIDEO_BURST_HZ + stage * VIDEO_RAMP_HZ;
    return (uint8_t)(hz > VIDEO_BURST_MAX_HZ ? VIDEO_BURST_MAX_HZ : hz);
}

enum { MAPPED_NONE, MAPPED_VIDEO_BURST, MAPPED_SMOKE_CHARGE, MAPPED_SMOKE_CHARGED_SHOT, MAPPED_NEON_SHOT,
       MAPPED_DRY_FIRE };

/* Which effect a script rumble maps to (MAPPED_NONE: no trigger effect for it). */
static inline int trigger_map_kind(int32_t power, int32_t rumble_id, int infinite) {
    if (power == POWER_VIDEO && rumble_id == 2 && infinite) return MAPPED_VIDEO_BURST;
    if (power == POWER_SMOKE && rumble_id == 1 && infinite) return MAPPED_SMOKE_CHARGE;
    if (power == POWER_SMOKE && rumble_id == 7 && !infinite) return MAPPED_SMOKE_CHARGED_SHOT;
    if (power == POWER_NEON && rumble_id == 9 && !infinite) return MAPPED_NEON_SHOT;
    /* R2 with no energy: id 2, finite 0.2 s (seen on smoke; assumed the same for the others) */
    if (power >= 0 && rumble_id == 2 && !infinite) return MAPPED_DRY_FIRE;
    return MAPPED_NONE;
}

static inline int trigger_map_is_mapped(int32_t power, int32_t rumble_id, int infinite) {
    return trigger_map_kind(power, rumble_id, infinite) != MAPPED_NONE;
}

static inline void trigger_map_start(TriggerMap* m, int32_t power, int32_t rumble_id, int infinite,
                                     int32_t handle, uint64_t caller_off, uint64_t now_us) {
    if (caller_off != ISS_CALLER_SCRIPT || m->paused) return;
    m->now_us = now_us;
    switch (trigger_map_kind(power, rumble_id, infinite)) {
    case MAPPED_VIDEO_BURST:
        trigger_effect_vibration_zones(m->r2, 0, VIDEO_BURST_AMPLITUDE, video_burst_hz(0));
        m->active = 1; m->event = EVENT_BURST; m->handle = handle; m->until_us = 0; m->seq++;
        m->event_start_us = now_us; m->stage = 0;
        break;
    case MAPPED_SMOKE_CHARGE:
        /* Taps start a charge too: wait before making the trigger firm */
        m->event = EVENT_PENDING_CHARGE; m->handle = handle; m->until_us = now_us + SMOKE_CHARGE_DELAY_US;
        break;
    case MAPPED_SMOKE_CHARGED_SHOT:
        trigger_effect_vibration_zones(m->r2, 0, SMOKE_KICK_AMPLITUDE, SMOKE_KICK_HZ);
        m->active = 1; m->event = EVENT_TIMED; m->handle = -1; m->until_us = now_us + TRIGGER_KICK_STRONG_US; m->seq++;
        break;
    case MAPPED_NEON_SHOT:
        trigger_effect_vibration_zones(m->r2, 0, NEON_KICK_AMPLITUDE, NEON_KICK_HZ);
        m->active = 1; m->event = EVENT_TIMED; m->handle = -1; m->until_us = now_us + NEON_KICK_US; m->seq++;
        break;
    default:
        break;
    }
}

static inline void trigger_map_stop(TriggerMap* m, int32_t handle, uint64_t now_us) {
    (void)now_us;
    if (handle < 0 || handle != m->handle) return;
    if (m->event == EVENT_HOLD || m->event == EVENT_BURST || m->event == EVENT_CHARGE) trigger_map_end_event(m);
    else if (m->event == EVENT_PENDING_CHARGE) { m->event = EVENT_NONE; m->handle = -1; m->until_us = 0; }
}

static inline void trigger_map_tick(TriggerMap* m, uint64_t now_us) {
    m->now_us = now_us;
    if (m->paused) return;
    if (m->event == EVENT_PENDING_CHARGE && now_us >= m->until_us) {
        /* until_us held the charge start + delay */
        m->event_start_us = m->until_us - SMOKE_CHARGE_DELAY_US;
        trigger_effect_feedback_zones(m->r2, SMOKE_WALL_POSITION, SMOKE_WALL_STRENGTH);
        m->active = 1; m->event = EVENT_CHARGE; m->until_us = 0; m->stage = 1; m->seq++;
    } else if (m->event == EVENT_CHARGE) {
        uint64_t t = now_us - m->event_start_us;
        /* stages: 1 wall, 2 ready pulse, 3 wall again */
        int stage = t >= SMOKE_CHARGE_FULL_US + SMOKE_READY_PULSE_US ? 3
                  : t >= SMOKE_CHARGE_FULL_US ? 2 : 1;
        if (stage != m->stage) {
            if (stage == 2) trigger_effect_vibration_zones(m->r2, 0, SMOKE_READY_AMPLITUDE, SMOKE_READY_HZ);
            else trigger_effect_feedback_zones(m->r2, SMOKE_WALL_POSITION, SMOKE_WALL_STRENGTH);
            m->stage = stage; m->seq++;
        }
    } else if (m->event == EVENT_BURST) {
        int stage = (int)((now_us - m->event_start_us) / VIDEO_RAMP_STEP_US);
        if (video_burst_hz(stage) != video_burst_hz(m->stage)) {
            trigger_effect_vibration_zones(m->r2, 0, VIDEO_BURST_AMPLITUDE, video_burst_hz(stage));
            m->seq++;
        }
        m->stage = stage;
    } else if (m->event == EVENT_TIMED && now_us >= m->until_us) {
        trigger_map_end_event(m);
    }
}
