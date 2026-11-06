#!/system/bin/sh
# Patch libril_wrapper.so:
# 1. Promote VOICE_REG unconditionally (NOP out b.ne and cbz)
printf '\x1f\x20\x03\xd5' | dd of=/vendor/lib64/libril_wrapper.so bs=1 seek=$((0x27f0)) count=4 conv=notrunc
printf '\x1f\x20\x03\xd5' | dd of=/vendor/lib64/libril_wrapper.so bs=1 seek=$((0x27fc)) count=4 conv=notrunc

# 2. Promote DATA_REG and set g_data_registered = 1
printf '\xe8\xff\xff\xd0\x08\xbd\x3d\x91\xa8\x02\x00\xf9\x29\x00\x80\x52\x08\x00\x00\xf0\x09\x41\x0a\x39' | dd of=/vendor/lib64/libril_wrapper.so bs=1 seek=$((0x2910)) count=24 conv=notrunc

chmod 644 /vendor/lib64/libril_wrapper.so
echo "PATCH_APPLIED"
