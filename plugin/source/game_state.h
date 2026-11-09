// "Is the player in the game?" for inFAMOUS Second Son v01.00.
// The hero (the player's controller object) is published in a global that is 0 while the game
// boots and loads; the HayatoG/iss_pc power wheel uses the same object and the same sanity check
// on its maximum power energy (hero +0x1a1f0). In v01.00 the global sits at 0x01e97b78, found by
// that port's byte pattern for a reader of it (lea rax,[rip+global]; mov rdi,[rax];
// vmovss xmm0,[rdi+0x1a1f0]), which matches exactly once in this eboot (at 0x609f83).
#pragma once

#include <math.h>
#include <stdint.h>

#define HERO_GLOBAL      0x01e97b78ULL
#define HERO_ENERGY      0x1a1e8ULL   /* float: power energy ("magic"), issn_pc */
#define HERO_ENERGY_MAX  0x1a1f0ULL

/* Checked before dereferencing: user-space heap, 8-byte aligned. */
static inline int hero_pointer_plausible(uint64_t p) {
    return p >= 0x100000000ULL && p < 0x800000000ULL && (p & 7) == 0;
}

static inline int game_state_playing(uint64_t hero, float energy_max) {
    return hero_pointer_plausible(hero) && isfinite(energy_max) && energy_max > 0.0f && energy_max <= 100000.0f;
}

/* The game clock. The missiles' setter (0x5975c0: cmp esi,[rdi+0xdc]; je; mov [rdi+0xdc],esi; ...)
 * stamps the save with it: lea rax,[rip+clock]; mov eax,[rax+0x24]. Liondarkz/issn_pc reads the
 * same field through the same setter in v1.07; in v01.00 the lea sits at +0x29 and resolves to
 * 0x011bea10. The hero's action start/end (+0x19fc0/+0x19fc4) are expected on this clock. */
#define GAME_CLOCK_SETTER     0x005975c0ULL
#define GAME_CLOCK            0x011bea10ULL
#define GAME_CLOCK_NOW        0x24ULL
#define GAME_CLOCK_SETTER_LEN 51

/* The hero's current action (research, from the neon capture): type 1 = shot, 3 = recovery
 * after a burst; +0x19c0 = 1 during the shot; counter bumps on every action. */
#define HERO_ACTION_TYPE   0x18f8ULL
#define HERO_ACTION_COUNT  0x1904ULL
#define HERO_ACTION_BUSY   0x19c0ULL
#define HERO_ACTION_START  0x19fc0ULL
#define HERO_ACTION_END    0x19fc4ULL

/* Returns GAME_CLOCK when `code` (GAME_CLOCK_SETTER_LEN bytes at `site`) is the v01.00 missile
 * setter whose lea resolves to it, else 0. */
static inline uint64_t game_clock_from_setter(uint64_t site, const uint8_t *code) {
    static const uint8_t head[6] = {0x39, 0xb7, 0xdc, 0x00, 0x00, 0x00};
    static const uint8_t tail[3] = {0x8b, 0x40, 0x24};
    for (int i = 0; i < 6; i++) if (code[i] != head[i]) return 0;
    if (code[41] != 0x48 || code[42] != 0x8d || code[43] != 0x05) return 0;
    for (int i = 0; i < 3; i++) if (code[48 + i] != tail[i]) return 0;
    int32_t disp = (int32_t)((uint32_t)code[44] | (uint32_t)code[45] << 8 |
                             (uint32_t)code[46] << 16 | (uint32_t)code[47] << 24);
    uint64_t target = site + 48 + (int64_t)disp;
    return target == GAME_CLOCK ? target : 0;
}

/* Pause gate. HayatoG/iss_pc reads a pause counter (US 1.07 0x1d1f7fc); no counter of that
 * shape exists in v01.00. Diffing three paused and three playing dumps (infamous-re/dumps/pause)
 * found the game's time scale in the clock object: a float at GAME_CLOCK + 0x20, 1.0 while
 * playing, 0.0 in the pause menu. (GAME_CLOCK_NOW, +0x24, keeps running in the pause: it is
 * not game time, so v3.0's "clock stopped" gate never fired.) */
#define GAME_TIME_SCALE 0x20ULL

static inline int game_time_paused(float scale) {
    return isfinite(scale) && scale < 0.01f && scale > -0.01f;
}

/* Usable mask (FUN_005c2500, CAN_USE_POWERS): bits 0x00c00000 are set whenever the hero may use
 * powers and clear during a dash (00000c00 in triggers_v22_neon.log); shots do not touch it,
 * which is what tells a dash from a shot. 0xffffffff: not seen yet. */
#define USABLE_POWERS_BITS 0x00c00000u

static inline int powers_locked(uint32_t mask) {
    return mask != 0xffffffffu && (mask & USABLE_POWERS_BITS) == 0;
}
