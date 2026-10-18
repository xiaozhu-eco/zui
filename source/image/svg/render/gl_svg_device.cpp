#include "image/svg/render/gl_svg_device.h"

#include "image/svg/render/frame_plan.h"
#include "image/svg/types/gpu_layout.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace zui {
namespace image {
namespace svg {
namespace {

backends::GLuint make_buffer(backends::GL& gl, backends::GLenum target, backends::GLsizeiptr size, const void* data) {
    backends::GLuint id = 0;
    gl.GenBuffers(1, &id);
    gl.BindBuffer(target, id);
    gl.BufferData(target, size, data, backends::GL_DYNAMIC_DRAW);
    return id;
}

void bind_storage(backends::GL& gl, backends::GLuint curves, backends::GLuint segs, backends::GLuint shapes,
                  backends::GLuint grads, backends::GLuint pixels) {
    gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, 1, curves);
    gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, 2, segs);
    gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, 3, shapes);
    gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, 4, grads);
    gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, 5, pixels);
}

}  // namespace

void GlSvgDevice::attach(backends::GlContext& context, backends::GLuint ui_program) {
    context_ = &context;
    ui_program_ = ui_program;
}

void GlSvgDevice::shutdown() {
    if (context_ != nullptr && context_->window_ != nullptr) {
        programs_.destroy(context_->gl_);
    }
    context_ = nullptr;
    ui_program_ = 0;
}

bool GlSvgDevice::supports_compute() const {
    if (context_ == nullptr || context_->window_ == nullptr) {
        return false;
    }
    const backends::GL& gl = context_->gl_;
    if (gl.GetIntegerv == nullptr || gl.DispatchCompute == nullptr || gl.BindBufferBase == nullptr ||
        gl.MemoryBarrier == nullptr || gl.GetBufferSubData == nullptr) {
        return false;
    }
    context_->window_->gl_make_current();
    backends::GLint major = 0;
    backends::GLint minor = 0;
    context_->gl_.GetIntegerv(backends::GL_MAJOR_VERSION, &major);
    context_->gl_.GetIntegerv(backends::GL_MINOR_VERSION, &minor);
    return major > 4 || (major == 4 && minor >= 5);
}

bool GlSvgDevice::render(const Scene& scene, Raster& out, std::string& error) {
    out = Raster{};
    if (!supports_compute()) {
        error = kSvgUnsupported;
        return false;
    }
    backends::GL& gl = context_->gl_;
    if (!programs_.ready() && !programs_.create(gl, error)) {
        return false;
    }
    FramePlan plan;
    if (!FramePlan::build(scene, plan, error)) {
        return false;
    }

    const backends::GLsizeiptr curve_bytes =
        static_cast<backends::GLsizeiptr>(std::max<std::size_t>(scene.curves.size(), 1) * sizeof(GpuCurve));
    const backends::GLsizeiptr seg_bytes = static_cast<backends::GLsizeiptr>(
        std::max<std::size_t>(scene.curves.size(), 1) * static_cast<std::size_t>(kSamplesPerCurve) * sizeof(GpuSeg));
    const backends::GLsizeiptr shape_bytes =
        static_cast<backends::GLsizeiptr>(std::max<std::size_t>(scene.shapes.size(), 1) * sizeof(GpuShape));
    const backends::GLsizeiptr grad_bytes =
        static_cast<backends::GLsizeiptr>(std::max<std::size_t>(scene.gradients.size(), 1) * sizeof(GpuGrad));
    const backends::GLsizeiptr pixel_bytes = static_cast<backends::GLsizeiptr>(
        static_cast<std::size_t>(plan.layers) * static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height) *
        sizeof(std::uint32_t));

    const backends::GLuint curves = make_buffer(
        gl, backends::GL_SHADER_STORAGE_BUFFER, curve_bytes, scene.curves.empty() ? nullptr : scene.curves.data());
    const backends::GLuint segs = make_buffer(gl, backends::GL_SHADER_STORAGE_BUFFER, seg_bytes, nullptr);
    const backends::GLuint shapes = make_buffer(
        gl, backends::GL_SHADER_STORAGE_BUFFER, shape_bytes, scene.shapes.empty() ? nullptr : scene.shapes.data());
    const backends::GLuint grads = make_buffer(gl, backends::GL_SHADER_STORAGE_BUFFER, grad_bytes,
                                              scene.gradients.empty() ? nullptr : scene.gradients.data());
    const backends::GLuint pixels = make_buffer(gl, backends::GL_SHADER_STORAGE_BUFFER, pixel_bytes, nullptr);
    const backends::GLuint params = make_buffer(gl, backends::GL_UNIFORM_BUFFER, sizeof(GpuParams), nullptr);
    bind_storage(gl, curves, segs, shapes, grads, pixels);

    std::vector<backends::GLuint> textures;
    for (const FramePlan::Item& item : plan.items) {
        const backends::GLuint program = programs_.program(item.op);
        gl.UseProgram(program);
        gl.BindBuffer(backends::GL_UNIFORM_BUFFER, params);
        gl.BufferData(backends::GL_UNIFORM_BUFFER, sizeof(GpuParams), &item.params, backends::GL_DYNAMIC_DRAW);
        gl.BindBufferBase(backends::GL_UNIFORM_BUFFER, 0, params);
        if (item.op == FramePlan::Op::Image && item.sprite >= 0 &&
            item.sprite < static_cast<int>(scene.sprites.size())) {
            const Sprite& sprite = scene.sprites[static_cast<std::size_t>(item.sprite)];
            backends::GLuint texture = 0;
            gl.GenTextures(1, &texture);
            gl.ActiveTexture(backends::GL_TEXTURE0 + 6);
            gl.BindTexture(backends::GL_TEXTURE_2D, texture);
            gl.TexParameteri(backends::GL_TEXTURE_2D, backends::GL_TEXTURE_MIN_FILTER, backends::GL_NEAREST);
            gl.TexParameteri(backends::GL_TEXTURE_2D, backends::GL_TEXTURE_MAG_FILTER, backends::GL_NEAREST);
            gl.TexParameteri(backends::GL_TEXTURE_2D, backends::GL_TEXTURE_WRAP_S, backends::GL_CLAMP_TO_EDGE);
            gl.TexParameteri(backends::GL_TEXTURE_2D, backends::GL_TEXTURE_WRAP_T, backends::GL_CLAMP_TO_EDGE);
            gl.PixelStorei(backends::GL_UNPACK_ALIGNMENT, 1);
            gl.TexImage2D(backends::GL_TEXTURE_2D, 0, backends::GL_RGBA8, sprite.width, sprite.height, 0,
                          backends::GL_RGBA, backends::GL_UNSIGNED_BYTE, sprite.rgba.data());
            textures.push_back(texture);
        }
        gl.MemoryBarrier(backends::GL_SHADER_STORAGE_BARRIER_BIT);
        gl.DispatchCompute(static_cast<backends::GLuint>(item.groups_x), static_cast<backends::GLuint>(item.groups_y),
                           static_cast<backends::GLuint>(item.groups_z));
    }
    gl.MemoryBarrier(backends::GL_SHADER_STORAGE_BARRIER_BIT | backends::GL_BUFFER_UPDATE_BARRIER_BIT);
    gl.BindBuffer(backends::GL_SHADER_STORAGE_BUFFER, pixels);
    const std::size_t count = static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height);
    std::vector<std::uint32_t> packed(count);
    gl.GetBufferSubData(backends::GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<backends::GLsizeiptr>(count * sizeof(std::uint32_t)), packed.data());

    out.width = plan.width;
    out.height = plan.height;
    out.rgba.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t pixel = packed[i];
        out.rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixel & 255u);
        out.rgba[i * 4 + 1] = static_cast<std::uint8_t>((pixel >> 8) & 255u);
        out.rgba[i * 4 + 2] = static_cast<std::uint8_t>((pixel >> 16) & 255u);
        out.rgba[i * 4 + 3] = static_cast<std::uint8_t>((pixel >> 24) & 255u);
    }

    const backends::GLuint ids[] = {curves, segs, shapes, grads, pixels, params};
    gl.DeleteBuffers(static_cast<backends::GLsizei>(6), ids);
    if (!textures.empty()) {
        gl.DeleteTextures(static_cast<backends::GLsizei>(textures.size()), textures.data());
    }
    for (backends::GLuint slot = 0; slot <= 5; ++slot) {
        gl.BindBufferBase(backends::GL_SHADER_STORAGE_BUFFER, slot, 0);
    }
    gl.BindBufferBase(backends::GL_UNIFORM_BUFFER, 0, 0);
    gl.ActiveTexture(backends::GL_TEXTURE0 + 6);
    gl.BindTexture(backends::GL_TEXTURE_2D, 0);
    gl.ActiveTexture(backends::GL_TEXTURE0);
    gl.UseProgram(ui_program_);
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
