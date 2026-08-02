# Patch ownership and dependencies

The LineageOS 18.1 camera bring-up crosses five source projects. Each patch is rooted at an exact upstream commit and can be inspected or applied independently with ordinary Git tooling.

| Order | Android project | Responsibility |
|---:|---|---|
| 1 | `hardware/amazon` | Exposes the legacy libui wrapper behavior required by the vendor camera stack. |
| 2 | `hardware/lineage/interfaces` | Adds an opt-in MediaTek legacy preview-buffer adapter to the Lineage legacy camera provider/device implementation. |
| 3 | `device/amazon/mt8163-common` | Advertises the correct front-camera-only hardware feature. |
| 4 | `device/amazon/checkers` | Installs the provider, compatibility shims, mount/orientation behavior, proprietary-file manifest, SELinux label, and AWB defaults. |
| 5 | `packages/apps/Camera2` | Corrects the ultrawide preview layout and applies the device video-orientation offset. |

Exact upstream URLs, branches, bases, and patch filenames are machine-readable in `patches/lineage-18.1/series.tsv`.

## Proprietary vendor tree

`vendor/amazon/checkers` is deliberately absent from the patch series. Its camera closure consists of generated makefiles and proprietary binaries extracted from a compatible device or firmware source. Patch 4 updates the extraction manifest and makes `device/amazon/checkers/extract-files.sh SOURCE` regenerate only the Checkers vendor tree by default.

The local manifest separately supplies `vendor/amazon/mt8163-common`. That shared tree contains Karnak-derived platform blobs and the Checkers extraction wrapper preserves it automatically.

This keeps the public repository useful without redistributing additional Amazon or MediaTek binaries.

## Runtime architecture

The Android 7-era Camera1 blobs run behind a dedicated legacy HIDL provider on LineageOS 18.1. Compatibility code in the Checkers device tree handles three device-specific boundaries:

1. old DpIsp/DpBlit object sizes and Android 9 `libdpframework` calls;
2. the sensor's upside-down physical mount and output orientation;
3. final ISP AWB gain trimming through explicit PLT replacement of MediaTek `setAWBGain` call sites.

The AWB wrappers remain local ELF symbols. Exporting the proprietary C++ names causes Android's linker to preempt unresolved blob calls before the original targets are captured, which makes unity trim fail with a severe green cast. Explicit PLT replacement preserves the original call and avoids output-buffer compounding.

## Regenerating the bundle

`regenerate-patches.sh` uses an alternate Git index for every Android project. It captures committed, modified, and untracked non-ignored files relative to the recorded base without staging or changing the development checkout. New payloads and checksums are built in a sibling staging directory and replace the published bundle only after the complete series succeeds.

```bash
./scripts/regenerate-patches.sh /path/to/lineage-18.1-development-tree
./scripts/validate-patch-bundle.sh /path/to/lineage-18.1-development-tree
```

Update `series.tsv` first when an upstream base changes. Validation creates detached temporary worktrees at every recorded base and applies the generated patches there.
