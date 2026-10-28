#!/usr/bin/env bash
# Self-check for the raw-Vulkan replay: dumps an offscreen showcase frame,
# replays it and expects a pixel match (exit 0), then corrupts one reference
# byte and expects the replay to fail (exit 1), and removes the reference and
# expects exit 2. Usage: benchmarks/prototype/check_replay.sh [vulkan header dir]
# Needs out/linux/rg_gui_demo_full (./build.sh demo_full), the Vulkan loader,
# and Vulkan headers (system, or the SDL source tree's src/video/khronos).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
headers=${1:-}
include=()
if [[ -n $headers ]]; then include=(-I "$headers"); fi
work=${RG_REPLAY_CHECK_DIR:-out/replay-check}
rm -rf "${work:?}" && mkdir -p "$work/dump"
gcc -std=gnu11 -O2 -Wall -Wextra -Wno-missing-field-initializers "${include[@]}" -I benchmarks/profile \
    benchmarks/prototype/vk_frame.c -l:libvulkan.so.1 -o "$work/vk_frame"
out/linux/rg_gui_demo_full --offscreen --scenario idle --frames 200 --present immediate --dump-frame "$work/dump" >/dev/null 2>&1
status=0; "$work/vk_frame" "$work/dump" 300 - 2>"$work/match.log" || status=$?
grep "reference comparison" "$work/match.log"
[[ $status -eq 0 ]] || { echo "expected a match (exit 0), got exit $status"; exit 1; }
cp "$work/dump/reference.bin" "$work/reference.orig"
python3 - "$work/dump/reference.bin" <<'PY'
import sys; p=sys.argv[1]; b=bytearray(open(p,'rb').read()); b[4*(640+400*1280)] ^= 0x40; open(p,'wb').write(b)
PY
status=0; "$work/vk_frame" "$work/dump" 300 - 2>"$work/corrupt.log" || status=$?
grep "reference comparison" "$work/corrupt.log"
[[ $status -eq 1 ]] || { echo "expected the corrupted reference to fail (exit 1), got exit $status"; exit 1; }
rm "$work/dump/reference.bin"
status=0; "$work/vk_frame" "$work/dump" 300 - 2>"$work/missing.log" || status=$?
[[ $status -eq 2 ]] || { echo "expected the missing reference to exit 2, got exit $status"; exit 1; }
mv "$work/reference.orig" "$work/dump/reference.bin"
echo "replay check passed: match exits 0, one corrupted pixel exits 1, missing reference exits 2"
