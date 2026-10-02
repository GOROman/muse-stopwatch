// SPDX-License-Identifier: MIT
#include "focus.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <math.h>
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static focus_core_t state;
static bool initialized;
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static void ensure_init(void) {
    if (!initialized) { focus_core_init(&state, now_ms()); initialized = true; }
}
focus_snapshot_t focus_snapshot(void) {
    portENTER_CRITICAL(&lock);
    ensure_init(); focus_core_tick(&state, now_ms());
    focus_snapshot_t out = state.value;
    portEXIT_CRITICAL(&lock); return out;
}
#define ACTION(name, core) void name(void) { portENTER_CRITICAL(&lock); ensure_init(); core(&state, now_ms()); portEXIT_CRITICAL(&lock); }
ACTION(focus_toggle, focus_core_toggle)
ACTION(focus_reset, focus_core_reset)
ACTION(focus_lap, focus_core_lap)
bool focus_set_target(uint32_t seconds) {
    portENTER_CRITICAL(&lock); ensure_init();
    bool ok = focus_core_target(&state, seconds, now_ms());
    portEXIT_CRITICAL(&lock); return ok;
}
static cJSON *error(const char *message) {
    cJSON *o = cJSON_CreateObject();
    cJSON *e = cJSON_CreateObject();
    if (!o || !e) { cJSON_Delete(o); cJSON_Delete(e); return NULL; }
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(e, "code", "invalid_params");
    cJSON_AddStringToObject(e, "message", message);
    cJSON_AddItemToObject(o, "error", e); return o;
}
cJSON *focus_command(const char *command, const cJSON *params) {
    if (strncmp(command, "focus.", 6)) return NULL;
    if (!strcmp(command, "focus.toggle")) focus_toggle();
    else if (!strcmp(command, "focus.reset")) focus_reset();
    else if (!strcmp(command, "focus.lap")) focus_lap();
    else if (!strcmp(command, "focus.configure")) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(params, "seconds");
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
            v->valuedouble > 86400 || floor(v->valuedouble) != v->valuedouble)
            return error("seconds must be an integer in 0..86400; 0 is stopwatch mode");
        focus_set_target((uint32_t)v->valuedouble);
    } else if (strcmp(command, "focus.status")) return error("unknown focus command");
    focus_snapshot_t s = focus_snapshot();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "elapsed_ms", s.elapsed_ms);
    cJSON_AddNumberToObject(o, "remaining_ms", s.remaining_ms);
    cJSON_AddNumberToObject(o, "target_ms", s.target_ms);
    cJSON_AddNumberToObject(o, "laps", s.laps);
    cJSON_AddBoolToObject(o, "running", s.running);
    cJSON_AddBoolToObject(o, "complete", s.complete);
    cJSON *result = cJSON_CreateObject();
    if (!result) { cJSON_Delete(o); return NULL; }
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddItemToObject(result, "payload", o);
    return result;
}
