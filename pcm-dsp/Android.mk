LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libmt8163_stereo_effect
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_SRC_FILES := android/effect_library.cpp src/engine.cpp
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    hardware/libhardware/include \
    system/media/audio/include \
    system/core/libcutils/include
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -Werror -fvisibility=hidden
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_SHARED_LIBRARY)
