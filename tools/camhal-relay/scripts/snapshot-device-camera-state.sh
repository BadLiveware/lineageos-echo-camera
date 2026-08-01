#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 OUTPUT_PREFIX" >&2
  exit 2
fi

prefix=$1
mkdir -p "$(dirname "$prefix")"

adb shell 'find /data/nvram -type f -exec sha256sum {} \; 2>/dev/null | sort -k2' \
  >"$prefix.nvram.sha256"

# The single-quoted script is intentionally evaluated by the device shell.
# shellcheck disable=SC2016
adb shell '
  echo "captured_at=$(date -Iseconds 2>/dev/null || date)"
  echo "boot_id=$(cat /proc/sys/kernel/random/boot_id)"
  echo "uptime=$(cat /proc/uptime)"
  echo
  echo "[camera processes]"
  ps -A | grep -E "camhal_host|camera" || true
  echo
  echo "[open camera device descriptors]"
  for process in /proc/[0-9]*; do
    for descriptor in "$process"/fd/*; do
      target=$(readlink "$descriptor" 2>/dev/null) || continue
      case "$target" in
        *camera*|*CAMERA*|*isp*|*seninf*)
          echo "${process##*/} $descriptor -> $target"
          ;;
      esac
    done
  done
  echo
  echo "[camera clocks]"
  if [ -r /sys/kernel/debug/clk/clk_summary ]; then
    grep -Ei "img_cam|img_sen_cam|mm_cam_mdp|camtg|scam" \
      /sys/kernel/debug/clk/clk_summary || true
  fi
  echo
  echo "[camera regulators]"
  if [ -r /sys/kernel/debug/regulator/regulator_summary ]; then
    grep -Ei "vcam|camera[12]" \
      /sys/kernel/debug/regulator/regulator_summary || true
  fi
  echo
  echo "[power domains]"
  if [ -r /sys/kernel/debug/pm_genpd/pm_genpd_summary ]; then
    grep -Ei "^(isp|mm)[[:space:]]" \
      /sys/kernel/debug/pm_genpd/pm_genpd_summary || true
  fi
  echo
  echo "[relay socket]"
  ls -l /data/local/tmp/camprobe/camrelay.sock 2>&1 || true
' >"$prefix.state.txt"

adb logcat -b all -d -v threadtime >"$prefix.logcat"
adb shell dmesg >"$prefix.dmesg"
adb forward --list >"$prefix.adb-forwards.txt"

(
  cd "$(dirname "$prefix")"
  base=$(basename "$prefix")
  sha256sum "$base".* >"$base.SHA256SUMS"
)

printf 'camera state snapshot: %s.{state.txt,nvram.sha256,logcat,dmesg,adb-forwards.txt,SHA256SUMS}\n' \
  "$prefix"
