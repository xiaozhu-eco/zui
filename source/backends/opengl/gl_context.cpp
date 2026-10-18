#include "backends/opengl/gl_context.h"

#include "log/log.h"

namespace zui {
namespace backends {

bool GlContext::create(IWindow& window) {
    window_ = &window;
    window.gl_make_current();
    if (!gl_.load(window)) {
        ZUI_ERROR("GL load failed");
        return false;
    }
    return true;
}

void GlContext::destroy() { window_ = nullptr; }

}  // namespace backends
}  // namespace zui