#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-enumerate}
camera_id=${2:-0}
remote=${CAMHAL_REMOTE_DIR:-/data/local/tmp/camprobe}
dp_mode=${CAMHAL_DP_MODE:-android9}
relay_socket=${CAMHAL_RELAY_SOCKET:-$remote/camrelay.sock}
relay_port=${CAMHAL_RELAY_PORT:-57321}
relay_seconds=${CAMHAL_RELAY_SECONDS:-20}
relay_frames=${CAMHAL_RELAY_FRAMES:-120}
case "$mode" in
  enumerate|open|parameters|capture-one|relay|close) ;;
  *)
    echo "Unsupported camhal mode: $mode" >&2
    exit 2
    ;;
esac
timeout_seconds=25
stamp=$(date +%Y%m%d-%H%M%S)
log="$root/results/${mode}-${stamp}.log"

# Invoked by the EXIT trap below.
# shellcheck disable=SC2329
cleanup_relay() {
  if [[ "$mode" == relay ]]; then
    adb forward --remove "tcp:$relay_port" >/dev/null 2>&1 || true
    adb shell "rm -f '$relay_socket'" >/dev/null 2>&1 || true
  fi
}
trap cleanup_relay EXIT

mkdir -p "$root/results"
"$root/scripts/build-camhal-host.sh"
adb push "$root/build/camhal_host" "$remote/camhal_host" >/dev/null
adb push "$root/build/libshim_dpframework.so" "$remote/libshim_dpframework.so" >/dev/null
adb push "$root/build/libshim_cmdq_path.so" "$remote/libshim_cmdq_path.so" >/dev/null
case "$dp_mode" in
  android7)
    adb shell "
      if [ -f '$remote/vendor/lib/libdpframework.android7.so.disabled' ]; then
        mv '$remote/vendor/lib/libdpframework.android7.so.disabled' '$remote/vendor/lib/libdpframework.so'
      fi
      chmod 0755 '$remote/camhal_host'
    "
    dp_preload=" $remote/libshim_cmdq_path.so"
    ;;
  android9)
    adb shell "
      if [ -f '$remote/vendor/lib/libdpframework.so' ]; then
        mv '$remote/vendor/lib/libdpframework.so' '$remote/vendor/lib/libdpframework.android7.so.disabled'
      fi
      chmod 0755 '$remote/camhal_host'
    "
    dp_preload=" $remote/libshim_dpframework.so"
    ;;
  *)
    echo "Unsupported CAMHAL_DP_MODE: $dp_mode" >&2
    exit 2
    ;;
esac

if [[ "$mode" == relay ]]; then
  if [[ "$relay_seconds" =~ ^[0-9]+$ ]]; then
    timeout_seconds=$((relay_seconds + 15))
  fi
  adb forward "tcp:$relay_port" "localfilesystem:$relay_socket"
  printf 'camhal relay tcp:localhost:%s -> localfilesystem:%s\n' \
    "$relay_port" "$relay_socket" >&2
fi

set +e
adb shell "
  export LD_LIBRARY_PATH='$remote/vendor/lib:$remote/system/lib:/vendor/lib:/system/lib'
  export LD_PRELOAD='/system/lib/libsensor.so $remote/libshim_graphic_buffer.so $remote/libshim_checkers_extra.so$dp_preload'
  export CAMERA_HAL_PATH='$remote/vendor/lib/hw/camera.mt8163.so'
  export CAMHAL_RELAY_SOCKET='$relay_socket'
  export CAMHAL_RELAY_SECONDS='$relay_seconds'
  export CAMHAL_RELAY_FRAMES='$relay_frames'
  cd '$remote'
  exec timeout -s KILL '$timeout_seconds' ./camhal_host '$mode' '$camera_id'
" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e

printf 'camhal host exit=%d log=%s\n' "$status" "$log" >&2
exit "$status"
