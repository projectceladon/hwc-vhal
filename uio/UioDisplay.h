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

#ifndef __UIO_DISPLAY__H
#define __UIO_DISPLAY__H

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <thread>
#include "BufferMapper.h"

#define ALIGN_DN(x) ((uintptr_t)(x) & ~0x7F)
#define ALIGN_UP(x) ALIGN_DN(x + 0x7F)
#define KVMFR_HEADER_MAGIC   "[[KVMFR]]"
#define KVMFR_HEADER_VERSION 8
#define MAX_FRAMES 2

#define KVMFR_FRAME_FLAG_UPDATE 1 // frame update available

#define KVMFR_HEADER_FLAG_RESTART 1 // restart signal from client
#define KVMFR_HEADER_FLAG_READY   2 // ready signal from client
#define KVMFR_HEADER_FLAG_PAUSED  4 // capture has been paused by the host

class UioDisplay {

 public:
  enum FrameType
  {
    FRAME_TYPE_INVALID   ,
    FRAME_TYPE_BGRA      , // BGRA interleaved: B,G,R,A 32bpp
    FRAME_TYPE_RGBA      , // RGBA interleaved: R,G,B,A 32bpp
    FRAME_TYPE_RGBA10    , // RGBA interleaved: R,G,B,A 10,10,10,2 bpp
    FRAME_TYPE_YUV420    , // YUV420
    FRAME_TYPE_MAX       , // sentinel value
  };
  struct KVMFRFrame
  {
    uint8_t     flags;       // KVMFR_FRAME_FLAGS
    FrameType   type;        // the frame data type
    uint32_t    width;       // the width
    uint32_t    height;      // the height
    uint32_t    stride;      // the row stride (zero if compressed data)
    uint32_t    pitch;       // the row pitch  (stride in bytes or the compressed frame size)
    uint64_t    dataPos;     // offset to the frame
    uint8_t     rotate;      // the frame rotation
  };
  struct KVMFRHeader
  {
    char        magic[sizeof(KVMFR_HEADER_MAGIC)];
    uint32_t    version;     // version of this structure
    uint8_t     flags;       // KVMFR_HEADER_FLAGS
    KVMFRFrame  frame;       // the frame information
  };
  struct app
  {
    KVMFRHeader * shmHeader;
    uint8_t     * pointerData;
    unsigned int  pointerDataSize;
    unsigned int  pointerOffset;
    uint8_t     * frames;
    unsigned int  frameSize;
    uint8_t     * frame[MAX_FRAMES];
    unsigned int  frameOffset[MAX_FRAMES];
    bool          running;
  };

 public:
  UioDisplay(int id, int w, int h);
  ~UioDisplay();
  int postFb(buffer_handle_t fb);
  int init();
  void setRotation(int rot) {
    mRot = rot;
  }

 private:
  int mDisplayId = 0;
  struct app app;
  int frame_id = 0;
  uint32_t mWidth = 720;
  uint32_t mHeight = 1280;
  int mRot = 0;
  std::unique_ptr<std::thread> mThread;

 private:
  int uioOpenFile(const char * shmDevice, const char * file);
  int shmOpenDev(const char * shmDevice);
  void threadProc();

};

#endif
