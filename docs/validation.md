# Validation record — 2026-10-02

- PASS: native C11 timer tests compiled with `-Wall -Wextra -Werror -fsanitize=undefined`; start, pause, resume, lap, reset, countdown completion, invalid duration, 64-bit elapsed time, backward time guard, saturated lap count.
- PASS: Python preparation tests check exact patch anchors, rejection of missing/duplicate anchors, and refusal to overwrite an existing destination.
- PASS: command adapter compiled against official cJSON v1.7.19 (`c859b25da02955fef659d658b8f324b5cde87be3`) with host clock/locking stubs. Validates SDK `ok`/`payload` and `error.message` envelopes, completion, invalid parameters and namespace dispatch. Does not validate actual ESP32 threading.
- PASS: command dispatcher/envelope and malformed-parameter tests against official cJSON 1.7.19.
- PASS: fresh offline export of the pinned SDK with all integration anchors and overlays applied.
- Firmware build attempt: `./tools/build.sh` stopped with `ESP-IDF v6.0.1 required`. An attempt to install official ESP-IDF v6.0.1 was made: recursive clone hit HTTP CONNECT 403 for an upstream submodule (`esp-ble-mesh-lib`), and the tool installation session ended with `automatic approval review was cancelled` after the GDB download. The required compiler was not installed. Firmware compile/link and partition-size checks therefore NOT RUN.
- Hardware flashing, boot, display orientation/colors, touch, power, battery behavior, BLE pairing, Wi-Fi and Muse command round-trip: NOT TESTED.
- CI is configured for host tests, command-envelope tests, fresh SDK preparation and a credential-free ESP-IDF 6.0.1 firmware build. Its result must be checked for the specific commit; configuration alone is not a passing build.

Before considering this port usable: install ESP-IDF 6.0.1; build from scratch; inspect generated config, app partition headroom and boot logs; check rail initialization and display; exercise A/B repeatedly and during pairing; verify reset semantics; run full encrypted-command round trips and offline timer behavior; validate battery/power/thermal behavior on hardware. Do not leave experimental firmware running unattended.
