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
- Headphone Jack (Audio playback and microphone)
- Built-in Microphones
- NFC (NXP PN547 stack)
- Sensors (Accel, Gyro, Mag, Light, Prox, Press, Temp)
- Bluetooth
- Bluetooth Headphones (A2DP Audio Routing)
- 12W Fast Charging


### Needs Testing

- Location (GPS)
- Hardware video decoding/encoding
- Bluetooth SCO Headset Microphone

### Not Working

- Cellular / RIL (Shows 'No Service', enabling mobile data says 'No SIM')
- Camera
- Others not listed as working

## Credits

Special thanks to **EpicLPer** for the TAS2552/3 speaker amplifier driver and Sensors and NFC patches for the Lumia 950 series!
