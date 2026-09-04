# Changelog

## [Unreleased]
### Added
- Audio support! Safely ported TAS2553 speaker amplifier driver from Lumia 950 (talkman) to Lumia 950 XL (cityman).
  - Routed Quaternary MI2S backend to properly bypass the internal WCD9330 and utilize the external smart amplifier.
  - Adapted I2S pinctrl and I2C settings for the msm8994 platform.
  - Carefully rewired `mixer_paths.xml` to strip dangerous motherboard-frying controls and mix stereo channels into Mono for the loudspeaker.
  - Fixed jack switch polarity detection.

### Acknowledgments
- **EpicLPer**: Thank you for the heavy lifting in porting the TAS2552/3 ASoC driver and discovering the Quaternary MI2S layout for the Lumia 950!
