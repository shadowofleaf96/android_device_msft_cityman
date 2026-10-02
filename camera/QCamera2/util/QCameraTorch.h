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

#ifndef __QCAMERA_TORCH_H__
#define __QCAMERA_TORCH_H__

#include <stdint.h>

namespace qcamera {

/* cityman (Lumia 950 RM-1104): the torch is a plain GPIO LED (TLMM 12) that
 * the kernel exposes as /sys/class/leds/led:flash_torch. mm-camera has no
 * flash driver for it (no sky81296, no FlashName in msm8992_camera.xml), so
 * the sensor capability reports zero flash modes. This class drives that LED
 * directly from the HAL so both camera_module_t::set_torch_mode (Quick
 * Settings flashlight) and the HAL1 "flash-mode=torch" parameter work.
 *
 * Only led:flash_torch is written. led:torch_0 / led:torch_1 are the red
 * indicator channels on this board and must not be touched. */
class QCameraTorch {
public:
    /* true when the LED node exists and is writable by this process */
    static bool hasTorch();
    /* 0 on success, -errno on failure */
    static int32_t setTorch(bool on);
    static bool isTorchOn();
};

}; // namespace qcamera

#endif /* __QCAMERA_TORCH_H__ */
