#ifndef _SHADER_PROGRAM_H_
#define _SHADER_PROGRAM_H_

#include <map>
#include <string>

#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

class ShaderProgram {
public:
    ShaderProgram(const char* pComputeSource);
    ShaderProgram(const char* pVertexSource, const char* pFragmentSource);
    ShaderProgram(const ShaderProgram& s){
        mProgram = s.mProgram;
        mShaders = s.mShaders;
    }

    ShaderProgram& operator=(const ShaderProgram& s){
        mProgram = s.mProgram;
        mShaders = s.mShaders;
        return *this;
    }
    ~ShaderProgram();

    GLuint getProgram() const { return mProgram; }
    GLint getUniformLocation(const char* name);
    GLint getAttribLocation(const char* name);

private:
    GLuint loadShader(GLenum shaderType, const char* pSource);
    GLuint createComputeProgram(const char* pComputeSource);
    GLuint createGraphicsProgram(const char* pVertexSource, const char* pFragmentSource);

private:
    GLuint mProgram = 0;
    bool mIsComputeProgram = false;
    std::map<GLenum, GLuint> mShaders;
    std::map<std::string, GLint> mUniformLoctions;
    std::map<std::string, GLint> mAttribLoctions;
};

#endif

