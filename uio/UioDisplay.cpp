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
Ren Chenglei (chenglei.ren@intel.com)
Tang Shaofeng (shaofeng.tang@intel.com)
Date: 2021.06.09

*/

#include "UioDisplay.h"
#include <cutils/log.h>

UioDisplay::UioDisplay(int id, int w, int h)
    : mDisplayId(id), frame_id(0), mWidth(w), mHeight(h) {
  ALOGV("%s", __func__);
}

UioDisplay::~UioDisplay() {
  ALOGV("%s", __func__);
}

int UioDisplay::uioOpenFile(const char * shmDevice, const char * file) {
  ALOGV("%s", __func__);
  int len = snprintf(NULL, 0, "/sys/class/uio/%s%d/%s", shmDevice, mDisplayId, file);
  char * path = (char *)malloc(len + 1);
  sprintf(path, "/sys/class/uio/%s%d/%s", shmDevice, mDisplayId, file);
  int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    close(fd);
    free(path);
    return -1;
  }

  free(path);
  return fd;
}

int UioDisplay::shmOpenDev(const char * shmDevice) {
  ALOGV("%s", __func__);
  int len  = snprintf(NULL, 0, "/dev/%s%d", shmDevice, mDisplayId);
  char * path = (char *)malloc(len + 1);
  sprintf(path, "/dev/%s%d", shmDevice, mDisplayId);
  int fd = open(path, O_RDWR);
  if (fd < 0)
  {
    close(fd);
    free(path);
    return -1;
  }

  free(path);
  return fd;
}

int UioDisplay::init() {
  ALOGV("%s", __func__);
  int fd = uioOpenFile("uio", "maps/map0/size");
  if (fd < 0)
  {
    ALOGE("Failed to OpenFile:%d\n", fd);
    close(fd);
    return -1;
  }
  char size[32];
  int  len = read(fd, size, sizeof(size) - 1);
  if (len <= 0)
  {
    ALOGE("Failed to read size:%d\n", len);
    close(fd);
    return -1;
  }
  size[len] = '\0';
  close(fd);
  unsigned int shmSize = strtoul(size, NULL, 16);
  int shmFd = shmOpenDev("uio");
  if (shmFd < 0)
  {
    ALOGE("Failed to OpenDev:%d\n", shmFd);
    close(shmFd);
    return -1;
  }
  uint8_t * access_address = NULL;
  access_address = (uint8_t *)mmap(NULL, shmSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED, shmFd, 0);
  if (access_address == MAP_FAILED)
  {
     ALOGE("Failed to mmap");
     return -1;
  }
  app.shmHeader        = (KVMFRHeader *)access_address;
  app.pointerData      = (uint8_t *)ALIGN_UP(access_address + sizeof(KVMFRHeader));
  app.pointerDataSize  = 1024; // 1Kb for pointer, Android doesn't need this in fact
  app.pointerOffset    = app.pointerData - access_address;
  app.frames           = (uint8_t *)ALIGN_UP(app.pointerData + app.pointerDataSize);
  app.frameSize        = ALIGN_DN((shmSize - (app.frames - access_address)) / MAX_FRAMES);
  for (int i = 0; i < MAX_FRAMES; ++i)
  {
    app.frame      [i] = app.frames + i * app.frameSize;
    app.frameOffset[i] = app.frame[i] - access_address;
  }
  // initialize the shared memory headers
  memcpy(app.shmHeader->magic, KVMFR_HEADER_MAGIC, sizeof(KVMFR_HEADER_MAGIC));
  app.shmHeader->version = KVMFR_HEADER_VERSION;
  // zero and notify the client we are starting
  memset(&(app.shmHeader->frame ), 0, sizeof(KVMFRFrame ));
  app.shmHeader->flags &= ~KVMFR_HEADER_FLAG_RESTART;
  app.running = true;
  mThread = std::unique_ptr<std::thread>(new std::thread(&UioDisplay::threadProc, this));
  return 0;
}

int UioDisplay::postFb(buffer_handle_t fb) {
  ALOGV("%s", __func__);
  if (app.running) {
    app.shmHeader->flags &= ~KVMFR_HEADER_FLAG_READY;
    volatile KVMFRFrame * fi = &(app.shmHeader->frame);
    uint8_t* rgb = nullptr;
    uint32_t stride = 0;
    auto& mapper = BufferMapper::getMapper();
    buffer_handle_t bufferHandle;
    mapper.importBuffer(fb, &bufferHandle);
    mapper.lockBuffer(bufferHandle, rgb, stride);
    if (rgb) {
      for (uint32_t i = 0; i < mHeight; i++) {
        memcpy(app.frame[frame_id] + i * mWidth * 4, rgb + i * stride * 4, mWidth * 4);
      }
      fi->type = FRAME_TYPE_RGBA;
      fi->width   = mWidth;
      fi->height  = mHeight;
      fi->stride  = stride;
      fi->pitch   = fi->width * 4;
      fi->dataPos = app.frameOffset[frame_id];
      fi->flags = KVMFR_FRAME_FLAG_UPDATE;
      fi->rotate = mRot;
    } else {
      ALOGE("Failed to lock front buffer\n");
    }

    mapper.unlockBuffer(bufferHandle);
    mapper.release(bufferHandle);
    if(++frame_id >= 2)
      frame_id = 0;
  }
  return 0;
}

void UioDisplay::threadProc() {
  while (true) {
    if (app.shmHeader->flags & KVMFR_HEADER_FLAG_RESTART)
      app.shmHeader->flags &= ~KVMFR_HEADER_FLAG_RESTART;
    usleep(16000);
  }
}
