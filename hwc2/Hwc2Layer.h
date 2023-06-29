#ifndef __HWC2_LAYER_H__
#define __HWC2_LAYER_H__

#include <hardware/hwcomposer2.h>
#include "RemoteDisplay.h"
#include "display_protocol.h"

#include <set>

class Hwc2Layer {
 public:
  Hwc2Layer(hwc2_layer_t idx);
  Hwc2Layer(const Hwc2Layer& h){
    mAcquireFence = h.mAcquireFence;
  }

  Hwc2Layer& operator=(const Hwc2Layer& h){
    mAcquireFence = h.mAcquireFence;
    return *this;
  }
  ~Hwc2Layer();

  static const int64_t kOneSecondNs = 1000ULL * 1000ULL * 1000ULL;
  enum VISIBLE_STATE {
    UNKNOWN = 0,
    VISIBLE = 1,
    INVISIBLE = 2,
  };

  void setRemoteDisplay(RemoteDisplay* disp) { mRemoteDisplay = disp; }
  HWC2::Composition type() const { return mType; }
  void setValidatedType(HWC2::Composition t) { mValidatedType = t; }
  HWC2::Composition validatedType() const { return mValidatedType; }
  bool typeChanged() const { return mValidatedType != mType; }
  void acceptTypeChange() { mType = mValidatedType; }

  int releaseFence() const { return mReleaseFence; }
  bool changed() const { return mInfo.changed; }
  layer_info_t& info() { return mInfo; }
  bool bufferChanged() const { return mLayerBuffer.changed; }
  layer_buffer_info_t& layerBuffer() { return mLayerBuffer; }
  void setUnchanged() {
    mInfo.changed = false;
    mLayerBuffer.changed = false;
  }
  void dump();
  char* name() { return mInfo.name; }
  uint32_t zOrder() { return mZOrder; }
  buffer_handle_t buffer() { return mBuffer; }
  int32_t acquireFence() { return mAcquireFence; }
  const std::set<buffer_handle_t>& buffers() const { return mBuffers; }
  int64_t getAgeInNs() const {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t currentNs = now.tv_nsec + now.tv_sec * kOneSecondNs;
    return (currentNs - mLastUpdateTime);
  }
  int64_t getLastFlipDuration() const { return mLastFlipDuration; }
  void setVisibleState(VISIBLE_STATE state) { mVisibleState = state; }
  VISIBLE_STATE getVisibleState() const { return mVisibleState; }
  VISIBLE_STATE getLastVisibleState() const { return mLastVisibleState; }

  // Layer hooks
  HWC2::Error setCursorPosition(int32_t x, int32_t y);
  HWC2::Error setBlendMode(int32_t mode);
  HWC2::Error setBuffer(buffer_handle_t buffer, int32_t acquireFence);
  HWC2::Error setColor(hwc_color_t color);
  HWC2::Error setCompositionType(int32_t type);
  HWC2::Error setDataspace(int32_t dataspace);
  HWC2::Error setDisplayFrame(hwc_rect_t frame);
  HWC2::Error setPlaneAlpha(float alpha);
  HWC2::Error setSidebandStream(const native_handle_t* stream);
  HWC2::Error setSourceCrop(hwc_frect_t crop);
  HWC2::Error setSurfaceDamage(hwc_region_t damage);
  HWC2::Error setTransform(int32_t transform);
  HWC2::Error setVisibleRegion(hwc_region_t visible);
  HWC2::Error setZOrder(uint32_t z);
#ifdef VIDEO_STREAMING_OPT
  HWC2::Error setName(char *name);
#endif
#ifdef SUPPORT_LAYER_TASK_INFO
  HWC2::Error setTaskInfo(uint32_t stackId,
                          uint32_t taskId,
                          uint32_t userId,
                          uint32_t index);
#endif

 private:
  hwc2_layer_t mLayerID = 0;
  HWC2::Composition mType = HWC2::Composition::Invalid;
  HWC2::Composition mValidatedType = HWC2::Composition::Invalid;
  int mReleaseFence = -1;

  std::set<buffer_handle_t> mBuffers;
  buffer_handle_t mBuffer = nullptr;
  int mAcquireFence = -1;
  int64_t mLastUpdateTime = 0;
  int64_t mLastFlipDuration = 0;
  VISIBLE_STATE mVisibleState = UNKNOWN;
  VISIBLE_STATE mLastVisibleState = UNKNOWN;

  int32_t mDataspace = 0;
  hwc_rect_t mDstFrame = {.left = 0, .top = 0, .right = 0, .bottom = 0};
  hwc_frect_t mSrcCrop = {.left = 0.0, .top = 0.0, .right = 0.0, .bottom = 0.0};
  hwc_region_t mDamage = {0, };
  hwc_region_t mVisibleRegion = {0, };

  hwc_color_t mColor = {.r = 0, .g = 0, .b = 0, .a = 0};
  float mAlpha = 0.0f;
  int32_t mTransform = 0;
  uint32_t mZOrder = 0;

  uint32_t mStackId = 0;
  uint32_t mTaskId = 0;
  uint32_t mUserId = 0;
  uint32_t mIndex = 0;

  RemoteDisplay* mRemoteDisplay = nullptr;
  layer_info_t mInfo = {0, };
  layer_buffer_info_t mLayerBuffer;
};

#endif  // __HWC2_LAYER_H__
