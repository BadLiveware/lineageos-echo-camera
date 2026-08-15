#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 DEVICE ANDROID_SOURCE_ROOT" >&2
  echo "DEVICE is one of: checkers, crown" >&2
  exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec "$repo_root/scripts/apply-patches.sh" --check "$1" "$2"
