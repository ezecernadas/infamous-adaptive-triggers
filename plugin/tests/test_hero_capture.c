#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/hero_capture.h"

static void test_waits_for_the_first_shot(void) {
    HeroCapture c; hero_capture_init(&c);
    assert(!hero_capture_due(&c, 5000000));
}

static void test_samples_every_interval_for_the_window(void) {
    HeroCapture c; hero_capture_init(&c);
    hero_capture_arm(&c, 1000000);
    int samples = 0;
    for (uint64_t t = 1000000; t < 1000000 + HERO_CAPTURE_WINDOW_US + 2000000; t += 10000)
        samples += hero_capture_due(&c, t);
    assert(samples == (int)(HERO_CAPTURE_WINDOW_US / HERO_CAPTURE_INTERVAL_US));
    assert(c.done);
}

static void test_arms_only_once(void) {
    HeroCapture c; hero_capture_init(&c);
    hero_capture_arm(&c, 0);
    for (uint64_t t = 0; t <= HERO_CAPTURE_WINDOW_US; t += 10000) hero_capture_due(&c, t);
    hero_capture_arm(&c, HERO_CAPTURE_WINDOW_US + 1000000);   /* a later shot does not restart it */
    assert(!hero_capture_due(&c, HERO_CAPTURE_WINDOW_US + 1000000));
}

int main(void) {
    test_waits_for_the_first_shot();
    test_samples_every_interval_for_the_window();
    test_arms_only_once();
    puts("test_hero_capture: all passed");
    return 0;
}
