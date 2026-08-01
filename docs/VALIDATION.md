# Validation record

The patch bundle was validated against the exact LineageOS 18.1 bases in `series.tsv` and on a physical Checkers device.

## Patch integrity

- All five patch checksums in `patches/lineage-18.1/SHA256SUMS` pass.
- Every patch applies cleanly in a detached worktree at its recorded base commit.
- Application validation covers both committed development changes and untracked non-ignored source files captured through alternate Git indexes.
- Proprietary binaries are not present in the bundle.

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

## Runtime health

After deployment and reboot:

- Camera2 held an active camera client;
- the camera crash log buffer was empty;
- the final library hash matched the system-image output;
- AWB properties survived reboot;
- the severe green-cast prototype regression was absent.

The relay toolkit has separate operational and resource-recovery checks in `tools/camhal-relay/README.md`.
