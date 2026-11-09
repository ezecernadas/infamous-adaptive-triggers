#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/trigger_map.h"
#include "../source/two_stage.h"

#define SCRIPT ISS_CALLER_SCRIPT
#define ENGINE 0x44ef2bULL

static int r2_mode(const TriggerMap* m) { return m->r2[0]; }

static TriggerMap with_power(int32_t power) {
    TriggerMap m; trigger_map_init(&m);
    trigger_map_set_power(&m, power, 0);
    return m;
}

static void test_no_power_requests_nothing(void) {
    TriggerMap m; trigger_map_init(&m);
    assert(!m.active);
    trigger_map_set_power(&m, -1, 0);
    assert(!m.active);
}

static int smoke_idle(const TriggerMap* m) {
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_feedback_zones(want, SMOKE_WALL_POSITION, SMOKE_WALL_STRENGTH);
    return m->active && memcmp(m->r2, want, sizeof(want)) == 0;
}

static int video_idle(const TriggerMap* m) {
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_feedback_zones(want, VIDEO_IDLE_POSITION, VIDEO_IDLE_STRENGTH);
    return m->active && memcmp(m->r2, want, sizeof(want)) == 0;
}

/* Strength (1-8) of a Feedback effect from zone 0, 0 if r2 is not one. */
static int charge_strength(const TriggerMap* m) {
    for (int k = 1; k <= 8; k++) {
        uint8_t want[DS_TRIGGER_EFFECT_SIZE];
        trigger_effect_feedback_zones(want, SMOKE_WALL_POSITION, (uint8_t)k);
        if (memcmp(m->r2, want, sizeof(want)) == 0) return k;
    }
    return 0;
}

static int jammed(const TriggerMap* m) {
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_feedback_zones(want, EMPTY_POSITION, EMPTY_STRENGTH);
    return m->active && memcmp(m->r2, want, sizeof(want)) == 0;
}

static void test_smoke_idle_is_free_to_60_percent_then_a_hard_wall(void) {
    /* User (v2.8.1): 0-60 % free, normal shot there; 61-100 % hard, the charge */
    TriggerMap m = with_power(POWER_SMOKE);
    assert(smoke_idle(&m));
    assert(SMOKE_WALL_POSITION == 7);                          /* user, v2.9.1: one zone deeper */
    assert(SMOKE_WALL_STRENGTH >= 7);
    assert(TWO_STAGE_FULL >= 196 && TWO_STAGE_FULL <= 202);    /* where the zone-7 wall is felt */
}

static void test_neon_idle_is_a_firmer_deeper_click(void) {
    TriggerMap m = with_power(POWER_NEON);
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_weapon_zones(want, NEON_IDLE_START, NEON_IDLE_END, NEON_IDLE_STRENGTH);
    assert(m.active && memcmp(m.r2, want, sizeof(want)) == 0);
}

static void test_neon_click_is_back_above_the_fire_point(void) {
    /* At 4-6 the game fired before the click (user, v2.6) */
    assert(NEON_IDLE_START == 3 && NEON_IDLE_END == 5);
}

static int neon_click(const TriggerMap* m) {
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_weapon_zones(want, NEON_IDLE_START, NEON_IDLE_END, NEON_IDLE_STRENGTH);
    return m->active && memcmp(m->r2, want, sizeof(want)) == 0;
}

static int trigger_free(const TriggerMap* m) {
    return m->active && m->r2[0] == DS_TRIGGER_MODE_OFF;
}

static void neon_shot(TriggerMap* m, int32_t handle, uint64_t t) {
    trigger_map_start(m, POWER_NEON, 9, 0, handle, SCRIPT, t);
}

static void test_neon_shot_kicks_briefly_while_the_game_says_busy(void) {
    TriggerMap m = with_power(POWER_NEON);
    neon_shot(&m, 1, 1000);                                       /* the shot: id 9, 0.3 s */
    trigger_map_set_busy(&m, 1, 1002);                            /* the hero's shot action starts */
    uint8_t kick[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_vibration_zones(kick, 0, NEON_KICK_AMPLITUDE, NEON_KICK_HZ);
    assert(memcmp(m.r2, kick, sizeof(kick)) == 0);                /* the kick is not cut by busy */
    trigger_map_tick(&m, 1000 + NEON_KICK_US - 1);
    assert(m.r2[0] == DS_TRIGGER_MODE_VIBRATION);
    assert(NEON_KICK_US <= 120000);                               /* short: the shot is reported late */
}

static void test_neon_trigger_is_free_while_the_hero_is_busy(void) {
    TriggerMap m = with_power(POWER_NEON);
    neon_shot(&m, 1, 0);
    trigger_map_set_busy(&m, 1, 2000);
    trigger_map_tick(&m, NEON_KICK_US);                           /* kick over, shot action still running */
    assert(trigger_free(&m));
    trigger_map_set_busy(&m, 1, 170000);                          /* no change: nothing new */
    assert(trigger_free(&m));
    trigger_map_set_busy(&m, 0, 175000);                          /* the game lets the hero act again */
    assert(neon_click(&m));
}

static void test_neon_recovery_and_dash_free_the_trigger_without_any_shot(void) {
    TriggerMap m = with_power(POWER_NEON);
    trigger_map_set_busy(&m, 1, 0);                               /* burst recovery or a dash */
    assert(trigger_free(&m));
    trigger_map_tick(&m, 5000000);                                /* an endless dash: no timer gives up */
    assert(trigger_free(&m));
    trigger_map_set_busy(&m, 0, 5000001);
    assert(neon_click(&m));
}

static void test_video_trigger_is_free_while_busy(void) {
    TriggerMap m = with_power(POWER_VIDEO);
    trigger_map_set_busy(&m, 1, 0);
    assert(trigger_free(&m));
    trigger_map_set_busy(&m, 0, 10);
    assert(video_idle(&m));
}

static void test_smoke_wall_ignores_busy(void) {
    /* Each normal shot makes the hero busy: freeing the trigger made the wall flicker (v2.8.1) */
    TriggerMap m = with_power(POWER_SMOKE);
    uint32_t seq = m.seq;
    trigger_map_set_busy(&m, 1, 0);
    assert(smoke_idle(&m) && m.seq == seq);
    trigger_map_set_busy(&m, 0, 10);
    assert(smoke_idle(&m));
}

static void test_busy_waits_for_a_running_effect(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 3, SCRIPT, 0);
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US);                  /* charge resistance */
    trigger_map_set_busy(&m, 1, SMOKE_CHARGE_DELAY_US + 1);
    assert(r2_mode(&m) == DS_TRIGGER_MODE_FEEDBACK);              /* charging is busy too: keep it */
}

static void test_other_neon_rumbles_leave_the_click(void) {
    TriggerMap m = with_power(POWER_NEON);
    uint32_t seq = m.seq;
    trigger_map_start(&m, POWER_NEON, 8, 0, 1, SCRIPT, 0);
    trigger_map_start(&m, POWER_NEON, 9, 1, 2, SCRIPT, 0);     /* an infinite id 9 is not the shot */
    trigger_map_start(&m, POWER_NEON, 9, 0, 3, 0x44ef2bULL, 0); /* nor one from the engine */
    trigger_map_tick(&m, 1000000);
    assert(m.seq == seq && m.r2[0] == DS_TRIGGER_MODE_WEAPON);
}

static void test_mapped_reports_what_has_an_effect(void) {
    assert(trigger_map_is_mapped(POWER_VIDEO, 2, 1));
    assert(trigger_map_is_mapped(POWER_SMOKE, 1, 1));
    assert(trigger_map_is_mapped(POWER_SMOKE, 7, 0));
    assert(!trigger_map_is_mapped(POWER_SMOKE, 8, 0));   /* tap: deliberately no effect */
    assert(!trigger_map_is_mapped(POWER_NEON, 8, 0));
    assert(trigger_map_is_mapped(POWER_NEON, 9, 0));
}


static void test_video_idle_is_an_even_soft_resistance(void) {
    TriggerMap m = with_power(POWER_VIDEO);
    assert(video_idle(&m));
    assert(VIDEO_IDLE_STRENGTH == 1);                            /* the floor of the Feedback effect */
    assert(VIDEO_IDLE_POSITION == 5);   /* strength 1 still felt firm from zone 1 (user, v2.9.2): only the second half */
}

static void test_no_power_leaves_the_user_setting(void) {
    TriggerMap m = with_power(-1);
    trigger_map_set_busy(&m, 1, 0);
    trigger_map_set_empty(&m, 1, 0);
    assert(!m.active);
}

static void test_video_burst_is_softer_than_before(void) {
    assert(VIDEO_BURST_AMPLITUDE == 3);                       /* of 8; user: 2 too soft, raised one step */
}

static void test_video_burst_vibrates_softly_until_stopped(void) {
    TriggerMap m = with_power(POWER_VIDEO);
    trigger_map_start(&m, POWER_VIDEO, 2, 1, 7, SCRIPT, 1000);
    uint8_t want[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_vibration_zones(want, 0, VIDEO_BURST_AMPLITUDE, VIDEO_BURST_HZ);
    assert(m.active && memcmp(m.r2, want, sizeof(want)) == 0);
    trigger_map_tick(&m, 5000000);
    assert(r2_mode(&m) == DS_TRIGGER_MODE_VIBRATION);
    assert(m.active);
    trigger_map_stop(&m, 7, 5100000);
    assert(video_idle(&m));
}

static void test_smoke_tap_changes_nothing(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    uint32_t seq = m.seq;
    trigger_map_start(&m, POWER_SMOKE, 8, 0, 9, SCRIPT, 100);
    assert(m.seq == seq && smoke_idle(&m));
}

static void test_smoke_charge_from_a_tap_never_resists(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 3, SCRIPT, 0);       /* the tap briefly starts a charge */
    trigger_map_tick(&m, 70000);
    trigger_map_stop(&m, 3, 70000);                                /* ...and cancels it at ~70 ms */
    trigger_map_tick(&m, 400000);
    assert(smoke_idle(&m));                                        /* the firm resistance never came */
}





static void test_smoke_full_charge_pulses_once_then_holds(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 3, SCRIPT, 0);
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US);
    trigger_map_tick(&m, SMOKE_CHARGE_FULL_US);
    assert(r2_mode(&m) == DS_TRIGGER_MODE_VIBRATION);              /* "ready" pulse */
    trigger_map_tick(&m, SMOKE_CHARGE_FULL_US + SMOKE_READY_PULSE_US);
    assert(charge_strength(&m) == SMOKE_WALL_STRENGTH);
    trigger_map_tick(&m, 5000000);                                 /* held long: no second pulse */
    assert(charge_strength(&m) == SMOKE_WALL_STRENGTH);
    assert(SMOKE_CHARGE_FULL_US <= 760000);                        /* charged shots came at ~0.76 s */
}

static void test_video_burst_speeds_up_the_longer_it_runs(void) {
    TriggerMap m = with_power(POWER_VIDEO);
    trigger_map_start(&m, POWER_VIDEO, 2, 1, 7, SCRIPT, 0);
    uint8_t first = m.r2[9];
    assert(first == VIDEO_BURST_HZ);
    trigger_map_tick(&m, VIDEO_RAMP_STEP_US);
    assert(m.r2[9] == VIDEO_BURST_HZ + VIDEO_RAMP_HZ);
    trigger_map_tick(&m, 100 * VIDEO_RAMP_STEP_US);
    assert(m.r2[9] == VIDEO_BURST_MAX_HZ);                        /* capped */
    trigger_map_stop(&m, 7, 100 * VIDEO_RAMP_STEP_US + 1);
    trigger_map_start(&m, POWER_VIDEO, 2, 1, 8, SCRIPT, 200 * VIDEO_RAMP_STEP_US);
    assert(m.r2[9] == VIDEO_BURST_HZ);                            /* a new burst starts slow again */
}

static void test_neon_kick_is_crisp(void) {
    assert(NEON_KICK_HZ >= 60 && NEON_KICK_US <= 80000);
}


static void test_no_energy_jams_every_power(void) {
    int32_t powers[] = { POWER_NEON, POWER_SMOKE, POWER_VIDEO };
    for (int i = 0; i < 3; i++) {
        TriggerMap m = with_power(powers[i]);
        trigger_map_set_empty(&m, 1, 0);
        assert(jammed(&m));
        trigger_map_set_empty(&m, 0, 10);                          /* absorbed: back to normal */
        assert(!jammed(&m) && m.active);
    }
    assert(EMPTY_STRENGTH == 8);
}

static void test_empty_beats_busy_and_effects_beat_both(void) {
    TriggerMap m = with_power(POWER_NEON);
    trigger_map_set_empty(&m, 1, 0);
    trigger_map_set_busy(&m, 1, 1);           /* a dry fire makes the hero busy: stay jammed */
    assert(jammed(&m));
    trigger_map_set_busy(&m, 0, 2);
    assert(jammed(&m));
    trigger_map_set_empty(&m, 0, 3);
    assert(neon_click(&m));
    trigger_map_set_empty(&m, 1, 3);
    trigger_map_set_power(&m, POWER_VIDEO, 3);
    trigger_map_start(&m, POWER_VIDEO, 2, 1, 9, SCRIPT, 4);       /* the last shot of a burst */
    assert(r2_mode(&m) == DS_TRIGGER_MODE_VIBRATION);
}

static void test_dry_fire_is_mapped_but_has_no_effect(void) {
    assert(trigger_map_kind(POWER_SMOKE, 2, 0) == MAPPED_DRY_FIRE);   /* no energy: id 2, 0.2 s */
    assert(trigger_map_kind(POWER_NEON, 2, 0) == MAPPED_DRY_FIRE);
    assert(trigger_map_kind(POWER_VIDEO, 2, 1) == MAPPED_VIDEO_BURST); /* the burst stays the burst */
    TriggerMap m = with_power(POWER_SMOKE);
    uint32_t seq = m.seq;
    trigger_map_start(&m, POWER_SMOKE, 2, 0, 5, SCRIPT, 0);
    assert(m.seq == seq);
}

static void test_energy_gate_empty_at_the_threshold(void) {
    EnergyGate g; energy_gate_init(&g);
    assert(!energy_gate_empty(&g, 5.01f));
    assert(energy_gate_empty(&g, 0.11f));                           /* the console's last leftover */
    assert(!energy_gate_empty(&g, 19.5f));                          /* absorbed */
}

static void test_energy_gate_dry_fire_holds_until_energy_rises(void) {
    EnergyGate g; energy_gate_init(&g);
    energy_gate_dry_fire(&g, 0.8f);                                 /* a leftover too small to fire */
    assert(energy_gate_empty(&g, 0.8f));
    assert(energy_gate_empty(&g, 0.8f));
    assert(!energy_gate_empty(&g, 6.0f));                           /* absorbing clears it */
    assert(!energy_gate_empty(&g, 0.8f));                           /* and it does not come back alone */
}

static void test_pause_hands_every_power_back_to_the_user(void) {
    int32_t powers[] = { POWER_NEON, POWER_SMOKE, POWER_VIDEO };
    for (int i = 0; i < 3; i++) {
        TriggerMap m = with_power(powers[i]);
        trigger_map_set_paused(&m, 1, 0);
        assert(!m.active);
        trigger_map_set_empty(&m, 1, 1);
        trigger_map_set_busy(&m, 1, 2);
        assert(!m.active);                                      /* nothing else wins over pause */
        trigger_map_set_paused(&m, 0, 3);
        assert(m.active);
    }
}

static void test_pause_cuts_a_running_effect(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 3, SCRIPT, 0);
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US);                 /* charging */
    trigger_map_set_paused(&m, 1, SMOKE_CHARGE_DELAY_US + 1);
    assert(!m.active);
    trigger_map_tick(&m, 5000000);
    assert(!m.active);                                           /* the charge did not come back */
    TriggerMap v = with_power(POWER_VIDEO);
    trigger_map_start(&v, POWER_VIDEO, 2, 1, 7, SCRIPT, 0);
    trigger_map_set_paused(&v, 1, 10);
    assert(!v.active);
    trigger_map_start(&v, POWER_VIDEO, 2, 1, 8, SCRIPT, 20);    /* a rumble in the pause: ignored */
    assert(!v.active);
}

static void test_dash_frees_even_smoke(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_set_busy(&m, 1, 0);
    assert(smoke_idle(&m));                                      /* a shot: the wall stays */
    trigger_map_set_locked(&m, 1, 1);                            /* the mask says: dashing */
    assert(trigger_free(&m));
    trigger_map_set_locked(&m, 0, 2);
    assert(smoke_idle(&m));
}

static void test_smoke_held_charge_resists_then_kicks_then_idles(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 3, SCRIPT, 0);
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US - 1);
    assert(smoke_idle(&m));
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US);
    assert(charge_strength(&m) == SMOKE_WALL_STRENGTH);
    trigger_map_start(&m, POWER_SMOKE, 7, 0, 4, SCRIPT, 750000);  /* charged shot */
    trigger_map_stop(&m, 3, 750001);                               /* charge stop right after */
    assert(r2_mode(&m) == DS_TRIGGER_MODE_VIBRATION);
    trigger_map_tick(&m, 750000 + TRIGGER_KICK_STRONG_US);
    assert(smoke_idle(&m));
}

static void test_engine_and_non_r2_rumbles_are_ignored(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    uint32_t seq = m.seq;
    trigger_map_start(&m, POWER_SMOKE, 8, 0, 1, ENGINE, 0);   /* heavy landing */
    trigger_map_start(&m, POWER_SMOKE, 7, 0, 2, ENGINE, 0);   /* end of an absorption */
    trigger_map_start(&m, POWER_SMOKE, 1, 0, 3, SCRIPT, 0);   /* melee/dash: finite id 1 */
    trigger_map_start(&m, POWER_SMOKE, 6, 0, 4, SCRIPT, 0);   /* vent exit */
    trigger_map_tick(&m, 1000000);
    assert(m.seq == seq);
}

static void test_switching_power_switches_idle(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    assert(m.active);
    trigger_map_set_power(&m, POWER_VIDEO, 10);
    assert(video_idle(&m));
    trigger_map_set_power(&m, -1, 15);
    assert(!m.active);
    trigger_map_set_power(&m, POWER_SMOKE, 20);
    assert(smoke_idle(&m));
}

static void test_power_change_during_an_effect_waits_for_it(void) {
    TriggerMap m = with_power(POWER_VIDEO);
    trigger_map_start(&m, POWER_VIDEO, 2, 1, 1, SCRIPT, 0);
    trigger_map_set_power(&m, POWER_SMOKE, 10);
    assert(r2_mode(&m) == DS_TRIGGER_MODE_VIBRATION);         /* the burst is still running */
    trigger_map_stop(&m, 1, 20);
    assert(smoke_idle(&m));                                     /* then the new power's idle */
}

static void test_l2_is_never_touched(void) {
    TriggerMap m = with_power(POWER_SMOKE);
    trigger_map_start(&m, POWER_SMOKE, 1, 1, 1, SCRIPT, 0);
    trigger_map_tick(&m, SMOKE_CHARGE_DELAY_US);
    for (int i = 0; i < DS_TRIGGER_EFFECT_SIZE; i++) assert(m.l2[i] == (i == 0 ? DS_TRIGGER_MODE_OFF : 0));
}

int main(void) {
    test_no_power_requests_nothing();
    test_smoke_idle_is_free_to_60_percent_then_a_hard_wall();
    test_neon_idle_is_a_firmer_deeper_click();
    test_neon_shot_kicks_briefly_while_the_game_says_busy();
    test_neon_trigger_is_free_while_the_hero_is_busy();
    test_neon_recovery_and_dash_free_the_trigger_without_any_shot();
    test_video_trigger_is_free_while_busy();
    test_smoke_wall_ignores_busy();
    test_busy_waits_for_a_running_effect();
    test_other_neon_rumbles_leave_the_click();
    test_mapped_reports_what_has_an_effect();
    test_video_idle_is_an_even_soft_resistance();
    test_no_power_leaves_the_user_setting();
    test_smoke_full_charge_pulses_once_then_holds();
    test_neon_click_is_back_above_the_fire_point();
    test_video_burst_speeds_up_the_longer_it_runs();
    test_neon_kick_is_crisp();
    test_no_energy_jams_every_power();
    test_empty_beats_busy_and_effects_beat_both();
    test_dry_fire_is_mapped_but_has_no_effect();
    test_energy_gate_empty_at_the_threshold();
    test_energy_gate_dry_fire_holds_until_energy_rises();
    test_video_burst_is_softer_than_before();
    test_video_burst_vibrates_softly_until_stopped();
    test_smoke_tap_changes_nothing();
    test_smoke_charge_from_a_tap_never_resists();
    test_smoke_held_charge_resists_then_kicks_then_idles();
    test_engine_and_non_r2_rumbles_are_ignored();
    test_switching_power_switches_idle();
    test_power_change_during_an_effect_waits_for_it();
    test_l2_is_never_touched();
    test_pause_hands_every_power_back_to_the_user();
    test_pause_cuts_a_running_effect();
    test_dash_frees_even_smoke();
    puts("test_trigger_map: all passed");
    return 0;
}
