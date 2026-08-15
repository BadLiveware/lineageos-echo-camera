# Patch architecture

The repository keeps shared MT8163/Lineage compatibility separate from device behavior. Each device owns an ordered `series.tsv` rooted at exact upstream commits:

- `patches/lineage-18.1/checkers/series.tsv`;
- `patches/lineage-18.1/crown/series.tsv`.

## Shared patches

| Android project | Responsibility |
|---|---|
| `hardware/amazon` | Adapts legacy camera code to the current libui boundary. |
| `hardware/lineage/interfaces` | Enables the MediaTek legacy preview-buffer adapter. |
| `device/amazon/mt8163-common` | Declares front-camera-only hardware. |
| `packages/apps/Camera2` | Prevents quality-zero JPEG recompression when no back camera exists. |

Shared payloads live in `patches/lineage-18.1/shared/` and are referenced directly by both device series.

## Checkers ownership

Checkers owns:

- its legacy provider and DpFramework compatibility integration;
- camera mount, capture rotation and flip behavior;
- AWB hooks and calibration defaults;
- Camera2 ultrawide preview layout and video-orientation offset.

Its proprietary and source contracts live under `manifests/checkers/`. `docs/CALIBRATION.md` documents the Checkers-only AWB behavior.

## Crown ownership

Crown owns:

- its legacy provider and camera compatibility library;
- legacy imgsensor ioctl translation to the verified v0.4 kernel ABI;
- system-Binder access required by `sensorservice`;
- disabling unusable temporal shading while preserving static lens shading;
- separate preview-port and still-capture rotation corrections;
- prepared-kernel integration for MT76x8 Wi-Fi and Bluetooth modules.

Its proprietary and source contracts live under `manifests/crown/`. Crown does not inherit Checkers AWB, capture-flip, or video-offset behavior.

## Series and scope invariants

Each `series.tsv` records:

```text
order  project_path  upstream_url  branch  base_commit  patch  scope
```

`scope` assigns source files to a patch generator. This lets the shared Camera2 JPEG fix and Checkers-only Camera2 UI/video changes remain separate even though they modify the same Android project.

The patch scripts enforce these invariants:

- every source project is at the recorded base and clean before application;
- repeated project entries use one base and apply sequentially;
- all payloads match `SHA256SUMS`;
- generated patches include only their declared source scope.

## Proprietary boundary

`vendor/amazon/checkers` and `vendor/amazon/crown` are generated from authorized local firmware sources and are not tracked. Each build selects only its device contract and verifies the extracted closure before compilation.
