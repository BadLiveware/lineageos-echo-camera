# Validation status

Validation is reported per device. Shared code does not imply shared runtime acceptance.

## Crown

Crown photo mode was validated on physical hardware using the canonical Soong-built provider, compatibility library, and Camera2 APK.

| Area | Result |
|---|---|
| Enumeration | One front-facing camera is exposed through the legacy HIDL provider. |
| Provider startup | Starts without blocking on `sensorservice`. |
| Preview | Live, correctly colored, upright, and conventionally mirrored. |
| Still capture | Upright, unmirrored, and saved successfully. |
| JPEG output | Full 1280×720 output without quality-zero corruption. |
| Orientation metadata | Provider reports orientation `0`; preview and still transforms remain separate. |
| Artifact integrity | Installed binaries matched their Soong-built SHA-256 hashes. |

The final verification capture was 1280×720, used top-left JPEG orientation, and was visually upright. ImageMagick estimated JPEG quality 84 for the HAL-encoded output.

Video recording was not part of Crown's final acceptance run and remains unverified.

## Checkers

The combined-repository Checkers build was rebuilt and validated on physical hardware as installed build `eng.root.20260814.231444`.

| Area | Result |
|---|---|
| Preview | Live and upright. |
| Still capture | Upright 1280×720 JPEG with EXIF orientation 1. |
| Video | Seven-second 1280×720 H.264/AAC recording renders upright with the expected `-180°` display matrix. |
| Service stability | Camera provider, `cameraserver`, and Camera2 remained alive without camera crash, ANR, or provider-death markers. |
| Touch mapping | Correct 90° coordinate transform. |

The Bluetooth framework restart loop reproduced on this build was also present in earlier Checkers runtime evidence and is outside the camera validation.

## Structural validation

Verify payload hashes:

```sh
cd patches/lineage-18.1
sha256sum -c SHA256SUMS
```

Validate a selected series against a matching Android source checkout:

```sh
scripts/validate-patch-bundle.sh checkers /path/to/lineage-18.1
scripts/validate-patch-bundle.sh crown /path/to/lineage-18.1
```

The validator applies repeated entries for the same Android project sequentially in an isolated worktree, which checks the combined shared and device-specific result rather than each patch in isolation.

## Local evidence

Ignored evidence is separated by device naming:

- `dist/checkers-*` and `dist/crown-*` for build exports;
- `tools/camhal-relay/results/` for device logs, frames, and state snapshots.

These paths may contain proprietary or device-derived material and are intentionally excluded from Git.
