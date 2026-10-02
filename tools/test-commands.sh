#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CJSON_SOURCE:?Set CJSON_SOURCE to official cJSON v1.7.19 source directory}"
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -Itests/stubs -Ioverlay/esp32/main -I"$CJSON_SOURCE" \
  tests/test_commands.c overlay/esp32/main/focus.c overlay/esp32/main/focus_core.c \
  "$CJSON_SOURCE/cJSON.c" -lm -o "$bin"
"$bin"
