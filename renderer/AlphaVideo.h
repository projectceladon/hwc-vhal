#ifndef _ALPHA_VIDEO_H_
#define _ALPHA_VIDEO_H_

#include <map>
#include <memory>

#include "RenderThread.h"
#include "BufferTexture.h"
#include "ShaderProgram.h"

class AlphaVideo : public RenderTask {
public:
    AlphaVideo();
    AlphaVideo(const AlphaVideo& a){
        mBufferTextures = a.mBufferTextures;
    }

    AlphaVideo& operator=(const AlphaVideo& a){
        mBufferTextures = a.mBufferTextures;
        return *this;
    }
    ~AlphaVideo();

    void setBuffer(buffer_handle_t h) {
        mCurrHandle = h;
    }
    buffer_handle_t getOutputHandle() const {
        return (mOutput != nullptr) ? mOutput->getBuffer() : nullptr;
    }

private:
    int run() override;
    void draw(GLuint tex, uint32_t width, uint32_t height);

private:
    std::map<buffer_handle_t, BufferTexture*> mBufferTextures;
    std::unique_ptr<BufferTexture> mOutput;
    std::unique_ptr<ShaderProgram> mAlphaVideoProgram;
    bool mProgramLoaded = false;
    bool mOutputCreated = false;
    buffer_handle_t mCurrHandle = nullptr;
};

#endif
