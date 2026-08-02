# LineageOS camera support for Amazon Checkers

This repository packages the camera bring-up for the Amazon Echo Show 5 (`checkers`) as a reproducible patch series for LineageOS 18.1. It includes the legacy camera provider integration, MediaTek compatibility shims, preview and orientation fixes, and a small system-wide AWB correction.

No proprietary camera binaries are distributed. Extract them from a compatible device or firmware source after applying the patches.

## Quick start

Start with a clean LineageOS 18.1 checkout. The pinned local manifest supplies the Amazon projects and fixes every patched project to the revisions recorded in [`series.tsv`](patches/lineage-18.1/series.tsv).

```bash
mkdir -p /path/to/lineage-18.1/.repo/local_manifests
cp manifests/amazon_mt8163-camera.xml \
  /path/to/lineage-18.1/.repo/local_manifests/
cd /path/to/lineage-18.1
repo sync

cd /path/to/lineageos-camera
./scripts/apply-patches.sh --check /path/to/lineage-18.1
./scripts/apply-patches.sh /path/to/lineage-18.1
```

Then extract the Checkers proprietary files, verify that the blob set is complete, and build:

```bash
cd /path/to/lineage-18.1/device/amazon/checkers
./extract-files.sh /path/to/compatible-system-dump
/path/to/lineageos-camera/scripts/verify-proprietary-files.sh /path/to/lineage-18.1

cd /path/to/lineage-18.1
source build/envsetup.sh
lunch lineage_checkers-userdebug
m bacon
```

See [installation and build instructions](docs/INSTALL.md) for prerequisites, expected state, and failure recovery.

## Repository contents

- [`patches/lineage-18.1`](patches/lineage-18.1) — ordered per-project patch bundle and checksums
- [`scripts`](scripts) — preflight, apply, regeneration, and detached-worktree validation
- [`docs/PATCHES.md`](docs/PATCHES.md) — patch ownership and dependency map
- [`docs/CALIBRATION.md`](docs/CALIBRATION.md) — AWB defaults and maintainer calibration boundary
- [`docs/VALIDATION.md`](docs/VALIDATION.md) — build and on-device evidence
- [`tools/camhal-relay`](tools/camhal-relay) — standalone legacy Camera1 HAL relay and diagnostic toolkit

## Compatibility boundary

The patches target the exact LineageOS 18.1 bases in `series.tsv` and the Checkers proprietary blob set listed by `device/amazon/checkers/proprietary-files.txt`. The apply script rejects unknown revisions and dirty project trees rather than guessing whether a partial application is safe.

The patch bundle is the maintained distribution format. Upstream pull requests can be prepared later if the relevant maintainers request them.
