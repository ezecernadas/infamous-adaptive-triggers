#include <assert.h>
#include <stdio.h>

#include "../source/two_stage.h"

/* Feed one pad sample; returns what the game sees. */
static TwoStageOut feed(TwoStage* s, uint8_t r2, int pressed, uint64_t t) {
    return two_stage_update(s, 1, r2, pressed, t);
}

static void test_light_touch_is_ignored(void) {
    TwoStage s; two_stage_init(&s);
    TwoStageOut o = feed(&s, TWO_STAGE_HALF_ON - 1, 1, 0);
    assert(!o.pressed && o.r2 == 0);
}

static void test_half_press_held_fires_one_normal_shot_after_the_wait(void) {
    TwoStage s; two_stage_init(&s);
    assert(!feed(&s, 130, 1, 0).pressed);                          /* deciding: nothing yet */
    assert(!feed(&s, 130, 1, TWO_STAGE_DECIDE_US - 1).pressed);
    TwoStageOut o = feed(&s, 130, 1, TWO_STAGE_DECIDE_US);         /* not deeper: a tap */
    assert(o.pressed && o.r2 == 255);
    assert(feed(&s, 130, 1, TWO_STAGE_DECIDE_US + TWO_STAGE_TAP_US - 1).pressed);
    assert(!feed(&s, 130, 1, TWO_STAGE_DECIDE_US + TWO_STAGE_TAP_US).pressed);   /* short: no charge */
    assert(!feed(&s, 130, 1, 2000000).pressed);                    /* held at half: one shot only */
    assert(TWO_STAGE_TAP_US < 70000);                              /* the game cancels a charge by ~70 ms */
}

static void test_quick_tap_fires_on_release_without_waiting(void) {
    TwoStage s; two_stage_init(&s);
    feed(&s, 130, 1, 0);
    TwoStageOut o = feed(&s, 20, 0, 30000);                        /* let go within the wait */
    assert(o.pressed);
    assert(!feed(&s, 0, 0, 30000 + TWO_STAGE_TAP_US).pressed);
}

static void test_rapid_taps_each_fire(void) {
    TwoStage s; two_stage_init(&s);
    int shots = 0, was = 0;
    for (int i = 0; i < 5; i++) {
        uint64_t t = (uint64_t)i * 200000;
        for (uint64_t dt = 0; dt < 200000; dt += 5000) {
            uint8_t r2 = dt < 60000 ? 140 : TWO_STAGE_HALF_OFF - 10;   /* never fully released */
            TwoStageOut o = feed(&s, r2, 1, t + dt);
            if (o.pressed && !was) shots++;
            was = o.pressed;
        }
    }
    assert(shots == 5);
}

static void test_full_press_charges_cleanly_and_release_fires(void) {
    TwoStage s; two_stage_init(&s);
    feed(&s, 130, 1, 0);
    TwoStageOut o = feed(&s, TWO_STAGE_FULL, 1, 40000);           /* through the wall within the wait */
    assert(o.pressed);
    for (uint64_t t = 40000; t < 1500000; t += 10000) assert(feed(&s, 240, 1, t).pressed);
    assert(feed(&s, TWO_STAGE_CHARGE_OFF, 1, 1500000).pressed);    /* easing off a bit keeps the charge */
    assert(!feed(&s, TWO_STAGE_CHARGE_OFF - 1, 1, 1510000).pressed);  /* out of the wall: the shot */
    assert(!feed(&s, 120, 1, 1600000).pressed);                    /* resting in the free half: no new shot */
}

static void test_no_normal_shot_before_a_charge(void) {
    TwoStage s; two_stage_init(&s);
    int presses = 0, was = 0;
    uint8_t ramp[] = { 60, 130, 170, 210, 250 };
    for (int i = 0; i < 5; i++) {
        TwoStageOut o = feed(&s, ramp[i], 1, (uint64_t)i * 15000);
        if (o.pressed && !was) presses++;
        was = o.pressed;
    }
    for (uint64_t t = 75000; t < 900000; t += 10000) {
        TwoStageOut o = feed(&s, 250, 1, t);
        assert(o.pressed);
        if (!was) presses++;
        was = 1;
    }
    assert(presses == 1);                                          /* one long press: the charge */
}

static void test_pressing_deeper_after_a_tap_starts_a_charge(void) {
    TwoStage s; two_stage_init(&s);
    feed(&s, 130, 1, 0);
    feed(&s, 130, 1, TWO_STAGE_DECIDE_US);                         /* tap fired */
    feed(&s, 130, 1, TWO_STAGE_DECIDE_US + TWO_STAGE_TAP_US);      /* tap over */
    assert(feed(&s, 240, 1, 500000).pressed);                      /* then all the way: charge */
}

static void test_disabled_passes_the_real_pad_through(void) {
    TwoStage s; two_stage_init(&s);
    TwoStageOut o = two_stage_update(&s, 0, 130, 1, 0);
    assert(o.pressed && o.r2 == 130);
    o = two_stage_update(&s, 0, 0, 0, 10);
    assert(!o.pressed && o.r2 == 0);
}

static void test_no_analog_data_passes_through(void) {
    /* A pad that reports the button but no analog value must keep working as before */
    TwoStage s; two_stage_init(&s);
    TwoStageOut o = feed(&s, 0, 1, 0);
    assert(o.pressed && o.r2 == 0);
}

static void test_disabling_mid_charge_resets(void) {
    TwoStage s; two_stage_init(&s);
    feed(&s, 250, 1, 0);
    feed(&s, 250, 1, 10000);
    two_stage_update(&s, 0, 0, 0, 20000);                          /* power switched */
    assert(!feed(&s, 0, 0, 30000).pressed);
    assert(s.state == TWO_STAGE_IDLE);
}

static void test_thresholds_are_ordered(void) {
    assert(TWO_STAGE_HALF_OFF < TWO_STAGE_HALF_ON && TWO_STAGE_HALF_ON < TWO_STAGE_CHARGE_OFF);
    assert(TWO_STAGE_CHARGE_OFF < TWO_STAGE_FULL);
    assert(TWO_STAGE_FULL * 100 / 255 == 78);   /* zone-7 wall, felt ~8 % later than its zone (v2.9.1) */
    assert(TWO_STAGE_DECIDE_US <= 80000);                          /* user chose a short wait */
}

static void test_thresholds_are_not_changed_by_pause_logic(void) {
    /* Pause disables the rewrite through `enabled` (main.c): the real pad passes */
    TwoStage s; two_stage_init(&s);
    TwoStageOut o = two_stage_update(&s, 0, 200, 1, 0);
    assert(o.pressed && o.r2 == 200);
}

int main(void) {
    test_thresholds_are_not_changed_by_pause_logic();
    test_light_touch_is_ignored();
    test_half_press_held_fires_one_normal_shot_after_the_wait();
    test_quick_tap_fires_on_release_without_waiting();
    test_rapid_taps_each_fire();
    test_full_press_charges_cleanly_and_release_fires();
    test_no_normal_shot_before_a_charge();
    test_pressing_deeper_after_a_tap_starts_a_charge();
    test_disabled_passes_the_real_pad_through();
    test_no_analog_data_passes_through();
    test_disabling_mid_charge_resets();
    test_thresholds_are_ordered();
    puts("test_two_stage: all passed");
    return 0;
}
