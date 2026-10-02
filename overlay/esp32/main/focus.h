// SPDX-License-Identifier: MIT
#pragma once
#include "focus_core.h"
#include "cJSON.h"
focus_snapshot_t focus_snapshot(void);
void focus_toggle(void);
void focus_reset(void);
void focus_lap(void);
bool focus_set_target(uint32_t seconds);
// Returns NULL for commands outside the focus namespace.
cJSON *focus_command(const char *command, const cJSON *params);
