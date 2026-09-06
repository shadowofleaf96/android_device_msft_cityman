# Microsoft Lumia 950 XL (cityman)

## LineageOS 18.1 Port Status

### What is Working

- Touchscreen Display
- Brightness control
- Rooted Debugging (Enabled by default)
- Baseband & IMEI detection (SIM Toolkit shows up, but RIL is not fully functional)
- MTP File Transfer (Fixed greyed-out USB preferences menu)
- Wi-Fi (2.4GHz and 5GHz fully working)
- Audio (Speaker via TAS2553 on Quaternary MI2S works!)
- Headphone Jack
- NFC (NXP PN547 stack)
- Sensors (Accel, Gyro, Mag, Light, Prox, Press, Temp)

### Needs Testing

- Location (GPS)
- Bluetooth
- Hardware video decoding/encoding

### Not Working

- Cellular / RIL (Shows 'No Service', enabling mobile data says 'No SIM')
- Camera
- Others not listed as working

## Credits

Special thanks to **EpicLPer** for the TAS2552/3 speaker amplifier driver and ASoC patches for the Lumia 950 series!
