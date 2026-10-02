# Changelog

## [Unreleased]

### Added

- **Cellular Network & RIL Bringup**: Full, verified carrier registration, SMS, USSD, and voice calling.
  - Resolved repetitive modem SSR (SubSystem Restart) crashes by discovering that the Qualcomm LTE-Coexistence interface (`/dev/smd_cxm_qmi`) and related QMI commands triggered internal firmware watchdog panics on Lumia modem images; implemented proactive filtering in `libril_wrapper.so`.
  - Developed and integrated `lumia-modem-init` initialization daemon to sequence modem bootup, verify interface nodes (`/dev/subsys_modem`, `/dev/smd0`, `/sys/devices/soc/soc:qcom,smd-cxm/cxm_ready`), and monitor modem stability.
  - Achieved real carrier registration on live networks across LTE, UMTS, and GSM (`mVoiceRegState=0(IN_SERVICE)`, `mDataRegState=0(IN_SERVICE)`).
  - Enabled and verified Circuit-Switched Fallback (CSFB) transitioning seamlessly from LTE to 3G/WCDMA during voice calls and restoring LTE upon hangup.
  - Verified bidirectional SMS transmit/receive and interactive multi-step USSD sessions (e.g. `*111#` carrier balance and service portals).

- **Call Audio Routing**: Comprehensive voice call audio routing across all endpoints.
  - Corrected `audio_platform_info.xml` by removing erroneous `backend="speaker"` and `interface="QUAT_MI2S_RX"` overrides on `SND_DEVICE_OUT_HANDSET` and `SND_DEVICE_OUT_VOICE_HANDSET`, restoring direct routing to the WCD9330 TomTom codec (`SLIMBUS_0_RX`) with digital microphone uplink (`voice-dmic-ef`).
  - Added missing `voice-call speaker`, `voice2-call speaker`, `volte-call speaker`, and `compress-voip-call speaker` mixer paths in `mixer_paths.xml` utilizing `QUAT_MI2S_RX_Voice Mixer CSVoice/Voice2/VoLTE/Voip` and `Voice_Tx Mixer SLIM_0_TX_Voice` for loudspeaker voice calling via the TI TAS2553 amplifier.
  - Added combo paths for `voice-call speaker-and-headphones`, `voice-call speaker-and-bt-sco`, and wideband SCO.
  - Fixed wired headphone/earphone call downlink by routing `SLIM_0_RX` inputs (`AIF1_PB`) to `RX1` and `RX2` in `voice-headphones` alongside `SLIM_5_RX`, ensuring downlink audio reaches both earphone channels.
  - Configured explicit sample rates (`8000 Hz` narrowband, `16000 Hz` wideband) and direct mixer controls for Bluetooth SCO calling.
  - Restored `SLIMBUS_0_RX Audio Mixer MultiMedia` controls on all base playback paths (`deep-buffer-playback`, `low-latency-playback`, `audio-ull-playback`, and `compress-offload-playback1-9`) to resolve `ASoC: no backend DAIs enabled` kernel errors, enabling in-call dialpad DTMF tones and ringback audio.

- **Audio Playback & Speaker Support**: Safely ported TAS2553 speaker amplifier driver from Lumia 950 (`talkman`) to Lumia 950 XL (`cityman`).
  - Routed Quaternary MI2S backend to properly bypass the internal WCD9330 and utilize the external smart amplifier.
  - Adapted I2S pinctrl and I2C settings for the msm8994 platform.
  - Carefully rewired `mixer_paths.xml` to strip dangerous motherboard-frying controls and mix stereo channels into Mono for the loudspeaker.
  - Fixed 3.5mm jack switch polarity detection.
  - **Bluetooth Audio**: Fixed A2DP media routing to Bluetooth headphones by switching the `android.hardware.bluetooth.audio` HAL from `passthrough` to `hwbinder` transport, allowing proper IPC between `audioserver` and the Bluetooth stack.
  - Remapped `dmic1`-`dmic4` (Digital Microphones) to their correct hardware nodes in `mixer_paths.xml` (fixing a wrong mapping to `DMIC6`).
  - Tuned digital microphone gain (`DEC` volume) from `110` (+26dB) down to `98` (+14dB) to eliminate static noise while preserving recording sensitivity.

- **Camera Bringup (Work In Progress)**:
  - Reverted VFE (Video Front End) binding back to `vfe46` in the device tree as VFE44 was causing hardware timeouts.
  - Intercepted Windows Camera HAL configuration blocks (`VFE_WRITE` and `VFE_WRITE_MB`) in `msm_isp_util.c` that were improperly overwriting the CAMIF geometry region (`0x3B4`) with a tiny 1215x1695 PDAF crop.
  - Implemented dynamic cache restoration for CAMIF config registers (`0x3B4`, `0x3B8`, `0x3BC`) to preserve the correct 5344x4016 full-sensor output size.
  - Enabled triple-LED torch and flash control.
  - *Note*: Rear camera sensor (IMX230) initializes and binds to ISP hardware, but preview viewfinder currently renders a black screen. Front-facing camera and Iris scanner are not working.

- **Power Management & Charging**: Enabled 13W fast charging support and fixed battery capacity reporting.
  - Integrated the correct Microsoft BV-T4D 3340mAh battery capacity profile for the PMI8994 fuel gauge, fixing coulomb counter scaling and capacity readings.
  - Fixed a critical 1W fallback (discharging) loop by explicitly disabling the PMIC hardware pre-charge and fast-charge safety timers, which were improperly latching at 56s into boot.
  - Disabled the faulty parallel charger (`smb1357`) to eliminate I2C NACK interference.
  - Hardcapped charging at a safe 13W (2.6A max input) to preserve battery health.

- **NFC Support**: Successfully brought up the NXP NFC stack for Lumia 950 XL!
  - Switched to the NXP-specific NFC HAL (`nfc_nci_nxp`) and included `android.hardware.nfc@1.2-service`.
  - Configured `BoardConfig.mk` to use `TARGET_USES_NQ_NFC` and `BOARD_NFC_CHIPSET := pn54x`.
  - Fixed an infinite crash loop in the NXP PN547 chip initialization by disabling incompatible `NXP_RF_CONF_BLK` settings inherited from Nexus 5X in `libnfc-nxp.conf`.
  - Configured correct `nfc:nfc` permissions for the `/dev/pn547` device node via `ueventd`.

- **Sensors**: Fixed device sensor initialization!
  - Eliminated the `dlopen` multi-HAL fallback error (`library "sensors.cityman.so" not found`) by removing the conflicting `hals.conf` configuration file from the build, allowing the HIDL service to load native `sensors.cityman.so` directly.
  - Added correct `system:system` ueventd permissions for `/dev/i2c-4` and `/dev/i2c-7` to allow the sensor HAL to communicate with the physical hardware chips.

---

### Acknowledgments

- **EpicLPer**: Thank you for the heavy lifting in porting the TAS2552/3 ASoC driver, discovering the Quaternary MI2S layout, and pioneering initial sensor/NFC research on Lumia 950!
- **LineageOS & CAF Community**: Platform development and Qualcomm MSM8994 reference implementations.
