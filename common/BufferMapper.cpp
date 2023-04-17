//#define LOG_NDEBUG 0
#include <cutils/log.h>

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

  if (NULL == mGralloc)
      return -1;
  if (mGralloc->common.version != HARDWARE_MODULE_API_VERSION(1, 0)) {
      ALOGE("This is Gralloc0, dont support getFunction API.\n");
      return -1;
  }
  if (NULL == mGralloc)
      return -1;
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
    pfnAddCallback = (GRALLOC1_PFN_ADD_CALLBACK)(
      mGralloc->getFunction(mGralloc, GRALLOC1_FUNCTION_ADD_CALLBACK));
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
int BufferMapper::addCallback(gralloc_cb cb, void* ctx) {
  if (pfnAddCallback) {
    return pfnAddCallback(mGralloc, cb, ctx);
  }
  return -1;
}

void BufferMapper::dump(buffer_handle_t b) {
  ALOGD("Dump of buffer %p", b);
  ALOGD("numFds=%d numInts=%d", b->numFds, b->numInts);
  for (int i=0; i < 32; i++) {
    ALOGD("data[%d] = %d", i, b->data[i]);
  }
}

bool BufferMapper::isGralloc1(buffer_handle_t b) {
  return ((b->numFds + b->numInts) * sizeof(int) ==
      sizeof(cros_gralloc_handle) - sizeof(native_handle_t));
}

void BufferMapper::gralloc4ToGralloc1(buffer_handle_t in, struct cros_gralloc_handle* out) {
  cros_gralloc4_handle_t g4h = (cros_gralloc4_handle_t)in;

  out->base.version = g4h->base.version;
  out->base.numFds = g4h->num_planes;
  out->base.numInts = (sizeof(cros_gralloc_handle) - sizeof(native_handle_t)) / sizeof(int) - out->base.numFds;

#define COPY_ELEMENT(e) out->e = g4h->e
  for (int i = 0; i < g4h->num_planes; i++) {
    COPY_ELEMENT(fds[i]);
    COPY_ELEMENT(strides[i]);
    COPY_ELEMENT(offsets[i]);
    COPY_ELEMENT(sizes[i]);
    COPY_ELEMENT(sizes[i]);
    COPY_ELEMENT(format_modifiers[2 * i]);
    COPY_ELEMENT(format_modifiers[2 * i + 1]);
  }
  COPY_ELEMENT(width);
  COPY_ELEMENT(height);
  COPY_ELEMENT(format);
  COPY_ELEMENT(tiling_mode);
  COPY_ELEMENT(pixel_stride);
  COPY_ELEMENT(droid_format);
  COPY_ELEMENT(usage);
}
