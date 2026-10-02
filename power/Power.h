/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ANDROID_HARDWARE_POWER_V1_0_POWER_H
#define ANDROID_HARDWARE_POWER_V1_0_POWER_H

#include <android/hardware/power/1.0/IPower.h>
#include <hidl/Status.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "HintConfig.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {

using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

class Power : public IPower {
public:
  Power();
  ~Power();

  bool usable() const { return mUsable; }

  Return<void> setInteractive(bool interactive) override;
  Return<void> powerHint(PowerHint hint, int32_t data) override;
  Return<void> setFeature(Feature feature, bool activate) override;
  Return<void>
  getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) override;

private:
  using clock = std::chrono::steady_clock;

  void commitLocked();
  int applyDeltaLocked(const cityman::power::HintDelta &d);
  void armTimerLocked();
  void timerLoop();
  const cityman::power::HintDelta *hintNamed(const char *name) const;

  cityman::power::PowerHintConfig mCfg;
  bool mUsable = false;
  bool mInteractive = true;
  bool mLowPower = false;
  bool mSustained = false;
  bool mVr = false;
  bool mVideoEncode = false;
  bool mLaunch = false;
  bool mInteraction = false;
  bool mStop = false;

  clock::time_point mLaunchUntil = clock::time_point::max();
  clock::time_point mInteractionUntil = clock::time_point::max();

  std::mutex mLock;
  std::condition_variable mCv;
  std::thread mTimer;
};

} // namespace implementation
} // namespace V1_0
} // namespace power
} // namespace hardware
} // namespace android

#endif
