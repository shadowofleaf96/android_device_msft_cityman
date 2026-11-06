#!/system/bin/sh
set -e
cp /vendor/lib/hw/camera.msm8994.so /data/local/tmp/camera.msm8994.so

# 1. Patch torch path at offset 247352 (39 bytes)
printf '/dev/flash_torch\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0' | dd of=/data/local/tmp/camera.msm8994.so bs=1 seek=247352 count=39 conv=notrunc

# 2. Patch sensor orientation (line 5330) at offset 298156: replace d0f8fc06 with 40f20e10
printf '\x40\xf2\x0e\x10' | dd of=/data/local/tmp/camera.msm8994.so bs=1 seek=298156 count=4 conv=notrunc

# 3. Patch getCamInfo orientation (line 5984) at offset 301496: replace d0f8fc06 with 40f20e10
printf '\x40\xf2\x0e\x10' | dd of=/data/local/tmp/camera.msm8994.so bs=1 seek=301496 count=4 conv=notrunc

# 4. Patch flashAvailable (line 5297) at offset 298082: replace 4258 with 0122 (movs r2, #1)
printf '\x01\x22' | dd of=/data/local/tmp/camera.msm8994.so bs=1 seek=298082 count=2 conv=notrunc

echo "Patch complete"
