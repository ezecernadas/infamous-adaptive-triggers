// inFAMOUS Second Son (CUSA00223 v01.00) -> OneFamily adaptive triggers.
// Hooks the game's start/stop rumble functions, reads the active power, maps both to DualSense
// trigger effects (trigger_map.h) and publishes them for the OneFamily daemon
// (game_trigger_protocol.h). The game itself is never changed: both hooks call the originals.
// Addresses come from this project's reverse engineering (infamous-re/) and, for the active
// power, from the HayatoG/iss_pc power wheel (player state +0xe4), mapped to v01.00.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <orbis/libkernel.h>

#include "plugin_common.h"
#include "game_trigger_protocol.h"
#include "trigger_map.h"
#include "game_state.h"
#include "hero_capture.h"
#include "two_stage.h"
#include "pause_dump.h"
#include <Patcher.h>
#include "pad.h"

attr_public const char *g_pluginName = "infamous_triggers";
attr_public const char *g_pluginDesc = "DualSense adaptive triggers for inFAMOUS Second Son via OneFamily";
attr_public const char *g_pluginAuth = "ezecernadas";
attr_public uint32_t g_pluginVersion = 0x00000310; // 3.10

#define TARGET_TITLE    "CUSA00223"
#define TARGET_VERSION  "01.00"
#define GHIDRA_BASE     0x00400000ULL
#define START_RUMBLE    0x007804d0ULL   // int start_rumble(int* ctx, int* pattern, float duration, float scale)
#define STOP_RUMBLE     0x00780860ULL   // void stop_rumble(int* ctx, int handle)
#define PATTERN_TABLE   0x0107a4d0ULL   // 18 patterns of 0x104 bytes, index = rumble id
#define PATTERN_STRIDE  0x104ULL
#define PATTERN_COUNT   18
#define ACTIVE_POWER    0x0128d22cULL   // int: player state (0x0128d148) + 0xe4
#define USABLE_MASK     0x005c2500ULL   // uint usable_mask(hero): what the hero may do right now (CAN_USE_POWERS)
#define HERO_READER     0x00609f83ULL   // lea rax,[rip+HERO_GLOBAL]; mov rdi,[rax]; vmovss xmm0,[rdi+0x1a1f0]
#define CAPTURE_PATH    "/data/GoldHEN/infamous_hero_capture.bin"
#ifndef ENABLE_HERO_CAPTURE
#define ENABLE_HERO_CAPTURE 0          // research only: 20 MB of hero snapshots per session
#endif
#define LOG_PATH        "/data/GoldHEN/infamous_triggers.log"
#ifndef ENABLE_PAUSE_DUMPS
#define ENABLE_PAUSE_DUMPS 0           // research only: 6 x 65 MB dumps, one per Options press
#endif
#define PAUSE_DUMP_FMT  "/data/GoldHEN/infamous_pause_dump_%d.bin"
#define RW_SEGMENT      0x01074000ULL  // the eboot's RW segment (as infamous_rumble_logger)
#define RW_SIZE         0x040b815fULL
#define TMP_PATH        GAME_TRIGGER_PATH ".tmp"

static const uint8_t k_start_prologue[14] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50
};
static const uint8_t k_stop_prologue[16] = {
    0x48, 0x8d, 0x4f, 0x10, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef int32_t (*start_rumble_t)(int32_t *ctx, int32_t *pattern, float duration, float scale);
typedef void (*stop_rumble_t)(int32_t *ctx, int32_t handle);

static struct proc_info s_proc;
static Detour s_start_detour, s_stop_detour;
static bool s_start_hooked, s_stop_hooked;
static volatile bool s_running;
static uint64_t s_start_us;
static TriggerMap s_map;
static int s_lock;   // spinlock: hooks run on game threads, the publisher on its own
static bool s_clock_ok;   // the game clock was located (game_state.h); research logging only

// Unmapped script rumbles, logged by the publisher so new powers can be mapped later.
// Written from game threads, read by the publisher; slots carry a ready flag.
typedef struct { uint64_t t; int32_t power, id, infinite, handle; float duration; int32_t mapped; } Unmapped;
static uint32_t s_dry_fires;        // dry-fire rumbles seen (game threads), read by the publisher
static uint64_t s_arm_request_us;   // first shot time, read by the publisher to arm the capture
#define UNMAPPED_SLOTS 128
static Unmapped s_unmapped[UNMAPPED_SLOTS];
static uint8_t s_unmapped_ready[UNMAPPED_SLOTS];
static uint32_t s_unmapped_head, s_unmapped_tail;

static void note_unmapped(const Unmapped *u) {
    uint32_t head = __atomic_fetch_add(&s_unmapped_head, 1, __ATOMIC_ACQ_REL);
    uint32_t i = head % UNMAPPED_SLOTS;
    if (__atomic_load_n(&s_unmapped_ready[i], __ATOMIC_ACQUIRE)) return;   // full: drop
    s_unmapped[i] = *u;
    __atomic_store_n(&s_unmapped_ready[i], 1, __ATOMIC_RELEASE);
}

static void lock(void) { while (__atomic_exchange_n(&s_lock, 1, __ATOMIC_ACQUIRE)) { } }
static void unlock(void) { __atomic_store_n(&s_lock, 0, __ATOMIC_RELEASE); }

static uint64_t now_us(void) { return sceKernelGetProcessTime() - s_start_us; }
static uint64_t runtime(uint64_t ghidra) { return s_proc.base_address + (ghidra - GHIDRA_BASE); }

// usable_mask: push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; mov r14,rdi (14 bytes)
static const uint8_t k_usable_prologue[14] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x49, 0x89, 0xfe
};
typedef uint32_t (*usable_mask_t)(void *hero);
static Detour s_usable_detour;
static bool s_usable_hooked;
// Research: changes of the usable mask, with the caller, for the publisher to log
typedef struct { uint64_t t, caller; uint32_t mask; } MaskChange;
#define MASK_SLOTS 256
static MaskChange s_masks[MASK_SLOTS];
static uint8_t s_mask_ready[MASK_SLOTS];
static uint32_t s_mask_head, s_mask_tail, s_last_mask = 0xffffffffu;
static uint64_t s_mask_seen_us;   // last call of the usable-mask function: older than MASK_STALE_US is ignored
#define MASK_STALE_US 300000ULL

static const uint8_t k_hero_reader_tail[11] = {0x48, 0x8b, 0x38, 0xc5, 0xfa, 0x10, 0x87, 0xf0, 0xa1, 0x01, 0x00};

static bool hero_reader_matches(void) {
    const uint8_t *site = (const uint8_t *)(uintptr_t)runtime(HERO_READER);
    if (site[0] != 0x48 || site[1] != 0x8d || site[2] != 0x05) return false;
    int32_t disp;
    memcpy(&disp, site + 3, 4);
    if ((int64_t)HERO_READER + 7 + disp != (int64_t)HERO_GLOBAL) return false;
    return memcmp(site + 7, k_hero_reader_tail, sizeof(k_hero_reader_tail)) == 0;
}

// True once the hero exists (0 while the game boots and loads)
static bool playing(void) {
    uint64_t hero;
    memcpy(&hero, (const void *)(uintptr_t)runtime(HERO_GLOBAL), sizeof(hero));
    if (!hero_pointer_plausible(hero)) return false;
    float energy_max;
    memcpy(&energy_max, (const void *)(uintptr_t)(hero + HERO_ENERGY_MAX), sizeof(energy_max));
    return game_state_playing(hero, energy_max);
}

static int32_t active_power(void) {
    if (!playing()) return -1;
    int32_t p;
    memcpy(&p, (const void *)(uintptr_t)runtime(ACTIVE_POWER), sizeof(p));
    return (p == POWER_NEON || p == POWER_SMOKE || p == POWER_VIDEO || p == POWER_CONCRETE) ? p : -1;
}

static int32_t rumble_id(const int32_t *pattern) {
    uint64_t p = (uint64_t)(uintptr_t)pattern, table = runtime(PATTERN_TABLE);
    if (p < table || (p - table) % PATTERN_STRIDE) return -1;
    uint64_t id = (p - table) / PATTERN_STRIDE;
    return id < PATTERN_COUNT ? (int32_t)id : -1;
}

static int32_t start_rumble_hook(int32_t *ctx, int32_t *pattern, float duration, float scale) {
    void *ret = __builtin_return_address(0);
    int32_t handle = Detour_Stub(&s_start_detour, start_rumble_t, ctx, pattern, duration, scale);
    // Infinite effects are passed as FLT_MAX by the game
    int infinite = duration > 1.0e30f;
    uint64_t caller = (uint64_t)(uintptr_t)ret - s_proc.base_address + GHIDRA_BASE;
    int32_t power = active_power();
    int32_t id = rumble_id(pattern);
    uint64_t now = now_us();
    lock();
    trigger_map_start(&s_map, power, id, infinite, handle, caller, now);
    unlock();
    if (caller == ISS_CALLER_SCRIPT && trigger_map_kind(power, id, infinite) == MAPPED_DRY_FIRE)
        __atomic_fetch_add(&s_dry_fires, 1, __ATOMIC_RELEASE);
    if (caller == ISS_CALLER_SCRIPT && power >= 0) {
        Unmapped u = { now, power, id, infinite, handle, duration, trigger_map_is_mapped(power, id, infinite) };
        note_unmapped(&u);
        // Research: the first neon/smoke shot starts the hero capture
        if (ENABLE_HERO_CAPTURE && !infinite && ((power == POWER_NEON && id == 9) || (power == POWER_SMOKE && id == 8))) {
            uint64_t zero = 0;
            __atomic_compare_exchange_n(&s_arm_request_us, &zero, now ? now : 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
        }
    }
    return handle;
}

static uint32_t usable_mask_hook(void *hero) {
    void *ret = __builtin_return_address(0);
    uint32_t mask = Detour_Stub(&s_usable_detour, usable_mask_t, hero);
    __atomic_store_n(&s_mask_seen_us, now_us(), __ATOMIC_RELAXED);
    if (mask != __atomic_load_n(&s_last_mask, __ATOMIC_RELAXED)) {
        __atomic_store_n(&s_last_mask, mask, __ATOMIC_RELAXED);
        uint32_t head = __atomic_fetch_add(&s_mask_head, 1, __ATOMIC_ACQ_REL);
        uint32_t i = head % MASK_SLOTS;
        if (!__atomic_load_n(&s_mask_ready[i], __ATOMIC_ACQUIRE)) {
            s_masks[i].t = now_us();
            s_masks[i].caller = (uint64_t)(uintptr_t)ret - s_proc.base_address + GHIDRA_BASE;
            s_masks[i].mask = mask;
            __atomic_store_n(&s_mask_ready[i], 1, __ATOMIC_RELEASE);
        }
    }
    return mask;
}

// Two-stage R2 for smoke (two_stage.h): the game's pad reads are hooked the way
// plugin_src/gamepad_helper does it (the *Ext variants are patched to behave like the plain
// reads, so the hooks can call them without recursing).
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);
static Patcher s_pad_read_patcher, s_pad_state_patcher;
static bool s_pad_hooked;
static TwoStage s_two_stage;              // touched only from the game's pad thread
static uint32_t s_r2_max;                 // research: deepest analog R2 seen, for the log
static uint32_t s_options_presses;        // research: Options presses (pause menu toggles)
static uint32_t s_options_prev;

static void two_stage_apply(ScePadData *d) {
    uint32_t options = d->buttons & SCE_PAD_BUTTON_OPTIONS;
    if (options && !s_options_prev) __atomic_fetch_add(&s_options_presses, 1, __ATOMIC_RELEASE);
    s_options_prev = options;
    uint8_t r2 = d->analogButtons.r2;
    int pressed = (d->buttons & SCE_PAD_BUTTON_R2) != 0;
    if (r2 > __atomic_load_n(&s_r2_max, __ATOMIC_RELAXED)) __atomic_store_n(&s_r2_max, r2, __ATOMIC_RELAXED);
    int enabled = active_power() == POWER_SMOKE && !__atomic_load_n(&s_map.empty, __ATOMIC_RELAXED) &&
                  !__atomic_load_n(&s_map.paused, __ATOMIC_RELAXED);
    TwoStageOut o = two_stage_update(&s_two_stage, enabled, r2, pressed, now_us());
    if (o.pressed) d->buttons |= SCE_PAD_BUTTON_R2;
    else d->buttons &= ~(uint32_t)SCE_PAD_BUTTON_R2;
    d->analogButtons.r2 = o.r2;
}

int32_t scePadRead_hook(int32_t handle, ScePadData *data, int32_t num) {
    int32_t ret = scePadReadExt(handle, data, num);
    for (int32_t i = 0; i < ret; i++) two_stage_apply(&data[i]);
    return ret;
}

int32_t scePadReadState_hook(int32_t handle, ScePadData *data) {
    int32_t ret = scePadReadStateExt(handle, data);
    if (ret == 0) two_stage_apply(data);
    return ret;
}

static void stop_rumble_hook(int32_t *ctx, int32_t handle) {
    lock();
    trigger_map_stop(&s_map, handle, now_us());
    unlock();
    Detour_Stub(&s_stop_detour, stop_rumble_t, ctx, handle);
}

static void write_message(const TriggerMap *m, uint32_t heartbeat, bool active) {
    GameTriggerMessage msg;
    game_trigger_message_init(&msg);
    msg.flags = active ? GAME_TRIGGER_FLAG_ACTIVE : 0;
    msg.seq = m->seq;
    msg.heartbeat = heartbeat;
    memcpy(msg.r2, m->r2, sizeof(msg.r2));
    memcpy(msg.l2, m->l2, sizeof(msg.l2));
    uint8_t buf[GAME_TRIGGER_MESSAGE_SIZE];
    game_trigger_encode(&msg, buf);
    // Write a temp file and rename it, so the daemon never reads a half-written message
    int fd = sceKernelOpen(TMP_PATH, 0x0001 | 0x0200 | 0x0400, 0666);
    if (fd < 0) return;
    sceKernelWrite(fd, buf, sizeof(buf));
    sceKernelClose(fd);
    sceKernelRename(TMP_PATH, GAME_TRIGGER_PATH);
}

static void log_line(int fd, const char *s) { if (fd >= 0) sceKernelWrite(fd, s, strlen(s)); }

// Research: copies the eboot's RW segment in 1 MiB writes; returns bytes written.
static uint64_t dump_rw_segment(int index) {
    char path[96];
    snprintf(path, sizeof(path), PAUSE_DUMP_FMT, index);
    int fd = sceKernelOpen(path, 0x0001 | 0x0200 | 0x0400, 0666);
    if (fd < 0) return 0;
    const uint8_t *src = (const uint8_t *)(uintptr_t)runtime(RW_SEGMENT);
    uint64_t done = 0;
    while (done < RW_SIZE) {
        uint64_t chunk = RW_SIZE - done < 0x100000 ? RW_SIZE - done : 0x100000;
        int64_t w = (int64_t)sceKernelWrite(fd, src + done, chunk);
        if (w <= 0) break;
        done += (uint64_t)w;
    }
    sceKernelClose(fd);
    return done;
}

// Research: the hero's current action, to find "can fire" (see game_state.h)
typedef struct { int32_t type, count, busy; uint32_t start, end; } HeroAction;

static bool read_hero_action(HeroAction *a) {
    uint64_t hero;
    memcpy(&hero, (const void *)(uintptr_t)runtime(HERO_GLOBAL), sizeof(hero));
    if (!hero_pointer_plausible(hero)) return false;
    memcpy(&a->type, (const void *)(uintptr_t)(hero + HERO_ACTION_TYPE), 4);
    memcpy(&a->count, (const void *)(uintptr_t)(hero + HERO_ACTION_COUNT), 4);
    memcpy(&a->busy, (const void *)(uintptr_t)(hero + HERO_ACTION_BUSY), 4);
    memcpy(&a->start, (const void *)(uintptr_t)(hero + HERO_ACTION_START), 4);
    memcpy(&a->end, (const void *)(uintptr_t)(hero + HERO_ACTION_END), 4);
    return true;
}

static bool read_energy(float *energy, float *max) {
    uint64_t hero;
    memcpy(&hero, (const void *)(uintptr_t)runtime(HERO_GLOBAL), sizeof(hero));
    if (!hero_pointer_plausible(hero)) return false;
    memcpy(energy, (const void *)(uintptr_t)(hero + HERO_ENERGY), 4);
    memcpy(max, (const void *)(uintptr_t)(hero + HERO_ENERGY_MAX), 4);
    return game_state_playing(hero, *max) && isfinite(*energy);
}

static float game_time_scale(void) {
    float scale;
    memcpy(&scale, (const void *)(uintptr_t)(runtime(GAME_CLOCK) + GAME_TIME_SCALE), 4);
    return scale;
}

static uint32_t game_clock_now(void) {
    if (!s_clock_ok) return 0xffffffffu;
    uint32_t c;
    memcpy(&c, (const void *)(uintptr_t)(runtime(GAME_CLOCK) + GAME_CLOCK_NOW), 4);
    return c;
}

static void *publisher_thread(void *arg) {
    (void)arg;
    int log = sceKernelOpen(LOG_PATH, 0x0001 | 0x0200 | 0x0400, 0666);
    char line[160];
    snprintf(line, sizeof(line), "# %s %s base=0x%lx power=%d\n", s_proc.titleid, s_proc.version,
             s_proc.base_address, active_power());
    log_line(log, line);

    HeroCapture capture;
    hero_capture_init(&capture);
    int capture_fd = -1;
    uint32_t capture_samples = 0;
    uint32_t heartbeat = 0, last_seq = 0xffffffffu;
    uint64_t last_write = 0;
    int32_t last_power = -2;
    HeroAction last_action = { -12345, 0, 0, 0, 0 };
    float last_energy = -1000.0f;
    EnergyGate energy_gate;
    energy_gate_init(&energy_gate);
    uint32_t last_dry = 0;
    uint32_t last_taps = 0, last_charges = 0, last_r2_max = 0;
    bool last_paused = false, last_locked = false;
    PauseDump pause_dump;
    pause_dump_init(&pause_dump);
    bool last_empty = false;
    snprintf(line, sizeof(line), "# clock %s, now=%08x\n", s_clock_ok ? "found" : "NOT found", game_clock_now());
    log_line(log, line);
    while (s_running) {
        uint64_t now = now_us();
        int32_t power = active_power();
        HeroAction act;
        bool have_act = power >= 0 && read_hero_action(&act);
        float energy = 0.0f, energy_max = 0.0f;
        bool have_energy = power >= 0 && read_energy(&energy, &energy_max);
        uint32_t dry = __atomic_load_n(&s_dry_fires, __ATOMIC_ACQUIRE);
        if (dry != last_dry) { if (have_energy) energy_gate_dry_fire(&energy_gate, energy); last_dry = dry; }
        bool empty = have_energy && energy_gate_empty(&energy_gate, energy);
        bool paused = s_clock_ok && power >= 0 && game_time_paused(game_time_scale());
        uint32_t mask = __atomic_load_n(&s_last_mask, __ATOMIC_RELAXED);
        bool mask_fresh = now - __atomic_load_n(&s_mask_seen_us, __ATOMIC_RELAXED) < MASK_STALE_US;
        bool locked = mask_fresh && powers_locked(mask);
        lock();
        trigger_map_set_paused(&s_map, paused, now);
        trigger_map_set_locked(&s_map, locked, now);
        trigger_map_set_power(&s_map, power, now);
        trigger_map_set_busy(&s_map, have_act && act.busy != 0, now);
        trigger_map_set_empty(&s_map, empty, now);
        trigger_map_tick(&s_map, now);
        TriggerMap snap = s_map;
        unlock();
        if (power != last_power) {
            snprintf(line, sizeof(line), "%10.3f power=%d\n", (double)now / 1e6, power);
            log_line(log, line);
            last_power = power;
        }
        while (__atomic_load_n(&s_unmapped_ready[s_unmapped_tail % UNMAPPED_SLOTS], __ATOMIC_ACQUIRE)) {
            uint32_t i = s_unmapped_tail % UNMAPPED_SLOTS;
            Unmapped u = s_unmapped[i];
            __atomic_store_n(&s_unmapped_ready[i], 0, __ATOMIC_RELEASE);
            s_unmapped_tail++;
            snprintf(line, sizeof(line), "%10.3f %s power=%d id=%d infinite=%d handle=%d dur=%.3f\n",
                     (double)u.t / 1e6, u.mapped ? "rumble" : "unmapped", u.power, u.id, u.infinite, u.handle,
                     u.infinite ? -1.0 : (double)u.duration);
            log_line(log, line);
        }
        while (__atomic_load_n(&s_mask_ready[s_mask_tail % MASK_SLOTS], __ATOMIC_ACQUIRE)) {
            uint32_t i = s_mask_tail % MASK_SLOTS;
            MaskChange c = s_masks[i];
            __atomic_store_n(&s_mask_ready[i], 0, __ATOMIC_RELEASE);
            s_mask_tail++;
            snprintf(line, sizeof(line), "%10.3f mask=%08x caller=%08llx\n", (double)c.t / 1e6, c.mask,
                     (unsigned long long)c.caller);
            log_line(log, line);
        }
        // Research: energy in steps of 5 and every empty/non-empty change, to check EMPTY_ENERGY
        if (have_energy && (empty != last_empty || fabsf(energy - last_energy) >= 5.0f)) {
            snprintf(line, sizeof(line), "%10.3f energy=%.2f/%.2f empty=%d\n", (double)now / 1e6,
                     (double)energy, (double)energy_max, empty);
            log_line(log, line);
            last_energy = energy;
            last_empty = empty;
        }
        if (paused != last_paused || locked != last_locked) {
            snprintf(line, sizeof(line), "%10.3f gate paused=%d locked=%d mask=%08x clock=%08x\n",
                     (double)now / 1e6, paused, locked, mask, game_clock_now());
            log_line(log, line);
            last_paused = paused; last_locked = locked;
        }
        // Research: two-stage R2 decisions and the deepest analog R2 seen
        uint32_t taps = __atomic_load_n(&s_two_stage.taps, __ATOMIC_RELAXED);
        uint32_t charges = __atomic_load_n(&s_two_stage.charges, __ATOMIC_RELAXED);
        uint32_t r2_max = __atomic_load_n(&s_r2_max, __ATOMIC_RELAXED);
        if (taps != last_taps || charges != last_charges || r2_max >= last_r2_max + 32) {
            snprintf(line, sizeof(line), "%10.3f two_stage taps=%u charges=%u r2_max=%u\n",
                     (double)now / 1e6, taps, charges, r2_max);
            log_line(log, line);
            last_taps = taps; last_charges = charges; last_r2_max = r2_max;
        }
        if (have_act && memcmp(&act, &last_action, sizeof(act)) != 0) {
            snprintf(line, sizeof(line), "%10.3f act type=%d busy=%08x count=%d start=%08x end=%08x clock=%08x mask=%08x\n",
                     (double)now / 1e6, act.type, (uint32_t)act.busy, act.count, act.start, act.end, game_clock_now(),
                     __atomic_load_n(&s_last_mask, __ATOMIC_RELAXED));
            log_line(log, line);
            last_action = act;
        }
        int dump = ENABLE_PAUSE_DUMPS ? pause_dump_due(&pause_dump, __atomic_load_n(&s_options_presses, __ATOMIC_ACQUIRE), now) : 0;
        if (dump) {
            uint64_t bytes = dump_rw_segment(dump);
            snprintf(line, sizeof(line), "%10.3f pause_dump %d %s bytes=%llu\n", (double)now / 1e6, dump,
                     pause_dump_paused(dump) ? "paused" : "playing", (unsigned long long)bytes);
            log_line(log, line);
            Notify(TEX_ICON_SYSTEM, "inFAMOUS dump %d (%s) saved", dump, pause_dump_paused(dump) ? "paused" : "playing");
        }
        uint64_t arm = __atomic_load_n(&s_arm_request_us, __ATOMIC_ACQUIRE);
        if (arm && !capture.armed && !capture.done) {
            hero_capture_arm(&capture, now);
            capture_fd = sceKernelOpen(CAPTURE_PATH, 0x0001 | 0x0200 | 0x0400, 0666);
            snprintf(line, sizeof(line), "%10.3f capture start (fd=%d)\n", (double)now / 1e6, capture_fd);
            log_line(log, line);
        }
        if (hero_capture_due(&capture, now) && capture_fd >= 0) {
            uint64_t hero;
            memcpy(&hero, (const void *)(uintptr_t)runtime(HERO_GLOBAL), sizeof(hero));
            if (hero_pointer_plausible(hero)) {
                // Record: u64 time_us, u64 hero, then HERO_CAPTURE_BYTES of the hero object
                uint64_t header[2] = { now, hero };
                sceKernelWrite(capture_fd, header, sizeof(header));
                sceKernelWrite(capture_fd, (const void *)(uintptr_t)hero, HERO_CAPTURE_BYTES);
                capture_samples++;
            }
        }
        if (capture.done && capture_fd >= 0) {
            sceKernelClose(capture_fd);
            capture_fd = -1;
            snprintf(line, sizeof(line), "%10.3f capture done: %u samples\n", (double)now / 1e6, capture_samples);
            log_line(log, line);
            Notify(TEX_ICON_SYSTEM, "inFAMOUS capture done (%u samples)", capture_samples);
        }
        if (snap.seq != last_seq || now - last_write >= GAME_TRIGGER_HEARTBEAT_US) {
            if (snap.seq != last_seq) {
                snprintf(line, sizeof(line), "%10.3f effect seq=%u active=%d r2=%02x %02x %02x %02x\n",
                         (double)now / 1e6, snap.seq, snap.active, snap.r2[0], snap.r2[1], snap.r2[2], snap.r2[3]);
                log_line(log, line);
            }
            write_message(&snap, ++heartbeat, snap.active);
            last_seq = snap.seq;
            last_write = now;
        }
        sceKernelUsleep(10000);
    }
    // Leaving: tell OneFamily to drop the override right away
    lock();
    TriggerMap snap = s_map;
    unlock();
    snap.seq++;
    write_message(&snap, ++heartbeat, false);
    if (log >= 0) sceKernelClose(log);
    return NULL;
}

int32_t attr_public plugin_load(int32_t argc, const char *argv[]) {
    (void)argc; (void)argv;
    if (sys_sdk_proc_info(&s_proc) != 0) return 0;
    if (strcmp(s_proc.titleid, TARGET_TITLE) != 0 || strcmp(s_proc.version, TARGET_VERSION) != 0) {
        final_printf("%s: not %s %s, idle\n", g_pluginName, TARGET_TITLE, TARGET_VERSION);
        return 0;
    }
    uint64_t start = runtime(START_RUMBLE), stop = runtime(STOP_RUMBLE);
    if (memcmp((const void *)(uintptr_t)start, k_start_prologue, sizeof(k_start_prologue)) != 0 ||
        memcmp((const void *)(uintptr_t)stop, k_stop_prologue, sizeof(k_stop_prologue)) != 0 ||
        !hero_reader_matches() ||
        memcmp((const void *)(uintptr_t)runtime(USABLE_MASK), k_usable_prologue, sizeof(k_usable_prologue)) != 0) {
        Notify(TEX_ICON_SYSTEM, "inFAMOUS triggers: unexpected game code, disabled");
        return 0;
    }

    s_clock_ok = game_clock_from_setter(GAME_CLOCK_SETTER,
                                        (const uint8_t *)(uintptr_t)runtime(GAME_CLOCK_SETTER)) != 0;
    trigger_map_init(&s_map);
    s_start_us = sceKernelGetProcessTime();
    s_running = true;
    OrbisPthread thread;
    scePthreadCreate(&thread, NULL, publisher_thread, NULL, "infamous_triggers");

    Detour_Construct(&s_start_detour, DetourMode_x64);
    Detour_DetourFunction(&s_start_detour, start, (void *)start_rumble_hook);
    s_start_hooked = true;
    Detour_Construct(&s_stop_detour, DetourMode_x64);
    Detour_DetourFunction(&s_stop_detour, stop, (void *)stop_rumble_hook);
    s_stop_hooked = true;
    Detour_Construct(&s_usable_detour, DetourMode_x64);
    Detour_DetourFunction(&s_usable_detour, runtime(USABLE_MASK), (void *)usable_mask_hook);
    s_usable_hooked = true;
    // Pad hooks for the two-stage R2 (as plugin_src/gamepad_helper)
    char module[256];
    snprintf(module, sizeof(module), "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libScePad.sprx");
    int pad_module = 0;
    sys_dynlib_load_prx(module, &pad_module);
    two_stage_init(&s_two_stage);
    Patcher_Construct(&s_pad_read_patcher);
    Patcher_Construct(&s_pad_state_patcher);
    uint8_t xor_ecx_ecx[5] = {0x31, 0xC9, 0x90, 0x90, 0x90};
    Patcher_Install_Patch(&s_pad_read_patcher, (uint64_t)scePadReadExt, xor_ecx_ecx, sizeof(xor_ecx_ecx));
    uint8_t xor_edx_edx[5] = {0x31, 0xD2, 0x90, 0x90, 0x90};
    Patcher_Install_Patch(&s_pad_state_patcher, (uint64_t)scePadReadStateExt, xor_edx_edx, sizeof(xor_edx_edx));
    HOOK32(scePadRead);
    HOOK32(scePadReadState);
    s_pad_hooked = true;
    Notify(TEX_ICON_SYSTEM, "inFAMOUS adaptive triggers v3.1 active");
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char *argv[]) {
    (void)argc; (void)argv;
    s_running = false;
    if (s_start_hooked) { Detour_Destroy(&s_start_detour); s_start_hooked = false; }
    if (s_stop_hooked) { Detour_Destroy(&s_stop_detour); s_stop_hooked = false; }
    if (s_usable_hooked) { Detour_Destroy(&s_usable_detour); s_usable_hooked = false; }
    if (s_pad_hooked) {
        UNHOOK(scePadRead);
        UNHOOK(scePadReadState);
        Patcher_Destroy(&s_pad_read_patcher);
        Patcher_Destroy(&s_pad_state_patcher);
        s_pad_hooked = false;
    }
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) { return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
