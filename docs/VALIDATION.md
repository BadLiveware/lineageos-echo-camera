# Validation record

The patch bundle was validated against the exact LineageOS 18.1 bases in `series.tsv` and on a physical Checkers device.

## Patch integrity

- Amazon-OSS's required patch repository is pinned at `e2060e7985fdf61ebd0663f170aed8609dbb5e1e`.
- Its complete six-patch inventory is declared in `manifests/amazon-oss-patches-lineage-18.1.tsv` and applies cleanly at every recorded LineageOS base.
- All five camera patch checksums in `patches/lineage-18.1/SHA256SUMS` pass.
- Every camera patch applies cleanly in a detached worktree at its recorded base commit.
- Camera patch regeneration covers both committed development changes and untracked non-ignored source files through alternate Git indexes.
- Proprietary binaries are not present in the patch bundles.

Run the same detached-worktree check with:

```bash
./scripts/validate-patch-bundle.sh /path/to/lineage-18.1
```

## Build validation

The final device-tree state passed:

```text
m libcheckers_dpframework_compat
m systemimage
```

A fresh dedicated Buildx builder completed the full pinned container workflow in 1 hour 18 minutes. The resulting OTA was:

```text
checkers-amazon-patches-20260802T174641Z/checkers-lineage-18.1.zip
SHA-256: 899be1a95a124b31d85bf3001ee45cdb5850c8f94612e2fe910db17fb9f4a6ce
```

All exported checksums passed. The exported manifest records the Amazon patch revision, and the OTA's `libinputreader.so` contains the `touch.orientation` and `ORIENTATION_90` implementation required by the Checkers IDC.

The generated `system/build.prop` contained all five `persist.vendor.camera.awbtrim.*` defaults. The deployed compatibility library matched the built library byte-for-byte by SHA-256.

## AWB runtime validation

The final ISP-only interposer reported:

```text
trim cool=509/512 warm=509/512 generation=2 trace=0
legacy ISP sensor=1 raw=1094/512/719 trim=509/512 output=1087/512/719
```

Three clean post-reboot neutral-grid captures produced these central-patch means:

| Input level | R-G | B-G |
|---:|---:|---:|
| 32 | +12.71 | +3.80 |
| 64 | -0.64 | -1.53 |
| 128 | +0.40 | -0.13 |
| 192 | -1.07 | -1.82 |

Across levels 64-192, the mean absolute red/blue residual was 0.931 8-bit code values. The unity comparison measured 1.039 code values under the same grid workflow, an absolute reduction of 0.108 code values (10.4%). Spatial and run-to-run variation is larger than this small global adjustment, so the result supports a conservative default rather than a broad color-quality claim.

The level-32 red residual remains a known shadow-path limitation and was not compensated with AWB gain.

## Camera modes and VACA motion detection

The production Camera1 path accepted every tested resolution and frame-rate mode, including the sensor-native 1280×720 output. Delivered rates were measured for five seconds after a 1.5-second settle period; each mode also returned a correctly sized NV21 frame with full luma range.

| Requested mode | Accepted parameters | Frames | Delivered fps | Error from target |
|---|---|---:|---:|---:|
| 1280×720 fixed 30, baseline | 1280×720, 30–30 | 138 | 27.742 | −7.53% |
| 1280×720 variable 5–30 | 1280×720, 5–30 | 140 | 28.083 | n/a |
| 1280×720 fixed 15 | 1280×720, 15–15 | 74 | 14.805 | −1.30% |
| 1280×720 fixed 10 | 1280×720, 10–10 | 50 | 9.997 | −0.03% |
| 1280×720 fixed 6 | 1280×720, 6–6 | 30 | 6.008 | +0.13% |
| 1280×720 fixed 30, repeat | 1280×720, 30–30 | 140 | 27.986 | −6.71% |
| 640×480 fixed 30, baseline | 640×480, 30–30 | 148 | 29.634 | −1.22% |
| 640×480 fixed 10 | 640×480, 10–10 | 50 | 9.996 | −0.04% |
| 320×240 variable 5–30 | 320×240, 5–30 | 150 | 30.002 | n/a |
| 320×240 fixed 30 | 320×240, 30–30 | 148 | 29.629 | −1.24% |
| 320×240 fixed 15 | 320×240, 15–15 | 75 | 15.012 | +0.08% |
| 320×240 fixed 10 | 320×240, 10–10 | 50 | 10.015 | +0.15% |
| 320×240 fixed 6 | 320×240, 6–6 | 30 | 6.000 | 0.00% |
| 176×144 fixed 10 | 176×144, 10–10 | 50 | 10.003 | +0.03% |
| 176×144 fixed 6 | 176×144, 6–6 | 30 | 5.999 | −0.02% |
| 640×480 fixed 30, repeat | 640×480, 30–30 | 149 | 29.846 | −0.51% |

At 1280×720, fixed 6, 10, and 15 fps requests delivered within 1.3% of target. Fixed 30 fps delivered 27.742–27.986 fps, 6.71–7.53% below target, so native-resolution operation is confirmed but exact 30 fps delivery is not.

VACA 0.12.1 motion detection was visually confirmed working through the production camera stack. CameraX selected an exact 320×240 YUV `ImageAnalysis` stream, and VACA processes at most one frame every 250 ms for an intended maximum analysis rate of approximately 4 fps.

The motion-analysis input is correct, but the upstream camera path is not optimized for the same workload: the sensor uses 1280×720, the legacy HAL uses 640×360 with a 5–30 fps range and nominal 30 fps rate, and CameraX leaves its target frame-rate range unspecified. VACA therefore discards most upstream frames rather than reducing sensor and ISP work.

## Runtime health

After deployment and reboot:

- InputReader reported `Orientation: ORIENTATION_90` and `SurfaceOrientation: 1` for the Goodix touchscreen;
- physical touchscreen behavior was confirmed across the display;
- Camera2 held an active camera client and produced a working preview;
- the camera crash log buffer was empty;
- the final library hash matched the system-image output;
- AWB properties survived reboot;
- the severe green-cast prototype regression was absent.

The relay toolkit has separate operational and resource-recovery checks in `tools/camhal-relay/README.md`.
