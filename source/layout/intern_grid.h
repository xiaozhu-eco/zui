#ifndef ZUI_SOURCE_LAYOUT_INTERN_GRID_H
#define ZUI_SOURCE_LAYOUT_INTERN_GRID_H

#include "layout/layout_style.h"
#include "layout/viewport.h"

#include "css/values.h"
#include "html/document.h"

#include <string>
#include <string_view>

namespace zui {
namespace layout {

// One viewport for every length read while a box is interned. The bytes live on the
// `LayoutEngine` that is running (`InternSession`). `intern_style_as` writes that object.
// A direct `intern_style` with no engine uses a fallback session that stays readable after
// the call, because `intern_used_viewport_unit` is read once `intern_style` has returned.
struct InternSession {
    Viewport length_viewport{};
    css::FontRelativeUnits font_rel{};
    bool used_viewport_unit = false;
};

inline InternSession*& active_intern_slot() {
    static InternSession* current = nullptr;
    return current;
}

#define g_length_viewport (zui::layout::active_intern_slot()->length_viewport)
#define g_font_rel (zui::layout::active_intern_slot()->font_rel)
#define g_intern_used_viewport_unit (zui::layout::active_intern_slot()->used_viewport_unit)

css::ResolvedLength resolve_with_viewport(std::string_view text, float font_size,
                                          float root_font_size);
/// Whether the intern that just finished resolved a viewport unit (vw/vh/dvh/vmin/vmax),
/// including through an ancestor font-size. The next intern clears it.
bool intern_used_viewport_unit();
void note_viewport_unit_text(std::string_view text);
/// Records that the value being resolved depends on the viewport without naming the unit, for the
/// paths that read a resolved value back out of a cache.
void note_viewport_unit_used();
float font_size_px(const html::Node* node);
float root_font_size_px(const html::Node* node);
/// Drops the per-layout `font_size_px` memo. `LayoutEngine::layout` calls it before any box is
/// interned, so a node freed since the previous layout cannot answer a later lookup.
void clear_font_px_cache();
FlexPack parse_pack(const std::string& value);

// Grid template, line names and alignment. Kept out of the general interner: the
// track-list parser is its own pass over the computed value.
void read_grid(LayoutStyle& style, const html::Node* node);

}  // namespace layout
}  // namespace zui

#endif
