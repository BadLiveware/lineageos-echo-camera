# Build the Checkers LineageOS image

The supported build runs entirely through Docker Buildx Bake. The host supplies Docker, disk space, and a lawful local firmware dump; the container supplies the LineageOS source checkout and build dependencies.

## Prerequisites

- Docker Engine with the `docker buildx` plugin;
- a trusted local Linux AMD64 Docker builder;
- at least 200 GB available in Docker's storage;
- a compatible Checkers system dump.

The firmware path must be an extracted directory with this file:

```text
system/vendor/lib/hw/camera.mt8163.so
```

The build does not copy the firmware dump into the exported artifact stage. BuildKit sends named contexts to the selected builder, so use a trusted local builder and never select a remote or shared builder for this build.

## Build

Create a dedicated builder backed by the local Docker Engine:

```bash
docker buildx create \
  --name checkers-builder \
  --driver docker-container \
  --driver-opt image=moby/buildkit:v0.31.2@sha256:63db51c9b30208a7c2b1c40392c7ebb9ce2f85ba238a18a85420f8f5ea2d4684 \
  --use unix:///var/run/docker.sock
docker buildx inspect --bootstrap
```

The inspection must show the `docker-container` driver and `unix:///var/run/docker.sock` endpoint. From the repository root, export a new build ID and run the Bake target:

```bash
export CHECKERS_FIRMWARE=/absolute/path/to/checkers-system-dump
export CHECKERS_BUILD_ID=checkers-clean-$(date -u +%Y%m%dT%H%M%SZ)
docker buildx bake --allow=fs.read="$CHECKERS_FIRMWARE" checkers
```

The filesystem entitlement grants the selected local builder read access only to the firmware directory. A new build ID creates a new BuildKit cache and output directory; use both a new builder and build ID for a clean-room validation build.

The target performs the complete workflow:

1. initializes LineageOS 18.1 from the repository's fully revision-locked manifest;
2. syncs every source project and the Amazon-OSS patch repository at their recorded commits;
3. verifies and applies all six required Amazon-OSS compatibility patches;
4. verifies and applies the five project-specific camera patches;
5. extracts the Checkers proprietary files without replacing the shared MT8163 vendor tree;
6. verifies every proprietary file referenced by both vendor trees;
7. runs `lunch lineage_checkers-userdebug` and `m bacon`;
8. exports the OTA, partition images, pinned manifest, and checksums from a `scratch` stage.

Any failed step stops the build. Do not repair files inside the BuildKit cache; fix the manifest, patch bundle, extraction input, or container definition and run with a new build ID.

## Output

The local exporter writes to:

```text
dist/$CHECKERS_BUILD_ID/
```

Expected files:

```text
OTA_SOURCE_NAME
PROPRIETARY_SHA256SUMS
SHA256SUMS
boot.img
checkers-lineage-18.1.zip
manifest.xml
recovery.img
```

Verify the exported files before installation:

```bash
cd dist/$CHECKERS_BUILD_ID
sha256sum -c SHA256SUMS
```

`checkers-lineage-18.1.zip` is the OTA package. Its compressed block payload carries the system partition; the Checkers `bacon` target does not emit a separate top-level `system.img`. `OTA_SOURCE_NAME` records the package's original generated filename. `PROPRIETARY_SHA256SUMS` records the exact proprietary payload accepted by the build.

After exporting and verifying the artifacts, remove the dedicated builder to delete its source cache and local firmware context:

```bash
docker buildx rm checkers-builder
```
