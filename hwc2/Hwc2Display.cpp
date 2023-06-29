//#define LOG_NDEBUG 0

#include <errno.h>
#include <inttypes.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sync/sync.h>

#include "Hwc2Display.h"
#include "LocalDisplay.h"
#include "RemoteDisplay.h"

#include "Hwc2Device.h"
#ifdef ENABLE_LAYER_DUMP
#include "BufferDumper.h"
#endif

#include "BufferMapper.h"
#include "FastBufferDump.h"

using namespace HWC2;

//#define DEBUG_LAYER
#ifdef DEBUG_LAYER
#define LAYER_TRACE(...) ALOGD(__VA_ARGS__)
#else
#define LAYER_TRACE(...)
#endif

#define HAL_PIXEL_FORMAT_INTEL_NV12_TILED     0x100

Hwc2Display::Hwc2Display(hwc2_display_t id, Hwc2Device& device)
  : mDevice(device),
    mVsyncThread(*this) {
  ALOGD("%s", __func__);
  mDisplayID = id;

  mShowCurrentFrame = false;
  int w = 0, h = 0;
  char * p_w_env = NULL;
  char * p_h_env = NULL;
  char value[PROPERTY_VALUE_MAX];
  p_w_env = getenv("K8S_ENV_DISPLAY_RESOLUTION_X");
  p_h_env = getenv("K8S_ENV_DISPLAY_RESOLUTION_Y");
  if (p_w_env != NULL && p_h_env!= NULL) {
    w = atoi(p_w_env);
    h = atoi(p_h_env);
    ALOGD("Display %" PRIu64 " default size <%d %d> from environment setting", id,
          w, h);
  } else if (property_get("sys.display.size", value, nullptr)) {
    sscanf(value, "%dx%d", &w, &h);
    ALOGD("Display %" PRIu64 " default size <%d %d> from property settings", id,
          w, h);
  } else if (getResFromFb(w, h) == 0) {
    ALOGD("Display %" PRIu64 " default size <%d %d> from fb device", id, w, h);
  } else if (getResFromDebugFs(w, h) == 0) {
    ALOGD("Display %" PRIu64 " default size <%d %d> from debug fs", id, w, h);
  }

  char * p_fps_env = NULL;
  p_fps_env = getenv("K8S_ENV_DISPLAY_FPS");
  if(p_fps_env != NULL) {
    mFramerate = atoi(p_fps_env);
  }

  property_get("ro.hwc_vhal.bypass", value, "false");
  if (0 == strcmp("true", value)) {
    mFullscreenOpt = true;
  }

  property_get("ro.hwc_vhal.video_bypass", value, "false");
  if (0 == strcmp("true", value)) {
    mEnableVideoBypass = true;
  }

  property_get("ro.hwc_vhal.multi_layer_bypass", value, "false");
  if (0 == strcmp("true", value)) {
    mEnableMultiLayerBypass = true;
  }

  if (mEnableVideoBypass) {
    property_get("ro.hwc_vhal.force_video_bypass", value, "false");
    if (0 == strcmp("true", value)) {
      mEnableMultiLayerBypass = true;
      mForceVideoBypass = true;
    }
  }

  property_get("ro.hwc_vhal.rotation_bypass", value, "false");
  if (0 == strcmp("true", value)) {
    mEnableRotationBypass = true;
  }

  property_get("ro.hwc_vhal.layer_dump", value, "false");
  if (0 == strcmp("true", value)) {
    mEnableLayerDump = true;
  }

  property_get("ro.hwc_vhal.fps_log", value, "false");
  if (0 == strcmp("true", value)) {
    mEnableFpsLog = true;
  }

  if (w && h) {
    mWidth = w;
    mHeight = h;
  }

  mDisplayControl = {.alpha = 0, .top_layer = 0, .rotation = 0, .viewport = {0, 0, (int16_t)w, (int16_t)h}};

#ifdef ENABLE_HWC_VNC
  // if (property_get("sys.display.vnc", value, nullptr) && (atoi(value) > 0)) {
  int port = 9000 + (int)id;
  mVncDisplay = new VncDisplay(port, mWidth, mHeight);
  if (mVncDisplay && mVncDisplay->init() < 0) {
    delete mVncDisplay;
    mVncDisplay = nullptr;
  }
  // }
#endif
  mVsyncThread.run("", -19 /* ANDROID_PRIORITY_URGENT_AUDIO */);
  BufferMapper::getMapper().addCallback(onGrallocCallbackHook, this);

}

Hwc2Display::~Hwc2Display() {
  if (mFbAcquireFenceFd >= 0) {
    close(mFbAcquireFenceFd);
    mFbAcquireFenceFd = -1;
  }
  if (mOutputBufferFenceFd >= 0) {
    close(mOutputBufferFenceFd);
    mOutputBufferFenceFd = -1;
  }
}

int Hwc2Display::attach(RemoteDisplay* rd) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  if (!rd)
    return -1;

  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  mRemoteDisplay = rd;
  mRemoteDisplay->setDisplayEventListener(this);
  mPort = (mDisplayID != kPrimayDisplay) ?  mRemoteDisplay->port() : 0;
  mWidth = mRemoteDisplay->width();
  mHeight = mRemoteDisplay->height();
  //mFramerate = mRemoteDisplay->fps();
  mXDpi = mRemoteDisplay->xdpi();
  mYDpi = mRemoteDisplay->ydpi();

  display_flags flags;
  flags.value = mRemoteDisplay->flags();
  mVersion = flags.version;
  mMode = flags.mode;

  ALOGD("Hwc2Display(%" PRIu64
        ")::%s w=%d,h=%d,fps=%d, xdpi=%d,ydpi=%d, protocal "
        "version=%d, mode=%d",
        mDisplayID, __func__, mWidth, mHeight, mFramerate, mXDpi, mYDpi,
        mVersion, mMode);
  return 0;
}

int Hwc2Display::detach(RemoteDisplay* rd) {
  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (rd == mRemoteDisplay) {
    mFbtBuffers.clear();
    mFullScreenBuffers.clear();
    mTransform = 0;
    mRemoteDisplay->setDisplayEventListener(nullptr);
    mRemoteDisplay = nullptr;
  }
  return 0;
}

int Hwc2Display::onBufferDisplayed(const buffer_info_t& info) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  return 0;
}
int Hwc2Display::onPresented(std::vector<layer_buffer_info_t>& layerBuffer,
                             int& fence) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  //display_flags flags;
  //flags.value = mRemoteDisplay->flags();
  // mMode = flags.mode;

  return 0;
}

int Hwc2Display::onSetVideoAlpha(int action) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
  ALOGD("Hwc2Display::onSetVideoAlpha: SetVideoAlpha to %d", action);

  std::unique_lock<std::mutex> lck(mRenderTaskMutex);

  if (action) {
    if (mRenderThread == nullptr) {
      mRenderThread = std::make_unique<RenderThread>();
      mRenderThread->init();
    }
    if (mAlphaVideo == nullptr) {
      mAlphaVideo = std::make_unique<AlphaVideo>();
    }
  } else {
    std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
    if (mRemoteDisplay) {
      for ( auto buffer : mAlphaVideoBuffers) {
        mRemoteDisplay->removeBuffer(buffer);
      }
      mAlphaVideoBuffers.clear();
    }

    if (mAlphaVideo != nullptr) {
      if (mRenderThread != nullptr) {
        ReleaseTask rt(mAlphaVideo.release());
        mRenderThread->runTask(&rt);
      } else {
        mAlphaVideo = nullptr;
      }
    }
  }
  return 0;
}

Error Hwc2Display::vsync(int64_t timestamp) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
  return Error::None;
}
Error Hwc2Display::refresh() {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
  return Error::None;
}
Error Hwc2Display::hotplug(bool in) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
  return Error::None;
}

Error Hwc2Display::acceptChanges() {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  for (auto& it : mLayers)
    it.second.acceptTypeChange();

  return Error::None;
}

Error Hwc2Display::createLayer(hwc2_layer_t* layer) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  LAYER_TRACE("Hwc2Display(%" PRIu64 ")::%s mode=%d layerId=%" PRIx64,
              mDisplayID, __func__, mMode, mLayerIndex);

  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay && mMode > 0) {
    mRemoteDisplay->createLayer(mLayerIndex);
  }
  mLayers.emplace(mLayerIndex, mLayerIndex);
  mLayers.at(mLayerIndex).setRemoteDisplay(mRemoteDisplay);
  *layer = mLayerIndex;
  mLayerIndex++;
  return Error::None;
}

Error Hwc2Display::destroyLayer(hwc2_layer_t layer) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  LAYER_TRACE("Hwc2Display(%" PRIu64 ")::%s mode=%d layerId=%" PRIx64,
              mDisplayID, __func__, mMode, mLayerIndex);

  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay && mMode > 0) {
    mRemoteDisplay->removeLayer(layer);
  }

  if (mRemoteDisplay && mMode == 0 && mFullScreenMode) {
    const std::set<buffer_handle_t> &buffers = mLayers.at(layer).buffers();
    for ( auto buffer : buffers) {
      mRemoteDisplay->removeBuffer(buffer);
      mFullScreenBuffers.erase((int64_t)buffer);
    }
  }

  mLayers.erase(layer);
  return Error::None;
}

Error Hwc2Display::getActiveConfig(hwc2_config_t* config) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *config = mConfig;
  return Error::None;
}

Error Hwc2Display::getChangedCompositionTypes(uint32_t* numElements,
                                              hwc2_layer_t* layers,
                                              int32_t* types) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  uint32_t numChanges = 0;
  for (auto& l : mLayers) {
    if (l.second.typeChanged()) {
      if (layers && types && numChanges < *numElements) {
        layers[numChanges] = l.first;
        types[numChanges] = static_cast<int32_t>(l.second.validatedType());
      }
      numChanges++;
    }
  }
  if (!layers && !types) {
    *numElements = numChanges;
  }
  return Error::None;
}

Error Hwc2Display::getClientTargetSupport(uint32_t width,
                                          uint32_t height,
                                          int32_t format,
                                          int32_t dataspace) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  if (width != (uint32_t)mWidth || height != (uint32_t)mHeight) {
    return Error::Unsupported;
  }

  if ((format == HAL_PIXEL_FORMAT_RGBA_8888 ||
       format == HAL_PIXEL_FORMAT_RGBX_8888 ||
       format == HAL_PIXEL_FORMAT_BGRA_8888) &&
      (dataspace == HAL_DATASPACE_UNKNOWN ||
       dataspace == HAL_DATASPACE_STANDARD_UNSPECIFIED)) {
    return HWC2::Error::None;
  }

  return Error::Unsupported;
}

Error Hwc2Display::getColorModes(uint32_t* numModes, int32_t* modes) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *numModes = 1;
  if (modes) {
    *modes = HAL_COLOR_MODE_NATIVE;
  }
  return Error::None;
}

Error Hwc2Display::getAttribute(hwc2_config_t config,
                                int32_t attribute,
                                int32_t* value) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s:config=%d,attribute=%d", mDisplayID,
        __func__, config, attribute);

  if (config != mConfig || !value) {
    return Error::BadConfig;
  }

  auto attr = static_cast<Attribute>(attribute);
  switch (attr) {
    case Attribute::Width:
      *value = mWidth;
      break;
    case Attribute::Height:
      *value = mHeight;
      break;
    case Attribute::VsyncPeriod:
      *value = 1000 * 1000 * 1000 / mFramerate;
      break;
    case Attribute::DpiX:
      *value = mXDpi * 1000;
      break;
    case Attribute::DpiY:
      *value = mYDpi * 1000;
      break;
    default:
      *value = -1;
      return Error::BadConfig;
  }
  return Error::None;
}

Error Hwc2Display::getConfigs(uint32_t* num_configs, hwc2_config_t* configs) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay) {
    for (auto& buffer : mFullScreenBuffers) {
      mRemoteDisplay->removeBuffer(buffer.second);
    }
  }
  mFullScreenBuffers.clear();

  if (mRemoteDisplay) {
    for (auto& buffer : mFbtBuffers) {
      mRemoteDisplay->removeBuffer(buffer);
    }
  }
  mFbtBuffers.clear();

  *num_configs = 1;
  if (configs) {
    configs[0] = mConfig;
  }
  return Error::None;
}

Error Hwc2Display::getName(uint32_t* size, char* name) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *size = strlen(mName) + 1;
  if (name) {
    strncpy(name, mName, *size);
  }
  return Error::None;
}

Error Hwc2Display::getRequests(int32_t* displayRequests,
                               uint32_t* numElements,
                               hwc2_layer_t* layers,
                               int32_t* layerRequests) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *numElements = 0;
  return Error::None;
}

Error Hwc2Display::getType(int32_t* type) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *type = HWC2_DISPLAY_TYPE_PHYSICAL;
  return Error::None;
}

Error Hwc2Display::getDozeSupport(int32_t* support) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *support = 0;
  return Error::None;
}

Error Hwc2Display::getHdrCapabilities(uint32_t* numTypes,
                                      int32_t* /*types*/,
                                      float* /*max_luminance*/,
                                      float* /*max_average_luminance*/,
                                      float* /*min_luminance*/) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  *numTypes = 0;
  return Error::None;
}

Error Hwc2Display::getReleaseFences(uint32_t* numElements,
                                    hwc2_layer_t* layers,
                                    int32_t* fences) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  if (!layers || !fences) {
    *numElements = mLayers.size();
    return Error::None;
  }

  uint32_t numLayers = 0;
  for (auto& l : mLayers) {
    if (numLayers < *numElements) {
      layers[numLayers] = l.first;
      fences[numLayers] = l.second.releaseFence();
      numLayers++;
    }
  }
  *numElements = numLayers;

  return Error::None;
}

bool Hwc2Display::updateDisplayControl() {
  if (!mRemoteDisplay) // called locked
    return false;

  bool update = false;
  bool alphaVideo = (mAlphaVideo != nullptr);
  if (mDisplayControl.alpha != alphaVideo) {
    mDisplayControl.alpha = alphaVideo;
    update = true;
  }
  int16_t l = 0, t = 0, r = (int16_t)mWidth, b = (int16_t)mHeight;
  int32_t rotation = 0;
  if (mFullScreenMode) {
    layer_info_t& info = mBypassLayer->info();
    switch (info.transform) {
    case HAL_TRANSFORM_ROT_90:
      rotation = 1;
      break;
    case HAL_TRANSFORM_ROT_180:
      rotation = 2;
      break;
    case HAL_TRANSFORM_ROT_270:
      rotation = 3;
      break;
    default:
      rotation = 0;
      break;
    }
    l = info.dstFrame.left;
    t = info.dstFrame.top;
    r = info.dstFrame.right;
    b = info.dstFrame.bottom;
  }
  if (rotation != mDisplayControl.rotation) {
    mDisplayControl.rotation = rotation;
    update = true;
  }
  if (mDisplayControl.viewport.l != l || mDisplayControl.viewport.t != t ||
      mDisplayControl.viewport.r != r || mDisplayControl.viewport.b != b) {
    mDisplayControl.viewport = {l, t, r, b};
    update = true;
  }
  bool multiLayerBypass = mFullScreenMode && (mLayers.size() > 1);
  if (mDisplayControl.top_layer != multiLayerBypass) {
    mDisplayControl.top_layer = multiLayerBypass;
    update = true;
  }
  return update;
}

Error Hwc2Display::present(int32_t* retireFence) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  buffer_handle_t target = nullptr;
  int32_t acquireFence = -1;
  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay) {
    std::unique_lock<std::mutex> lck(mRenderTaskMutex);
    if (mAlphaVideo != nullptr && mRenderThread != nullptr) {
	    uint32_t zOrder = UINT32_MAX;
      for (auto& l : mLayers) {
        Hwc2Layer& layer = l.second;
        if ( layer.zOrder() < zOrder) {
          zOrder = layer.zOrder();
          target = layer.buffer();
        }
      }
      int32_t format = -1;
      if (target) {
        BufferMapper::getMapper().getBufferFormat(target, format);
      }
      if (HAL_PIXEL_FORMAT_RGBA_8888 == format) {
        mAlphaVideo->setBuffer(target);
        mRenderThread->runTask(mAlphaVideo.get());
        target = mAlphaVideo->getOutputHandle();

        if (target && mAlphaVideoBuffers.find(target) == mAlphaVideoBuffers.end()) {
          mAlphaVideoBuffers.insert(target);
          mRemoteDisplay->createBuffer(target);
        }
      } else {
          target = nullptr;
      }
    }
    if (target == nullptr) {
      if (!mFullScreenMode) {
        target = mFbTarget;
        acquireFence = mFbAcquireFenceFd;
      } else {
        target = mBypassLayer->buffer();
        acquireFence = mBypassLayer->acquireFence();
        if (mFullScreenBuffers.find((int64_t)target) == mFullScreenBuffers.end()) {
          mFullScreenBuffers.insert(std::make_pair(((int64_t)target), target));
          mRemoteDisplay->createBuffer(target);
        }
      }
    }

    if (mShowCurrentFrame) {
      bool updateCtrl = updateDisplayControl();
      if (acquireFence >= 0) {
        int error = sync_wait(acquireFence, 1000);
        if (error < 0) {
          ALOGE("%s: fence %d, error errno = %d, desc = %s",
                __FUNCTION__, acquireFence, errno, strerror(errno));
        }
      }
      mRemoteDisplay->displayBuffer(target, updateCtrl ? &mDisplayControl : nullptr);
    }
  }

#ifdef ENABLE_HWC_VNC
  if (mVncDisplay && mFbTarget) {
    mVncDisplay->postFb(mFbTarget);
  }
#endif

#define unlikely(x) __builtin_expect(!!(x), 0)

  if (unlikely(mEnableLayerDump)) {
    char value[PROPERTY_VALUE_MAX];
    uint32_t zOrderDump = UINT32_MAX;
    bool dumpAll = false;
    bool dumpFB = !mFullScreenMode;
    int dumpType = 0; // 0 - png, 1 - ppm, 2 - raw
    if (mFrameToDump == 0) {
      if (property_get("debug.hwc_vhal.frame_to_dump", value, nullptr)) {
        mFrameToDump = atoi(value);
        if (property_set("debug.hwc_vhal.frame_to_dump", "0"))
          ALOGW("failed to set property debug.hwc_vhal.frame_to_dump = 0");;
      }
    }
    if (mFrameToDump > 0) {
      if (mRenderThread == nullptr) {
        mRenderThread = std::make_unique<RenderThread>();
        mRenderThread->init();
      }
      if (mFrameToDump) {
        if (property_get("debug.hwc_vhal.layer_to_dump", value, nullptr)) {
          if (!strcmp(value, "all")) {
            dumpAll = true;
          } else if (!strcmp(value, "fb")) {
            dumpFB = true;
          } else {
            zOrderDump = atoi(value);
          }
        }
      }
      if (property_get("debug.hwc_vhal.layer_dump_type", value, nullptr)) {
        if (!strcmp(value, "png")) {
          dumpType = 0;
        } else if (!strcmp(value, "ppm")) {
          dumpType = 1;
        } else if (!strcmp(value, "raw")) {
          dumpType = 2;
        }
      }

      if (dumpAll || zOrderDump < mLayers.size()) {
        for (auto& l : mLayers) {
          Hwc2Layer& layer = l.second;
          auto t = layer.buffer();
          uint32_t z = layer.zOrder();
          if (t && (dumpAll || z == zOrderDump)) {
            auto fbd = new FastBufferDump(mFrameNum, z, t, dumpType);
            mRenderThread->runTaskAsync(fbd);
          }
        }
      }
      if (dumpFB) {
        auto fbd = new FastBufferDump(mFrameNum, UINT32_MAX, mFbTarget, dumpType);
        mRenderThread->runTaskAsync(fbd);
      }
      mFrameToDump--;
    }
  }
  if (unlikely(mEnableFpsLog)) {
    if (mFrameNum % mFramerate == 0) {
      int64_t currentNS = systemTime(SYSTEM_TIME_MONOTONIC);
      int64_t lastNS = mLastPresentTime;
      float deltaMS = (currentNS - lastNS) / 1000000.0;
      float fps = 1000.0 * mFramerate / deltaMS;
      ALOGI("total mFrameNum = %d, fps = %.2f\n", mFrameNum, fps);
      mLastPresentTime = currentNS;
    }
  }

  mFrameNum++;
  mShowCurrentFrame = false;
  *retireFence = -1;
  return Error::None;
}

Error Hwc2Display::setActiveConfig(hwc2_config_t config) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  mConfig = config;
  return Error::None;
}

Error Hwc2Display::setClientTarget(buffer_handle_t target,
                                   int32_t acquireFence,
                                   int32_t dataspace,
                                   hwc_region_t damage) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
  mFbTarget = target;
  if (mFbAcquireFenceFd >= 0) {
    close(mFbAcquireFenceFd);
  }
  mFbAcquireFenceFd = acquireFence;

  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay) {
    bool isNew = true;

    for (auto fbt : mFbtBuffers) {
      if (fbt == mFbTarget) {
        isNew = false;
      }
    }
    if (isNew) {
      mFbtBuffers.push_back(mFbTarget);
      mRemoteDisplay->createBuffer(mFbTarget);
    }
  }
  return Error::None;
}

Error Hwc2Display::setColorMode(int32_t mode) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  mColorMode = mode;
  return Error::None;
}

Error Hwc2Display::setColorTransform(const float* matrix, int32_t hint) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  return Error::None;
}

Error Hwc2Display::setOutputBuffer(buffer_handle_t buffer,
                                   int32_t releaseFence) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  mOutputBuffer = buffer;
  if (mOutputBufferFenceFd >= 0) {
    close(mOutputBufferFenceFd);
  }
  mOutputBufferFenceFd = releaseFence;

  return Error::None;
}

Error Hwc2Display::setPowerMode(int32_t mode) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  return Error::None;
}

static bool isValid(Vsync enable) {
  switch (enable) {
    case Vsync::Enable: // Fall-through
    case Vsync::Disable: return true;
    case Vsync::Invalid: return false;
  }
  return false;
}

Error Hwc2Display::setVsyncEnabled(int32_t enabled) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s %d", mDisplayID, __func__, enabled);
  Vsync enable = static_cast<Vsync>(enabled);
  if (!isValid(enable)) {
    return Error::BadParameter;
  }
  if (enable == mVsyncEnabled) {
    return Error::None;
  }

  mVsyncEnabled = enable;
  return Error::None;
}

bool Hwc2Display::VsyncThread::threadLoop() {
  struct timespec rt;
  if (clock_gettime(CLOCK_MONOTONIC, &rt) == -1) {
    ALOGE("%s: error in vsync thread clock_gettime: %s",
          __FUNCTION__, strerror(errno));
    return true;
  }
  const int logInterval = 60;
  int64_t lastLogged = rt.tv_sec;
  int sent = 0;
  int lastSent = 0;

  struct timespec wait_time;
  wait_time.tv_sec = 0;
  wait_time.tv_nsec = 1000 * 1000 * 1000 / mDisplay.mFramerate;
  const int64_t kOneRefreshNs = 1000 * 1000 * 1000 / mDisplay.mFramerate;
  const int64_t kOneSecondNs = 1000ULL * 1000ULL * 1000ULL;
  int64_t lastTimeNs = -1;
  int64_t phasedWaitNs = 0;
  int64_t currentNs = 0;

  while (true) {
    if (mDisplay.mFramerate <= 0) {
      ALOGE("VsyncThread::threadLoop is exiting!");
      break;
    }
    clock_gettime(CLOCK_MONOTONIC, &rt);
    currentNs = rt.tv_nsec + rt.tv_sec * kOneSecondNs;

    if (lastTimeNs < 0) {
      phasedWaitNs = currentNs + kOneRefreshNs;
    } else {
      phasedWaitNs = kOneRefreshNs *
        (( currentNs - lastTimeNs) / kOneRefreshNs + 1) + lastTimeNs;
    }

    wait_time.tv_sec = phasedWaitNs / kOneSecondNs;
    wait_time.tv_nsec = phasedWaitNs - wait_time.tv_sec * kOneSecondNs;

    int ret;
    do {
      ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wait_time, NULL);
    } while (ret == -1 && errno == EINTR);

    lastTimeNs = phasedWaitNs;

    if (mDisplay.mVsyncEnabled != Vsync::Enable) {
      continue;
    }

    auto callbacks = mDisplay.mDevice.getCallbacks();
    const auto& callbackInfo = callbacks[(int32_t)Callback::Vsync];
    auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(callbackInfo.pointer);
    if (vsync) {
      vsync(callbackInfo.data, mDisplay.mDisplayID, lastTimeNs);
    }

    if (rt.tv_sec - lastLogged >= logInterval) {
      ALOGV("sent %d syncs in %ld", sent - lastSent, (long)(rt.tv_sec - lastLogged));
      lastLogged = rt.tv_sec;
      lastSent = sent;
    }
    ++sent;
  }
  return false;
}

bool Hwc2Display::IsBufferVisible(buffer_handle_t bh) {
  if (mRenderThread == nullptr) {
    mRenderThread = std::make_unique<RenderThread>();
    mRenderThread->init();
  }
  if (mVisibleBoundDetect == nullptr) {
    mVisibleBoundDetect = std::make_unique<VisibleBoundDetect>();
  }
  mVisibleBoundDetect->setBuffer(bh);
  mRenderThread->runTask(mVisibleBoundDetect.get());
  auto r = mVisibleBoundDetect->getResult();

  // Check Logo or fully transparent
  int w = r->bound[2] - r->bound[0];
  int h = r->bound[3] - r->bound[1];

  ALOGD("%s:bound l=%d t=%d r=%d b=%d size=%dx%d", __func__,
      r->bound[0], r->bound[1], r->bound[2], r->bound[3], w, h);

  if (w < 0 || h < 0)
    return false;

  // traditional iqiyi-tv logo size 137x30, 84x52 and 103x21 at 280 DPI,
  // 117x26, 72x44 and 88x18 at 80 DPI
  if (w < 140 && h < 55)
    return false;

  return true;
}

bool Hwc2Display::checkMultiLayerVideoBypass() {
  if (!mEnableVideoBypass)
    return false;

  int32_t format = -1;
  int bufferLayerCount = 0;
  Hwc2Layer* videoLayer = nullptr;
  Hwc2Layer* topLayer = nullptr;

  for (auto& it : mLayers) {
    auto* layer = &it.second;
    auto bh = layer->buffer();
    if (bh) {
      BufferMapper::getMapper().getBufferFormat(bh, format);
      if (HAL_PIXEL_FORMAT_INTEL_NV12_TILED == format) {
        videoLayer = layer;
      } else if (HAL_PIXEL_FORMAT_RGBA_8888 == format) {
        topLayer = layer;
      }
      bufferLayerCount ++;
//      ALOGD("%s:buf=%p z=%d type=%d format=%d age=%" PRId64 " last flip duration=%" PRId64 "", __FUNCTION__,
//        layer->buffer(), layer->info().z, layer->type(), format, layer->getAgeInNs(), layer->getLastFlipDuration());
    }
  }

  // reject if not video playback
  if (!videoLayer) {
    if (mVisibleBoundDetect != nullptr) {
      if (mRenderThread != nullptr) {
        ReleaseTask rt(mVisibleBoundDetect.release());
        mRenderThread->runTask(&rt);
      }
      else {
        mVisibleBoundDetect = nullptr;
      }
    }
    return false;
  }

  // Rotated video is not supported
  if (videoLayer->info().transform != 0)
    return false;

  // force video bypass or video layer is the only buffer layer
  if (mForceVideoBypass || bufferLayerCount == 1) {
    mBypassLayer = videoLayer;
    return true;
  }

  // reject if 2 or more other buffer layers.
  if (bufferLayerCount > 2)
     return false;

  if (topLayer) {
    if (topLayer->zOrder() < videoLayer->zOrder())
      return false;

    // If top layer not flipped in 2s or flipped from invisible, check if
    // current is visible.
    if ((topLayer->getAgeInNs() > 2 * Hwc2Layer::kOneSecondNs) ||
        (topLayer->getLastVisibleState() == Hwc2Layer::INVISIBLE)) {
      auto visibleState = topLayer->getVisibleState();
      if (Hwc2Layer::UNKNOWN == visibleState) {
        visibleState = IsBufferVisible(topLayer->buffer()) ? Hwc2Layer::VISIBLE : Hwc2Layer::INVISIBLE;
        topLayer->setVisibleState(visibleState);
      }

      if (Hwc2Layer::INVISIBLE == visibleState) {
        mBypassLayer = videoLayer;
        return true;
      }
    }
  }
  return false;
}

bool Hwc2Display::checkFullScreenMode() {
  if (!mFullscreenOpt)
    return false;

  // Single layer
  if (mLayers.size() == 1) {
    int32_t format = -1;
    auto& layer = mLayers.begin()->second;

    // Layer Buffer
    if (!layer.buffer())
      return false;

    // No rotation
    if (!mEnableRotationBypass && layer.info().transform != 0)
      return false;

    // Fullscreen
    if (layer.info().dstFrame.right != mWidth || layer.info().dstFrame.bottom != mHeight)
      return false;

    // Supported formats
    BufferMapper::getMapper().getBufferFormat(layer.buffer(), format);
    if (format == HAL_PIXEL_FORMAT_RGBA_8888 ||
        format == HAL_PIXEL_FORMAT_RGBX_8888 ||
        format == HAL_PIXEL_FORMAT_RGB_565 ||
        (mEnableVideoBypass && format == HAL_PIXEL_FORMAT_INTEL_NV12_TILED)) {
      mBypassLayer = &layer;
      return true;
    }
  } else if (mEnableMultiLayerBypass) {
    return checkMultiLayerVideoBypass();
  }
  return false;
}

void Hwc2Display::exitFullScreenMode() {
  ALOGD("Exit fullscreen mode");
  // Don't call it in mRemoteDisplayMutex locked
  std::unique_lock<std::mutex> lck(mRemoteDisplayMutex);
  if (mRemoteDisplay) {
    for (auto &buffer : mFullScreenBuffers) {
      mRemoteDisplay->removeBuffer(buffer.second);
    }
    mFullScreenBuffers.clear();
  }
}

void Hwc2Display::onGrallocCallback(int event, const buffer_handle_t buffer) {
  switch (event) {
    case GRALLOC_EVENT_ALLOCATE:
      //ALOGD("%s: buffer %p allocated", __func__, buffer);
      break;
    case GRALLOC_EVENT_RETAIN:
      //ALOGD("%s: buffer %p retained", __func__, buffer);
      break;
    case GRALLOC_EVENT_RELEASE:
      //ALOGD("%s: buffer %p released", __func__, buffer);
      // todo: move handle to main thread to avoid race condition
      for (auto& it : mFullScreenBuffers) {
        if (it.second == buffer) {
          ALOGD("%s: fullscreen buffer %p released", __func__, buffer);
          exitFullScreenMode();
          break;
        }
      }
      break;
    default:
      ALOGD("%s: unknown gralloc event with buffer %p", __func__, buffer);
      break;
  }
}

Error Hwc2Display::validate(uint32_t* numTypes, uint32_t* numRequests) {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  // check if there's any layer occupying the 1/2 display, if not, wo shouldn't show this frame
  for (auto& l : mLayers) {
    Hwc2Layer& layer = l.second;
    if (((layer.info().dstFrame.right - layer.info().dstFrame.left) > mWidth/2) && ((layer.info().dstFrame.bottom - layer.info().dstFrame.top) > mHeight/2)) {
      mShowCurrentFrame = true;
      break;
    }
  }
  if (false == mShowCurrentFrame) {
    ALOGW("there's no any layer occupying the 1/2 display !!!\n");
  }

  *numTypes = 0;
  *numRequests = 0;

if (mFullscreenOpt) {
  bool tmp = checkFullScreenMode();
  if(tmp != mFullScreenMode) {
    mFullScreenMode = tmp;
    if (mFullScreenMode == false)
      exitFullScreenMode();
    else {
      ALOGD("Enter fullscreen mode");
    }
  }

  // notify streamer to clear old buffers when layer resizes/reallocate new buffers
  if ((tmp == true) && (1 == mLayers.size())) {
    for (auto& layer : mLayers) {
      if (layer.second.changed() == true) {
        ALOGI("Layer has geometry change, so clear the fullscreen buffer.\n");
        exitFullScreenMode();
      }
    } // end of for
    for (auto& layer : mLayers) {
      layer.second.setUnchanged();
    }
  }
}

  for (auto& l : mLayers) {
    Hwc2Layer& layer = l.second;
    switch (layer.type()) {
      case Composition::Device:
      case Composition::SolidColor:
	      if (!mFullScreenMode) {
          layer.setValidatedType(Composition::Client);
          ++*numTypes;
	      } else {
          layer.setValidatedType(Composition::Device);
	      }
        break;
      case Composition::Cursor:
      case Composition::Sideband:
        layer.setValidatedType(Composition::Client);
        ++*numTypes;
        break;
      default:
        layer.setValidatedType(layer.type());
        break;
    }
  }

  // dump();
  return *numTypes > 0 ? Error::HasChanges : Error::None;
}

int Hwc2Display::updateRotation() {
  ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

  if (!mRemoteDisplay) // called locked
    return 0;

  uint32_t tr = 0;
  for (auto& layer : mLayers) {
    tr = layer.second.info().transform;
    if (tr == mTransform)
      break;
  }
  if (tr != mTransform) {
    int rot = (tr == 0) ? 0 : (tr == 4) ? 1 : (tr == 3) ? 2 : 3;

    ALOGD("Hwc2Display(%" PRIu64 ")::%s, setRotation to %d, tr=%d", mDisplayID,
          __func__, rot, tr);

    mRemoteDisplay->setRotation(rot);
    mTransform = tr;

#ifdef ENABLE_LAYER_DUMP
    if (mDebugRotationTransition) {
      mFrameToDump = 10;
    }
#endif
  }

  return 0;
}
Error Hwc2Display::getCapabilities(uint32_t* outNumCapabilities, uint32_t* /*outCapabilities*/) {
    ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
    *outNumCapabilities = 0;

    return Error::None;
}

Error Hwc2Display::setBrightness(float brightness) {
    ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);

    return Error::None;
}

Error Hwc2Display::getIdentificationData(uint8_t* outPort, uint32_t* outDataSize, uint8_t* /*outData*/) {
    ALOGV("Hwc2Display(%" PRIu64 ")::%s", mDisplayID, __func__);
    *outPort = mPort;
    *outDataSize = 0;
    return Error::None;
}

void Hwc2Display::dump() {
  ALOGD("-----Dump of Display(%" PRIu64 "): frame=%d remote=%p, mode=%d-----",
        mDisplayID, mFrameNum, mRemoteDisplay, mMode);
  for (auto& l : mLayers) {
    l.second.dump();
  }
}
