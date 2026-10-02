#include "focus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
static cJSON *call(const char *cmd, const char *json) {
    cJSON *params = cJSON_Parse(json), *result = focus_command(cmd, params);
    cJSON_Delete(params); return result;
}
int main(void) {
    cJSON *r = call("focus.configure", "{\"seconds\":1}");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
    assert(cJSON_IsObject(cJSON_GetObjectItem(r, "payload"))); cJSON_Delete(r);
    r = call("focus.toggle", "{}"); cJSON_Delete(r);
    clock_us = 1500000;
    r = call("focus.status", "{}");
    // Noise transport retains only ok/payload/error; assert the actual envelope.
    cJSON *payload = cJSON_GetObjectItem(r, "payload");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(payload, "complete")));
    assert(cJSON_GetObjectItem(payload, "elapsed_ms")->valuedouble == 1000);
    cJSON_Delete(r);
    const char *bad[] = {"{}", "{\"seconds\":-1}", "{\"seconds\":0.5}", "{\"seconds\":86401}", "{\"seconds\":\"3\"}", "null"};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        r = call("focus.configure", bad[i]);
        assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
        assert(cJSON_IsString(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "error"), "message")));
        cJSON_Delete(r);
    }
    r = call("focus.unknown", "{}"); assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    assert(call("device.discover", "{}") == NULL);
    puts("PASS: commands + Muse Noise result envelope + invalid parameters");
}
