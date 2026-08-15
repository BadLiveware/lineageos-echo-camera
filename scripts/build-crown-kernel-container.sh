#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 OUTPUT_DIR [PATCH_FILE]" >&2
    exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage

output_dir=$(realpath -m "$1")
patch_file=${2:-}
kernel_repo=https://github.com/amazon-oss/android_kernel_amazon_mt8163.git
kernel_revision=7c8251b354f11a4703e6f7d5ad7b0c545bbed15d
toolchain_repo=https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-gnu-6.3.1.git
toolchain_revision=eb2998abfd6a9bacef48f0c4cdd4952aefffe7d4
source_dir=/home/r0rt1z2/lineage-18.1/kernel/amazon/mt8163-4.9
toolchain_dir=/home/r0rt1z2/lineage-18.1/prebuilts/linaro/linux-x86/aarch64/aarch64-linux-gnu
build_dir=/build

output_prefix=${output_dir%/}/
for reserved_dir in "$source_dir" "$toolchain_dir" "$build_dir"; do
    reserved_prefix=${reserved_dir%/}/
    if [[ "$output_prefix" == "$reserved_prefix"* ||
          "$reserved_prefix" == "$output_prefix"* ]]; then
        echo "output directory overlaps build workspace: $output_dir" >&2
        exit 1
    fi
done
mkdir -p "$output_dir"
if [[ -n $(find "$output_dir" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    echo "output directory must be empty: $output_dir" >&2
    exit 1
fi
rm -rf "$source_dir" "$toolchain_dir" "$build_dir"
mkdir -p "$(dirname "$source_dir")" "$(dirname "$toolchain_dir")" "$build_dir"

git init "$source_dir"
git -C "$source_dir" remote add origin "$kernel_repo"
git -C "$source_dir" fetch --depth=1 origin "$kernel_revision"
git -C "$source_dir" checkout --detach FETCH_HEAD
git init "$toolchain_dir"
git -C "$toolchain_dir" remote add origin "$toolchain_repo"
git -C "$toolchain_dir" fetch --depth=1 origin "$toolchain_revision"
git -C "$toolchain_dir" checkout --detach FETCH_HEAD

if [[ -n "$patch_file" ]]; then
    git -C "$source_dir" apply --check "$patch_file"
    git -C "$source_dir" apply "$patch_file"
fi

# Keep the release identity stable for module/firmware lookup. Patch provenance
# is recorded separately and must not alter UTS_RELEASE.
printf '%s\n' "-g${kernel_revision:0:12}" > "$source_dir/.scmversion"
git -C "$source_dir" add -f -A
kernel_source_tree=$(git -C "$source_dir" write-tree)
kernel_diff_sha256=$(
    git -C "$source_dir" diff --cached --binary --full-index HEAD |
        sha256sum | cut -d' ' -f1
)

export ARCH=arm64
export CROSS_COMPILE="$toolchain_dir/bin/aarch64-linux-gnu-"
export KBUILD_BUILD_USER=nobody
export KBUILD_BUILD_HOST=android-build
export KBUILD_BUILD_VERSION=7
# The release build used separate timestamps for the embedded default initramfs
# and the final compile identity. Generate the initramfs with its observed mtime,
# then switch to the compile timestamp before the full build.
export KBUILD_BUILD_TIMESTAMP='Thu Apr 16 11:40:01 CEST 2026'
export LC_ALL=C
export TZ=UTC
# Android's kernel build passes the product variant into MediaTek makefiles.
# Crown v0.4 is a userdebug build, which selects the user GED feature set.
export TARGET_BUILD_VARIANT=userdebug

make -C "$source_dir" O="$build_dir" crown_defconfig
make -C "$source_dir" O="$build_dir" usr/built-in.o
export KBUILD_BUILD_TIMESTAMP='Wed Jun 24 02:19:46 CEST 2026'
make -C "$source_dir" O="$build_dir" -j"$(nproc)" Image.gz-dtb

python3 - "$build_dir" "$output_dir" <<'PY'
from pathlib import Path
import hashlib
import json
import shutil
import struct
import sys

build = Path(sys.argv[1])
out = Path(sys.argv[2])
boot = build / "arch/arm64/boot"
gzip_image = (boot / "Image.gz").read_bytes()
dtb_names = [
    "crown_proto.dtb",
    "crown_evt.dtb",
    "crown_evt_1_1.dtb",
    "crown_dvt.dtb",
    "crown_pvt.dtb",
]
dtb_dir = boot / "dts/mediatek"
padding = b"\0" * (-len(gzip_image) % 4)
kernel = gzip_image + padding + b"".join((dtb_dir / name).read_bytes() for name in dtb_names)
(out / "Image.gz-dtb").write_bytes(kernel)
shutil.copy2(build / ".config", out / "crown.config")
shutil.copy2(build / "vmlinux", out / "vmlinux")

provenance = {
    "image_gz_bytes": len(gzip_image),
    "appended_dtb_padding_bytes": len(padding),
    "image_gz_dtb_bytes": len(kernel),
    "image_gz_dtb_sha256": hashlib.sha256(kernel).hexdigest(),
    "dtbs": {
        name: hashlib.sha256((dtb_dir / name).read_bytes()).hexdigest()
        for name in dtb_names
    },
}
(out / "provenance.json").write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n")
PY

{
    echo "kernel_repo=$kernel_repo"
    echo "kernel_revision=$kernel_revision"
    echo "kernel_base_tree=$(git -C "$source_dir" rev-parse 'HEAD^{tree}')"
    echo "kernel_source_tree=$kernel_source_tree"
    echo "kernel_worktree_diff_sha256=$kernel_diff_sha256"
    if [[ -n "$patch_file" ]]; then
        echo "patch_sha256=$(sha256sum "$patch_file" | cut -d' ' -f1)"
    else
        echo "patch_sha256=none"
    fi
    echo "toolchain_repo=$toolchain_repo"
    echo "toolchain_revision=$toolchain_revision"
    echo "toolchain_tree=$(git -C "$toolchain_dir" rev-parse 'HEAD^{tree}')"
    echo "source_dir=$source_dir"
    echo "cross_compile=$CROSS_COMPILE"
    echo "gcc=$(${CROSS_COMPILE}gcc --version | head -n 1)"
    echo "ld=$(${CROSS_COMPILE}ld --version | head -n 1)"
    echo "host_gcc=$(gcc --version | head -n 1)"
    echo "make=$(make --version | head -n 1)"
    echo "gzip=$(gzip --version | head -n 1)"
    echo "ubuntu_snapshot=${CROWN_KERNEL_UBUNTU_SNAPSHOT:-unknown}"
    echo "base_image=${CROWN_KERNEL_BASE_IMAGE:-unknown}"
    echo "target_build_variant=$TARGET_BUILD_VARIANT"
    echo "initramfs_timestamp=Thu Apr 16 11:40:01 CEST 2026"
    echo "kernel_timestamp=$KBUILD_BUILD_TIMESTAMP"
    echo "patch=${patch_file:-none}"
} > "$output_dir/build-provenance.txt"

(
    cd "$output_dir"
    sha256sum Image.gz-dtb crown.config vmlinux provenance.json build-provenance.txt > SHA256SUMS
    sha256sum -c SHA256SUMS
)
