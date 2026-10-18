#ifndef ZUI_IMAGE_SVG_RENDER_GL_SVG_DEVICE_H
#define ZUI_IMAGE_SVG_RENDER_GL_SVG_DEVICE_H

#include "backends/opengl/gl_context.h"
#include "image/svg/render/device.h"
#include "image/svg/render/gl_programs.h"

namespace zui {
namespace image {
namespace svg {

class GlSvgDevice final : public Device {
public:
    void attach(backends::GlContext& context, backends::GLuint ui_program);
    void shutdown();

    bool supports_compute() const override;
    bool render(const Scene& scene, Raster& out, std::string& error) override;

private:
    backends::GlContext* context_ = nullptr;
    backends::GLuint ui_program_ = 0;
    GlPrograms programs_{};
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
