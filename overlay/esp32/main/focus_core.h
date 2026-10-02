// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t elapsed_ms, remaining_ms, target_ms;
    bool running, complete;
    uint32_t laps;
} focus_snapshot_t;
typedef struct { focus_snapshot_t value; uint64_t updated_ms; } focus_core_t;
void focus_core_init(focus_core_t *s, uint64_t now);
void focus_core_tick(focus_core_t *s, uint64_t now);
void focus_core_toggle(focus_core_t *s, uint64_t now);
void focus_core_reset(focus_core_t *s, uint64_t now);
bool focus_core_target(focus_core_t *s, uint32_t seconds, uint64_t now);
void focus_core_lap(focus_core_t *s, uint64_t now);
