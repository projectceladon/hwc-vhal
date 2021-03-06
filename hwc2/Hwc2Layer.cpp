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
Wan Shuang (shuang.wan@intel.com)
Date: 2021.06.09

*/

//#define LOG_NDEBUG 0

#include <errno.h>
#include <inttypes.h>

#include <cutils/properties.h>
#include <cutils/log.h>
#include <unistd.h>

#include "Hwc2Layer.h"

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
      if (mRemoteDisplay) {
        mRemoteDisplay->createBuffer(buffer);
      }
    }

    mBuffer = buffer;
    mAcquireFence = acquireFence;
    mLayerBuffer.bufferId = (uint64_t)mBuffer;
    mLayerBuffer.fence = acquireFence;
    mLayerBuffer.changed = true;
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
