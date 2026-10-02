/*
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "Vibrator"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/vibrator.h>

/*
 * PMI8994 haptic@c000 (qcom,qpnp-haptic). mmo/common/haptics.dtsi status=okay,
 * ERM, vmax 2300 mV (WOA HWN1 HAPC 0xE6).
 * WOA HWN1 HID QCOM24A4 (qchwnhaptics8994.inf), ACPI VIB1 SPMI 0xC000.
 * qpnp-haptic.c timed_dev.name = "vibrator"
 *   → /sys/class/timed_output/vibrator/enable
 * mmo_defconfig: CONFIG_QPNP_HAPTIC=y, CONFIG_QPNP_VIBRATOR is not set.
 */

static const char VIB_ENABLE[] = "/sys/class/timed_output/vibrator/enable";

static int vib_write_timeout(unsigned int timeout_ms) {
  int fd = TEMP_FAILURE_RETRY(open(VIB_ENABLE, O_RDWR));
  if (fd < 0) {
    ALOGE("open %s failed (%s)", VIB_ENABLE, strerror(errno));
    return -errno;
  }

  char value[20];
  int nwr = snprintf(value, sizeof(value), "%u\n", timeout_ms);
  int ret = TEMP_FAILURE_RETRY(write(fd, value, nwr));
  int saved = errno;
  close(fd);

  if (ret < 0) {
    ALOGE("write %s failed (%s)", VIB_ENABLE, strerror(saved));
    return -saved;
  }
  return ret == nwr ? 0 : -EIO;
}

static int vibra_exists(void) {
  int fd = TEMP_FAILURE_RETRY(open(VIB_ENABLE, O_RDWR));
  if (fd < 0) {
    ALOGE("%s missing (%s)", VIB_ENABLE, strerror(errno));
    return 0;
  }
  close(fd);
  return 1;
}

static int vibra_on(vibrator_device_t *vibradev __unused,
                    unsigned int timeout_ms) {
  return vib_write_timeout(timeout_ms);
}

static int vibra_off(vibrator_device_t *vibradev __unused) {
  return vib_write_timeout(0);
}

static int vibra_close(hw_device_t *device) {
  free(device);
  return 0;
}

static int vibra_open(const hw_module_t *module, const char *id __unused,
                      hw_device_t **device) {
  if (!vibra_exists())
    return -ENODEV;

  vibrator_device_t *vibradev = calloc(1, sizeof(*vibradev));
  if (!vibradev)
    return -ENOMEM;

  vibradev->common.tag = HARDWARE_DEVICE_TAG;
  vibradev->common.module = (hw_module_t *)module;
  vibradev->common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
  vibradev->common.close = vibra_close;
  vibradev->vibrator_on = vibra_on;
  vibradev->vibrator_off = vibra_off;

  *device = (hw_device_t *)vibradev;
  return 0;
}

static struct hw_module_methods_t vibrator_module_methods = {
    .open = vibra_open,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = VIBRATOR_API_VERSION,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = VIBRATOR_HARDWARE_MODULE_ID,
    .name = "Cityman QPNP haptic",
    .author = "The Android Open Source Project",
    .methods = &vibrator_module_methods,
};
