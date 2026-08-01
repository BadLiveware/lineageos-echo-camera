# Apply and build the Checkers camera patches

This guide is for integrators with an existing LineageOS 18.1 build environment and lawful access to the Checkers proprietary files.

## Prerequisites

- a clean LineageOS 18.1 source checkout;
- Amazon MT8163 projects from `manifests/amazon_mt8163-camera.xml`;
- the exact project revisions recorded in `patches/lineage-18.1/series.tsv`;
- a compatible Checkers device or firmware dump for proprietary-file extraction;
- the normal LineageOS 18.1 host build dependencies.

Copy the manifest before syncing a new checkout:

```bash
cp /path/to/lineageos-camera/manifests/amazon_mt8163-camera.xml \
  /path/to/lineage-18.1/.repo/local_manifests/
cd /path/to/lineage-18.1
repo sync
```

## 1. Preflight the patch series

The preflight checks all five project revisions, requires clean project trees, and asks Git to validate every patch before any files are changed.

```bash
cd /path/to/lineageos-camera
./scripts/apply-patches.sh --check /path/to/lineage-18.1
```

Expected final line:

```text
All patches apply cleanly.
```

A revision mismatch is intentional protection. Sync to the recorded revision or regenerate and validate a new patch bundle; do not bypass the check by applying fragments manually.

## 2. Apply the patches

```bash
./scripts/apply-patches.sh /path/to/lineage-18.1
```

The script performs the complete preflight again, then applies the patches in dependency order. It leaves normal uncommitted source changes in each Android project so they can be inspected before building.

## 3. Extract proprietary files

The repository does not contain Amazon or MediaTek camera binaries. The Checkers patch adds the required entries to `proprietary-files.txt`.

```bash
cd /path/to/lineage-18.1/device/amazon/checkers
./extract-files.sh /path/to/compatible/system-dump
```

Use a source matching the Checkers Android generation expected by the device tree. Review the generated `vendor/amazon/checkers` tree before building. Do not publish extracted binaries unless their redistribution terms permit it.

## 4. Build

```bash
cd /path/to/lineage-18.1
source build/envsetup.sh
lunch lineage_checkers-userdebug
m bacon
```

For a faster source check after camera changes:

```bash
m libcheckers_dpframework_compat
m systemimage
```

## Updating an existing installation

Distribute the resulting LineageOS OTA as one unit. The provider executable, init declaration, compatibility library, framework adapter, Camera2 behavior, properties, and proprietary blob closure are version-coupled.

Do not install `libcheckers_dpframework_compat.so` on stock or unrelated ROM builds. The compatibility boundary is described in `docs/PATCHES.md`.

## Removing an uncommitted application

Because `apply-patches.sh` does not create commits, each affected Android project can be restored with Git after preserving any wanted work. Inspect before discarding changes:

```bash
git -C /path/to/project status --short
git -C /path/to/project diff
```

Use a fresh checkout when possible. Avoid broad reset or clean commands in a source tree containing unrelated work.
