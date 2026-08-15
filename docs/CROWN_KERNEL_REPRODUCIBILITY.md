# Crown kernel reproducibility

The Crown v0.4 kernel can be rebuilt deterministically in the pinned container:

```bash
docker build -f Dockerfile.crown-kernel -t crown-kernel-repro:ubuntu20 .
mkdir -p "$PWD/dist/crown-kernel"
docker run --rm \
  -v "$PWD/dist/crown-kernel:/artifacts" \
  crown-kernel-repro:ubuntu20 /artifacts
```

The build pins the Ubuntu base image and package snapshot, Amazon kernel revision, Linaro toolchain revision, historical absolute source path, Android `userdebug` build variant, kernel build metadata, SCM suffix, DTB order, and DTB alignment. `build-provenance.txt`, `provenance.json`, and `SHA256SUMS` describe the result, including any optional patch digest and resulting source tree and diff digests. The mounted output directory must be dedicated to the build and empty at startup; the entrypoint refuses to delete existing contents.

## Reproducibility result

Two clean container runs produced byte-identical copies of:

- `Image.gz-dtb`
- `vmlinux`
- `crown.config`
- provenance files

The deterministic unmodified kernel has SHA-256:

```text
c6b47cc5e291a282eb1fe092753cfff2ec917dbb2e039849e0849b4281166de3  Image.gz-dtb
```

## Crown v0.4 comparison

The verified Crown v0.4 release kernel has SHA-256:

```text
c6b47cc5e291a282eb1fe092753cfff2ec917dbb2e039849e0849b4281166de3  kernel
```

The container reproduces the verified Crown v0.4 kernel byte-for-byte.

Two implicit Android build inputs accounted for the earlier mismatch:

- `TARGET_BUILD_VARIANT=userdebug` enables the MediaTek GED user feature set. Omitting it changed GED code and shifted downstream linked addresses.
- The embedded default initramfs records `Thu Apr 16 11:40:01 CEST 2026`, while the final kernel identity records `Wed Jun 24 02:19:46 CEST 2026`. Generating the initramfs before switching to the final compile timestamp reproduces both values.

With the Android build variant restored, all 41,163 common embedded kallsyms addresses matched the release. Only the GNU build ID and default-initramfs timestamp remained different; restoring the initramfs timestamp made the final compressed kernel and appended DTBs byte-identical.

## Crown camera ABI findings

Do not make `CONFIG_CROWN` include `kd_imgsensor_define_checkers.h`. That header also replaces internal sensor structures; both test kernels failed before Android boot and were rolled back to v0.4.

The actionable mismatch is at the 32-bit HAL boundary:

- legacy `KDIMGSENSORIOC_X_GETINFO` supplies 144-byte sensor-info buffers;
- the v0.4 kernel writes its 216-byte default structure, corrupting userspace; the HAL then faults at address `0x24`;
- `libcrown_imgsensor_compat.so` translates GETINFO/GETINFO2 buffers, MCLK payloads, scenario IDs, and legacy feature IDs without changing the kernel;
- Crown's Lineage workspace has no usable TSF calibration; leaving temporal shading enabled crashes `libcamalgo.so` in `TsfCore::rfk_coef_1d`, so the same shim disables TSF while retaining static lens shading.

With the shim and the consistent Crown stock camera blob closure, enumeration, open, parameters, preview start/stop, and a 640×480 NV21 capture pass on the unmodified v0.4 kernel. A subsequent bounded relay delivered all 120 requested 460800-byte frames in 20 seconds. Post-run state matched the pre-run camera descriptors, clocks, power domain, NVRAM hashes, relay socket, and ADB forwards.

After each reboot, restart adbd as root before using the maintainer relay; an unprivileged shell cannot open the camera device nodes and reports zero cameras.

## Legacy boot image note

Crown uses Android boot image header v0. Its release `mkbootimg` SHA-1 includes the legacy trailing `dt_size` field. Modern host `mkbootimg` may omit it. `scripts/repack-crown-boot.py` preserves that calculation and refuses non-v0 templates, non-zero trailers, or output larger than the template's size budget. Repacking the original v0.4 kernel and ramdisk with this helper reproduces the verified release boot image byte-for-byte.
