#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -Ioverlay/esp32/main tests/test_focus.c overlay/esp32/main/focus_core.c -o "$bin"
"$bin"
python3 -m unittest discover -s tests -p 'test_*.py'
