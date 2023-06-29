#ifndef _VISIBLE_BOUND_DETECT_H_
#define _VISIBLE_BOUND_DETECT_H_

#include <map>
#include <memory>

#include "RenderThread.h"
#include "BufferTexture.h"
#include "ShaderProgram.h"

struct PixelStat {
    uint32_t count[4];
    int32_t  bound[4];
};

class VisibleBoundDetect : public RenderTask {
public:
    VisibleBoundDetect();
    VisibleBoundDetect(const VisibleBoundDetect& h){
        for (int i = 0; i < 2; i++) {
            mPixelStatBuffer[i] = h.mPixelStatBuffer[i];
        }
    }

    VisibleBoundDetect& operator=(const VisibleBoundDetect& h){
        for (int i = 0; i < 2; i++) {
            mPixelStatBuffer[i] = h.mPixelStatBuffer[i];
        }
        return *this;
    }
    ~VisibleBoundDetect();

    void setBuffer(buffer_handle_t h) {
        mCurrHandle = h;
    }
    const PixelStat* getResult() const {
        return &mResult;
    }

private:
    int run() override;

    void createPixelStatBuffers(uint32_t count);
    void runImagePixelStat(GLuint tex, uint32_t width, uint32_t height, uint32_t outW, uint32_t outH);
    void runPixelStatMerge(uint32_t inW, uint32_t inH);
    void readbackResult(GLuint ssbo);

private:
    std::map<buffer_handle_t, BufferTexture*> mBufferTextures;
    GLuint mPixelStatBuffer[2] = {0,};
    std::unique_ptr<ShaderProgram> mImagePixelStat;
    std::unique_ptr<ShaderProgram> mPixelStatMerge;
    PixelStat mResult;
    bool mProgramLoaded = false;
    bool mStatBufferCreated = false;
    buffer_handle_t mCurrHandle = nullptr;
};

#endif
