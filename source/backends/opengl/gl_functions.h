#ifndef ZUI_SOURCE_BACKENDS_OPENGL_GL_FUNCTIONS_H
#define ZUI_SOURCE_BACKENDS_OPENGL_GL_FUNCTIONS_H

// Minimal GL/GLES function-pointer table. The window backend supplies the proc addresses both
// for desktop GL and GLES, so the renderer never links a GL library directly.

#include "backends/window.h"

namespace zui {
namespace backends {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLchar = char;
using GLboolean = unsigned char;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;
using GLbitfield = unsigned int;
using GLfloat = float;

constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_ZERO = 0;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_TRUE = 1;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11;
constexpr GLenum GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000;
constexpr GLenum GL_BUFFER_UPDATE_BARRIER_BIT = 0x00000200;
constexpr GLenum GL_MAJOR_VERSION = 0x821B;
constexpr GLenum GL_MINOR_VERSION = 0x821C;

struct GL {
    void (*Clear)(GLenum) = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*BlendFunc)(GLenum, GLenum) = nullptr;
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    void (*CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform4fv)(GLint, GLsizei, const GLfloat*) = nullptr;
    void (*Uniform2fv)(GLint, GLsizei, const GLfloat*) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    void (*VertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void*) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void*) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,
                            GLenum) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    void (*DispatchCompute)(GLuint, GLuint, GLuint) = nullptr;
    void (*BindBufferBase)(GLenum, GLuint, GLuint) = nullptr;
    void (*MemoryBarrier)(GLbitfield) = nullptr;
    void (*GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void*) = nullptr;
    GLuint (*GetUniformBlockIndex)(GLuint, const GLchar*) = nullptr;
    void (*UniformBlockBinding)(GLuint, GLuint, GLuint) = nullptr;

    bool load(IWindow& w) {
#define ZUI_GL(name) name = reinterpret_cast<decltype(name)>(w.gl_proc("gl" #name))
        ZUI_GL(Clear);
        ZUI_GL(ClearColor);
        ZUI_GL(Viewport);
        ZUI_GL(Enable);
        ZUI_GL(Disable);
        ZUI_GL(BlendFunc);
        ZUI_GL(BlendFuncSeparate);
        ZUI_GL(CopyTexSubImage2D);
        ZUI_GL(Scissor);
        ZUI_GL(CreateShader);
        ZUI_GL(ShaderSource);
        ZUI_GL(CompileShader);
        ZUI_GL(GetShaderiv);
        ZUI_GL(GetShaderInfoLog);
        ZUI_GL(DeleteShader);
        ZUI_GL(CreateProgram);
        ZUI_GL(AttachShader);
        ZUI_GL(LinkProgram);
        ZUI_GL(GetProgramiv);
        ZUI_GL(GetProgramInfoLog);
        ZUI_GL(DeleteProgram);
        ZUI_GL(UseProgram);
        ZUI_GL(GetUniformLocation);
        ZUI_GL(Uniform2f);
        ZUI_GL(Uniform1i);
        ZUI_GL(Uniform1f);
        ZUI_GL(Uniform4f);
        ZUI_GL(Uniform4fv);
        ZUI_GL(Uniform2fv);
        ZUI_GL(GenBuffers);
        ZUI_GL(BindBuffer);
        ZUI_GL(BufferData);
        ZUI_GL(BufferSubData);
        ZUI_GL(GenVertexArrays);
        ZUI_GL(BindVertexArray);
        ZUI_GL(EnableVertexAttribArray);
        ZUI_GL(VertexAttribPointer);
        ZUI_GL(VertexAttribIPointer);
        ZUI_GL(DrawArrays);
        ZUI_GL(GenTextures);
        ZUI_GL(BindTexture);
        ZUI_GL(TexImage2D);
        ZUI_GL(TexParameteri);
        ZUI_GL(ActiveTexture);
        ZUI_GL(PixelStorei);
        ZUI_GL(ReadPixels);
        ZUI_GL(GenFramebuffers);
        ZUI_GL(DeleteFramebuffers);
        ZUI_GL(BindFramebuffer);
        ZUI_GL(BlitFramebuffer);
        ZUI_GL(FramebufferTexture2D);
        ZUI_GL(CheckFramebufferStatus);
        ZUI_GL(DeleteBuffers);
        ZUI_GL(DeleteVertexArrays);
        ZUI_GL(DeleteTextures);
        ZUI_GL(GetIntegerv);
        ZUI_GL(DispatchCompute);
        ZUI_GL(BindBufferBase);
        ZUI_GL(MemoryBarrier);
        ZUI_GL(GetBufferSubData);
        ZUI_GL(GetUniformBlockIndex);
        ZUI_GL(UniformBlockBinding);
#undef ZUI_GL
        return CreateShader && DrawArrays && GenVertexArrays;
    }
};

}  // namespace backends
}  // namespace zui

#endif
