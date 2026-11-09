#include <assert.h>
#include <stdio.h>

#include "../source/pause_dump.h"

static void test_each_options_press_schedules_one_dump_after_the_delay(void) {
    PauseDump d; pause_dump_init(&d);
    assert(pause_dump_due(&d, 0, 0) == 0);                       /* no press: nothing */
    assert(pause_dump_due(&d, 1, 100) == 0);                     /* pressed: wait for the menu */
    assert(pause_dump_due(&d, 1, 100 + PAUSE_DUMP_DELAY_US - 1) == 0);
    assert(pause_dump_due(&d, 1, 100 + PAUSE_DUMP_DELAY_US) == 1);   /* dump 1: paused */
    assert(pause_dump_due(&d, 1, 10000000) == 0);                /* once */
}

static void test_dumps_alternate_paused_and_playing(void) {
    PauseDump d; pause_dump_init(&d);
    uint64_t t = 0;
    for (uint32_t press = 1; press <= PAUSE_DUMP_MAX; press++) {
        pause_dump_due(&d, press, t);
        int n = pause_dump_due(&d, press, t + PAUSE_DUMP_DELAY_US);
        assert(n == (int)press);
        assert(pause_dump_paused(n) == (press % 2 == 1));        /* odd: the menu is open */
        t += 5000000;
    }
    assert(pause_dump_due(&d, PAUSE_DUMP_MAX + 1, t) == 0);
    assert(pause_dump_due(&d, PAUSE_DUMP_MAX + 1, t + PAUSE_DUMP_DELAY_US) == 0);   /* capped */
}

static void test_a_new_press_before_the_dump_restarts_the_wait(void) {
    PauseDump d; pause_dump_init(&d);
    pause_dump_due(&d, 1, 0);
    pause_dump_due(&d, 2, 1000000);                              /* closed again quickly */
    assert(pause_dump_due(&d, 2, PAUSE_DUMP_DELAY_US) == 0);
    assert(pause_dump_due(&d, 2, 1000000 + PAUSE_DUMP_DELAY_US) == 2);   /* numbered by presses */
}

int main(void) {
    test_each_options_press_schedules_one_dump_after_the_delay();
    test_dumps_alternate_paused_and_playing();
    test_a_new_press_before_the_dump_restarts_the_wait();
    puts("test_pause_dump: all passed");
    return 0;
}
