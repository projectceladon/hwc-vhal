#ifndef _BUFFER_TEXTURE_H_
#define _BUFFER_TEXTURE_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <cutils/native_handle.h>
#include <ui/GraphicBuffer.h>

using namespace android;

class BufferTexture {
public:
    BufferTexture(buffer_handle_t b, bool fbo = false);
    BufferTexture(uint32_t width, uint32_t height, int32_t format);
    BufferTexture(const BufferTexture& b){
        mTexture = b.mTexture;
        mFBO = b.mFBO;
    }

    BufferTexture& operator=(const BufferTexture& b){
        mTexture = b.mTexture;
        mFBO = b.mFBO;
        return *this;
    }
    ~BufferTexture();

    GLuint getTexture() const {
        return mTexture;
    }
    buffer_handle_t getBuffer() const {
        return  mBuffer ? mBuffer->getNativeBuffer()->handle : nullptr;
    }
    GLuint getFBO() const {
        return mFBO;
    }

    uint32_t getWidth() const {
        return mWidth;
    }
    uint32_t getHeight() const {
        return mHeight;
    }
    int32_t getFormat() const {
        return mFormat;
    }
private:
    sp<GraphicBuffer> mBuffer = nullptr;
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLImage mImage = EGL_NO_IMAGE_KHR;
    GLuint mTexture = 0;
    GLuint mFBO = 0;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    int32_t  mFormat = -1;
};

#endif
