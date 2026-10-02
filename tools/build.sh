#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v idf.py >/dev/null; then
  echo 'ESP-IDF v6.0.1 required. Source its export.sh first; see README.md.' >&2
  exit 2
fi
idf.py --version | grep -Eq 'v6\.0\.1($|[^0-9])' || { echo 'Expected ESP-IDF v6.0.1' >&2; exit 2; }
[ -d "$ROOT/.build-sdk" ] || python3 "$ROOT/tools/prepare.py"
cd "$ROOT/.build-sdk/esp32"
# Do not flash automatically. All generated config and tokens stay in ignored dirs.
idf.py -B "$ROOT/build" -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.stopwatch" "${@:-build}"
