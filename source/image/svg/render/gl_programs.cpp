#include "image/svg/render/gl_programs.h"

#include "log/log.h"

#include <fstream>
#include <iterator>
#include <string>

namespace zui {
namespace image {
namespace svg {
namespace {

bool read_shader(const char* name, std::string& source, std::string& error) {
    const std::string path = std::string(ZUI_SOURCE_DIR) + "/source/image/svg/shaders/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ZUI_ERROR("svg shader missing {}", path);
        error = "svg shader";
        return false;
    }
    source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (source.empty()) {
        error = "svg shader";
        return false;
    }
    return true;
}

backends::GLuint compile_compute(backends::GL& gl, const std::string& source, std::string& error) {
    const char* text = source.c_str();
    const backends::GLuint shader = gl.CreateShader(backends::GL_COMPUTE_SHADER);
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    backends::GLint ok = 0;
    gl.GetShaderiv(shader, backends::GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.GetShaderInfoLog(shader, static_cast<backends::GLsizei>(sizeof(log)), nullptr, log);
        ZUI_ERROR("svg shader: {}", log);
        gl.DeleteShader(shader);
        error = "svg shader";
        return 0;
    }
    const backends::GLuint program = gl.CreateProgram();
    gl.AttachShader(program, shader);
    gl.LinkProgram(program);
    gl.DeleteShader(shader);
    gl.GetProgramiv(program, backends::GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.GetProgramInfoLog(program, static_cast<backends::GLsizei>(sizeof(log)), nullptr, log);
        ZUI_ERROR("svg shader: {}", log);
        gl.DeleteProgram(program);
        error = "svg shader";
        return 0;
    }
    if (gl.GetUniformBlockIndex != nullptr && gl.UniformBlockBinding != nullptr) {
        const backends::GLuint block = gl.GetUniformBlockIndex(program, "ParamsBlock");
        if (block != 0xffffffffu) {
            gl.UniformBlockBinding(program, block, 0);
        }
    }
    return program;
}

}  // namespace

bool GlPrograms::create(backends::GL& gl, std::string& error) {
    destroy(gl);
    const char* names[] = {"flatten.comp", "clear.comp", "raster.comp", "composite_image.comp",
                           "composite_layer.comp"};
    backends::GLuint* slots[] = {&flatten_, &clear_, &raster_, &image_, &layer_};
    for (int i = 0; i < 5; ++i) {
        std::string source;
        if (!read_shader(names[i], source, error)) {
            destroy(gl);
            return false;
        }
        *slots[i] = compile_compute(gl, source, error);
        if (*slots[i] == 0) {
            destroy(gl);
            return false;
        }
    }
    const backends::GLint sampler = gl.GetUniformLocation(image_, "zui_tex");
    if (sampler >= 0) {
        gl.UseProgram(image_);
        gl.Uniform1i(sampler, 6);
        gl.UseProgram(0);
    }
    return true;
}

void GlPrograms::destroy(backends::GL& gl) {
    const backends::GLuint ids[] = {flatten_, clear_, raster_, image_, layer_};
    for (backends::GLuint id : ids) {
        if (id != 0 && gl.DeleteProgram != nullptr) {
            gl.DeleteProgram(id);
        }
    }
    flatten_ = clear_ = raster_ = image_ = layer_ = 0;
}

backends::GLuint GlPrograms::program(FramePlan::Op op) const {
    switch (op) {
    case FramePlan::Op::Flatten:
        return flatten_;
    case FramePlan::Op::Clear:
        return clear_;
    case FramePlan::Op::Raster:
        return raster_;
    case FramePlan::Op::Image:
        return image_;
    case FramePlan::Op::Layer:
        return layer_;
    }
    return 0;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
