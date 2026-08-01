# Checkers camera relay toolkit

This maintainer tool runs the stock Checkers Camera1 HAL from `/data/local/tmp/camprobe` without installing a camera provider or modifying System/Vendor. It captures 640x480 NV21 frames through the Android 9-compatible `libdpframework` bridge.

Run the commands below from `tools/camhal-relay`.

## Capture one frame

Prerequisites:

- rooted ADB access to the device;
- `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` pointing to an installed NDK;
- the stock blob tree staged under `/data/local/tmp/camprobe`;
- the device-specific graphics and sensor shims from the workspace setup.

```bash
CAMHAL_DP_MODE=android9 ./scripts/run-camhal-host.sh capture-one 0
adb pull /data/local/tmp/camprobe/frame-640x480.nv21 results/frame.nv21

ffplay -f rawvideo -pixel_format nv21 -video_size 640x480 results/frame.nv21
```

The build script reads `/system/vendor/lib/libdpframework.so` from the connected device so the compatibility shim links against the exact runtime library. For an offline rebuild, set `CAMHAL_DP_LIBRARY` to a previously pulled exact copy.

## Run the bounded frame relay

Terminal 1 starts the HAL and creates an ADB forward. By default it must deliver 120 frames before the 20-second deadline on TCP port 57321. Reaching the deadline with fewer frames is a failed run, so the producer and receiver cannot report contradictory success.

```bash
CAMHAL_RELAY_FRAMES=120 \
CAMHAL_RELAY_SECONDS=20 \
CAMHAL_RELAY_PORT=57321 \
./scripts/run-camhal-host.sh relay 0
```

Terminal 2 receives framed NV21 payloads:

```bash
./scripts/receive-camhal-relay.py \
  --port 57321 \
  --frames 120 \
  --output-dir results/relay
```

The device keeps at most one partially transmitted frame. Socket writes are nonblocking; if the pending packet cannot advance, the next camera callback is dropped rather than blocking the HAL. `dropped_frames` in each header is cumulative and includes frames produced before a client connects. Start the receiver promptly enough to meet the configured deadline.

### Relay wire format

Each frame is a packed 44-byte little-endian header followed by `payload_bytes` of NV21 data.

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `uint32` | Magic `CRLY` (`0x594c5243`) |
| 4 | `uint16` | Protocol version, currently 1 |
| 6 | `uint16` | Header size, currently 44 |
| 8 | `uint64` | Delivered-frame sequence |
| 16 | `uint64` | Monotonic timestamp in nanoseconds |
| 24 | `uint32` | Width, currently 640 |
| 28 | `uint32` | Height, currently 480 |
| 32 | `uint32` | Pixel format `NV21` (`0x3132564e`) |
| 36 | `uint32` | Payload size, currently 460800 bytes |
| 40 | `uint32` | Cumulative dropped-frame count |

## Validate powered-down state

Capture state before and after each operation class:

```bash
./scripts/snapshot-device-camera-state.sh results/check-pre
# Run capture-one or relay.
./scripts/snapshot-device-camera-state.sh results/check-post

diff -u results/check-pre.nvram.sha256 results/check-post.nvram.sha256
```

A recovered device should show:

- no `camhal_host` process;
- no open descriptors for `/dev/camera-*`, `/dev/kd_camera_hw`, ISP, or SENINF devices;
- `img_cam_*`, `img_sen_cam`, `mm_cam_mdp`, `camtg_sel`, and `scam_sel` clock counts at zero;
- the `isp` generic power domain as `off-0`;
- no relay socket and no relay ADB forward.

Compare regulator snapshots rather than requiring every camera-named regulator to read zero: `vcamaf` has a device baseline user independent of this fixed-focus sensor.

## Stop and clean up

The runner removes its ADB forward and relay socket on normal exit, timeout, or shell interruption. If the runner itself is killed externally, remove only the relay runtime state manually:

```bash
adb shell 'pkill -9 camhal_host 2>/dev/null || true; rm -f /data/local/tmp/camprobe/camrelay.sock'
adb forward --remove tcp:57321 2>/dev/null || true
```

Remove the entire disposable device experiment only after preserving any wanted frames or logs:

```bash
adb shell 'rm -rf /data/local/tmp/camprobe'
```

No System/Vendor cleanup is required because these scripts do not install files there.

## Restore persistent camera state

The verified rollback point is:

```text
backups/checkers-20260731-152344/
```

Verify it on the host before use:

```bash
cd backups/checkers-20260731-152344
sha256sum -c SHA256SUMS
```

Restore only from recovery with Android and NVRAM services stopped. Preserve the current state first. A typical recovery sequence is:

```bash
adb push data-nvram.tar /tmp/data-nvram.tar
adb push persist.img /tmp/persist.img
adb shell

mount /data
mv /data/nvram /data/nvram.pre-camera-restore
mkdir -p /data/nvram
tar -xpf /tmp/data-nvram.tar -C /data
restorecon -RF /data/nvram
sync
```

Restore the separate Persist partition only when its contents are known to be the problem:

```bash
# Recovery only. Confirm /dev/block/by-name/persist identifies the 16 MiB partition.
dd if=/tmp/persist.img of=/dev/block/by-name/persist bs=4M conv=fsync
sync
```

Do not write `persist.img` while Android is running. Keep `/data/nvram.pre-camera-restore` until the restored system has booted and its NVRAM hashes, Wi-Fi/Bluetooth identity, and camera enumeration have been verified.
