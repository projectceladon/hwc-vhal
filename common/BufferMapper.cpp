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
Mao Marc (marc.mao@intel.com)
Ren Chenglei (chenglei.ren@intel.com)
Date: 2021.06.09

*/

//#define LOG_NDEBUG 0
#include <cutils/log.h>
#include <unistd.h>

#include "BufferMapper.h"

BufferMapper::BufferMapper() {
  ALOGV("%s", __func__);
  getGrallocDevice();
}

BufferMapper::~BufferMapper() {
  ALOGV("%s", __func__);
  if (mGralloc) {
    gralloc1_close(mGralloc);
  }
}

int BufferMapper::getGrallocDevice() {
  ALOGV("%s", __func__);

  const hw_module_t* mod = nullptr;

  if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mod) != 0) {
    ALOGE("Failed to load gralloc module");
    return -1;
  }

  if (gralloc1_open(mod, &mGralloc) != 0) {
    ALOGE("Failed to open gralloc1 device");
    return -1;
  }

  if (mGralloc) {
    pfnLock = (GRALLOC1_PFN_LOCK)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_LOCK));
    pfnUnlock = (GRALLOC1_PFN_UNLOCK)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_UNLOCK));
    pfnGetDimensions = (GRALLOC1_PFN_GET_DIMENSIONS)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_GET_DIMENSIONS));
    pfnGetFormat = (GRALLOC1_PFN_GET_FORMAT)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_GET_FORMAT));
    pfnGetStride = (GRALLOC1_PFN_GET_STRIDE)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_GET_STRIDE));
    pfnImportBuffer = (GRALLOC1_PFN_IMPORT_BUFFER)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_IMPORT_BUFFER));
    pfnRelease = (GRALLOC1_PFN_RELEASE)(
        mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_RELEASE));
  }
  return 0;
}

int BufferMapper::getBufferSize(buffer_handle_t b, uint32_t& w, uint32_t& h) {
  ALOGV("%s", __func__);

  if (!b || !pfnGetDimensions) {
    return -1;
  }

  if (pfnGetDimensions(mGralloc, b, &w, &h) != 0) {
    ALOGE("Failed to getDimensions for buffer %p", b);
    return -1;
  }

  return 0;
}

int BufferMapper::getBufferFormat(buffer_handle_t b, int32_t& f) {
  ALOGV("%s", __func__);

  if (!b || !pfnGetFormat) {
    return -1;
  }

  if (pfnGetFormat(mGralloc, b, &f) != 0) {
    ALOGE("Failed to pfnGetFormat for buffer %p", b);
    return -1;
  }

  return 0;
}

int BufferMapper::getBufferStride(buffer_handle_t b, uint32_t& s) {
  ALOGV("%s", __func__);

  if (!b || !pfnGetStride) {
    return -1;
  }

  if (pfnGetStride(mGralloc, b, &s) != 0) {
    ALOGE("Failed to get buffer %p stride", b);
    return -1;
  }

  return 0;
}

int BufferMapper::lockBuffer(buffer_handle_t b, uint8_t*& data, uint32_t& s) {
  ALOGV("%s", __func__);

  if (!b || !pfnLock) {
    return -1;
  }

  uint32_t w, h;
  if (getBufferSize(b, w, h) < 0) {
    ALOGE("Failed to get buffer size for buffer %p", b);
    return -1;
  }
  if (getBufferStride(b, s) < 0) {
    ALOGE("Failed to get buffer %p stride", b);
    return -1;
  }

  gralloc1_rect_t rect = {0, 0, (int32_t)w, (int32_t)h};
  int fenceFd = -1;
  if (pfnLock(mGralloc, b, 0x0, 0x3, &rect, (void**)&data, fenceFd) != 0) {
    ALOGE("Failed to lock buffer %p", b);
    return -1;
  }
  // ALOGD("lock buffer with w=%d h= %d return addr %p stride=%d", w, h, *data,
  // *s);
  return 0;
}

int BufferMapper::unlockBuffer(buffer_handle_t b) {
  ALOGV("%s", __func__);

  if (!b || !pfnUnlock) {
    return -1;
  }

  int releaseFenceFd = -1;

  if (pfnUnlock(mGralloc, b, &releaseFenceFd) != 0) {
    ALOGE("Failed to unlock buffer %p", b);
    return -1;
  }
  if (releaseFenceFd >= 0) {
    close(releaseFenceFd);
  }
  return 0;
}

int BufferMapper::importBuffer(buffer_handle_t b, buffer_handle_t *bufferHandle) {
  ALOGV("%s", __func__);

  if (!b || !pfnImportBuffer) {
    return -1;
  }
  if (pfnImportBuffer(mGralloc, b, bufferHandle) != 0) {
    return -1;
  }
  return 0;
}

int BufferMapper::release(buffer_handle_t b) {
  ALOGV("%s", __func__);

  if (!b || !pfnRelease) {
    return -1;
  }
  if (pfnRelease(mGralloc, b) != 0) {
    return -1;
  }
  return 0;
}
