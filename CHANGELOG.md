# Changelog

## [Unreleased]

### Added
- **RIL / Telephony (Spoofing)**: Implemented a custom `libril_wrapper.so` interceptor daemon.
  - Successfully intercepted QMI RIL communications to force the `gsm.sim.state` to `READY` and `LOADED`.
  - Spoofed the `RIL_REQUEST_VOICE_REGISTRATION_STATE` responses to return a registered state and hardcoded the MCC/MNC to 208/01 (Orange) to eliminate the "No Service" UI grey-out issue.
  - *Note: This is a UI/framework spoof only; the underlying baseband is still not scanning L1 WWAN due to missing Windows Phone DIAG NV initialization.*

- **Power Management (Sensors)**: Implemented a `libpm_wrapper.so` to shim power management calls.
  - Bypassed the restrictive `libvss_nv_core` and `libsensor1` crashes by dropping unsupported proprietary calls.

- **Camera Setup**: Brought up the IMX230 20MP sensor!
  - Reverted VFE (Video Front End) binding back to `vfe46` in the device tree as VFE44 was causing hardware timeouts.
  - Intercepted malicious Windows Camera HAL configuration blocks (`VFE_WRITE` and `VFE_WRITE_MB`) in `msm_isp_util.c` that were improperly overwriting the CAMIF geometry region (`0x3B4`) with a tiny 1215x1695 PDAF crop.
  - Implemented dynamic cache restoration for the CAMIF config registers (`0x3B4`, `0x3B8`, `0x3BC`) to preserve the correct 5344x4016 full-sensor output size.
  - [x] Front camera: Works.
  - [x] Rear camera: Fully functional (Viewfinder fixed). Fixed Windows Camera HAL schizophrenic geometry bug where 21MP frames were requested but 1695x1215 was provided to CAMIF without updating Stats engines.

- **Charging & Battery**: Enabled 13W fast charging support and fixed battery capacity reporting.
  - Integrated the correct Microsoft BVT4D 3340mAh battery capacity profile for the PMI8994 fuel gauge, fixing coulomb counter scaling and capacity readings.
  - Fixed a critical 1W fallback (discharging) loop by explicitly disabling the PMIC hardware pre-charge and fast-charge safety timers, which were improperly latching at 56s into boot.
  - Disabled the faulty parallel charger (`smb1357`) to eliminate I2C NACK interference.
  - Hardcapped charging at a safe 13W (2.6A max input) to preserve battery health.

- **NFC Support**: Successfully brought up the NXP NFC stack for Lumia 950 XL!
  - Switched to the NXP-specific NFC HAL (`nfc_nci_nxp`) and included `android.hardware.nfc@1.2-service`.
  - Configured `BoardConfig.mk` to use `TARGET_USES_NQ_NFC` and `BOARD_NFC_CHIPSET := pn54x`.
  - Fixed an infinite crash loop in the NXP PN547 chip initialization by disabling incompatible `NXP_RF_CONF_BLK` settings inherited from Nexus 5X in `libnfc-nxp.conf`.
  - Configured correct `nfc:nfc` permissions for the `/dev/pn547` device node via `ueventd`.

- **Sensors**: Fixed device sensor initialization!
  - Eliminated the `dlopen` multi-HAL fallback error (`library "sensors.cityman.so" not found`) by removing the conflicting `hals.conf` configuration file from the build. This allows the HIDL service to load the native `sensors.cityman.so` HAL directly.
  - Added correct `system:system` ueventd permissions for `/dev/i2c-4` and `/dev/i2c-7` to allow the sensor HAL to communicate with the physical hardware chips.


- Audio support! Safely ported TAS2553 speaker amplifier driver from Lumia 950 (cityman) to Lumia 950 XL (cityman).
  - Routed Quaternary MI2S backend to properly bypass the internal WCD9330 and utilize the external smart amplifier.
  - Adapted I2S pinctrl and I2C settings for the msm8994 platform.
  - Carefully rewired `mixer_paths.xml` to strip dangerous motherboard-frying controls and mix stereo channels into Mono for the loudspeaker.
  - Fixed jack switch polarity detection.
  - **Bluetooth Audio**: Fixed A2DP media routing to Bluetooth headphones by switching the `android.hardware.bluetooth.audio` HAL from `passthrough` to `hwbinder` transport, allowing proper IPC between `audioserver` and the Bluetooth stack.

- **Call Audio Routing**: Fixed voice call audio routing for earpiece, speakerphone, wired earphones, and Bluetooth SCO.
  - Removed erroneous `backend="speaker"` and `interface="QUAT_MI2S_RX"` overrides on `SND_DEVICE_OUT_HANDSET` and `SND_DEVICE_OUT_VOICE_HANDSET` in `audio_platform_info.xml`, allowing the earpiece to route to WCD9330 (`SLIMBUS_0_RX`) instead of failing.
  - Added missing `voice-call speaker`, `voice2-call speaker`, `volte-call speaker`, and `compress-voip-call speaker` paths to `mixer_paths.xml` using `QUAT_MI2S_RX_Voice Mixer CSVoice/Voice2/VoLTE/Voip` and `Voice_Tx Mixer SLIM_0_TX_Voice`.
  - Added missing `voice-call speaker-and-headphones`, `voice-call speaker-and-bt-sco`, and wideband SCO combo paths.
  - Fixed wired headphone/earphone call downlink by feeding `SLIM_0_RX` to `RX1` and `RX2` in `voice-headphones`.
  - Fixed Bluetooth SCO call audio by configuring explicit sample rates (`8000` and `16000`) and standardizing backend tags.
  - Restored `SLIMBUS_0_RX Audio Mixer MultiMedia` controls on base playback paths (`deep-buffer-playback`, `low-latency-playback`, `audio-ull-playback`, and `compress-offload-playback1-9`) so in-call dialer DTMF and ringback tones play without backend DAI errors.


### Acknowledgments

- **EpicLPer**: Thank you for the heavy lifting in porting the TAS2552/3 ASoC driver and discovering the Quaternary MI2S layout for the Lumia 950!
