//#define LOG_NDEBUG 0

#include <errno.h>
#include <inttypes.h>

#include <cutils/properties.h>
#include <cutils/log.h>

#include "Hwc2Layer.h"
#define  MODE_MARSK 0x00000030

using namespace HWC2;

Hwc2Layer::Hwc2Layer(hwc2_layer_t idx) {
  mLayerID = idx;
  memset(&mInfo, 0, sizeof(mInfo));
  mInfo.layerId = idx;
  mInfo.changed = true;
  memset(&mLayerBuffer, 0, sizeof(layer_buffer_info_t));
  mLayerBuffer.layerId = idx;
}

Hwc2Layer::~Hwc2Layer() {
  // since connection is broken, we can't ask remote to remove buffers
  //if (mRemoteDisplay) {
  //  for (auto buffer : mBuffers) {
  //    mRemoteDisplay->removeBuffer(buffer);
  //  }
  //}
  if (mAcquireFence >= 0) {
    close(mAcquireFence);
    mAcquireFence = -1;
  }
}

Error Hwc2Layer::setCursorPosition(int32_t /*x*/, int32_t /*y*/) {
  ALOGV("%s", __func__);
  return Error::None;
}

Error Hwc2Layer::setBlendMode(int32_t mode) {
  ALOGV("%s", __func__);
  if (mInfo.blendMode != mode) {
    mInfo.blendMode = mode;
    mInfo.changed = true;
  }
  return Error::None;
}

Error Hwc2Layer::setBuffer(buffer_handle_t buffer, int32_t acquireFence) {
  ALOGV("%s", __func__);

  if (mAcquireFence >= 0) {
    close(mAcquireFence);
  }
  mAcquireFence = acquireFence;

  if (mBuffer != buffer) {
    if (mBuffers.count(buffer) == 0) {
      mBuffers.insert(buffer);
    }

    mBuffer = buffer;
    mAcquireFence = acquireFence;
    mLayerBuffer.bufferId = (uint64_t)mBuffer;
    mLayerBuffer.fence = acquireFence;
    mLayerBuffer.changed = true;

    mLastVisibleState = mVisibleState;
    mVisibleState = UNKNOWN;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t currentNs = now.tv_nsec + now.tv_sec * kOneSecondNs;
    mLastFlipDuration = currentNs - mLastUpdateTime;
    mLastUpdateTime = currentNs;
  }
  return Error::None;
}

Error Hwc2Layer::setColor(hwc_color_t color) {
  ALOGV("%s", __func__);
  // We only support Opaque colors so far.
  if ((mColor.r != color.r) || (mColor.g != color.g) || (mColor.b != color.b) ||
      (mColor.a != color.a)) {
    mColor = color;
    mInfo.color = color.r | (color.g << 8) | (color.g << 16) | (color.a << 24);
    mInfo.changed = true;
  }

  return Error::None;
}

Error Hwc2Layer::setCompositionType(int32_t type) {
  ALOGV("%s", __func__);

  mType = static_cast<Composition>(type);
  return Error::None;
}

Error Hwc2Layer::setDataspace(int32_t dataspace) {
  ALOGV("%s", __func__);

  mDataspace = dataspace;
  return Error::None;
}

Error Hwc2Layer::setDisplayFrame(hwc_rect_t frame) {
  ALOGV("%s", __func__);

  if ((mDstFrame.left != frame.left) || (mDstFrame.top != frame.top) ||
      (mDstFrame.right != frame.right) || (mDstFrame.bottom != frame.bottom)) {
    mDstFrame = frame;

    mInfo.dstFrame.left = mDstFrame.left;
    mInfo.dstFrame.top = mDstFrame.top;
    mInfo.dstFrame.right = mDstFrame.right;
    mInfo.dstFrame.bottom = mDstFrame.bottom;
    mInfo.changed = true;
  }
  return Error::None;
}

Error Hwc2Layer::setPlaneAlpha(float alpha) {
  ALOGV("%s", __func__);

  if (mAlpha != alpha) {
    mAlpha = alpha;

    mInfo.planeAlpha = alpha;
    mInfo.changed = true;
  }
  return Error::None;
}

Error Hwc2Layer::setSidebandStream(const native_handle_t* stream) {
  ALOGV("%s", __func__);
  return Error::Unsupported;
}

Error Hwc2Layer::setSourceCrop(hwc_frect_t crop) {
  ALOGV("%s", __func__);

  if ((mSrcCrop.left != crop.left) || (mSrcCrop.top != crop.top) ||
      (mSrcCrop.right != crop.right) || (mSrcCrop.bottom != crop.bottom)) {
    mSrcCrop = crop;

    mInfo.srcCrop.left = (int)mSrcCrop.left;
    mInfo.srcCrop.top = (int)mSrcCrop.top;
    mInfo.srcCrop.right = (int)mSrcCrop.right;
    mInfo.srcCrop.bottom = (int)mSrcCrop.bottom;
    mInfo.changed = true;
  }
  return Error::None;
}

Error Hwc2Layer::setSurfaceDamage(hwc_region_t damage) {
  ALOGV("%s", __func__);

  mDamage = damage;
  return Error::None;
}

Error Hwc2Layer::setTransform(int32_t transform) {
  ALOGV("%s", __func__);

  if (mTransform != transform) {
    mTransform = transform;

    mInfo.transform = transform;
    mInfo.changed = true;
  }
  return Error::None;
}

Error Hwc2Layer::setVisibleRegion(hwc_region_t visible) {
  ALOGV("%s", __func__);

  mVisibleRegion = visible;
  return Error::None;
}

Error Hwc2Layer::setZOrder(uint32_t order) {
  ALOGV("%s", __func__);

  mZOrder = order;
  mInfo.z = order;
  return Error::None;
}
#ifdef VIDEO_STREAMING_OPT
Error Hwc2Layer::setName(char *name) {
  ALOGV("%s", __func__);

  memcpy(mInfo.name,name,96);
  return Error::None;
}
#endif
#ifdef SUPPORT_LAYER_TASK_INFO
HWC2::Error Hwc2Layer::setTaskInfo(uint32_t stackId,
                                   uint32_t taskId,
                                   uint32_t userId,
                                   uint32_t index) {
  mStackId = stackId;
  mTaskId = taskId;
  mUserId = userId;
  mIndex = index;

  mInfo.stackId = stackId;
  mInfo.taskId = taskId;
  mInfo.userId = userId;
  mInfo.index = index;
  return Error::None;
}
#endif

void Hwc2Layer::dump() {
  ALOGD("  Layer %" PRIu64
        ": type=%d, buf=%p dst=<%d,%d,%d,%d> src=<%.1f,%.1f,%.1f %.1f> tr=%d "
        "alpha=%.2f z=%d stack=%d task=%d user=%d index=%d\n",
        mLayerID, mType, mBuffer, mDstFrame.left, mDstFrame.top,
        mDstFrame.right, mDstFrame.bottom, mSrcCrop.left, mSrcCrop.top,
        mSrcCrop.right, mSrcCrop.bottom, mTransform, mAlpha, mZOrder, mStackId,
        mTaskId, mUserId, mIndex);
}
