#
# Copyright 2016 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

TARGET_USE_HWCOMPOSER_VHAL := true

ifeq ($(TARGET_USE_HWCOMPOSER_VHAL), true)

LOCAL_PATH := $(call my-dir)

#####################lib###########################
include $(CLEAR_VARS)

#TARGET_USES_HWC2 := false

#ENABLE_LAYER_DUMP := true
ENABLE_HWC_UIO := true
ENABLE_MULTI_DISPLAY := true

LOCAL_CFLAGS := -g -DLOG_TAG=\"hwc_vhal\" -g -Wno-missing-field-initializers -Wno-unused-parameter
LOCAL_CPPFLAGS := -g -std=c++11 -Wall -Werror -Wno-unused-parameter
LOCAL_LDFLAGS :=  -g

ifeq ($(TARGET_USES_HWC2), false)
LOCAL_SRC_FILES := \
        common/RemoteDisplay.cpp \
        common/RemoteDisplayMgr.cpp \
        hwc1/Hwc1Device.cpp \
        hwc1/Hwc1Display.cpp \

else
LOCAL_CPPFLAGS += \
	-DHWC2_INCLUDE_STRINGIFICATION \
	-DHWC2_USE_CPP11 \
        -DSUPPORT_HWC_2_0 \

ifneq ($(filter 8, $(PLATFORM_VERSION)),)
LOCAL_CPPFLAGS += \
        -DSUPPORT_HWC_2_1
endif
ifneq ($(filter 9, $(PLATFORM_VERSION)),)
LOCAL_CPPFLAGS += \
        -DSUPPORT_HWC_2_1 \
        -DSUPPORT_HWC_2_2
endif
ifneq ($(filter 10, $(PLATFORM_VERSION)),)
LOCAL_CPPFLAGS += \
        -DSUPPORT_HWC_2_1 \
        -DSUPPORT_HWC_2_2 \
        -DSUPPORT_HWC_2_3
endif
ifneq ($(filter 11, $(PLATFORM_VERSION)),)
LOCAL_CPPFLAGS += \
        -DSUPPORT_HWC_2_1 \
        -DSUPPORT_HWC_2_2 \
        -DSUPPORT_HWC_2_3 \
        -DSUPPORT_HWC_2_4
endif

LOCAL_SRC_FILES := \
        common/RemoteDisplay.cpp \
        common/RemoteDisplayMgr.cpp \
        common/LocalDisplay.cpp \
        common/BufferMapper.cpp \
        hwc2/Hwc2Device.cpp \
        hwc2/Hwc2Display.cpp \
        hwc2/Hwc2Layer.cpp \

endif

ifeq ($(ENABLE_LAYER_DUMP), true)
LOCAL_SRC_FILES += \
        common/BufferDumper.cpp \

LOCAL_CPPFLAGS += \
        -DENABLE_LAYER_DUMP=1

LOCAL_STATIC_LIBRARIES += libpng libz

endif

ifeq ($(ENABLE_HWC_UIO), true)
LOCAL_SRC_FILES += \
        uio/UioDisplay.cpp

LOCAL_CPPFLAGS += \
        -DENABLE_HWC_UIO

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/uio
endif

ifeq ($(ENABLE_MULTI_DISPLAY), true)
LOCAL_CPPFLAGS += \
        -DENABLE_MULTI_DISPLAY
endif

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH) \
        $(LOCAL_PATH)/common \

LOCAL_SHARED_LIBRARIES := \
        liblog \
        libcutils \
        libhardware \

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := hwcomposer.remote
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_RELATIVE_PATH := hw
include $(BUILD_SHARED_LIBRARY)

endif
