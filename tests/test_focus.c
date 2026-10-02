#include "focus_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    focus_core_t s;
    focus_core_init(&s, 100);
    assert(s.value.target_ms == 1500000 && !s.value.running);
    focus_core_toggle(&s, 100); focus_core_tick(&s, 1100);
    assert(s.value.elapsed_ms == 1000);
    focus_core_lap(&s, 1200); assert(s.value.laps == 1);
    focus_core_toggle(&s, 2100); focus_core_tick(&s, 10000);
    assert(s.value.elapsed_ms == 2000 && !s.value.running);
    focus_core_toggle(&s, 10000); focus_core_tick(&s, 11000);
    assert(s.value.elapsed_ms == 3000);
    assert(!focus_core_target(&s, 86401, 11000));
    assert(s.value.elapsed_ms == 3000);
    assert(focus_core_target(&s, 1, 11000));
    focus_core_toggle(&s, 11000); focus_core_tick(&s, 15000);
    assert(s.value.complete && !s.value.running && s.value.elapsed_ms == 1000 && !s.value.remaining_ms);
    focus_core_toggle(&s, 16000); assert(!s.value.running);
    focus_core_reset(&s, 17000); assert(!s.value.complete && !s.value.laps);
    focus_core_target(&s, 0, 17000); focus_core_toggle(&s, 17000);
    focus_core_tick(&s, UINT64_C(4294967296) + 17000);
    assert(s.value.elapsed_ms == UINT64_C(4294967296) && s.value.running);
    focus_core_tick(&s, 100); assert(s.value.elapsed_ms == UINT64_C(4294967296));
    s.value.laps = UINT32_MAX; focus_core_lap(&s, s.updated_ms); assert(s.value.laps == UINT32_MAX);
    puts("PASS: focus core start/pause/resume/reset/lap/countdown/validation/64-bit time");
}
