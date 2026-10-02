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
#define LOG_TAG "android.hardware.vibrator@1.0-service.cityman"

#include <android/hardware/vibrator/1.0/IVibrator.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <utils/Errors.h>
#include <utils/StrongPointer.h>

#include "Vibrator.h"

using android::OK;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::vibrator::V1_0::IVibrator;
using android::hardware::vibrator::V1_0::implementation::Vibrator;

int main() {
    sp<Vibrator> vibrator = new Vibrator();
    if (!vibrator->usable()) {
        ALOGE("qpnp-haptic timed_output not usable; not registering IVibrator");
        return 1;
    }

    configureRpcThreadpool(1, true /* callerWillJoin */);
    if (vibrator->registerAsService() != OK) {
        ALOGE("Could not register android.hardware.vibrator@1.0::IVibrator");
        return 1;
    }
    joinRpcThreadpool();
    ALOGE("vibrator HIDL service exited");
    return 1;
}
