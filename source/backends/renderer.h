#ifndef ZUI_BACKENDS_RENDERER_H
#define ZUI_BACKENDS_RENDERER_H

#include "backends/window.h"
#include "paint/display_list.h"

#include <zui/export.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace zui {
namespace backends {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool create(IWindow& window) = 0;
    virtual void destroy() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void begin_frame() = 0;
    virtual void submit(const paint::DisplayList& list) = 0;
    virtual void present() = 0;
    /// RGBA, top-left origin, of the framebuffer just submitted. Not an application API.
    /// The default is false. A backend implements it only where `ZUI_PROBE_FRAME` is 1.
    virtual bool read_frame(std::vector<std::uint8_t>& rgba, int& width, int& height) {
        rgba.clear();
        width = 0;
        height = 0;
        return false;
    }
    virtual const char* name() const = 0;
};

/// Empty kind → getenv ZUI_WINDOW (default sdl3). "ios"/"android" → mobile stubs (XP-2).
ZUI_EXPORT std::unique_ptr<IWindow> create_window(std::string_view kind);
ZUI_EXPORT std::unique_ptr<IRenderer> create_renderer(std::string_view kind);

GfxApi gfx_api_from_renderer(std::string_view kind);

}  // namespace backends
}  // namespace zui

#endif
