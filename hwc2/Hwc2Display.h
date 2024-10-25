/*
Copyright (C) 2021 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.


SPDX-License-Identifier: Apache-2.0

Author:
Xue Yifei (yifei.xue@intel.com)
Mao Marc (marc.mao@intel.com)
Ren Chenglei (chenglei.ren@intel.com)
Tang Shaofeng (shaofeng.tang@intel.com)
Date: 2021.06.09

*/

#ifndef __HWC2_DISPLAY_H__
#define __HWC2_DISPLAY_H__

#include <map>
#include <memory>
#include <vector>
#include <optional>

#include <hardware/hwcomposer2.h>

#include "Hwc2Layer.h"
#include "IRemoteDevice.h"
#include "display_protocol.h"

#ifdef ENABLE_HWC_UIO
#include "UioDisplay.h"
#endif

//TODO: include HWC3 headers
struct ClockMonotonicTimestamp {
    int64_t timestampNanos;
};
typedef enum {
    HWC3_FUNCTION_SET_EXPECTED_PRESENT_TIME = HWC2_FUNCTION_GET_LAYER_GENERIC_METADATA_KEY + 1,
} hwc3_function_descriptor_t;
typedef int32_t /*hwc_error_t*/ (*HWC3_PFN_SET_EXPECTED_PRESENT_TIME)(hwc2_device_t* device,
        hwc2_display_t display, const std::optional<ClockMonotonicTimestamp>& expectedPresentTime);


class RemoteDisplay;

class Hwc2Display : public DisplayEventListener {
 public:
  Hwc2Display(hwc2_display_t id);
  virtual ~Hwc2Display();

  int width() const { return mWidth; }
  int height() const { return mHeight; }
  bool attachable() const { return !mRemoteDisplay; }
  int attach(RemoteDisplay* rd);
  int detach(RemoteDisplay* rd);

  // DisplayEventListener
  int onBufferDisplayed(const buffer_info_t& info) override;
  int onPresented(std::vector<layer_buffer_info_t>& layerBuffer,
                  int& fence) override;

  hwc2_display_t getDisplayID() const { return mDisplayID; }
  Hwc2Layer& getLayer(hwc2_layer_t l) { return mLayers.at(l); }

  void dump();

  // HWC Hooks
  HWC2::Error acceptChanges();
  HWC2::Error createLayer(hwc2_layer_t* layer);
  HWC2::Error destroyLayer(hwc2_layer_t layer);
  HWC2::Error getActiveConfig(hwc2_config_t* config);
  HWC2::Error getChangedCompositionTypes(uint32_t* numElements,
                                         hwc2_layer_t* layers,
                                         int32_t* types);
  HWC2::Error getClientTargetSupport(uint32_t width,
                                     uint32_t height,
                                     int32_t format,
                                     int32_t dataspace);
  HWC2::Error getColorModes(uint32_t* numModes, int32_t* modes);
  HWC2::Error getAttribute(hwc2_config_t config,
                           int32_t attribute,
                           int32_t* value);
  HWC2::Error getConfigs(uint32_t* numConfigs, hwc2_config_t* configs);
  HWC2::Error getName(uint32_t* size, char* name);
  HWC2::Error getRequests(int32_t* displayRequests,
                          uint32_t* numElements,
                          hwc2_layer_t* layers,
                          int32_t* layerRequests);
  HWC2::Error getType(int32_t* type);
  HWC2::Error getDozeSupport(int32_t* support);
  HWC2::Error getHdrCapabilities(uint32_t* numTypes,
                                 int32_t* types,
                                 float* maxLuminance,
                                 float* maxAverageLuminance,
                                 float* minLuminance);
  HWC2::Error getReleaseFences(uint32_t* numElements,
                               hwc2_layer_t* layers,
                               int32_t* fences);
  HWC2::Error present(int32_t* retire_fence);
  HWC2::Error setActiveConfig(hwc2_config_t config);
  HWC2::Error setClientTarget(buffer_handle_t target,
                              int32_t acquireFence,
                              int32_t dataspace,
                              hwc_region_t damage);
  HWC2::Error setColorMode(int32_t mode);
  HWC2::Error setColorTransform(const float* matrix, int32_t hint);
  HWC2::Error setOutputBuffer(buffer_handle_t buffer, int32_t release_fence);
  HWC2::Error setPowerMode(int32_t mode);
  HWC2::Error setVsyncEnabled(int32_t enabled);
  HWC2::Error validate(uint32_t* numTypes, uint32_t* numRequests);
  HWC2::Error getIdentificationData(uint8_t* outPort, uint32_t* outDataSize, uint8_t* outData);
  HWC2::Error getCapabilities(uint32_t* outNumCapabilities, uint32_t* outCapabilities);
  HWC2::Error setBrightness(float brightness);
  HWC2::Error getVsyncPeriod(hwc2_vsync_period_t* period);
  HWC2::Error setActiveConfigWithConstraints(hwc2_config_t config,
                                             hwc_vsync_period_change_constraints_t* constraints,
                                             hwc_vsync_period_change_timeline_t* timeline_t);
  HWC2::Error setExpectedPresentTime(
      const std::optional<ClockMonotonicTimestamp>& expectedPresentTime);

 protected:
  HWC2::Error hotplug(bool in);
  HWC2::Error vsync(int64_t timestamp);
  HWC2::Error refresh();
  int updateRotation();
#ifdef ENABLE_HWC_UIO
  int checkRotation();
#endif

 protected:
  const char* mName = "PrimaryDisplay";
  const hwc2_display_t kPrimayDisplay = 0;
  hwc2_display_t mDisplayID = 0;
  std::map<hwc2_layer_t, Hwc2Layer> mLayers;
  hwc2_layer_t mLayerIndex = 0;

  uint32_t mConfig = 1;
  int32_t mWidth = 1280;
  int32_t mHeight = 720;
  int32_t mFramerate = 60;
  int32_t mXDpi = 240;
  int32_t mYDpi = 240;
  uint32_t mTransform = 0;

  buffer_handle_t mFbTarget = nullptr;
  int mFbAcquireFenceFd = -1;
  std::vector<buffer_handle_t> mFbtBuffers;

  buffer_handle_t mOutputBuffer = nullptr;
  int mOutputBufferFenceFd = -1;

  int32_t mColorMode = 0;

  // remote display
  RemoteDisplay* mRemoteDisplay = nullptr;
  uint32_t mVersion = 0;
  uint32_t mMode = 0;
  int mReleaseFence = -1;

  int mFrameNum = 0;

#ifdef ENABLE_LAYER_DUMP
  int mFrameToDump = 0;
  bool mDebugRotationTransition = false;
#endif

#ifdef ENABLE_HWC_UIO
  UioDisplay* mUioDisplay = nullptr;
#endif
};

#endif  // __HWC2_DISPLAY_H__
