# Talkman sensors HAL

Userspace I²C HAL for the buses enabled in
`android_kernel_mmo_msm8994` (`lineage-18.1-talkman`).

| Sensor | Chip | Path |
|---|---|---|
| Accel / gyro | ICM-20648-class @ `0x68` | `/dev/i2c-4` |
| Magnetometer | AK09912 @ `0x0c` | `/dev/i2c-4` |
| Pressure / die temp | ZPA2326 @ `0x5c` | `/dev/i2c-4` |
| Proximity / light | QPDS-T900 (APDS-9930) @ `0x39` | `/dev/i2c-7` |
| Hall (front / back) | GPIO 42 / 75 | sysfs |

`ro.hardware=talkman`, so this builds as `sensors.talkman.so`.
VINTF already lists `android.hardware.sensors@1.0` as passthrough;
`android.hardware.sensors@1.0-impl` wraps this module in-process.

HAL 1.11 subtracts still-detected gyro bias and a magnetometer
calibration (least-squares sphere hard-iron, then a symmetric 3×3
soft-iron ellipsoid). Cal file is `/data/misc/talkman-sensors/cal.txt`
(`v4`). Mag accuracy is field quality only, not motion.

Do not enable `sensors.qcom` or the leftover `sensorhal/` tree.
Those are bullhead ADSP / nanohub leftovers and do not match this
hardware.

Gravity, linear acceleration, rotation vector, and the other
fusion types are produced by Android from accel + gyro + mag.
They are not extra chips.
