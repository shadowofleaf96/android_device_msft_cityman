# cityman Vulkan (Adreno 418)

Lumia 950 RM-1104. SoC MSM8992. GPU Adreno 418.

Vocabulary follows the parent README (ASD-STE100 style).

---

## Purpose

This directory holds on-device Vulkan 1.0 probes for cityman.

`cityman-vk-probe` shows the HAL and the `libvulkan` loader.
`cityman-vk-tri` does a window-less offscreen CLEAR + `vkCmdDraw` and host readback.

The work is Vulkan **1.0** only. It does not turn on HWUI Vulkan.

---

## Progress (2026-09-05)

Enumerate **works** (n=1). The physical device is Adreno (TM) 418, `apiVersion` **1.0.49** (`0x00400031`).

Measured logs:

| Log | What it shows |
|---|---|
| `out/qa-vulkan-probe.log` | HAL and loader 1.0: n=1 |
| `out/qa-vulkan-tri.log` | Offscreen CLEAR+draw; pixels `ff 00 ff ff`. **No present** |
| `out/qa-vulkan-tri.ppm` | Pulled 64×64 PPM from `/data/local/tmp/cityman-vk-tri.ppm` |
| `out/qa-vulkan-clear.log` | `vkCmdClearColorImage` readback **RGBA 0,255,0,255** (GPU write) |
| `out/qa-vulkan-vkjson.json` | `devices: 1` after the `same_process_hal_file` label |
| `out/qa-vulkan-present.log` | `vkQueuePresentKHR` **VK_SUCCESS** FIFO 1440×2504 ~60 fps |
| `out/qa-vulkan-copy.log` | `vkCmdCopyImageToBuffer` honors `bufferRowLength` |
| `out/qa-vulkan-map.log` | HOST_VISIBLE type 2 map/unmap match |

ICD path that `libvulkan` LoadDriver finds:

- File: `/vendor/lib64/vulkan.msm8992.so`
- SELinux: `u:object_r:same_process_hal_file:s0`

LoadDriver searches the basename `vulkan.msm8992.so` under `/vendor/lib64`. It does not search `/vendor/lib64/hw`. Without that copy and that label, `cmd gpu vkjson` reports 0 devices.

HAL `CreateInstance` with API **1.1** returns `VK_ERROR_INCOMPATIBLE_DRIVER`. Do not advertise Vulkan 1.1.

HWUI stays **GLES**. This blob has no `AHardwareBuffer` import and no Vulkan 1.1. Leave `ro.hwui.use_vulkan` unset. Android 11 Skia Vulkan stops below instance 1.1.

Offscreen GPU write **works**. Present **works** on a dedicated SurfaceView (`cityman-vk-present`, magenta-pink clear, not SystemUI). HWUI stays GLES.

---

## Changes

Build from the LOS 18.1 tree (`lunch lineage_cityman-userdebug`):

```
mka cityman-vk-probe cityman-vk-tri
```

Outputs:

- `out/target/product/cityman/system/bin/cityman-vk-probe`
- `out/target/product/cityman/system/bin/cityman-vk-tri`

These are **system** binaries. A boot-only flash does not install them. Put them on the telephone with `adb push`:

```
adb push out/target/product/cityman/system/bin/cityman-vk-probe /data/local/tmp/
adb push out/target/product/cityman/system/bin/cityman-vk-tri /data/local/tmp/
adb shell /data/local/tmp/cityman-vk-probe
adb shell /data/local/tmp/cityman-vk-tri
```

`cityman-vk-probe`, `cityman-vk-probe32`, and `cityman-vk-tri` are in `PRODUCT_PACKAGES` (`cityman-vulkan.mk`). A later system image installs them. Until then, `adb push`.

`cityman-vulkan.mk` also copies the ICD to `/vendor/lib64/vulkan.msm8992.so`. Feature XML stays `android.hardware.vulkan.version-1_0_3`. Do not set `ro.hwui.use_vulkan`.
