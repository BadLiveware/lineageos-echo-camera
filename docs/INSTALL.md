# Build LineageOS camera support

The container pipeline pins Android source revisions, applies the selected device's ordered patch series, verifies proprietary inputs, and exports reproducible artifacts.

## Prepare firmware input

Use an authorized firmware dump for the selected device. Preserve the original directory layout and a `SHA256SUMS` file covering the source:

```text
<device>-system-dump/
├── SHA256SUMS
└── system/
    └── vendor/
        └── lib/
            └── hw/
                └── camera.mt8163.so
```

If `/vendor` is exposed separately, place its content below `system/vendor` in the dump.

## Create the builder

```sh
docker buildx create \
  --name lineageos-camera-builder \
  --driver docker-container \
  --platform linux/amd64 \
  --use
docker buildx inspect --bootstrap
```

The Buildx cache contains the LineageOS checkout and compiled objects. Device build IDs intentionally select separate caches.

## Checkers build

```sh
export CHECKERS_FIRMWARE=/absolute/path/to/checkers-system-dump
export CHECKERS_BUILD_ID=checkers-$(date -u +%Y%m%dT%H%M%SZ)

docker buildx bake \
  --allow=fs.read="$CHECKERS_FIRMWARE" \
  --builder lineageos-camera-builder \
  checkers
```

The complete package is exported under `dist/$CHECKERS_BUILD_ID`.

## Crown builds

```sh
export CROWN_FIRMWARE=/absolute/path/to/crown-system-dump
export CROWN_BUILD_ID=crown-$(date -u +%Y%m%dT%H%M%SZ)
```

Checksum review only:

```sh
docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown-checksums
```

Provider, compatibility library, and Camera2 APK:

```sh
docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown-modules
```

Complete LineageOS package:

```sh
docker buildx bake --allow=fs.read="$CROWN_FIRMWARE" \
  --builder lineageos-camera-builder crown
```

## Pipeline guarantees

Both pipelines:

1. initialize the device's pinned LineageOS 18.1 manifest;
2. restore every cached source project to its recorded revision;
3. validate and apply the shared Amazon compatibility patches;
4. validate and apply the selected device series;
5. verify the device's proprietary closure;
6. build the selected product in `userdebug` mode.

Crown additionally supports a checksum-only review and a focused module build. Its full build reproduces the verified Crown kernel and checks boot, recovery, target-files, and OTA image identity.

## Outputs

A complete build exports the OTA, boot and recovery images, source/proprietary provenance, pinned manifest, and `SHA256SUMS`. Crown module builds export:

```text
android.hardware.camera.provider@2.4-service.crown
libcrown_camera_compat.so
Camera2.apk
MODULE_BUILD_RESULT
FIRMWARE_MANIFEST_SHA256
OUTPUT_SHA256SUMS
SOURCE_INPUT_MAP.tsv
manifest.xml
```

Verify any export containing `SHA256SUMS` before installation:

```sh
cd dist/<build-id>
sha256sum -c SHA256SUMS
```

Building does not authorize deployment. Installation modifies device partitions and should use the device's established recovery or rooted-ADB procedure only after artifact review.
