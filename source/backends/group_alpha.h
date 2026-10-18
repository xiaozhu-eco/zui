#ifndef ZUI_BACKENDS_GROUP_ALPHA_H
#define ZUI_BACKENDS_GROUP_ALPHA_H

#include "paint/display_list.h"

#include <zui/export.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace zui {
namespace backends {

struct GroupCursor {
    float alpha = 1.f;
    float brightness = 1.f;
    /// When set, an opacity, blur, drop-shadow, color-matrix, gradient-mask, or non-normal
    /// mix-blend group is not premultiplied onto each primitive. BeginFilter / EndFilter are
    /// passed to `draw` so the backend can paint the group into a layer and composite it once.
    /// Brightness stays per primitive. A color matrix is applied on that layer when one is open,
    /// and on each primitive when the backend does not isolate groups.
    /// Vulkan leaves this false and keeps the per-primitive scale.
    bool isolate_groups = false;
    std::vector<float> stack;
    std::vector<float> brightness_stack;
    std::vector<std::uint8_t> layer_open;
    /// Color functions outside an isolated layer, inner filter first. Each matrix clamps before
    /// the next one. Empty until a color-matrix filter is entered.
    int color_matrix_count = 0;
    float color_matrix[paint::FilterEffects::kMaxFilterColorMatrices * 20] = {};
    bool has_color_matrix = false;
    std::vector<int> color_matrix_count_stack;
    std::vector<std::array<float, paint::FilterEffects::kMaxFilterColorMatrices * 20>>
        color_matrix_stack;
    std::vector<std::uint8_t> color_matrix_flag_stack;
};

/// The only DisplayItem walk. Scales by `cursor.alpha`, then applies Begin/EndFilter.
/// `draw` receives the already-scaled item. Group markers are passed through only when
/// `isolate_groups` opened a real layer for that group.
ZUI_EXPORT void submit_items(GroupCursor& cursor, const std::vector<paint::DisplayItem>& items,
                             const std::function<void(const paint::DisplayItem&)>& draw);

}  // namespace backends
}  // namespace zui

#endif
