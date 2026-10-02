/*
 * Copyright (C) 2017 The LineageOS Project
 * Copyright (C) 2018 The Android Open Source Project
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
#ifndef ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
#define ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H

#include <android/hardware/light/2.0/ILight.h>
#include <hidl/Status.h>

#include <pthread.h>

#include <mutex>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::light::V2_0::Flash;
using ::android::hardware::light::V2_0::ILight;
using ::android::hardware::light::V2_0::LightState;
using ::android::hardware::light::V2_0::Status;
using ::android::hardware::light::V2_0::Type;

class Light : public ILight {
  public:
    Light();
    ~Light();

    bool usable() const { return mBacklightOk; }

    Return<Status> setLight(Type type, const LightState& state) override;
    Return<void> getSupportedTypes(getSupportedTypes_cb _hidl_cb) override;

  private:
    struct LedConfig {
        unsigned int colorRgb = 0;
        int onMs = 0;
        int offMs = 0;
    };

    Status setBacklight(const LightState& state);
    Status setRgbLight(const LightState& state, int type);
    Status setAttention(const LightState& state);
    Status setLightLocked(const LightState& state, int type);
    int writeLedsLocked(const LedConfig* led);
    int writeRgb(int red, int green, int blue);
    void rgbClaimTriggers();
    void stopBlinkLocked();
    void blinkLoop();
    void sleepBlinkMs(int ms);

    static void* blinkThreadEntry(void* arg);

    std::mutex mLock;
    bool mBacklightOk = false;
    bool mRgbOk = false;
    LedConfig mLeds[3];
    int mCurLed = -1;
    LedConfig mBlinkLed;
    pthread_t mBlinkThread = {};
    bool mBlinkThreadStarted = false;
    bool mBlinkRunning = false;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
