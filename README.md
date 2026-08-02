# LineageOS camera support for Amazon Checkers

This repository packages the camera bring-up for the Amazon Echo Show 5 (`checkers`) as a reproducible patch series for LineageOS 18.1. It includes the legacy camera provider integration, MediaTek compatibility shims, preview and orientation fixes, and system-wide AWB correction. The build also pins and applies Amazon-OSS's required baseline compatibility patch bundle, including fixed touchscreen-orientation support.

No proprietary camera binaries are distributed. The build reads them from a compatible device or firmware dump supplied locally.

## Create a firmware dump

From a rooted Checkers installation or recovery with ADB access, copy the system partition to a local directory:

```bash
export CHECKERS_FIRMWARE="$PWD/checkers-system-dump"
mkdir -p "$CHECKERS_FIRMWARE"

adb root
adb wait-for-device
adb pull /system "$CHECKERS_FIRMWARE/"
```

Fire OS stores the vendor files under `/system/vendor`. On installations with a separate `/vendor` partition, populate the same dump layout if the camera HAL was not included by the first pull:

```bash
if [[ ! -f "$CHECKERS_FIRMWARE/system/vendor/lib/hw/camera.mt8163.so" ]]; then
  mkdir -p "$CHECKERS_FIRMWARE/system/vendor"
  adb pull /vendor/. "$CHECKERS_FIRMWARE/system/vendor/"
fi

test -f "$CHECKERS_FIRMWARE/system/vendor/lib/hw/camera.mt8163.so"
```

The dump remains local and is passed to BuildKit as a read-only build context.

## Build

Requirements:

- Docker Engine with Docker Buildx;
- a trusted local Docker builder;
- at least 200 GB available in Docker's storage;
- a Checkers system dump containing `system/vendor/lib/hw/camera.mt8163.so`.

Create a dedicated local builder, then run the build from this repository with a new build ID:

```bash
docker buildx create \
  --name checkers-builder \
  --driver docker-container \
  --driver-opt image=moby/buildkit:v0.31.2@sha256:63db51c9b30208a7c2b1c40392c7ebb9ce2f85ba238a18a85420f8f5ea2d4684 \
  --use unix:///var/run/docker.sock
docker buildx inspect --bootstrap

export CHECKERS_FIRMWARE=/absolute/path/to/checkers-system-dump
export CHECKERS_BUILD_ID=checkers-clean-$(date -u +%Y%m%dT%H%M%SZ)
docker buildx bake --allow=fs.read="$CHECKERS_FIRMWARE" checkers
```

The Bake target uses a pinned Ubuntu base image, dated package snapshot, and Android `repo` launcher. It syncs the pinned LineageOS sources and Amazon-OSS patch repository, verifies and applies the required Amazon baseline followed by the camera patch series, extracts and verifies all proprietary files, runs `m bacon`, and exports only the finished artifacts from a `scratch` stage.

A full clean build, including source sync, compilation, packaging, export, and checksum verification, took **1 hour 18 minutes on an AMD Ryzen AI Max+ 395**. First-build time will vary with network and storage performance; this measurement does not represent an incremental rebuild.

Artifacts are written to `dist/$CHECKERS_BUILD_ID`:

- `checkers-lineage-18.1.zip`;
- `boot.img` and `recovery.img`;
- `manifest.xml` with every synced project revision;
- `PROPRIETARY_SHA256SUMS` identifying the exact blob set;
- `SHA256SUMS`.

See [installation and build instructions](docs/INSTALL.md) for the complete input, cache, and output contract.

## Repository contents

- [`Dockerfile`](Dockerfile) and [`docker-bake.hcl`](docker-bake.hcl) — isolated build environment and local artifact export
- [`patches/lineage-18.1`](patches/lineage-18.1) — ordered project-specific camera patch bundle and checksums
- [`manifests/amazon-oss-patches-lineage-18.1.tsv`](manifests/amazon-oss-patches-lineage-18.1.tsv) — exact Amazon baseline patch-to-project contract
- [`scripts`](scripts) — container build, Amazon and camera patch preflight/application, regeneration, and validation
- [`docs/PATCHES.md`](docs/PATCHES.md) — patch ownership and dependency map
- [`docs/CALIBRATION.md`](docs/CALIBRATION.md) — AWB defaults and maintainer calibration boundary
- [`docs/VALIDATION.md`](docs/VALIDATION.md) — build and on-device evidence
- [`tools/camhal-relay`](tools/camhal-relay) — standalone legacy Camera1 HAL relay and diagnostic toolkit

## Compatibility boundary

The Amazon baseline and camera patches target the exact LineageOS 18.1 bases in `manifests/amazon-oss-patches-lineage-18.1.tsv` and `patches/lineage-18.1/series.tsv`. The proprietary input contract is the Checkers blob set listed by `device/amazon/checkers/proprietary-files.txt`. The build rejects unknown source or patch-repository revisions, undeclared Amazon patches, patch mismatches, incomplete proprietary files, and missing output artifacts.

The patch bundle is the maintained distribution format. Upstream pull requests can be prepared later if the relevant maintainers request them.
