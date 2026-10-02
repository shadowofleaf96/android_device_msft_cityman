/* Copyright (c) 2026, archienz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "QCameraTorch"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utils/Log.h>

#include "QCameraTorch.h"

namespace qcamera {

static const char TORCH_BRIGHTNESS[] =
        "/sys/class/leds/led:flash_torch/brightness";
static const char TORCH_MAX_BRIGHTNESS[] =
        "/sys/class/leds/led:flash_torch/max_brightness";

static int readInt(const char *path, int def)
{
    char buf[16] = {0};
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return def;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return def;
    }
    return atoi(buf);
}

bool QCameraTorch::hasTorch()
{
    if (access(TORCH_BRIGHTNESS, W_OK) != 0) {
        ALOGV("%s: %s not writable: %s", __func__, TORCH_BRIGHTNESS,
                strerror(errno));
        return false;
    }
    return readInt(TORCH_MAX_BRIGHTNESS, 0) > 0;
}

int32_t QCameraTorch::setTorch(bool on)
{
    char buf[16];
    int level = on ? readInt(TORCH_MAX_BRIGHTNESS, 255) : 0;
    int fd = open(TORCH_BRIGHTNESS, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        int err = errno;
        ALOGE("%s: open %s failed: %s", __func__, TORCH_BRIGHTNESS,
                strerror(err));
        return -err;
    }
    int len = snprintf(buf, sizeof(buf), "%d\n", level);
    ssize_t n = write(fd, buf, len);
    int err = (n < 0) ? errno : 0;
    close(fd);
    if (n < 0) {
        ALOGE("%s: write %s failed: %s", __func__, TORCH_BRIGHTNESS,
                strerror(err));
        return -err;
    }
    ALOGI("%s: led:flash_torch brightness=%d", __func__, level);
    return 0;
}

bool QCameraTorch::isTorchOn()
{
    return readInt(TORCH_BRIGHTNESS, 0) > 0;
}

}; // namespace qcamera
