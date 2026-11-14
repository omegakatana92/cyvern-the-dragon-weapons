#!/usr/bin/env bash
# Development build + headless smoke tests.
# usage: scripts/build-local.sh /path/to/cyvern.zip /path/to/skns.zip [build-dir]
set -euo pipefail
if [[ $# -lt 2 ]]; then echo "usage: $0 /path/to/cyvern.zip /path/to/skns.zip [build-dir]" >&2; exit 2; fi
ROM=$1
BIOS=$2
BUILD_DIR=${3:-build/cmake}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
python3 tools/rom_set.py "$ROM" "$BIOS"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCYVERN_ROM="$ROM" -DSKNS_BIOS="$BIOS"
cmake --build "$BUILD_DIR"
(cd "$BUILD_DIR" && ctest --output-on-failure -R "headless_boot|save_state_round_trip")
echo "Build and headless smoke tests passed."
