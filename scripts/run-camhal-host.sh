#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-enumerate}
camera_id=${2:-0}
remote=${CAMHAL_REMOTE_DIR:-/data/local/tmp/camprobe}
stamp=$(date +%Y%m%d-%H%M%S)
log="$root/results/${mode}-${stamp}.log"

mkdir -p "$root/results"
"$root/scripts/build-camhal-host.sh"
adb push "$root/build/camhal_host" "$remote/camhal_host" >/dev/null
adb shell "chmod 0755 '$remote/camhal_host'"

set +e
adb shell "
  export LD_LIBRARY_PATH='$remote/vendor/lib:$remote/system/lib:/vendor/lib:/system/lib'
  export LD_PRELOAD='/system/lib/libsensor.so $remote/libshim_graphic_buffer.so $remote/libshim_checkers_extra.so'
  export CAMERA_HAL_PATH='$remote/vendor/lib/hw/camera.mt8163.so'
  cd '$remote'
  exec timeout -s KILL 25 ./camhal_host '$mode' '$camera_id'
" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e

printf 'camhal host exit=%d log=%s\n' "$status" "$log" >&2
exit "$status"
