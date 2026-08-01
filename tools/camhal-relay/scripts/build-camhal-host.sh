#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [[ -z "$ndk" ]]; then
  echo "Set ANDROID_NDK_HOME or ANDROID_NDK_ROOT to an installed Android NDK." >&2
  exit 1
fi
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

current_dp="$root/build/libdpframework.current.so"
current_dp_tmp="$current_dp.tmp"
rm -f "$current_dp_tmp"
if [[ -n "${CAMHAL_DP_LIBRARY:-}" ]]; then
  cp "$CAMHAL_DP_LIBRARY" "$current_dp_tmp"
elif adb pull /system/vendor/lib/libdpframework.so "$current_dp_tmp" >/dev/null; then
  :
elif [[ -s "$current_dp" ]]; then
  echo "Device unavailable; reusing cached $current_dp" >&2
else
  echo "Unable to pull libdpframework.so and no cached copy is available." >&2
  echo "Set CAMHAL_DP_LIBRARY to an exact runtime copy for an offline build." >&2
  exit 1
fi
if [[ -s "$current_dp_tmp" ]]; then
  mv "$current_dp_tmp" "$current_dp"
fi
"$cc" \
  -std=c11 \
  -O2 -g3 \
  -Wall -Wextra -Werror \
  -fPIC -shared \
  -Wl,-soname,libshim_dpframework.so \
  -Wl,--no-as-needed \
  "$root/src/dpframework_compat.c" \
  -L"$(dirname "$current_dp")" \
  -l:libdpframework.current.so \
  -llog \
  -o "$root/build/libshim_dpframework.so"

"$cc" \
  -std=c11 \
  -O2 -g3 \
  -Wall -Wextra -Werror \
  -fPIC -shared \
  -Wl,-soname,libshim_cmdq_path.so \
  "$root/src/cmdq_path_compat.c" \
  -llog \
  -o "$root/build/libshim_cmdq_path.so"

file "$root/build/camhal_host" \
     "$root/build/libshim_dpframework.so" \
     "$root/build/libshim_cmdq_path.so"
