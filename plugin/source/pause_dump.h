// Research: find the pause flag of v01.00 by diffing memory. Every Options press toggles the
// pause menu; PAUSE_DUMP_DELAY_US later the RW segment is dumped, so dumps alternate
// paused (odd) / playing (even) for tools/power_diff.py. The game clock was ruled out: it keeps
// running in the pause (triggers_v30b.log). Pure C: the plugin feeds it the press count.
#pragma once

#include <stdint.h>

#define PAUSE_DUMP_DELAY_US 1500000ULL
#define PAUSE_DUMP_MAX      6

typedef struct { uint32_t presses; uint64_t due_us; uint32_t taken; } PauseDump;

static inline void pause_dump_init(PauseDump* d) { d->presses = 0; d->due_us = 0; d->taken = 0; }

/* Returns the dump number (1..PAUSE_DUMP_MAX) once it is due, else 0. */
static inline int pause_dump_due(PauseDump* d, uint32_t presses, uint64_t now_us) {
    if (presses != d->presses) {
        d->presses = presses;
        d->due_us = presses <= PAUSE_DUMP_MAX ? now_us + PAUSE_DUMP_DELAY_US : 0;
        return 0;
    }
    if (!d->due_us || now_us < d->due_us) return 0;
    d->due_us = 0;
    d->taken++;
    return (int)d->presses;
}

static inline int pause_dump_paused(int n) { return n % 2 == 1; }
