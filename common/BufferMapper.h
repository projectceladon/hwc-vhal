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

#ifndef __BUFFER_MAPPER_H__
#define __BUFFER_MAPPER_H__

#include <hardware/gralloc1.h>
#include <hardware/hwcomposer2.h>
#include <system/graphics.h>

class BufferMapper {
 public:
  ~BufferMapper();

  static BufferMapper& getMapper() {
    static BufferMapper sInst;
    return sInst;
  }

  int getBufferSize(buffer_handle_t b, uint32_t& w, uint32_t& h);
  int getBufferFormat(buffer_handle_t b, int32_t& f);
  int getBufferStride(buffer_handle_t b, uint32_t& s);
  int lockBuffer(buffer_handle_t b, uint8_t*& data, uint32_t& s);
  int unlockBuffer(buffer_handle_t b);
  int importBuffer(buffer_handle_t b, buffer_handle_t *bufferHandle);
  int release(buffer_handle_t b);

 private:
  BufferMapper();
  int getGrallocDevice();

 private:
  gralloc1_device_t* mGralloc = nullptr;
  GRALLOC1_PFN_LOCK pfnLock = nullptr;
  GRALLOC1_PFN_UNLOCK pfnUnlock = nullptr;
  GRALLOC1_PFN_GET_DIMENSIONS pfnGetDimensions = nullptr;
  GRALLOC1_PFN_GET_FORMAT pfnGetFormat = nullptr;
  GRALLOC1_PFN_GET_STRIDE pfnGetStride = nullptr;
  GRALLOC1_PFN_IMPORT_BUFFER pfnImportBuffer = nullptr;
  GRALLOC1_PFN_RELEASE pfnRelease = nullptr;
};
#endif
