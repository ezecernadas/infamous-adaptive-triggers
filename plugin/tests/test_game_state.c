#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <stdint.h>

#include "../source/game_state.h"

static void test_null_hero_means_not_playing(void) {
    assert(!hero_pointer_plausible(0));
    assert(!game_state_playing(0, 100.0f));
}

static void test_garbage_pointers_are_rejected_before_any_read(void) {
    assert(!hero_pointer_plausible(0x1234));            /* small value */
    assert(!hero_pointer_plausible(0x22e100003ULL));    /* unaligned */
    assert(hero_pointer_plausible(0x22e100000ULL));
}

static void test_energy_maximum_must_be_sane(void) {
    assert(game_state_playing(0x22e100000ULL, 100.0f));
    assert(!game_state_playing(0x22e100000ULL, 0.0f));       /* not set up yet */
    assert(!game_state_playing(0x22e100000ULL, -1.0f));
    assert(!game_state_playing(0x22e100000ULL, 1.0e9f));     /* garbage */
    assert(!game_state_playing(0x22e100000ULL, NAN));
}

/* The missile setter at 0x5975c0 (v01.00), bytes as dumped from the eboot */
static const uint8_t k_setter[51] = {
    0x39, 0xb7, 0xdc, 0x00, 0x00, 0x00, 0x74, 0x37, 0x89, 0xb7, 0xdc, 0x00, 0x00, 0x00,
    0x80, 0xbf, 0xc9, 0x0a, 0x02, 0x00, 0x00, 0x74, 0x28, 0xff, 0x87, 0xd0, 0x0a, 0x02, 0x00,
    0x81, 0xbf, 0xd4, 0x0a, 0x02, 0x00, 0xc0, 0xc7, 0xff, 0xff, 0x75, 0x10,
    0x48, 0x8d, 0x05, 0x20, 0x74, 0xc2, 0x00, 0x8b, 0x40, 0x24};

static void test_game_clock_is_found_through_the_missile_setter(void) {
    assert(game_clock_from_setter(GAME_CLOCK_SETTER, k_setter) == GAME_CLOCK);
}

static void test_game_clock_rejects_other_code(void) {
    uint8_t code[sizeof(k_setter)];
    memcpy(code, k_setter, sizeof(code));
    code[48] = 0x90;                                  /* not "mov eax,[rax+0x24]" */
    assert(game_clock_from_setter(GAME_CLOCK_SETTER, code) == 0);
    memcpy(code, k_setter, sizeof(code));
    code[2] = 0xe4;                                   /* another field's setter */
    assert(game_clock_from_setter(GAME_CLOCK_SETTER, code) == 0);
    memcpy(code, k_setter, sizeof(code));
    code[44] ^= 0x10;                                 /* lea points elsewhere */
    assert(game_clock_from_setter(GAME_CLOCK_SETTER, code) == 0);
}



static void test_time_scale_zero_means_paused(void) {
    /* infamous-re/dumps/pause: clock +0x20 was 0.0 in the 3 paused dumps, 1.0 in the 3 playing */
    assert(game_time_paused(0.0f));
    assert(!game_time_paused(1.0f));
    assert(!game_time_paused(0.25f));          /* a slow motion is still gameplay */
    assert(!game_time_paused(NAN));            /* garbage: never block the triggers on it */
    assert(GAME_TIME_SCALE == 0x20);
}

static void test_dash_mask_locks_powers(void) {
    assert(powers_locked(0x00000c00));          /* neon dash (triggers_v22_neon.log) */
    assert(!powers_locked(0x00c00f53));         /* free */
    assert(!powers_locked(0x00c00f73));
    assert(!powers_locked(0x00c00d53));
    assert(!powers_locked(0x00c00833));         /* right after a dash */
    assert(!powers_locked(0x00c00c11));         /* after flying off a dash */
    assert(!powers_locked(0xffffffffu));        /* never seen yet */
}

int main(void) {
    test_dash_mask_locks_powers();
    test_time_scale_zero_means_paused();
    test_game_clock_is_found_through_the_missile_setter();
    test_game_clock_rejects_other_code();
    test_null_hero_means_not_playing();
    test_garbage_pointers_are_rejected_before_any_read();
    test_energy_maximum_must_be_sane();
    puts("test_game_state: all passed");
    return 0;
}
