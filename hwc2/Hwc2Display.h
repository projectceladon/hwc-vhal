#ifndef __HWC2_DISPLAY_H__
#define __HWC2_DISPLAY_H__

#include <map>
#include <set>
#include <memory>
#include <vector>

#include <hardware/hwcomposer2.h>
#include "Hwc2Layer.h"
#include "IRemoteDevice.h"
#include "display_protocol.h"
#include <utils/Thread.h>

#include "RenderThread.h"
#include "VisibleBoundDetect.h"
#include "AlphaVideo.h"

#ifdef ENABLE_HWC_VNC
#include "VncDisplay.h"
#endif

class RemoteDisplay;
class Hwc2Device;

class Hwc2Display : public DisplayEventListener {
 public:
  Hwc2Display(hwc2_display_t id, Hwc2Device& device);
  Hwc2Display(const Hwc2Display& h):
    mDevice(h.mDevice),
    mVsyncThread(*this){
    mFbAcquireFenceFd = h.mFbAcquireFenceFd;
    mOutputBufferFenceFd = h.mOutputBufferFenceFd;
  }

  Hwc2Display& operator=(const Hwc2Display& h){
    mFbAcquireFenceFd = h.mFbAcquireFenceFd;
    mOutputBufferFenceFd = h.mOutputBufferFenceFd;
    return *this;
  }
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
  int onSetVideoAlpha(int action) override;

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
  HWC2::Error getCapabilities(uint32_t* outNumCapabilities, uint32_t* outCapabilities);
  HWC2::Error setBrightness(float brightness);
  HWC2::Error getIdentificationData(uint8_t* outPort, uint32_t* outDataSize, uint8_t* outData);

  bool checkMultiLayerVideoBypass();
  bool checkFullScreenMode();
  void exitFullScreenMode();

  // Handle Gralloc buffer retain, release event
  static void onGrallocCallbackHook(void* ctx, int event, const buffer_handle_t buffer) {
    auto d = static_cast<Hwc2Display *>(ctx);
    if (d) {
      d->onGrallocCallback(event, buffer);
    }
  }
  void onGrallocCallback(int event, const buffer_handle_t buffer);

  // Generate sw vsync signal
  class VsyncThread : public android::Thread {
  public:
      VsyncThread(Hwc2Display& display)
        : mDisplay(display) {}
      virtual ~VsyncThread() {}
  private:
      Hwc2Display& mDisplay;
      bool threadLoop() final;
  };

 protected:
  HWC2::Error hotplug(bool in);
  HWC2::Error vsync(int64_t timestamp);
  HWC2::Error refresh();
  int updateRotation();

  bool IsBufferVisible(buffer_handle_t bh);
  bool updateDisplayControl();

 protected:
  const char* mName = "PrimaryDisplay";
  const hwc2_display_t kPrimayDisplay = 0;
  hwc2_display_t mDisplayID = 0;
  std::map<hwc2_layer_t, Hwc2Layer> mLayers;
  hwc2_layer_t mLayerIndex = 0;

  uint32_t mConfig = 1;
  int32_t mPort = 0;
  int32_t mWidth = 1280;
  int32_t mHeight = 720;
  int32_t mFramerate = 30;
  int32_t mXDpi = 240;
  int32_t mYDpi = 240;
  uint32_t mTransform = 0;

  buffer_handle_t mFbTarget = nullptr;
  int mFbAcquireFenceFd = -1;
  std::vector<buffer_handle_t> mFbtBuffers;
  std::map<int64_t,buffer_handle_t> mFullScreenBuffers;
  bool mFullScreenMode = false;
  bool mFullscreenOpt = false;
  bool mEnableRotationBypass = false;
  bool mEnableVideoBypass = false;
  bool mEnableMultiLayerBypass = false;
  bool mForceVideoBypass = false;

  Hwc2Layer* mBypassLayer = nullptr;

  buffer_handle_t mOutputBuffer = nullptr;
  int mOutputBufferFenceFd = -1;

  int32_t mColorMode = 0;

  // remote display
  RemoteDisplay* mRemoteDisplay = nullptr;
  std::mutex mRemoteDisplayMutex;
  uint32_t mVersion = 0;
  uint32_t mMode = 0;
  int mReleaseFence = -1;

  bool mShowCurrentFrame = false;
  int64_t mLastPresentTime = 0;
  int mFrameNum = 0;
  Hwc2Device& mDevice;
  HWC2::Vsync mVsyncEnabled;
  VsyncThread mVsyncThread;

  bool mEnableLayerDump = false;
  int mFrameToDump = 0;
  bool mDebugRotationTransition = false;
  bool mEnableFpsLog = false;

#ifdef ENABLE_HWC_VNC
  VncDisplay* mVncDisplay = nullptr;
#endif

  std::unique_ptr<RenderThread> mRenderThread;
  std::unique_ptr<VisibleBoundDetect> mVisibleBoundDetect;
  std::unique_ptr<AlphaVideo> mAlphaVideo;
  std::set<buffer_handle_t> mAlphaVideoBuffers;
  std::mutex mRenderTaskMutex;

  display_control_t mDisplayControl;
};

#endif  // __HWC2_DISPLAY_H__
