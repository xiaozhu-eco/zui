#ifndef ZUI_BACKENDS_FRAME_PROBE_H
#define ZUI_BACKENDS_FRAME_PROBE_H

// Framebuffer readback for tests. Not an application header: nothing under include/zui/
// may include this. The picture is the real renderer's offscreen target, not a second paint.

#include "paint/display_list.h"

#include <zui/export.h>

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace backends {

struct ProbeFontFace {
    std::string family;
    std::string path;
};

enum class ProbeFrame {
    Ok,
    /// ZUI_PROBE_FRAME is 0, or this build has no readback.
    Unavailable,
    /// The context or the read failed.
    Failed,
};

/// Draw `list` with the GL renderer and return top-left RGBA of `width` by `height` pixels.
/// The hidden window is only a context. This function does not poll pointer or keyboard
/// input; tests insert those through `source/input/Interaction` before building `list`.
/// `fonts` are registered on this module's font chain so the glyphs match the faces the
/// layout measured. A process that also links its own copy of the font chain must pass
/// the same files here.
ZUI_EXPORT ProbeFrame probe_read_frame(const paint::DisplayList& list, int width, int height,
                                       const std::vector<ProbeFontFace>& fonts,
                                       std::vector<std::uint8_t>& rgba);

/// Same contract as `probe_read_frame`, drawn by the Vulkan renderer into an offscreen image.
/// Unavailable when this build has no Vulkan device.
ZUI_EXPORT ProbeFrame probe_read_frame_vk(const paint::DisplayList& list, int width, int height,
                                          const std::vector<ProbeFontFace>& fonts,
                                          std::vector<std::uint8_t>& rgba);

}  // namespace backends
}  // namespace zui

#endif
