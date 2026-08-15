# MT8163 Camera1 HAL diagnostic relay

This harness loads the legacy 32-bit Camera1 HAL outside Android's camera service for bounded enumeration, parameter, capture, and preview-frame experiments on Checkers or Crown. It is a diagnostic tool; production uses each device's HIDL provider.

## Safety

MediaTek camera initialization can write calibration state. Use a rooted test device, preserve `/persist` and `/data/nvram`, and snapshot state around experiments:

```sh
adb root
adb wait-for-device
tools/camhal-relay/scripts/snapshot-device-camera-state.sh device-pre
```

Do not run the host harness while Android's camera provider owns the HAL.

## Build

Install Android NDK r27d or a compatible NDK:

```sh
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r27d
tools/camhal-relay/scripts/build-camhal-host.sh
```

The build produces the host executable, shared DpFramework/CMDQ shims, and Crown's optional imgsensor ABI translator. It pulls the connected device's current `libdpframework.so` unless `CAMHAL_DP_LIBRARY` points to an exact offline copy.

## Stage device libraries

The runner expects a private directory, defaulting to `/data/local/tmp/camprobe`, containing the selected device's camera closure and shared compatibility shims:

```text
vendor/lib/hw/camera.mt8163.so
system/lib/libsensor.so
libshim_graphic_buffer.so
libshim_mt8163_extra.so
```

The repository does not distribute proprietary libraries. Device-specific files must come from the corresponding firmware source; do not mix Checkers and Crown camera closures.

## Run bounded commands

```sh
tools/camhal-relay/scripts/run-camhal-host.sh enumerate 0
tools/camhal-relay/scripts/run-camhal-host.sh open 0
tools/camhal-relay/scripts/run-camhal-host.sh parameters 0
tools/camhal-relay/scripts/run-camhal-host.sh capture-one 0
```

| Variable | Default | Purpose |
|---|---|---|
| `CAMHAL_REMOTE_DIR` | `/data/local/tmp/camprobe` | Private device staging directory. |
| `CAMHAL_DP_MODE` | `android9` | Selects proxied Android 9 or staged Android 7 DpFramework. |
| `CAMHAL_IMGSENSOR_COMPAT` | `auto` | Enables Crown's imgsensor translator when `ro.product.device=crown`; use `none` for Checkers. |
| `CAMHAL_RELAY_SECONDS` | `20` | Maximum preview relay duration. |
| `CAMHAL_RELAY_FRAMES` | `120` | Maximum relayed frame count. |
| `CAMHAL_RELAY_PORT` | `57321` | Host TCP port forwarded to the device socket. |

## Receive preview frames

Start the relay:

```sh
CAMHAL_RELAY_SECONDS=20 CAMHAL_RELAY_FRAMES=120 \
  tools/camhal-relay/scripts/run-camhal-host.sh relay 0
```

Receive frames in another terminal:

```sh
tools/camhal-relay/scripts/receive-camhal-relay.py \
  --host 127.0.0.1 \
  --port 57321 \
  --output tools/camhal-relay/results/device-relay
```

The receiver stores NV21 frames and reports sequence gaps or malformed payloads. Keep result names device-specific; `results/` is ignored because it may contain device-derived data.
