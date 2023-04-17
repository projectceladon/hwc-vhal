#include <errno.h>
#include <sys/stat.h>
#include <cutils/log.h>

#include "FastBufferDump.h"
#include "BufferMapper.h"

//#define DEBUG_GL

#ifdef DEBUG_GL
#define CHECK() \
{\
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) \
    {\
        ALOGD("%d:glGetError returns %d", __LINE__, err); \
    }\
}
#else
#define CHECK()
#endif

FastBufferDump::FastBufferDump(uint32_t num, uint32_t z, buffer_handle_t b, int type) 
    :mFrameNum(num), mZOrder(z), mHandle(b), mDumpType(type) {
    prepareFolder();
}

void FastBufferDump::prepareFolder() {
    struct stat st;
    bool exist = false;

    mEnableDump = false;
    int ret = stat(kDumpFolder, &st);
    if (ret < 0) {
        if (errno != ENOENT) {
            ALOGE("Failed to access %s:%s", kDumpFolder, strerror(errno));
            return;
        }
    } else {
        if (!(st.st_mode & S_IFDIR)) {  // not a folder
            ALOGD("%s exits but it is not a folder, try to recreate it as folder", kDumpFolder);
            if (unlink(kDumpFolder) < 0) {
                ALOGE("Can't remove %s:%s", kDumpFolder, strerror(errno));
                return;
            }
        } else {
            exist = true;
        }
    }
    // now create the folder
    if (!exist) {
        if (mkdir(kDumpFolder, 0777) < 0) {
            ALOGE("%s doesn't exit but failed to create it:%s", kDumpFolder, strerror(errno));
            return;
        }
    }
    // now check the write access to the folder
    if (access(kDumpFolder, F_OK | W_OK) < 0) {
        ALOGE("Can't write %s, dump is disabled", kDumpFolder);
        return;
    }
    mEnableDump = true;
}

int FastBufferDump::preparePngHeader(FILE* file,
                                   const ImageData* image,
                                   png_structp& png,
                                   png_infop& info) {
  ALOGV("BufferDumper::%s", __func__);

  if (!file || !image)
    return -1;

  png = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (!png) {
    ALOGE("Faild to create png_create_write_struct");
    return -1;
  }
  info = png_create_info_struct(png);
  if (!info) {
    ALOGE("Faild to create png_create_info_struct");
    return -1;
  }
  if (setjmp(png_jmpbuf(png))) {
    ALOGE("Error in create png headers");
    return -1;
  }
  png_init_io(png, file);
  if (setjmp(png_jmpbuf(png))) {
    ALOGE("Error while init io");
    return -1;
  }
  int colorType = image->alpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png, info, image->width, image->height, image->bpc, colorType,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);
  png_write_info(png, info);
  if (setjmp(png_jmpbuf(png))) {
    ALOGE("Failed to write png info");
    return -1;
  }
  return 0;
}

int FastBufferDump::fillPngData(const ImageData* image, png_structp& png) {
  ALOGV("BufferDumper::%s", __func__);

  if (!image)
    return -1;

  uint32_t i;
  for (i = 0; i < image->height; i++) {
    png_write_row(png, image->data + i * image->stride);
  }
  if (setjmp(png_jmpbuf(png))) {
    ALOGE("BufferDumper::Failed to write image data");
    return -1;
  }
  png_write_end(png, nullptr);

  return 0;
}

int FastBufferDump::dumpToPng(const char* path, const ImageData* image) {
  ALOGV("BufferDumper::%s", __func__);

  if (!path || !image)
    return -1;

  png_structp png;
  png_infop info;
  FILE* fp = nullptr;

  ALOGD("Dump buffer to %s", path);
  
  fp = fopen(path, "wb");
  if (!fp) {
    ALOGE("Failed to create PNG file %s", path);
    return -1;
  }

  if (preparePngHeader(fp, image, png, info) < 0) {
    return -1;
  }
  if (fillPngData(image, png) < 0) {
    return -1;
  }

  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return 0;
}

int FastBufferDump::run() {
    if (!mEnableDump)
        return -1;
    BufferTexture bufTex(mHandle, true);
    uint32_t w = bufTex.getWidth();
    uint32_t h = bufTex.getHeight();
    int32_t f = bufTex.getFormat();
    if (f != HAL_PIXEL_FORMAT_RGBA_8888) {
        ALOGD("FastBufferDump: Skip to dump format %d", f);
        return 0;
    }
    uint8_t* data = (uint8_t*)malloc(w * h * 4);
    if (!data) {
        ALOGE("FastBufferDump: failed to alloc readback data buffer");
        return -1;
    }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    CHECK();

    char path[256];
    const char* type[] = {"png", "ppm", "raw"};

    if (mDumpType > 2)
        mDumpType = 0;
    if (mZOrder == UINT32_MAX) {
        snprintf(path, 255, "%s/fb-%d-%dx%d.%s", kDumpFolder, mFrameNum, w, h, type[mDumpType]);
    } else {
        snprintf(path, 255, "%s/frame-%d-layer-%d-%dx%d.%s", kDumpFolder, mFrameNum, mZOrder, w, h, type[mDumpType]);
    }

    if (mDumpType == 1) {
        FILE* fp = fopen(path, "wb");
        if (fp) {
            ALOGD("Dump buffer to %s", path);
            fprintf(fp, "P6\n%d %d\n 255\n", w, h);
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++) {
                    fputc(data[j * w * 4 + i * 4], fp);
                    fputc(data[j * w * 4 + i * 4 + 1], fp);
                    fputc(data[j * w * 4 + i * 4 + 2], fp);
                }
            fclose(fp);
        }
    } else if (mDumpType == 2) {
        FILE* fp = fopen(path, "wb");
        if (fp) {
            ALOGD("Dump buffer to %s", path);
            fwrite(data, w * h, 4, fp);
            fclose(fp);
        }
    } else {
        ImageData image;
        image.width = w;
        image.height = h;
        image.stride = w * 4;
        image.bpc = 8;
        image.alpha = true;
        image.data = data;
        dumpToPng(path, &image);
    }
    return 0;
}
