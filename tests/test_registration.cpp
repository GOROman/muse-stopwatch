#include "cJSON.h"
#include <cassert>
#include <cstdio>
static void add_command(cJSON *commands, const char *name, const char *description,
                        cJSON *required, cJSON *optional) {
    cJSON *command = cJSON_CreateObject();
    cJSON_AddStringToObject(command, "description", description);
    cJSON_AddItemToObject(command, "required", required ? required : cJSON_CreateObject());
    cJSON_AddItemToObject(command, "optional", optional ? optional : cJSON_CreateObject());
    cJSON_AddItemToObject(commands, name, command);
}
int main() {
    cJSON *commands = cJSON_CreateObject();
#include "focus_commands.inc"
    assert(cJSON_GetArraySize(commands) == 5);
    const char *names[] = {"focus.status", "focus.toggle", "focus.reset", "focus.lap", "focus.configure"};
    for (const char *name : names) {
        cJSON *command = cJSON_GetObjectItemCaseSensitive(commands, name);
        assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(command, "description")));
        assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(command, "required")));
        assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(command, "optional")));
    }
    cJSON *configure = cJSON_GetObjectItemCaseSensitive(commands, "focus.configure");
    cJSON *required = cJSON_GetObjectItemCaseSensitive(configure, "required");
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(required, "seconds")));
    cJSON_Delete(commands);
    std::puts("PASS: five Muse command registration schemas");
}
