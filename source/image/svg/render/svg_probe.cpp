#include "image/svg/render/svg_probe.h"

#include "backends/renderer.h"
#include "image/driver.h"

#include <memory>
#include <utility>

#ifdef ZUI_HAS_VULKAN
namespace zui {
namespace backends {
std::unique_ptr<IRenderer> create_vk_renderer();
}
}  // namespace zui
#endif

namespace zui {
namespace image {
namespace svg {

bool probe_svg_raster(const std::uint8_t* bytes, std::size_t size, int& width, int& height,
                      std::vector<std::uint8_t>& rgba, std::string& error) {
    width = 0;
    height = 0;
    rgba.clear();
    error.clear();
#ifndef ZUI_HAS_VULKAN
    (void)bytes;
    (void)size;
    error = "vulkan";
    return false;
#else
    // Leaked for the process, same reason as the frame probe: tearing the device down from a
    // static destructor races SDL. Decode runs only after this renderer has bound the device.
    static backends::IWindow* window = nullptr;
    static backends::IRenderer* renderer = nullptr;
    if (renderer == nullptr) {
        backends::WindowDesc desc;
        desc.title = "zui-svg-probe";
        desc.width = 64;
        desc.height = 64;
        desc.api = backends::GfxApi::Vulkan;
        desc.hidden = true;
        std::unique_ptr<backends::IWindow> created = backends::create_window("sdl3");
        if (created == nullptr || !created->create(desc)) {
            error = "vulkan";
            return false;
        }
        std::unique_ptr<backends::IRenderer> vk = backends::create_vk_renderer();
        if (vk == nullptr || !vk->create(*created)) {
            error = "vulkan";
            return false;
        }
        window = created.release();
        renderer = vk.release();
    }
    (void)window;
    const Driver* driver = DriverManager::instance().find("svg");
    Raster raster;
    if (driver == nullptr || !driver->decode(bytes, size, raster, error) || raster.rgba.empty()) {
        rgba.clear();
        width = 0;
        height = 0;
        return false;
    }
    width = raster.width;
    height = raster.height;
    rgba = std::move(raster.rgba);
    return true;
#endif
}

}  // namespace svg
}  // namespace image
}  // namespace zui
