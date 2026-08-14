#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 ANDROID_SOURCE_ROOT DEVICE" >&2
  echo "DEVICE is one of: checkers, crown" >&2
  exit 2
fi

android_root=$(realpath "$1")
device=$2
case "$device" in
  checkers|crown) ;;
  *)
    echo "unsupported device: $device" >&2
    exit 2
    ;;
esac
checked=0
missing=0

check_list() {
  local device=$1
  local list="$android_root/device/amazon/$device/proprietary-files.txt"
  local vendor_root="$android_root/vendor/amazon/$device/proprietary"

  [[ -f "$list" ]] || {
    echo "Missing proprietary list: $list" >&2
    return 1
  }
  [[ -d "$vendor_root" ]] || {
    echo "Missing extracted vendor directory: $vendor_root" >&2
    return 1
  }

  while IFS= read -r entry || [[ -n "$entry" ]]; do
    entry=${entry%$'\r'}
    [[ -z "$entry" || "$entry" == \#* ]] && continue

    # Drop an optional source hash, then use the explicit destination when
    # the extraction list renames a blob.
    entry=${entry%%|*}
    local destination=${entry#*:}
    destination=${destination#/}

    ((checked += 1))
    if [[ ! -e "$vendor_root/$destination" ]]; then
      printf 'missing %s: %s\n' "$device" "$destination" >&2
      ((missing += 1))
    fi
  done < "$list"
}

check_list mt8163-common
check_list "$device"

if ((missing != 0)); then
  printf 'Proprietary extraction is incomplete: %d of %d files are missing.\n' \
    "$missing" "$checked" >&2
  exit 1
fi

printf 'Verified %d proprietary files across mt8163-common and %s.\n' \
  "$checked" "$device"
