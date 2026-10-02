// SPDX-License-Identifier: MIT
// Included inside the SDK's authenticated link.register command builder.
add_command(commands, "focus.status",
            "Read the C152 focus timer or stopwatch: elapsed_ms, remaining_ms, "
            "target_ms, laps, running, complete. All state is lost on reboot.",
            nullptr, nullptr);
add_command(commands, "focus.toggle",
            "Start, pause or resume the C152 timer. Not idempotent: do not retry "
            "blindly. After completion, reset before starting again.",
            nullptr, nullptr);
add_command(commands, "focus.reset",
            "Reset elapsed time and lap count, keeping the configured duration; "
            "leave the timer stopped.", nullptr, nullptr);
add_command(commands, "focus.lap",
            "Increment the lap count only while running; no timestamps are stored. "
            "Not idempotent: do not retry blindly.", nullptr, nullptr);
cJSON *focus_required = cJSON_CreateObject();
cJSON *focus_seconds = cJSON_CreateObject();
cJSON_AddStringToObject(focus_seconds, "type", "integer");
cJSON_AddStringToObject(focus_seconds, "description",
                       "Whole seconds 0..86400. Zero selects stopwatch mode. "
                       "A positive number selects a countdown.");
cJSON_AddItemToObject(focus_required, "seconds", focus_seconds);
add_command(commands, "focus.configure",
            "Set the duration and reset the C152 timer, leaving it stopped. "
            "Use focus.toggle separately to start it.", focus_required, nullptr);
