/*
 * Game -> OneFamily trigger override protocol.
 *
 * A game-side GoldHEN plugin writes GAME_TRIGGER_PATH with the DualSense trigger effects the
 * game wants right now; the daemon applies them instead of the user's dualsense_trigger_mode
 * while the message is active. The plugin rewrites the file at least every
 * GAME_TRIGGER_HEARTBEAT_US (bumping `heartbeat`); if it stops (game closed, plugin crash) the
 * daemon drops the override after GAME_TRIGGER_TIMEOUT_US.
 *
 * Wire format (little endian, GAME_TRIGGER_MESSAGE_SIZE bytes):
 *   0  u32 magic 'OFGT'   4  u8 version   5  u8 flags   6  u16 reserved
 *   8  u32 seq (changes whenever the effects change)
 *   12 u8 r2[11]          23 u8 l2[11]    34 u32 heartbeat
 * This header is shared verbatim with the plugin; keep both copies identical.
 */
#ifndef GAME_TRIGGER_PROTOCOL_H
#define GAME_TRIGGER_PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define GAME_TRIGGER_PATH          "/data/onefamily/game_triggers.bin"
#define GAME_TRIGGER_MAGIC         0x5447464fu  /* "OFGT" */
#define GAME_TRIGGER_VERSION       1
#define GAME_TRIGGER_FLAG_ACTIVE   0x01
#define GAME_TRIGGER_EFFECT_SIZE   11
#define GAME_TRIGGER_MESSAGE_SIZE  38
#define GAME_TRIGGER_HEARTBEAT_US  500000ULL
#define GAME_TRIGGER_TIMEOUT_US    2000000ULL

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t seq;
    uint8_t r2[GAME_TRIGGER_EFFECT_SIZE];
    uint8_t l2[GAME_TRIGGER_EFFECT_SIZE];
    uint32_t heartbeat;
} GameTriggerMessage;

static inline void game_trigger_message_init(GameTriggerMessage* m) {
    memset(m, 0, sizeof(*m));
    m->magic = GAME_TRIGGER_MAGIC;
    m->version = GAME_TRIGGER_VERSION;
    m->r2[0] = 0x05;
    m->l2[0] = 0x05;
}

/* DualSense trigger effect modes this protocol accepts (simple and official ones). */
static inline int game_trigger_mode_known(uint8_t mode) {
    switch (mode) {
    case 0x00: case 0x01: case 0x02: case 0x05: case 0x06:
    case 0x21: case 0x22: case 0x23: case 0x25: case 0x26: case 0x27:
        return 1;
    default:
        return 0;
    }
}

static inline void game_trigger_encode(const GameTriggerMessage* m, uint8_t out[GAME_TRIGGER_MESSAGE_SIZE]) {
    memset(out, 0, GAME_TRIGGER_MESSAGE_SIZE);
    memcpy(out + 0, &m->magic, 4);
    out[4] = m->version;
    out[5] = m->flags;
    memcpy(out + 8, &m->seq, 4);
    memcpy(out + 12, m->r2, GAME_TRIGGER_EFFECT_SIZE);
    memcpy(out + 23, m->l2, GAME_TRIGGER_EFFECT_SIZE);
    memcpy(out + 34, &m->heartbeat, 4);
}

/* Returns 0 for anything that is not a complete, current, well-formed message. */
static inline int game_trigger_decode(const uint8_t* buf, int len, GameTriggerMessage* out) {
    if (!buf || len < GAME_TRIGGER_MESSAGE_SIZE) return 0;
    GameTriggerMessage m;
    memset(&m, 0, sizeof(m));
    memcpy(&m.magic, buf + 0, 4);
    m.version = buf[4];
    m.flags = buf[5];
    memcpy(&m.seq, buf + 8, 4);
    memcpy(m.r2, buf + 12, GAME_TRIGGER_EFFECT_SIZE);
    memcpy(m.l2, buf + 23, GAME_TRIGGER_EFFECT_SIZE);
    memcpy(&m.heartbeat, buf + 34, 4);
    if (m.magic != GAME_TRIGGER_MAGIC || m.version != GAME_TRIGGER_VERSION) return 0;
    if (!game_trigger_mode_known(m.r2[0]) || !game_trigger_mode_known(m.l2[0])) return 0;
    *out = m;
    return 1;
}

/* Daemon-side state. */
typedef struct {
    int have;
    int active;
    int expired;                 /* dropped for silence; only a newer message brings it back */
    uint32_t seq;
    uint32_t heartbeat;
    uint64_t last_heard_us;
    uint8_t r2[GAME_TRIGGER_EFFECT_SIZE];
    uint8_t l2[GAME_TRIGGER_EFFECT_SIZE];
} GameTriggerOverride;

static inline void game_trigger_override_init(GameTriggerOverride* o) { memset(o, 0, sizeof(*o)); }

static inline int game_trigger_override_active(const GameTriggerOverride* o) { return o->active; }

/* Returns 1 when the requested effects changed (the controller needs a new output report). */
/* The game's last message stays on disk after it closes and is read on every poll, so a message
 * counts only when it is newer (seq or heartbeat) than the last one seen; an expired override
 * stays dropped until then. */
static inline int game_trigger_override_update(GameTriggerOverride* o, const GameTriggerMessage* m, uint64_t now_us) {
    int fresh = !o->have || m->seq != o->seq || m->heartbeat != o->heartbeat;
    if (!fresh) return 0;
    int changed = !o->have || m->seq != o->seq || o->expired;
    o->last_heard_us = now_us;
    o->have = 1;
    o->expired = 0;
    o->seq = m->seq;
    o->heartbeat = m->heartbeat;
    if (changed) {
        o->active = (m->flags & GAME_TRIGGER_FLAG_ACTIVE) != 0;
        memcpy(o->r2, m->r2, GAME_TRIGGER_EFFECT_SIZE);
        memcpy(o->l2, m->l2, GAME_TRIGGER_EFFECT_SIZE);
    }
    return changed;
}

/* Returns 1 when the override was dropped because the game went quiet. */
static inline int game_trigger_override_expire(GameTriggerOverride* o, uint64_t now_us) {
    if (!o->active || now_us - o->last_heard_us < GAME_TRIGGER_TIMEOUT_US) return 0;
    o->active = 0;
    o->expired = 1;
    return 1;
}

#endif
