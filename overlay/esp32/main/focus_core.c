// SPDX-License-Identifier: MIT
#include "focus_core.h"
#include <limits.h>
void focus_core_init(focus_core_t *s, uint64_t now) {
    *s = (focus_core_t){.value = {.target_ms = 25 * 60 * 1000,
                                .remaining_ms = 25 * 60 * 1000}, .updated_ms = now};
}
void focus_core_tick(focus_core_t *s, uint64_t now) {
    // Monotonic microsecond clock on device; ignore backwards host clock input.
    if (now < s->updated_ms) return;
    if (s->value.running) {
        uint64_t delta = now - s->updated_ms;
        s->value.elapsed_ms = UINT64_MAX - s->value.elapsed_ms < delta ? UINT64_MAX : s->value.elapsed_ms + delta;
    }
    s->updated_ms = now;
    if (s->value.target_ms && s->value.elapsed_ms >= s->value.target_ms) {
        s->value.elapsed_ms = s->value.target_ms;
        s->value.running = false;
        s->value.complete = true;
    }
    s->value.remaining_ms = s->value.target_ms ? s->value.target_ms - s->value.elapsed_ms : 0;
}
void focus_core_toggle(focus_core_t *s, uint64_t now) {
    focus_core_tick(s, now);
    if (!s->value.complete) s->value.running = !s->value.running;
}
void focus_core_reset(focus_core_t *s, uint64_t now) {
    uint64_t target = s->value.target_ms;
    *s = (focus_core_t){.value = {.target_ms = target, .remaining_ms = target}, .updated_ms = now};
}
bool focus_core_target(focus_core_t *s, uint32_t seconds, uint64_t now) {
    if (seconds > 86400) return false;
    s->value.target_ms = (uint64_t)seconds * 1000;
    focus_core_reset(s, now);
    return true;
}
void focus_core_lap(focus_core_t *s, uint64_t now) {
    focus_core_tick(s, now);
    if (s->value.running && s->value.laps < UINT32_MAX) ++s->value.laps;
}
