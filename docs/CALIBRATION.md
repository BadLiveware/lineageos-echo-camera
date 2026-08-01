# AWB calibration boundary

The ROM ships a small global red correction and unity blue correction. The values use Q9 fixed-point gain units where `512` is 1.0x.

| Property | Default | Multiplier |
|---|---:|---:|
| `persist.vendor.camera.awbtrim.r` | 509 | 0.9941x |
| `persist.vendor.camera.awbtrim.b` | 512 | 1.0000x |
| `persist.vendor.camera.awbtrim.r.warm` | 509 | 0.9941x |
| `persist.vendor.camera.awbtrim.b.warm` | 512 | 1.0000x |
| `persist.vendor.camera.awbtrim.trace` | 0 | logging disabled |

The interposer reads these properties periodically while the camera is active. Red and blue trims interpolate between cool and warm endpoints using the vendor AWB result's normalized blue gain. Values outside the accepted range `128..2048`, malformed values, and missing properties fail back to unity.

## What ships

Only the runtime interposer and default coefficients are part of the ROM patches. Monitor grids, captures, analysis scripts, sensor-register probes, and calibration logs are maintainer tools rather than runtime dependencies.

The defaults were measured on one physical Checkers unit. They are intentionally conservative: neutral midtones were already close to balanced, and a large global correction would hide neither spatial shading nor black-level behavior.

## Maintainer override

A rooted development build can change the properties without rebuilding:

```bash
adb root
adb shell setprop persist.vendor.camera.awbtrim.r 509
adb shell setprop persist.vendor.camera.awbtrim.b 512
adb shell setprop persist.vendor.camera.awbtrim.r.warm 509
adb shell setprop persist.vendor.camera.awbtrim.b.warm 512
```

Enable bounded diagnostic logging only while measuring:

```bash
adb shell setprop persist.vendor.camera.awbtrim.trace 1
adb logcat -s CheckersCameraAwb:V
adb shell setprop persist.vendor.camera.awbtrim.trace 0
```

## Interpretation limit

A channel cast confined to very dark patches is not evidence for a global AWB multiplier. The calibration run retained a red lift at input level 32 while levels 64-192 remained near neutral. That behavior belongs to the sensor black-level, shading, or downstream tone path and should be investigated separately.
