#ifndef ZUI_LAYOUT_CARET_TYPES_H
#define ZUI_LAYOUT_CARET_TYPES_H

#include <cstddef>
#include <string>
#include <vector>

namespace zui {
namespace layout {

enum class CaretAffinity { Upstream, Downstream };

struct CaretHit {
    std::size_t offset = 0;  // UTF-8 bytes into EditMapping::text
    CaretAffinity affinity = CaretAffinity::Downstream;
    bool trailing = false;  // DWrite isTrailingHit
    bool ok = false;        // false = fail closed
};

struct CaretRect {
    float x = 0;
    float y = 0;
    float width = 1.f;
    float height = 0;  // world/document CSS px; width usually 1
    bool ok = false;
};

/// One mappable shaped-run span on a visual LineBox, byte range into EditMapping::text.
struct MappedSpan {
    std::size_t line_index = 0;
    std::size_t byte_begin = 0;
    std::size_t byte_end = 0;
    float local_x = 0;  // border-box local (same as TextRun::x/y)
    float local_y = 0;
    float ascent = 0;  // line baseline from line top (TextRun::ascent)
    float font_ascent = 0;
    float font_descent = 0;
    float line_block = 0;
    std::vector<float> glyph_advances;
    /// Visual glyph order is the reverse of the logical edit string (an RTL run).
    bool reverse = false;
    /// One pixel of inline advance in border-box space. Horizontal text is +x.
    /// Vertical sideways text is +y, which is the axis paint steps.
    float axis_x = 1.f;
    float axis_y = 0.f;
};

struct EditMapping {
    const std::string* text = nullptr;  // focused edit text
    std::vector<MappedSpan> spans;
};

}  // namespace layout
}  // namespace zui

#endif