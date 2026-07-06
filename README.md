# Microsoft Lumia 950 XL (cityman)

## LineageOS 18.1 Port Status

### What is Working
- Touchscreen Display
- Brightness control
- Rooted Debugging (Enabled by default)
- Baseband & IMEI detection (SIM Toolkit shows up, but RIL is not fully functional)
- MTP File Transfer (Fixed greyed-out USB preferences menu)
- Wi-Fi (2.4GHz and 5GHz fully working, optimized for 1x1 hardware limits to avoid AP rejections)

### Needs Testing
- NFC
- Location (GPS)
- Bluetooth
- Hardware video decoding/encoding

### Not Working
- Audio
- Cellular / RIL (Shows 'No Service', enabling mobile data says 'No SIM')
- Camera
- Sensors
- Others not listed as working
