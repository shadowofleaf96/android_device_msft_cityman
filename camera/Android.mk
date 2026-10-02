LOCAL_PATH := $(call my-dir)
CITYMAN_CAM_PATH := $(LOCAL_PATH)

# mm-qcamera-daemon SensorName mot_imx230. No CameraId 1. No slave-id.
include $(CLEAR_VARS)
LOCAL_MODULE := msm8994_camera.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := msm8994_camera.xml
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_ETC)/camera
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := mot_imx230_chromatix.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := mot_imx230_chromatix.xml
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_ETC)/camera
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := msm8994_camera.xml.system
LOCAL_MODULE_STEM := msm8992_camera.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := msm8994_camera.xml
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/camera
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := mot_imx230_chromatix.xml.system
LOCAL_MODULE_STEM := mot_imx230_chromatix.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := mot_imx230_chromatix.xml
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/camera
include $(BUILD_PREBUILT)

# all-subdir-makefiles calls my-dir after BUILD_PREBUILT and fails kati.
# Pass this file's path explicitly.
ifneq ($(TARGET_BOARD_AUTO),true)
  ifneq ($(filter msm8992 msm8994,$(TARGET_BOARD_PLATFORM)),)
    ifneq ($(strip $(USE_CAMERA_STUB)),true)
      ifneq ($(BUILD_TINY_ANDROID),true)
        include $(call all-makefiles-under,$(CITYMAN_CAM_PATH))
      endif
    endif
  endif
endif
