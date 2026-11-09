/* Official DualSense trigger effects (Nielk1's TriggerEffectGenerator: Feedback 0x21, Weapon 0x25,
 * Vibration 0x26). Expected bytes were worked out by hand from that reference:
 *   zones are 10 bits (one per tenth of the travel), strength 1-8 is stored as (s-1) in 3 bits
 *   per zone. Feedback(0, 4): every zone 3 -> octal 3333333333 = 0x1b6db6db, active 0x03ff.
 *   Vibration(0, 2, 20): every zone 1 -> octal 1111111111 = 0x09249249, frequency at byte 9.
 *   Weapon(2, 4, 2): zone bits (1<<2)|(1<<4) = 0x0014, strength 2 -> 1. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/trigger_map.h"

static void check(const uint8_t* got, const uint8_t* want) {
    if (memcmp(got, want, DS_TRIGGER_EFFECT_SIZE) != 0) {
        for (int i = 0; i < DS_TRIGGER_EFFECT_SIZE; i++) printf("%02x/%02x ", got[i], want[i]);
        puts("");
        assert(0);
    }
}

static void test_feedback(void) {
    uint8_t e[DS_TRIGGER_EFFECT_SIZE];
    static const uint8_t want[] = {0x21, 0xff, 0x03, 0xdb, 0xb6, 0x6d, 0x1b, 0, 0, 0, 0};
    trigger_effect_feedback_zones(e, 0, 4);
    check(e, want);
}

static void test_vibration(void) {
    uint8_t e[DS_TRIGGER_EFFECT_SIZE];
    static const uint8_t want[] = {0x26, 0xff, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 20, 0};
    trigger_effect_vibration_zones(e, 0, 2, 20);
    check(e, want);
}

static void test_weapon(void) {
    uint8_t e[DS_TRIGGER_EFFECT_SIZE];
    static const uint8_t want[] = {0x25, 0x14, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0};
    trigger_effect_weapon_zones(e, 2, 4, 2);
    check(e, want);
}

static void test_feedback_from_a_later_zone(void) {
    uint8_t e[DS_TRIGGER_EFFECT_SIZE];
    /* position 8, strength 8: zones 8 and 9 = 7 -> (7<<24)|(7<<27) = 0x3f000000, active 0x0300 */
    static const uint8_t want[] = {0x21, 0x00, 0x03, 0x00, 0x00, 0x00, 0x3f, 0, 0, 0, 0};
    trigger_effect_feedback_zones(e, 8, 8);
    check(e, want);
}

static void test_out_of_range_turns_off(void) {
    uint8_t e[DS_TRIGGER_EFFECT_SIZE];
    trigger_effect_feedback_zones(e, 0, 9);      /* strength above 8 */
    assert(e[0] == DS_TRIGGER_MODE_OFF);
    trigger_effect_weapon_zones(e, 1, 4, 2);     /* weapon start must be 2..7 */
    assert(e[0] == DS_TRIGGER_MODE_OFF);
    trigger_effect_weapon_zones(e, 4, 4, 2);     /* end must be after start */
    assert(e[0] == DS_TRIGGER_MODE_OFF);
    trigger_effect_vibration_zones(e, 0, 0, 20); /* zero amplitude */
    assert(e[0] == DS_TRIGGER_MODE_OFF);
}

int main(void) {
    test_feedback();
    test_vibration();
    test_weapon();
    test_feedback_from_a_later_zone();
    test_out_of_range_turns_off();
    puts("test_trigger_effects: all passed");
    return 0;
}
