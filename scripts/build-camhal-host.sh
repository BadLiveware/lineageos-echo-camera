#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ndk=${ANDROID_NDK_HOME:-/home/fl/Downloads/checkers-camera-probe/tools/android-ndk-r27d}
toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
cc="$toolchain/armv7a-linux-androideabi30-clang"

if [[ ! -x "$cc" ]]; then
  echo "Android NDK compiler not found: $cc" >&2
  echo "Set ANDROID_NDK_HOME to an installed NDK." >&2
  exit 1
fi

mkdir -p "$root/build"
"$cc" \
  -std=c11 \
  -O1 -g3 \
  -Wall -Wextra -Werror \
  -fno-omit-frame-pointer \
  -fPIE -pie \
  -I"$root/src" \
  "$root/src/camhal_host.c" \
  -ldl \
  -o "$root/build/camhal_host"

file "$root/build/camhal_host"
