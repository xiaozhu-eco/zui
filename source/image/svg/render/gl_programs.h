#ifndef ZUI_IMAGE_SVG_RENDER_GL_PROGRAMS_H
#define ZUI_IMAGE_SVG_RENDER_GL_PROGRAMS_H

#include "backends/opengl/gl_functions.h"
#include "image/svg/render/frame_plan.h"

#include <string>

namespace zui {
namespace image {
namespace svg {

class GlPrograms {
public:
    bool create(backends::GL& gl, std::string& error);
    void destroy(backends::GL& gl);
    bool ready() const { return flatten_ != 0; }
    backends::GLuint program(FramePlan::Op op) const;

    backends::GLuint flatten_ = 0;
    backends::GLuint clear_ = 0;
    backends::GLuint raster_ = 0;
    backends::GLuint image_ = 0;
    backends::GLuint layer_ = 0;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
