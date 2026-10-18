#ifndef ZUI_SOURCE_BACKENDS_OPENGL_GL_CONTEXT_H
#define ZUI_SOURCE_BACKENDS_OPENGL_GL_CONTEXT_H

// Window binding plus the loaded GL/GLES entry points and framebuffer facts.

#include "backends/opengl/gl_functions.h"
#include "backends/window.h"

namespace zui {
namespace backends {

class GlContext {
public:
    bool create(IWindow& window);
    void destroy();

    GL gl_;
    IWindow* window_ = nullptr;
    int fb_w_ = 1;
    int fb_h_ = 1;
    float scale_ = 1.f;
    /// Framebuffer begin_frame bound. Layer passes rebind and then restore this. GetIntegerv
    /// is not loaded, so the renderer records the id instead of querying it.
    GLuint draw_fbo_ = 0;
};

}  // namespace backends
}  // namespace zui

#endif