#include "backends/vulkan/renderer_impl.h"

#ifdef ZUI_HAS_VULKAN
namespace zui {
namespace backends {

std::unique_ptr<IRenderer> create_vk_renderer() { return std::make_unique<VkRenderer>(); }

ProbeFrame probe_read_frame_vk(const paint::DisplayList& list, int width, int height,
                               const std::vector<ProbeFontFace>& fonts,
                               std::vector<std::uint8_t>& rgba) {
#if !ZUI_PROBE_FRAME
    (void)list;
    (void)width;
    (void)height;
    (void)fonts;
    rgba.clear();
    return ProbeFrame::Unavailable;
#else
    // Leaked for the process, same reason as the GL probe: tearing the device down from a
    // static destructor races SDL. The window is not polled; input stays on Interaction.
    static IWindow* window = nullptr;
    static VkRenderer* renderer = nullptr;
    if (renderer == nullptr) {
        WindowDesc desc;
        desc.title = "zui-probe-vk";
        desc.width = 64;
        desc.height = 64;
        desc.api = GfxApi::Vulkan;
        desc.hidden = true;
        std::unique_ptr<IWindow> created = create_window("sdl3");
        if (created == nullptr || !created->create(desc)) {
            rgba.clear();
            return ProbeFrame::Failed;
        }
        renderer = new VkRenderer();
        if (!renderer->create(*created)) {
            delete renderer;
            renderer = nullptr;
            rgba.clear();
            return ProbeFrame::Failed;
        }
        window = created.release();
    }
    for (const ProbeFontFace& face : fonts) {
        text::register_embedded_font(face.family, face.path);
    }
    renderer->begin_frame();
    if (!renderer->prepare_offscreen(width, height)) {
        rgba.clear();
        return ProbeFrame::Failed;
    }
    renderer->submit(list);
    int got_w = 0;
    int got_h = 0;
    if (!renderer->read_frame(rgba, got_w, got_h) || got_w != width || got_h != height) {
        rgba.clear();
        return ProbeFrame::Failed;
    }
    return ProbeFrame::Ok;
#endif
}

}  // namespace backends
}  // namespace zui

#endif  // ZUI_HAS_VULKAN

#ifndef ZUI_HAS_VULKAN
#include "backends/frame_probe.h"
namespace zui {
namespace backends {
ProbeFrame probe_read_frame_vk(const paint::DisplayList&, int, int, const std::vector<ProbeFontFace>&,
                               std::vector<std::uint8_t>& rgba) {
    rgba.clear();
    return ProbeFrame::Unavailable;
}
}  // namespace backends
}  // namespace zui
#endif
