#include "layout/caret/caret_service.h"
#include "layout/caret/caret_internal.h"
#include "layout/caret/inline_lines.h"

#include "layout/box.h"
#include "layout/box_role.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/box_geometry.h"
#include "layout/cache/direction.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/line_box.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/shaped_run.h"
#include "layout/cache/writing_mode.h"

#include "html/document.h"
#include "html/selection.h"
#include "text/grapheme_break.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

using namespace caret_detail;

std::string caret_edit_text(const html::Node* node) {
    return html::range_edit_text(node);
}

namespace {

const Box* find_node_box(const Box& box, const html::Node* node) {
    if (box.node == node) {
        return &box;
    }
    for (const Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (const Box* found = find_node_box(*child, node)) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

CaretRect caret_bar_for(const Box& box, const html::Node* node, std::size_t offset) {
    CaretRect none;
    if (node == nullptr) {
        return none;
    }
    const std::string edit = caret_edit_text(node);
    if (!edit.empty()) {
        const EditMapping mapping = make_edit_mapping(box, edit);
        if (!mapping.spans.empty()) {
            return get_rect(box, mapping, std::min(offset, edit.size()), CaretAffinity::Downstream);
        }
    }
    std::size_t consumed = 0;
    const html::Node* last = nullptr;
    const Box* last_box = nullptr;
    std::size_t last_at = 0;
    for (const auto& child : node->children) {
        if (child == nullptr) {
            continue;
        }
        const std::string part = caret_edit_text(child.get());
        const Box* child_box = find_node_box(box, child.get());
        if (part.empty() || child_box == nullptr) {
            continue;
        }
        if (offset < consumed + part.size()) {
            return caret_bar_for(*child_box, child.get(), offset - consumed);
        }
        last = child.get();
        last_box = child_box;
        last_at = part.size();
        consumed += part.size();
    }
    if (last_box != nullptr) {
        return caret_bar_for(*last_box, last, last_at);
    }
    return none;
}

const InlineContent* inline_lines(const Box& box) {
    const Box* owner = lines_owner(box);
    const LayoutResult* result = pinned_of(*owner);
    if (result == nullptr || result->lines == nullptr || result->lines->lines.empty()) {
        return nullptr;
    }
    return result->lines.get();
}

std::size_t line_of(const Box& box, const EditMapping& mapping, std::size_t offset,
                    CaretAffinity affinity) {
    (void)box;
    const MappedSpan* span = span_covering(mapping, offset, affinity);
    if (span == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    return span->line_index;
}

std::size_t line_edge(const Box& box, const EditMapping& mapping, std::size_t offset,
                      bool to_end, CaretAffinity affinity, CaretAffinity* landed) {
    (void)box;
    if (mapping.text == nullptr || mapping.spans.empty()) {
        if (landed != nullptr) {
            *landed = affinity;
        }
        return offset;
    }
    offset = std::min(offset, mapping.text->size());
    const MappedSpan* span = span_covering(mapping, offset, affinity);
    if (span == nullptr) {
        if (landed != nullptr) {
            *landed = affinity;
        }
        return offset;
    }
    const std::size_t li = span->line_index;
    std::size_t begin = span->byte_begin;
    std::size_t end = span->byte_end;
    for (const MappedSpan& s : mapping.spans) {
        if (s.line_index != li) {
            continue;
        }
        begin = std::min(begin, s.byte_begin);
        end = std::max(end, s.byte_end);
    }
    const std::size_t edge = to_end ? end : begin;
    if (landed != nullptr) {
        *landed = CaretAffinity::Downstream;
        if (to_end) {
            for (const MappedSpan& s : mapping.spans) {
                if (s.line_index == li + 1 && s.byte_begin == edge) {
                    *landed = CaretAffinity::Upstream;
                    break;
                }
            }
        }
    }
    return edge;
}

std::size_t line_step(const Box& box, const EditMapping& mapping, std::size_t offset,
                      CaretAffinity affinity, int dir, float& preferred_x,
                      CaretAffinity* landed) {
    if (landed != nullptr) {
        *landed = affinity;
    }
    if (mapping.text == nullptr || mapping.spans.empty() || dir == 0) {
        return offset;
    }
    const Box& geo = *lines_owner(box);
    offset = std::min(offset, mapping.text->size());
    const std::size_t cur = line_of(box, mapping, offset, affinity);
    if (cur == static_cast<std::size_t>(-1)) {
        return offset;
    }
    std::size_t max_line = 0;
    for (const MappedSpan& s : mapping.spans) {
        max_line = std::max(max_line, s.line_index);
    }
    const int target = static_cast<int>(cur) + dir;
    if (target < 0) {
        return line_edge(box, mapping, offset, false, affinity, landed);
    }
    if (target > static_cast<int>(max_line)) {
        return line_edge(box, mapping, offset, true, affinity, landed);
    }
    float y = 0.f;
    bool found_y = false;
    for (const MappedSpan& s : mapping.spans) {
        if (s.line_index != static_cast<std::size_t>(target)) {
            continue;
        }
        const float top = world_y_of(geo, s.local_y + s.ascent - s.font_ascent);
        const float height = s.font_ascent + s.font_descent;
        y = top + height * 0.5f;
        found_y = true;
        break;
    }
    if (!found_y) {
        return offset;
    }
    // Seed preferred_x from the current caret when the caller left it unset (NaN).
    if (!std::isfinite(preferred_x)) {
        const CaretRect here = get_rect(box, mapping, offset, affinity);
        if (here.ok) {
            preferred_x = here.x;
        } else {
            preferred_x = world_x_of(geo, mapping.spans.front().local_x);
        }
    }
    const CaretHit hit = hit_test(box, mapping, preferred_x, y);
    if (!hit.ok) {
        // Land on the target line's edge closest to the step direction.
        std::size_t edge_off = offset;
        for (const MappedSpan& s : mapping.spans) {
            if (s.line_index == static_cast<std::size_t>(target)) {
                edge_off = dir > 0 ? s.byte_begin : s.byte_end;
                break;
            }
        }
        // byte_begin selects the line that starts there; byte_end selects the line that ends
        // there. Downstream / Upstream are those two sides of a shared wrap offset.
        const CaretAffinity pick =
            dir > 0 ? CaretAffinity::Downstream : CaretAffinity::Upstream;
        return line_edge(box, mapping, edge_off, dir > 0, pick, landed);
    }
    if (landed != nullptr) {
        *landed = hit.affinity;
    }
    return hit.offset;
}

html::BoundaryPoint hit_point(const Box& box, html::Node* host, float x, float y) {
    html::BoundaryPoint p;
    if (host == nullptr) {
        return p;
    }
    const std::string edit = caret_edit_text(host);
    const EditMapping mapping = make_edit_mapping(box, edit);
    const CaretHit hit = hit_test(box, mapping, x, y);
    const std::size_t off = hit.ok ? hit.offset : edit.size();
    (void)html::flatten_to_point(host, off, p);
    if (p.node == nullptr) {
        p.node = host;
        p.offset = off;
    }
    return p;
}

CaretRect caret_rect_at(const Box& box, const html::BoundaryPoint& point, CaretAffinity affinity) {
    if (point.node == nullptr) {
        return CaretRect{};
    }
    html::Node* map_host = box.node != nullptr ? box.node : point.node;
    const std::size_t offset = html::point_to_flatten(map_host, point);
    const std::string edit = caret_edit_text(map_host);
    const EditMapping mapping = make_edit_mapping(box, edit);
    if (!mapping.spans.empty()) {
        return get_rect(box, mapping, std::min(offset, edit.size()), affinity);
    }
    return caret_bar_for(box, point.node, point.offset);
}

}  // namespace layout
}  // namespace zui
