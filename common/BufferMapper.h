#ifndef __BUFFER_MAPPER_H__
#define __BUFFER_MAPPER_H__

#include <hardware/gralloc1.h>
#include <hardware/hwcomposer2.h>
#include <system/graphics.h>

#include "gralloc_handle.h"

const int32_t GRALLOC1_FUNCTION_ADD_CALLBACK = 108;
enum {
  GRALLOC_EVENT_ALLOCATE  = 0,
  GRALLOC_EVENT_RETAIN    = 1,
  GRALLOC_EVENT_RELEASE   = 2,
};

typedef void (*gralloc_cb)(void* ctx, int event, const buffer_handle_t buffer);
typedef int32_t /*gralloc1_error_t*/ (*GRALLOC1_PFN_ADD_CALLBACK)(
    gralloc1_device_t *device,  gralloc_cb cb, void* ctx);

class BufferMapper {
 public:
  BufferMapper(const BufferMapper& b){
    mGralloc = b.mGralloc;
  }

  BufferMapper& operator=(const BufferMapper& b){
    mGralloc = b.mGralloc;
    return *this;
  }

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
  int addCallback(gralloc_cb cb, void* ctx);

  void dump(buffer_handle_t b);
  bool isGralloc1(buffer_handle_t b);
  void gralloc4ToGralloc1(buffer_handle_t in, struct cros_gralloc_handle* out);

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
  GRALLOC1_PFN_ADD_CALLBACK pfnAddCallback = nullptr;

};
#endif
