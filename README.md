# LineageOS camera support for Amazon MT8163 devices

This repository packages revision-pinned camera bring-up and reproducible LineageOS 18.1 builds for two Amazon Echo Show devices:

- **Checkers** — Echo Show 5;
- **Crown** — Echo Show 8.

The devices share the legacy Camera1/libui/Lineage preview compatibility layer and front-camera-only JPEG handling. Device directories isolate their provider integration, orientation behavior, proprietary contracts, manifests, kernel requirements, and documentation.

## Support status

Crown photo mode is validated on physical hardware: the provider starts, preview has correct color and orientation, the conventional front-camera preview remains mirrored, and 1280×720 still captures are upright, unmirrored, and free from quality-zero JPEG corruption.

The existing Checkers patch and build path is preserved by the restructuring. It was not rebuilt or revalidated on hardware during the Crown bring-up; do not treat Crown validation as Checkers validation.

## Repository layout

```text
patches/lineage-18.1/
├── shared/       # patches used by both devices
├── checkers/     # Checkers-only provider and Camera2 behavior
└── crown/        # Crown-only provider and kernel-module behavior

manifests/
├── shared/       # common Amazon compatibility patch contract
├── checkers/     # Checkers source and proprietary contracts
└── crown/        # Crown source and proprietary contracts
```

Each device has an ordered `series.tsv` that references shared patches first and device-specific patches second. Build caches and output directories are separate.

## Prerequisites

- Docker with Buildx and the container driver;
- an amd64 Linux builder;
- enough storage for a LineageOS 18.1 checkout and build cache;
- an authorized local firmware dump for the selected device, including its preserved `SHA256SUMS`.

Create a reusable builder:

```sh
docker buildx create \
  --name lineageos-camera-builder \
  --driver docker-container \
  --platform linux/amd64 \
  --use
docker buildx inspect --bootstrap
```

## Build Checkers

```sh
export CHECKERS_FIRMWARE=/absolute/path/to/checkers-system-dump
export CHECKERS_BUILD_ID=checkers-$(date -u +%Y%m%dT%H%M%SZ)

docker buildx bake \
  --allow=fs.read="$CHECKERS_FIRMWARE" \
  --builder lineageos-camera-builder \
  checkers
```

Artifacts are exported to `dist/$CHECKERS_BUILD_ID`.

## Build Crown

```sh
export CROWN_FIRMWARE=/absolute/path/to/crown-system-dump
export CROWN_BUILD_ID=crown-$(date -u +%Y%m%dT%H%M%SZ)
```

Review proprietary inputs, build focused camera modules, or build the complete package:

```sh
docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown-checksums

docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown-modules

docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown
```

Outputs use `dist/$CROWN_BUILD_ID-proprietary-review`, `dist/$CROWN_BUILD_ID-modules`, and `dist/$CROWN_BUILD_ID`.

## Documentation

- `docs/INSTALL.md` — firmware inputs, build commands, and artifacts;
- `docs/PATCHES.md` — shared and device-specific ownership;
- `docs/VALIDATION.md` — per-device validation status and limitations;
- `docs/CALIBRATION.md` — Checkers AWB calibration reference;
- `docs/CROWN_KERNEL_REPRODUCIBILITY.md` — Crown exact-kernel constraints;
- `tools/camhal-relay/README.md` — low-level Camera1 HAL diagnostics.

Proprietary Amazon and MediaTek binaries are not tracked. They must come from an authorized local firmware source and are verified before use.
