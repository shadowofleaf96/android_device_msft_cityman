# Changelog

## [Unreleased]

### Added
- **Charging**: Enabled 12W fast charging support.

- **NFC Support**: Successfully brought up the NXP NFC stack for Lumia 950 XL!
  - Switched to the NXP-specific NFC HAL (`nfc_nci_nxp`) and included `android.hardware.nfc@1.2-service`.
  - Configured `BoardConfig.mk` to use `TARGET_USES_NQ_NFC` and `BOARD_NFC_CHIPSET := pn54x`.
  - Fixed an infinite crash loop in the NXP PN547 chip initialization by disabling incompatible `NXP_RF_CONF_BLK` settings inherited from Nexus 5X in `libnfc-nxp.conf`.
  - Configured correct `nfc:nfc` permissions for the `/dev/pn547` device node via `ueventd`.

- **Sensors**: Fixed device sensor initialization!
  - Eliminated the `dlopen` multi-HAL fallback error (`library "sensors.cityman.so" not found`) by removing the conflicting `hals.conf` configuration file from the build. This allows the HIDL service to load the native `sensors.cityman.so` HAL directly.
  - Added correct `system:system` ueventd permissions for `/dev/i2c-4` and `/dev/i2c-7` to allow the sensor HAL to communicate with the physical hardware chips.


- Audio support! Safely ported TAS2553 speaker amplifier driver from Lumia 950 (talkman) to Lumia 950 XL (cityman).
  - Routed Quaternary MI2S backend to properly bypass the internal WCD9330 and utilize the external smart amplifier.
  - Adapted I2S pinctrl and I2C settings for the msm8994 platform.
  - Carefully rewired `mixer_paths.xml` to strip dangerous motherboard-frying controls and mix stereo channels into Mono for the loudspeaker.
  - Fixed jack switch polarity detection.
  - **Bluetooth Audio**: Fixed A2DP media routing to Bluetooth headphones by switching the `android.hardware.bluetooth.audio` HAL from `passthrough` to `hwbinder` transport, allowing proper IPC between `audioserver` and the Bluetooth stack.

- **Audio Routing**: Fixed extensive audio routing issues for headphones and microphones.
  - Corrected headphone playback routing to use `SLIMBUS_5_RX` instead of the speaker's backend.
  - Restored missing `headphones`, `headset`, and `bt-sco` device mappings to `audio_platform_info.xml`.
  - Remapped `dmic1`-`dmic4` (Digital Microphones) to their correct hardware nodes in `mixer_paths.xml` (fixing a wrong mapping to `DMIC6`).
  - Tuned the digital microphone gain (`DEC` volume) from `110` (+26dB) down to `98` (+14dB) to eliminate excessive static noise while maintaining optimal recording volume.
  - Restored `AUDIO_OUTPUT_FLAG_FAST` flags in `audio_policy_configuration.xml`.

### Acknowledgments

- **EpicLPer**: Thank you for the heavy lifting in porting the TAS2552/3 ASoC driver and discovering the Quaternary MI2S layout for the Lumia 950!
