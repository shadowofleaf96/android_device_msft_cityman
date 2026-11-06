#!/system/bin/sh
set -e
cp /vendor/lib64/libril_wrapper.so /data/local/tmp/libril_wrapper.so

# 1. Place string "14\0" at offset 0x1574 (5492)
printf '14\0' | dd of=/data/local/tmp/libril_wrapper.so bs=1 seek=5492 count=3 conv=notrunc

# 2. VOICE_REG: Point x8 to 0x1574 (offset within page 0x574: 0x91155d08 -> 08 5d 15 91) at file offset 10312 (0x2848)
printf '\x08\x5d\x15\x91' | dd of=/data/local/tmp/libril_wrapper.so bs=1 seek=10312 count=4 conv=notrunc

# 3. DATA_REG: Inject strings[3] = "14" at file offset 10560 (0x2940)
# e9 ff ff f0 (adrp x9, -1 page = 0x1000)
# 29 d1 15 91 (add x9, x9, #0x574)
# a9 0e 00 f9 (str x9, [x21, #24])
printf '\xe9\xff\xff\xf0\x29\xd1\x15\x91\xa9\x0e\x00\xf9' | dd of=/data/local/tmp/libril_wrapper.so bs=1 seek=10560 count=12 conv=notrunc

# 4. SIGNAL_STRENGTH: Inject signal strength metrics at file offset 9934 (0x26ce)
# cbz x20, +48 (94 01 00 b4)
# mov w8, #22 (c8 02 80 52)
# str w8, [x20, #0] (88 02 00 b9)
# str wzr, [x20, #4] (9f 06 00 b9)
# str w8, [x20, #28] (88 1e 00 b9)
# mov w8, #85 (a8 0a 80 52)
# str w8, [x20, #32] (88 22 00 b9)
# mov w8, #8 (08 01 80 52)
# str w8, [x20, #36] (88 26 00 b9)
# mov w8, #200 (08 19 80 52)
# str w8, [x20, #40] (88 2a 00 b9)
# mov w8, #15 (e8 01 80 52)
# str w8, [x20, #44] (88 2e 00 b9)
# mov w19, #0 (13 00 80 52)
printf '\x94\x01\x00\xb4\xc8\x02\x80\x52\x88\x02\x00\xb9\x9f\x06\x00\xb9\x88\x1e\x00\xb9\xa8\x0a\x80\x52\x88\x22\x00\xb9\x08\x01\x80\x52\x88\x26\x00\xb9\x08\x19\x80\x52\x88\x2a\x00\xb9\xe8\x01\x80\x52\x88\x2e\x00\xb9\x13\x00\x80\x52' | dd of=/data/local/tmp/libril_wrapper.so bs=1 seek=9934 count=56 conv=notrunc

echo "RIL patch complete"
