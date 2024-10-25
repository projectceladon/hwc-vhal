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

//#define LOG_NDEBUG 0

#include <errno.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include "Hwc2Device.h"
#include "RemoteDisplayMgr.h"

using namespace HWC2;

std::atomic<hwc2_display_t> Hwc2Device::sNextId(1);

Hwc2Device::Hwc2Device() {
  ALOGV("%s", __func__);

  common.tag = HARDWARE_DEVICE_TAG;
  common.version = HWC_DEVICE_API_VERSION_2_0;
  common.close = closeHook;
  getCapabilities = getCapabilitiesHook;
  getFunction = getFunctionHook;
}

Error Hwc2Device::init() {
  ALOGV("%s", __func__);

  mRemoteDisplayMgr = std::unique_ptr<RemoteDisplayMgr>(new RemoteDisplayMgr());
  if (!mRemoteDisplayMgr) {
    ALOGE("Failed to create remote display manager, out of memory");
    return Error::NoResources;
  }
  mRemoteDisplayMgr->init(this);
  if (mRemoteDisplayMgr->connectToRemote() < 0) {
    mDisplays.emplace(kPrimayDisplay, 0);
    onHotplug(kPrimayDisplay, true);
  }

#ifdef ENABLE_MULTI_DISPLAY
  int maxDisplayCount = kMaxDisplayCount;
#ifdef ENABLE_HWC_UIO
  for(int i = 0; i < kMaxDisplayCount; i ++) {
    int len  = snprintf(NULL, 0, "/dev/uio%d", i);
    char * path = (char *)malloc(len + 1);
    sprintf(path, "/dev/uio%d", i);
    int fd = open(path, O_RDWR);
    if (fd < 0)
    {
      close(fd);
      free(path);
      maxDisplayCount = i;
      break;
    }
    close(fd);
    free(path);
  }
#endif
  for(int i = maxDisplayCount - 1; i >= 1 ; i--) {
    mDisplays.emplace(i, i);
    onHotplug(i, true);
  }
#endif

  return Error::None;
}

int Hwc2Device::addRemoteDisplay(RemoteDisplay* rd) {
  if (!rd)
    return -1;

  std::unique_lock<std::mutex> lk(mDisplayMutex);

  if (mDisplays.find(kPrimayDisplay) != mDisplays.end() &&
      mDisplays.at(kPrimayDisplay).attachable()) {
    ALOGD("%s: attach to %" PRIu64, __func__, kPrimayDisplay);

    rd->setDisplayId(kPrimayDisplay);
    if (!rd->primaryHotplug() ||
        (mDisplays.at(kPrimayDisplay).width() == rd->width() &&
         mDisplays.at(kPrimayDisplay).height() == rd->height())) {
      ALOGD("Attach to primary");
      mDisplays.at(kPrimayDisplay).attach(rd);
      onRefresh(kPrimayDisplay);
    } else {
      ALOGD("Reconfig primary");
      onHotplug(kPrimayDisplay, false);
      mDisplays.at(kPrimayDisplay).attach(rd);
      onHotplug(kPrimayDisplay, true);
    }
  } else {
    auto id = sNextId++;
    ALOGD("%s: add new display %" PRIu64, __func__, id);

    rd->setDisplayId(id);
    mDisplays.emplace(id, id);
    mDisplays.at(id).attach(rd);
    onHotplug(id, true);
  }
  return 0;
}

int Hwc2Device::removeRemoteDisplay(RemoteDisplay* rd) {
  if (!rd)
    return -1;

  std::unique_lock<std::mutex> lk(mDisplayMutex);

  hwc2_display_t id = rd->getDisplayId();

  if (mDisplays.find(id) != mDisplays.end()) {
    ALOGD("%s: detach remote from display %" PRIu64, __func__, id);

    mDisplays.at(id).detach(rd);
    if (id != kPrimayDisplay) {
      ALOGD("%s: remove display %" PRIu64, __func__, id);

      onHotplug(id, false);
      mDisplays.erase(id);
      if (mDisplays.empty()) {
        mDisplays.emplace(kPrimayDisplay, 0);
        onHotplug(kPrimayDisplay, true);
      }
    }
  }
  return 0;
}
int Hwc2Device::getMaxRemoteDisplayCount() {
  ALOGV("%s", __func__);

  return kMaxDisplayCount;
}
int Hwc2Device::getRemoteDisplayCount() {
  ALOGV("%s", __func__);

  return mDisplays.size() - 1;
}

Error Hwc2Device::createVirtualDisplay(uint32_t width,
                                       uint32_t height,
                                       int32_t* format,
                                       hwc2_display_t* display) {
  ALOGV("%s", __func__);
  return Error::None;
}

Error Hwc2Device::destroyVirtualDisplay(hwc2_display_t display) {
  ALOGV("%s", __func__);
  return Error::None;
}

void Hwc2Device::dump(uint32_t* size, char* buffer) {
  ALOGV("%s", __func__);
}

uint32_t Hwc2Device::getMaxVirtualDisplayCount() {
  ALOGV("%s", __func__);
  return 2;
}

Error Hwc2Device::onHotplug(hwc2_display_t disp, bool connected) {
  ALOGV("%s:disp=%" PRIu64 ", connected=%d", __func__, disp, connected);

  if (mCallbacks.count(HWC2_CALLBACK_HOTPLUG) == 0) {
    mPendingHotplugs.emplace_back(disp, connected);
    return Error::None;
  }

  auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(
      mCallbacks[HWC2_CALLBACK_HOTPLUG].pointer);
  hotplug(mCallbacks[HWC2_CALLBACK_HOTPLUG].data, disp, connected);
  return Error::None;
}

Error Hwc2Device::onRefresh(hwc2_display_t disp) {
  if (mCallbacks.count(HWC2_CALLBACK_REFRESH) == 0) {
    return Error::None;
  }

  auto refresh = reinterpret_cast<HWC2_PFN_REFRESH>(
      mCallbacks[HWC2_CALLBACK_REFRESH].pointer);
  refresh(mCallbacks[HWC2_CALLBACK_REFRESH].data, disp);
  return Error::None;
}

Error Hwc2Device::registerCallback(int32_t descriptor,
                                   hwc2_callback_data_t data,
                                   hwc2_function_pointer_t function) {
  ALOGV("%s:descriptor=%d", __func__, descriptor);

  auto desc = static_cast<hwc2_callback_descriptor_t>(descriptor);
  if (function != nullptr) {
    mCallbacks[desc] = {data, function};
  } else {
    ALOGI("unregisterCallback(%s)", getCallbackDescriptorName(desc));
    mCallbacks.erase(desc);
    return Error::None;
  }

  if (HWC2_CALLBACK_HOTPLUG == descriptor && !mPendingHotplugs.empty()) {
    auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(function);
    for (auto& info : mPendingHotplugs) {
      hotplug(data, info.first, info.second);
    }
    mPendingHotplugs.clear();
  }
  return Error::None;
}

// static
int Hwc2Device::closeHook(hw_device_t* dev) {
  ALOGV("%s", __func__);
  return 0;
}

// static
void Hwc2Device::getCapabilitiesHook(hwc2_device_t* dev,
                                     uint32_t* outCount,
                                     int32_t* outCap) {
  ALOGV("%s", __func__);
  *outCount = 0;
}

// static
hwc2_function_pointer_t Hwc2Device::getFunctionHook(struct hwc2_device* dev,
                                                    int32_t descriptor) {
  ALOGV("%s:descriptor=%d", __func__, descriptor);

  auto func = static_cast<FunctionDescriptor>(descriptor);
  switch (func) {
    // Device functions
    case FunctionDescriptor::CreateVirtualDisplay:
      return asFP<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(
          DeviceHook<int32_t, decltype(&Hwc2Device::createVirtualDisplay),
                     &Hwc2Device::createVirtualDisplay, uint32_t, uint32_t,
                     int32_t*, hwc2_display_t*>);
    case FunctionDescriptor::DestroyVirtualDisplay:
      return asFP<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(
          DeviceHook<int32_t, decltype(&Hwc2Device::destroyVirtualDisplay),
                     &Hwc2Device::destroyVirtualDisplay, hwc2_display_t>);
    case FunctionDescriptor::Dump:
      return asFP<HWC2_PFN_DUMP>(
          DeviceHook<void, decltype(&Hwc2Device::dump), &Hwc2Device::dump,
                     uint32_t*, char*>);
    case FunctionDescriptor::GetMaxVirtualDisplayCount:
      return asFP<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(
          DeviceHook<uint32_t, decltype(&Hwc2Device::getMaxVirtualDisplayCount),
                     &Hwc2Device::getMaxVirtualDisplayCount>);
    case FunctionDescriptor::RegisterCallback:
      return asFP<HWC2_PFN_REGISTER_CALLBACK>(
          DeviceHook<int32_t, decltype(&Hwc2Device::registerCallback),
                     &Hwc2Device::registerCallback, int32_t,
                     hwc2_callback_data_t, hwc2_function_pointer_t>);

    // Display functions
    case FunctionDescriptor::AcceptDisplayChanges:
      return asFP<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(
          DisplayHook<decltype(&Hwc2Display::acceptChanges),
                      &Hwc2Display::acceptChanges>);
    case FunctionDescriptor::CreateLayer:
      return asFP<HWC2_PFN_CREATE_LAYER>(
          DisplayHook<decltype(&Hwc2Display::createLayer),
                      &Hwc2Display::createLayer, hwc2_layer_t*>);
    case FunctionDescriptor::DestroyLayer:
      return asFP<HWC2_PFN_DESTROY_LAYER>(
          DisplayHook<decltype(&Hwc2Display::destroyLayer),
                      &Hwc2Display::destroyLayer, hwc2_layer_t>);
    case FunctionDescriptor::GetActiveConfig:
      return asFP<HWC2_PFN_GET_ACTIVE_CONFIG>(
          DisplayHook<decltype(&Hwc2Display::getActiveConfig),
                      &Hwc2Display::getActiveConfig, hwc2_config_t*>);
    case FunctionDescriptor::GetChangedCompositionTypes:
      return asFP<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(
          DisplayHook<decltype(&Hwc2Display::getChangedCompositionTypes),
                      &Hwc2Display::getChangedCompositionTypes, uint32_t*,
                      hwc2_layer_t*, int32_t*>);
    case FunctionDescriptor::GetClientTargetSupport:
      return asFP<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(
          DisplayHook<decltype(&Hwc2Display::getClientTargetSupport),
                      &Hwc2Display::getClientTargetSupport, uint32_t, uint32_t,
                      int32_t, int32_t>);
    case FunctionDescriptor::GetColorModes:
      return asFP<HWC2_PFN_GET_COLOR_MODES>(
          DisplayHook<decltype(&Hwc2Display::getColorModes),
                      &Hwc2Display::getColorModes, uint32_t*, int32_t*>);
    case FunctionDescriptor::GetDisplayAttribute:
      return asFP<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(
          DisplayHook<decltype(&Hwc2Display::getAttribute),
                      &Hwc2Display::getAttribute, hwc2_config_t, int32_t,
                      int32_t*>);
    case FunctionDescriptor::GetDisplayConfigs:
      return asFP<HWC2_PFN_GET_DISPLAY_CONFIGS>(
          DisplayHook<decltype(&Hwc2Display::getConfigs),
                      &Hwc2Display::getConfigs, uint32_t*, hwc2_config_t*>);
    case FunctionDescriptor::GetDisplayName:
      return asFP<HWC2_PFN_GET_DISPLAY_NAME>(
          DisplayHook<decltype(&Hwc2Display::getName), &Hwc2Display::getName,
                      uint32_t*, char*>);
    case FunctionDescriptor::GetDisplayRequests:
      return asFP<HWC2_PFN_GET_DISPLAY_REQUESTS>(
          DisplayHook<decltype(&Hwc2Display::getRequests),
                      &Hwc2Display::getRequests, int32_t*, uint32_t*,
                      hwc2_layer_t*, int32_t*>);
    case FunctionDescriptor::GetDisplayType:
      return asFP<HWC2_PFN_GET_DISPLAY_TYPE>(
          DisplayHook<decltype(&Hwc2Display::getType), &Hwc2Display::getType,
                      int32_t*>);
    case FunctionDescriptor::GetDozeSupport:
      return asFP<HWC2_PFN_GET_DOZE_SUPPORT>(
          DisplayHook<decltype(&Hwc2Display::getDozeSupport),
                      &Hwc2Display::getDozeSupport, int32_t*>);
    case FunctionDescriptor::GetHdrCapabilities:
      return asFP<HWC2_PFN_GET_HDR_CAPABILITIES>(
          DisplayHook<decltype(&Hwc2Display::getHdrCapabilities),
                      &Hwc2Display::getHdrCapabilities, uint32_t*, int32_t*,
                      float*, float*, float*>);
    case FunctionDescriptor::GetReleaseFences:
      return asFP<HWC2_PFN_GET_RELEASE_FENCES>(
          DisplayHook<decltype(&Hwc2Display::getReleaseFences),
                      &Hwc2Display::getReleaseFences, uint32_t*, hwc2_layer_t*,
                      int32_t*>);
    case FunctionDescriptor::PresentDisplay:
      return asFP<HWC2_PFN_PRESENT_DISPLAY>(
          DisplayHook<decltype(&Hwc2Display::present), &Hwc2Display::present,
                      int32_t*>);
    case FunctionDescriptor::SetActiveConfig:
      return asFP<HWC2_PFN_SET_ACTIVE_CONFIG>(
          DisplayHook<decltype(&Hwc2Display::setActiveConfig),
                      &Hwc2Display::setActiveConfig, hwc2_config_t>);
    case FunctionDescriptor::SetClientTarget:
      return asFP<HWC2_PFN_SET_CLIENT_TARGET>(
          DisplayHook<decltype(&Hwc2Display::setClientTarget),
                      &Hwc2Display::setClientTarget, buffer_handle_t, int32_t,
                      int32_t, hwc_region_t>);
    case FunctionDescriptor::SetColorMode:
      return asFP<HWC2_PFN_SET_COLOR_MODE>(
          DisplayHook<decltype(&Hwc2Display::setColorMode),
                      &Hwc2Display::setColorMode, int32_t>);
    case FunctionDescriptor::SetColorTransform:
      return asFP<HWC2_PFN_SET_COLOR_TRANSFORM>(
          DisplayHook<decltype(&Hwc2Display::setColorTransform),
                      &Hwc2Display::setColorTransform, const float*, int32_t>);
    case FunctionDescriptor::SetOutputBuffer:
      return asFP<HWC2_PFN_SET_OUTPUT_BUFFER>(
          DisplayHook<decltype(&Hwc2Display::setOutputBuffer),
                      &Hwc2Display::setOutputBuffer, buffer_handle_t, int32_t>);
    case FunctionDescriptor::SetPowerMode:
      return asFP<HWC2_PFN_SET_POWER_MODE>(
          DisplayHook<decltype(&Hwc2Display::setPowerMode),
                      &Hwc2Display::setPowerMode, int32_t>);
    case FunctionDescriptor::SetVsyncEnabled:
      return asFP<HWC2_PFN_SET_VSYNC_ENABLED>(
          DisplayHook<decltype(&Hwc2Display::setVsyncEnabled),
                      &Hwc2Display::setVsyncEnabled, int32_t>);
    case FunctionDescriptor::ValidateDisplay:
      return asFP<HWC2_PFN_VALIDATE_DISPLAY>(
          DisplayHook<decltype(&Hwc2Display::validate), &Hwc2Display::validate,
                      uint32_t*, uint32_t*>);

    // Layer functions
    case FunctionDescriptor::SetCursorPosition:
      return asFP<HWC2_PFN_SET_CURSOR_POSITION>(
          LayerHook<decltype(&Hwc2Layer::setCursorPosition),
                    &Hwc2Layer::setCursorPosition, int32_t, int32_t>);
    case FunctionDescriptor::SetLayerBlendMode:
      return asFP<HWC2_PFN_SET_LAYER_BLEND_MODE>(
          LayerHook<decltype(&Hwc2Layer::setBlendMode),
                    &Hwc2Layer::setBlendMode, int32_t>);
    case FunctionDescriptor::SetLayerBuffer:
      return asFP<HWC2_PFN_SET_LAYER_BUFFER>(
          LayerHook<decltype(&Hwc2Layer::setBuffer), &Hwc2Layer::setBuffer,
                    buffer_handle_t, int32_t>);
    case FunctionDescriptor::SetLayerColor:
      return asFP<HWC2_PFN_SET_LAYER_COLOR>(
          LayerHook<decltype(&Hwc2Layer::setColor), &Hwc2Layer::setColor,
                    hwc_color_t>);
    case FunctionDescriptor::SetLayerCompositionType:
      return asFP<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(
          LayerHook<decltype(&Hwc2Layer::setCompositionType),
                    &Hwc2Layer::setCompositionType, int32_t>);
    case FunctionDescriptor::SetLayerDataspace:
      return asFP<HWC2_PFN_SET_LAYER_DATASPACE>(
          LayerHook<decltype(&Hwc2Layer::setDataspace),
                    &Hwc2Layer::setDataspace, int32_t>);
    case FunctionDescriptor::SetLayerDisplayFrame:
      return asFP<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(
          LayerHook<decltype(&Hwc2Layer::setDisplayFrame),
                    &Hwc2Layer::setDisplayFrame, hwc_rect_t>);
    case FunctionDescriptor::SetLayerPlaneAlpha:
      return asFP<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(
          LayerHook<decltype(&Hwc2Layer::setPlaneAlpha),
                    &Hwc2Layer::setPlaneAlpha, float>);
    case FunctionDescriptor::SetLayerSidebandStream:
      return asFP<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(
          LayerHook<decltype(&Hwc2Layer::setSidebandStream),
                    &Hwc2Layer::setSidebandStream, const native_handle_t*>);
    case FunctionDescriptor::SetLayerSourceCrop:
      return asFP<HWC2_PFN_SET_LAYER_SOURCE_CROP>(
          LayerHook<decltype(&Hwc2Layer::setSourceCrop),
                    &Hwc2Layer::setSourceCrop, hwc_frect_t>);
    case FunctionDescriptor::SetLayerSurfaceDamage:
      return asFP<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(
          LayerHook<decltype(&Hwc2Layer::setSurfaceDamage),
                    &Hwc2Layer::setSurfaceDamage, hwc_region_t>);
    case FunctionDescriptor::SetLayerTransform:
      return asFP<HWC2_PFN_SET_LAYER_TRANSFORM>(
          LayerHook<decltype(&Hwc2Layer::setTransform),
                    &Hwc2Layer::setTransform, int32_t>);
    case FunctionDescriptor::SetLayerVisibleRegion:
      return asFP<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(
          LayerHook<decltype(&Hwc2Layer::setVisibleRegion),
                    &Hwc2Layer::setVisibleRegion, hwc_region_t>);
    case FunctionDescriptor::SetLayerZOrder:
      return asFP<HWC2_PFN_SET_LAYER_Z_ORDER>(
          LayerHook<decltype(&Hwc2Layer::setZOrder), &Hwc2Layer::setZOrder,
                    uint32_t>);

#ifdef SUPPORT_LAYER_TASK_INFO
    case FunctionDescriptor::SetLayerTaskInfo:
      return asFP<HWC2_PFN_SET_LAYER_TASK_INFO>(
          LayerHook<decltype(&Hwc2Layer::setTaskInfo), &Hwc2Layer::setTaskInfo,
                    uint32_t, uint32_t, uint32_t, uint32_t>);
#endif

#ifdef SUPPORT_HWC_2_2
    case FunctionDescriptor::SetLayerFloatColor:
    case FunctionDescriptor::SetLayerPerFrameMetadata:
    case FunctionDescriptor::GetPerFrameMetadataKeys:
    case FunctionDescriptor::SetReadbackBuffer:
    case FunctionDescriptor::GetReadbackBufferAttributes:
    case FunctionDescriptor::GetReadbackBufferFence:
    case FunctionDescriptor::GetRenderIntents:
    case FunctionDescriptor::SetColorModeWithRenderIntent:
    case FunctionDescriptor::GetDataspaceSaturationMatrix:
        ALOGI("%s: return nullptr", __func__);
        return nullptr;
#endif

#ifdef SUPPORT_HWC_2_3
    // composer 2.3
    case FunctionDescriptor::GetDisplayIdentificationData:
        return asFP<HWC2_PFN_GET_DISPLAY_IDENTIFICATION_DATA>(
            DisplayHook<decltype(&Hwc2Display::getIdentificationData),
                &Hwc2Display::getIdentificationData, uint8_t*, uint32_t*, uint8_t*>);
    case FunctionDescriptor::GetDisplayCapabilities:
        return asFP<HWC2_PFN_GET_DISPLAY_CAPABILITIES>(
            DisplayHook<decltype(&Hwc2Display::getCapabilities),
                &Hwc2Display::getCapabilities, uint32_t*, uint32_t*>);
    case FunctionDescriptor::SetDisplayBrightness:
        return asFP<HWC2_PFN_SET_DISPLAY_BRIGHTNESS>(
            DisplayHook<decltype(&Hwc2Display::setBrightness),
                &Hwc2Display::setBrightness, float>);
    //optional
    case FunctionDescriptor::SetLayerColorTransform:
    case FunctionDescriptor::GetDisplayedContentSamplingAttributes:
    case FunctionDescriptor::SetDisplayedContentSamplingEnabled:
    case FunctionDescriptor::GetDisplayedContentSample:
    case FunctionDescriptor::SetLayerPerFrameMetadataBlobs:
    case FunctionDescriptor::GetDisplayBrightnessSupport:
        ALOGI("%s: return nullptr", __func__);
        return nullptr;
#endif

#ifdef SUPPORT_HWC_2_4
    // composer 2.4
    case FunctionDescriptor::GetDisplayVsyncPeriod:
        return asFP<HWC2_PFN_GET_DISPLAY_VSYNC_PERIOD>(
            DisplayHook<decltype(&Hwc2Display::getVsyncPeriod),
                &Hwc2Display::getVsyncPeriod, hwc2_vsync_period_t*>);
    case FunctionDescriptor::SetActiveConfigWithConstraints:
        return asFP<HWC2_PFN_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS>(
            DisplayHook<decltype(&Hwc2Display::setActiveConfigWithConstraints),
                &Hwc2Display::setActiveConfigWithConstraints,
                hwc2_config_t, hwc_vsync_period_change_constraints_t*,
                hwc_vsync_period_change_timeline_t*>);
    //optional
    case FunctionDescriptor::GetDisplayConnectionType:
    case FunctionDescriptor::SetAutoLowLatencyMode:
    case FunctionDescriptor::GetSupportedContentTypes:
    case FunctionDescriptor::SetContentType:
    case FunctionDescriptor::GetClientTargetProperty:
    case FunctionDescriptor::SetLayerGenericMetadata:
    case FunctionDescriptor::GetLayerGenericMetadataKey:
        ALOGI("%s: return nullptr", __func__);
        return nullptr;
#endif

    case FunctionDescriptor::Invalid:
    default:
      if (descriptor == HWC3_FUNCTION_SET_EXPECTED_PRESENT_TIME)
        return asFP<HWC3_PFN_SET_EXPECTED_PRESENT_TIME>(
            DisplayHook<decltype(&Hwc2Display::setExpectedPresentTime),
                        &Hwc2Display::setExpectedPresentTime, const std::optional<ClockMonotonicTimestamp>&>);
      else {
        ALOGE("%s:Unsupported HWC2 function, descriptor=%d", __func__,
              descriptor);
        return nullptr;
      }
  }
}

// static
int Hwc2Device::openHook(const struct hw_module_t* module,
                         const char* name,
                         struct hw_device_t** dev) {
  ALOGV("%s:name=%s", __func__, name);

  if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
    ALOGE("Invalid module name- %s", name);
    return -EINVAL;
  }

  signal(SIGPIPE, SIG_IGN);

  std::unique_ptr<Hwc2Device> ctx(new Hwc2Device());
  if (!ctx) {
    ALOGE("Failed to allocate Hwc2Device");
    return -ENOMEM;
  }

  Error err = ctx->init();
  if (err != Error::None) {
    ALOGE("Failed to initialize Hwc2Device err=%d\n", err);
    return -EINVAL;
  }

  ctx->common.module = const_cast<hw_module_t*>(module);
  *dev = &ctx->common;
  ctx.release();
  return 0;
}

static struct hw_module_methods_t hwc2_module_methods = {
    .open = Hwc2Device::openHook,
};

hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = HARDWARE_MODULE_API_VERSION(2, 0),
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "IHWComposer vHAL",
    .author = "AOSP Team",
    .methods = &hwc2_module_methods,
    .dso = NULL,
    .reserved = {0},
};
