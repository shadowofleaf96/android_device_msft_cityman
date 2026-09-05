# cityman power / thermal-engine

Snappiness is configured here in the device tree, not in the kernel
sources and not with Magisk.

`init.cityman.rc` (`on boot`) runs `/vendor/bin/init.cityman.power.sh`,
which writes kernel sysfs (interactive governor, `cpu_boost`, HMP
up/down migrate, kgsl min/default pwrlevel). Max CPU frequencies stay
1.55 GHz (A53) / 1.95 GHz (A57).

`device.mk` already ships `android.hardware.power-service-qti` (HIDL
Power HAL). The current prebuilt LGE `vendor.img` does not contain that
service, so a ROM rebuild is what actually installs it.

## Why thermal-engine died on the prebuilt vendor

CAF `thermal-engine` has a DT_NEEDED on `libqti-perfd-client.so`. That
library is **not** in the cityman LGE vendor image, so the daemon never
started:

```
CANNOT LINK EXECUTABLE "/vendor/bin/thermal-engine":
library "libqti-perfd-client.so" not found
```

`proprietary-blobs.txt` already lists `/system/bin/perfd` and
`/system/bin/thermal-engine`. It does **not** list the client library,
and the LGE dump does not provide it either.

The in-tree stub (`libqti-perfd-client/`) is enough for thermal-engine
to start. Cityman's `thermal-engine-8994.conf` applies CPU/GPU/LCD
policy through ioctl/sysfs; `perf_lock_use_profile` is optional and the
stub returns -1 ("no profile").

## Optional msm8994 blobs (not in this git tree)

Do **not** commit Qualcomm binaries here. If you want real MP-CTL
perflocks (Power HAL launch/interaction hints), copy the **msm8994 /
Android 7–8** blobs from a similar device (like angler).

If those files are present in the vendor extract, drop
`libqti-perfd-client` from `PRODUCT_PACKAGES` so the stub and the blob
do not collide as the same make module.

Socket after `perfd` starts: `/data/misc/perfd/mpctl`.
