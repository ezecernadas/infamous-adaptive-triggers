// One-shot research capture: once armed (first shot), take a sample every
// HERO_CAPTURE_INTERVAL_US for HERO_CAPTURE_WINDOW_US, then stop for good.
#pragma once

#include <stdint.h>

#define HERO_CAPTURE_INTERVAL_US 50000ULL
#define HERO_CAPTURE_WINDOW_US   8000000ULL
#define HERO_CAPTURE_BYTES       0x1f000u   /* v01.00 code reads the hero up to +0x1ef68 (FUN_005bb2e0) */

typedef struct {
    int armed, done;
    uint64_t start_us, next_us;
} HeroCapture;

static inline void hero_capture_init(HeroCapture* c) { c->armed = c->done = 0; c->start_us = c->next_us = 0; }

static inline void hero_capture_arm(HeroCapture* c, uint64_t now_us) {
    if (c->armed || c->done) return;
    c->armed = 1; c->start_us = c->next_us = now_us;
}

/* 1 when a sample should be taken now. */
static inline int hero_capture_due(HeroCapture* c, uint64_t now_us) {
    if (!c->armed || c->done) return 0;
    if (now_us >= c->start_us + HERO_CAPTURE_WINDOW_US) { c->armed = 0; c->done = 1; return 0; }
    if (now_us < c->next_us) return 0;
    c->next_us += HERO_CAPTURE_INTERVAL_US;
    if (c->next_us <= now_us) c->next_us = now_us + HERO_CAPTURE_INTERVAL_US;  /* never burst to catch up */
    return 1;
}
