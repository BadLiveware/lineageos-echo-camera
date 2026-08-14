#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 LINEAGE_ROOT FIRMWARE_ROOT ARTIFACT_ROOT MODE" >&2
    echo "MODE is one of: checksums, modules, full" >&2
    exit 2
}

[[ $# -eq 4 ]] || usage

lineage_root=$1
firmware_root=$2
artifact_root=$3
mode=$4
case "$mode" in
    checksums|modules|full) ;;
    *) usage ;;
esac

distribution_root=/opt/lineageos-camera
pinned_manifest=manifests/crown/lineage-18.1-pinned.xml
manifest_repo_revision=fed547c557daea86389b10bf3e25ad6ae13114e2
proprietary_checksums=manifests/crown/proprietary-SHA256SUMS
product_out=out/target/product/crown
verified_kernel_sha256=c6b47cc5e291a282eb1fe092753cfff2ec917dbb2e039849e0849b4281166de3
source_input_map=$artifact_root/SOURCE_INPUT_MAP.tsv

required_firmware_file=system/vendor/lib/hw/camera.mt8163.so
if [[ ! -f "$firmware_root/$required_firmware_file" ]]; then
    echo "Firmware context is not a Crown system dump: missing $required_firmware_file" >&2
    exit 1
fi
if [[ ! -f "$firmware_root/SHA256SUMS" ]]; then
    echo "Crown firmware context lacks its preserved SHA256SUMS" >&2
    exit 1
fi

report_error() {
    status=$?
    echo "Crown container pipeline failed at line ${BASH_LINENO[0]}: $BASH_COMMAND" >&2
    return "$status"
}
trap report_error ERR

cleanup_build_state() {
    status=$?
    rm -rf "$lineage_root/out"
    for path in \
        "$lineage_root/vendor/amazon/crown" \
        "$lineage_root/device/amazon/crown"; do
        if git -C "$path" rev-parse --git-dir >/dev/null 2>&1; then
            git -C "$path" reset --hard HEAD >/dev/null 2>&1 || true
            git -C "$path" clean -ffdx >/dev/null 2>&1 || true
        fi
    done
    return "$status"
}
trap cleanup_build_state EXIT

mkdir -p "$lineage_root"
if [[ -d "$lineage_root/.repo" ]]; then
    echo "Restoring cached LineageOS checkout"
    rm -rf \
        "$lineage_root/out" \
        "$lineage_root/.repo/local_manifests"
else
    if [[ -n $(find "$lineage_root" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        echo "LineageOS cache is neither empty nor a repo checkout: $lineage_root" >&2
        exit 1
    fi
fi

(
    cd "$lineage_root"
    repo init \
        -u https://github.com/LineageOS/android.git \
        -b lineage-18.1 \
        --git-lfs \
        --repo-rev v2.54
    git -C .repo/manifests checkout --detach "$manifest_repo_revision"
    cp "$distribution_root/$pinned_manifest" .repo/manifests/crown-pinned.xml
    ln -sfn manifests/crown-pinned.xml .repo/manifest.xml
    repo sync --no-manifest-update --force-checkout
    repo forall -c 'git reset --hard HEAD && git clean -ffdx'
)

"$distribution_root/scripts/apply-amazon-patches.sh" --check "$lineage_root"
"$distribution_root/scripts/apply-patches.sh" --check crown "$lineage_root"
"$distribution_root/scripts/apply-amazon-patches.sh" "$lineage_root"
"$distribution_root/scripts/apply-patches.sh" crown "$lineage_root"

rm -rf "$artifact_root"
mkdir -p "$artifact_root"
printf '%s  %s\n' \
    "$(sha256sum "$firmware_root/SHA256SUMS" | cut -d' ' -f1)" \
    "SHA256SUMS" > "$artifact_root/FIRMWARE_MANIFEST_SHA256"
"$distribution_root/scripts/verify-firmware-inputs.py" \
    "$firmware_root" \
    "$lineage_root" \
    crown \
    --section Camera \
    --report "$source_input_map"

(
    cd "$lineage_root/device/amazon/crown"
    ./extract-files.sh -s Camera "$firmware_root"
)

write_vendor_checksums() {
    local output=$1
    (
        cd "$lineage_root"
        find \
            vendor/amazon/crown/proprietary \
            vendor/amazon/mt8163-common/proprietary \
            -type f -print0 |
            sort -z |
            xargs -0 sha256sum > "$output"
    )
}

output_checksums=$artifact_root/OUTPUT_SHA256SUMS
write_vendor_checksums "$output_checksums"

if [[ "$mode" == checksums ]]; then
    cp "$distribution_root/$pinned_manifest" "$artifact_root/manifest.xml"
    printf '%s\n' "$verified_kernel_sha256" > "$artifact_root/VERIFIED_KERNEL_SHA256"
    echo "Exported Crown proprietary checksum review:"
    find "$artifact_root" -maxdepth 1 -type f -printf '  %f\n' | sort
    exit 0
fi

if [[ ! -f "$distribution_root/$proprietary_checksums" ]]; then
    echo "Missing reviewed Crown proprietary manifest: $proprietary_checksums" >&2
    exit 1
fi
"$distribution_root/scripts/verify-proprietary-files.sh" "$lineage_root" crown
(
    cd "$lineage_root"
    sha256sum -c "$distribution_root/$proprietary_checksums"
)
cmp "$output_checksums" "$distribution_root/$proprietary_checksums"

(
    cd "$lineage_root"
    set +u
    export EXT2FS_NO_MTAB_OK=1
    # shellcheck disable=SC1091
    source build/envsetup.sh
    lunch lineage_crown-userdebug
    if [[ "$mode" == modules ]]; then
        m \
            android.hardware.camera.provider@2.4-service.crown \
            libcrown_camera_compat \
            Camera2
    else
        expected_root=/home/r0rt1z2/lineage-18.1
        if [[ $(pwd -P) != "$expected_root" ]]; then
            echo "Crown exact-kernel build requires source root $expected_root" >&2
            exit 1
        fi
        kernel_source=$lineage_root/kernel/amazon/mt8163-4.9
        kernel_output=$lineage_root/out/target/product/crown/obj/KERNEL_OBJ
        toolchain=$lineage_root/prebuilts/linaro/linux-x86/aarch64/aarch64-linux-gnu/bin/aarch64-linux-gnu-
        printf '%s\n' '-g7c8251b354f1' > "$kernel_source/.scmversion"
        export ARCH=arm64
        export CROSS_COMPILE=$toolchain
        export KBUILD_BUILD_USER=nobody
        export KBUILD_BUILD_HOST=android-build
        export KBUILD_BUILD_VERSION=7
        export KBUILD_BUILD_TIMESTAMP='Thu Apr 16 11:40:01 CEST 2026'
        export LC_ALL=C
        export TZ=UTC
        export TARGET_BUILD_VARIANT=userdebug
        make -C "$kernel_source" O="$kernel_output" crown_defconfig
        make -C "$kernel_source" O="$kernel_output" usr/built-in.o
        export KBUILD_BUILD_TIMESTAMP='Wed Jun 24 02:19:46 CEST 2026'
        make -C "$kernel_source" O="$kernel_output" -j"$(nproc)" Image.gz-dtb

        exact_kernel_dir=$lineage_root/out/crown-exact-kernel
        exact_kernel=$exact_kernel_dir/Image.gz-dtb
        mkdir -p "$exact_kernel_dir"
        cp "$kernel_output/arch/arm64/boot/Image.gz-dtb" "$exact_kernel"
        printf '%s  %s\n' "$verified_kernel_sha256" "$exact_kernel" |
            sha256sum -c -

        # The Lineage kernel rule unconditionally regenerates .config when the
        # Android graph is first created, which changes the otherwise exact
        # release kernel. Build the kernel from pinned source above, retain its
        # prepared KERNEL_OBJ for external modules, and force that just-built
        # image into boot and recovery packaging.
        export TARGET_PREBUILT_KERNEL=$exact_kernel
        export TARGET_PREBUILT_RECOVERY_KERNEL=$exact_kernel
        export TARGET_FORCE_PREBUILT_KERNEL=true
        export CROWN_PREPARED_KERNEL_OBJ=true
        m bacon
    fi
)

if [[ "$mode" == modules ]]; then
    provider_binary=$lineage_root/$product_out/system/vendor/bin/hw/android.hardware.camera.provider@2.4-service.crown
    compat_library=$lineage_root/$product_out/system/vendor/lib/libcrown_camera_compat.so
    camera_apk=$lineage_root/$product_out/system/system_ext/priv-app/Camera2/Camera2.apk
    for module_artifact in "$provider_binary" "$compat_library" "$camera_apk"; do
        if [[ ! -f "$module_artifact" ]]; then
            echo "Module build completed without artifact: $module_artifact" >&2
            exit 1
        fi
        cp "$module_artifact" "$artifact_root/"
    done
    printf 'module-build=ok\n' > "$artifact_root/MODULE_BUILD_RESULT"
    cp "$distribution_root/$pinned_manifest" "$artifact_root/manifest.xml"
    echo "Crown camera modules built successfully."
    exit 0
fi

product_output="$lineage_root/$product_out"
if [[ ! -d "$product_output" ]]; then
    echo "Build completed without product output directory: $product_output" >&2
    exit 1
fi

ota_list=$(find "$product_output" -maxdepth 1 -type f \
    -name 'lineage-*.zip' ! -name '*target_files*' -print | sort)
ota_count=$(printf '%s\n' "$ota_list" | grep -c . || true)
if [[ $ota_count -ne 1 ]]; then
    echo "Expected exactly one Crown LineageOS OTA, found $ota_count" >&2
    printf '%s\n' "$ota_list" | sed 's/^/  /' >&2
    exit 1
fi
ota_file=$ota_list

cp "$ota_file" "$artifact_root/crown-lineage-18.1.zip"
printf '%s\n' "$(basename "$ota_file")" > "$artifact_root/OTA_SOURCE_NAME"
cp "$distribution_root/$proprietary_checksums" \
    "$artifact_root/PROPRIETARY_SHA256SUMS"

for image in boot.img recovery.img; do
    source_image="$product_output/$image"
    if [[ ! -f "$source_image" ]]; then
        echo "Build completed without required artifact: $image" >&2
        exit 1
    fi
    cp "$source_image" "$artifact_root/$image"
    python3 - "$source_image" "$verified_kernel_sha256" "$image" <<'PY'
import hashlib
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
expected = sys.argv[2]
label = sys.argv[3]
image = path.read_bytes()
if image[:8] != b"ANDROID!":
    raise SystemExit(f"{label}: not an Android boot image")
size = struct.unpack_from("<I", image, 8)[0]
page = struct.unpack_from("<I", image, 36)[0]
kernel = image[page : page + size]
actual = hashlib.sha256(kernel).hexdigest()
if actual != expected:
    raise SystemExit(f"{label}: kernel hash {actual}, expected {expected}")
print(f"{label}: verified kernel {actual}")
PY
done

target_files_list=$(find \
    "$product_output/obj/PACKAGING/target_files_intermediates" \
    -maxdepth 1 -type f -name '*.zip' -print | sort)
target_files_count=$(printf '%s\n' "$target_files_list" | grep -c . || true)
if [[ $target_files_count -ne 1 ]]; then
    echo "Expected exactly one Crown target-files archive, found $target_files_count" >&2
    printf '%s\n' "$target_files_list" | sed 's/^/  /' >&2
    exit 1
fi
target_files=$target_files_list

image_crosscheck=$(mktemp -d)
unzip -p "$target_files" IMAGES/boot.img > "$image_crosscheck/target-files-boot.img"
unzip -p "$target_files" IMAGES/recovery.img > "$image_crosscheck/target-files-recovery.img"
unzip -p "$artifact_root/crown-lineage-18.1.zip" boot.img > "$image_crosscheck/ota-boot.img"

cmp "$product_output/boot.img" "$image_crosscheck/target-files-boot.img"
cmp "$product_output/recovery.img" "$image_crosscheck/target-files-recovery.img"
cmp "$product_output/boot.img" "$image_crosscheck/ota-boot.img"
rm -rf "$image_crosscheck"
echo "Verified product, target-files, and OTA boot/recovery image identity."

cp "$distribution_root/$pinned_manifest" "$artifact_root/manifest.xml"
(
    cd "$artifact_root"
    checksum_file=$(mktemp)
    find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' |
        sort -z |
        xargs -0 sha256sum > "$checksum_file"
    mv "$checksum_file" SHA256SUMS
    sha256sum -c SHA256SUMS
)

echo "Exported Crown build artifacts:"
find "$artifact_root" -maxdepth 1 -type f -printf '  %f\n' | sort
