#include "input/detail.h"
#include "input/interaction.h"

#include "layout/point.h"
#include "layout/place_dirty.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {
SnapType parse_snap_type(const html::Node* node) {
    SnapType out;
    if (node == nullptr) {
        return out;
    }
    const std::string* raw = node->style("scroll-snap-type");
    if (raw == nullptr || raw->empty() || *raw == "none") {
        return out;
    }
    std::string s = *raw;
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // Tokenize on whitespace so "proximity" does not spuriously match axis "y".
    std::vector<std::string> toks;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        toks.push_back(cur);
    }
    auto has_tok = [&](const char* tok) {
        for (const std::string& t : toks) {
            if (t == tok) {
                return true;
            }
        }
        return false;
    };
    if (has_tok("both")) {
        out.axis = SnapAxis::Both;
    } else if (has_tok("x") || has_tok("inline")) {
        out.axis = SnapAxis::X;
    } else if (has_tok("y") || has_tok("block")) {
        out.axis = SnapAxis::Y;
    } else {
        return out;
    }
    out.strict = has_tok("mandatory") ? SnapStrictness::Mandatory : SnapStrictness::Proximity;
    return out;
}

SnapAlign parse_snap_align_token(std::string_view tok) {
    if (tok == "start") {
        return SnapAlign::Start;
    }
    if (tok == "center") {
        return SnapAlign::Center;
    }
    if (tok == "end") {
        return SnapAlign::End;
    }
    return SnapAlign::None;
}

void parse_snap_align(const html::Node* node, SnapAlign& block, SnapAlign& inline_axis) {
    block = SnapAlign::None;
    inline_axis = SnapAlign::None;
    if (node == nullptr) {
        return;
    }
    const std::string* raw = node->style("scroll-snap-align");
    if (raw == nullptr || raw->empty() || *raw == "none") {
        return;
    }
    std::string s = *raw;
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string first;
    std::string second;
    {
        std::size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
            first.push_back(s[i++]);
        }
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
            second.push_back(s[i++]);
        }
    }
    block = parse_snap_align_token(first);
    inline_axis = second.empty() ? block : parse_snap_align_token(second);
}

float snap_offset(SnapAlign align, float area_start, float area_size, float port_size) {
    switch (align) {
        case SnapAlign::Start:
            return area_start;
        case SnapAlign::Center:
            return area_start - (port_size - area_size) * 0.5f;
        case SnapAlign::End:
            return area_start + area_size - port_size;
        case SnapAlign::None:
            break;
    }
    return area_start;
}

float choose_snap(float proposed, const std::vector<float>& stops, SnapStrictness strict,
                  float port_size) {
    if (stops.empty() || strict == SnapStrictness::None) {
        return proposed;
    }
    float best = stops.front();
    float best_d = std::fabs(proposed - best);
    for (float s : stops) {
        const float d = std::fabs(proposed - s);
        if (d < best_d) {
            best_d = d;
            best = s;
        }
    }
    if (strict == SnapStrictness::Proximity) {
        // UA-defined proximity: snap when within ~1/3 of the snapport (enough for difftest).
        const float threshold = std::max(32.f, port_size * 0.33f);
        if (best_d > threshold) {
            return proposed;
        }
    }
    return best;
}

bool overflow_anchor_none(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string* raw = node->style("overflow-anchor");
    return raw != nullptr && *raw == "none";
}

bool is_scroll_anchor_candidate(const layout::Box& box) {
    if (box.node == nullptr || box.style_ptr == nullptr) {
        return false;
    }
    if (box.node->tag.empty() || box.node->tag == "#text") {
        return false;
    }
    if (overflow_anchor_none(box.node)) {
        return false;
    }
    if (box.style().position_fixed || box.style().position_absolute) {
        return false;
    }
    return true;
}

float content_block_in_scroller(const layout::Box* target, const layout::Box* scroller) {
    float y = 0.f;
    for (const layout::Box* box = target; box != nullptr && box != scroller; box = box->parent) {
        y += box->placement.cb_offset.block_offset;
    }
    return y;
}

/// CSS Scroll Anchoring §2.1 candidate examination: prefer a fully visible node; descend into a
/// partially visible one; skip clipped / excluded subtrees.
html::Node* examine_anchor_candidate(layout::Box& box, layout::Box& scroller, float view0,
                                     float view1) {
    if (!is_scroll_anchor_candidate(box)) {
        return nullptr;
    }
    const float y0 = content_block_in_scroller(&box, &scroller);
    const float y1 = y0 + box.geo.height;
    if (y1 <= view0 + 0.01f || y0 >= view1 - 0.01f) {
        return nullptr;
    }
    if (y0 >= view0 - 0.01f && y1 <= view1 + 0.01f) {
        return box.node;
    }
    for (layout::Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (html::Node* found = examine_anchor_candidate(*child, scroller, view0, view1)) {
            return found;
        }
    }
    return box.node;
}

html::Node* select_scroll_anchor(layout::Box& scroller) {
    if (scroller.node == nullptr || scroller.style_ptr == nullptr) {
        return nullptr;
    }
    if (overflow_anchor_none(scroller.node)) {
        return nullptr;
    }
    const layout::OverflowMode oy = scroller.style().overflow_y;
    if (oy != layout::OverflowMode::Auto && oy != layout::OverflowMode::Scroll) {
        return nullptr;
    }
    // Only when scrolled away from the block-start origin.
    if (scroller.node->scroll_y <= 0.01f) {
        return nullptr;
    }
    const ClientRect port = client_rect(scroller, 15.f);
    const float view0 = scroller.node->scroll_y;
    const float view1 = view0 + port.h;
    for (layout::Box* child : scroller.children) {
        if (child == nullptr) {
            continue;
        }
        if (html::Node* found = examine_anchor_candidate(*child, scroller, view0, view1)) {
            return found;
        }
    }
    return nullptr;
}

layout::Box* find_box_for_node(layout::Box& root, html::Node* node) {
    // Membership is decided by comparing node *pointers* down the box tree and never by walking
    // `node->parent`: `Node::scroll_anchor` is remembered across frames, and the content it names
    // can have been destroyed by an `App::set_model` rebuild in between (a list whose rows are
    // re-created) while the scroller itself - which holds the field - lives on. An ancestor walk
    // started at the remembered node read freed memory and crashed here (`descendant_of`). A
    // pointer that the allocator handed to a new node compares equal to that new node, which only
    // moves the anchor baseline, so the walk is not needed for correctness either.
    if (node == nullptr) {
        return nullptr;
    }
    if (root.node == node) {
        return &root;
    }
    for (layout::Box* child : root.children) {
        if (child == nullptr) {
            continue;
        }
        if (layout::Box* found = find_box_for_node(*child, node)) {
            return found;
        }
    }
    return nullptr;
}

void remember_scroll_anchor(layout::Box& scroller) {
    if (scroller.node == nullptr) {
        return;
    }
    html::Node* anchor = select_scroll_anchor(scroller);
    scroller.node->scroll_anchor = anchor;
    if (anchor == nullptr) {
        scroller.node->scroll_anchor_y = 0.f;
        return;
    }
    layout::Box* anchor_box = find_box_for_node(scroller, anchor);
    scroller.node->scroll_anchor_y =
        anchor_box != nullptr ? content_block_in_scroller(anchor_box, &scroller) : 0.f;
}

bool adjust_scroll_anchor_one(layout::Box& scroller) {
    if (scroller.node == nullptr || scroller.style_ptr == nullptr) {
        return false;
    }
    const layout::OverflowMode oy = scroller.style().overflow_y;
    if (oy != layout::OverflowMode::Auto && oy != layout::OverflowMode::Scroll) {
        return false;
    }
    if (overflow_anchor_none(scroller.node)) {
        scroller.node->scroll_anchor = nullptr;
        return false;
    }
    html::Node* anchor = scroller.node->scroll_anchor;
    layout::Box* anchor_box = nullptr;
    if (anchor != nullptr) {
        // `find_box_for_node` is the validity test as well as the lookup: a null result means the
        // anchor is not in this scroller any more (destroyed, or moved), which re-selects below.
        anchor_box = find_box_for_node(scroller, anchor);
    }
    if (anchor_box == nullptr) {
        remember_scroll_anchor(scroller);
        return false;
    }
    const float new_y = content_block_in_scroller(anchor_box, &scroller);
    const float delta = new_y - scroller.node->scroll_anchor_y;
    scroller.node->scroll_anchor_y = new_y;
    if (std::fabs(delta) <= 0.01f) {
        return false;
    }
    // Anchoring adjustment bypasses snap: it preserves the visual position of the anchor.
    const ScrollRange range = scroll_range(scroller);
    const float next_y =
        std::min(range.max_y, std::max(range.min_y, scroller.node->scroll_y + delta));
    if (std::fabs(next_y - scroller.node->scroll_y) <= 0.01f) {
        return false;
    }
    scroller.node->scroll_y = next_y;
    // Scroll anchoring moved the scroller outside `set_scroll`, so the placement mark lands here
    // (docs/PERF_REFACTOR.md stage P).
    mark_place_dirty(scroller);
    return true;
}

bool adjust_scroll_anchors_walk(layout::Box& box) {
    bool moved = adjust_scroll_anchor_one(box);
    for (layout::Box* child : box.children) {
        if (child != nullptr) {
            moved = adjust_scroll_anchors_walk(*child) || moved;
        }
    }
    return moved;
}


void apply_scroll_snap(layout::Box& box, float& x, float& y, float view_w, float view_h) {
    const SnapType type = parse_snap_type(box.node);
    if (type.axis == SnapAxis::None || type.strict == SnapStrictness::None) {
        return;
    }
    const ClientRect port = client_rect(box, 15.f);
    float port_w = port.w;
    float port_h = port.h;
    if (view_w > 0.f) {
        port_w = view_w;
    }
    if (view_h > 0.f) {
        port_h = view_h;
    }
    std::vector<float> stops_x;
    std::vector<float> stops_y;
    // Snap targets: in-flow children of the scroll container with a non-none align.
    for (layout::Box* child : box.children) {
        if (child == nullptr || child->node == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        if (child->style().position_absolute || child->style().position_fixed) {
            continue;
        }
        SnapAlign block_align = SnapAlign::None;
        SnapAlign inline_align = SnapAlign::None;
        parse_snap_align(child->node, block_align, inline_align);
        if (block_align == SnapAlign::None && inline_align == SnapAlign::None) {
            continue;
        }
        // Snap area in the scroller's unscrolled padding-box coordinates. Prefer placement
        // (stable across set_scroll without sync_world) over world+scroll undo.
        const float area_x = child->placement.cb_offset.inline_offset;
        const float area_y = child->placement.cb_offset.block_offset;
        const float area_w = child->geo.width;
        const float area_h = child->geo.height;
        if (type.axis == SnapAxis::X || type.axis == SnapAxis::Both) {
            if (inline_align != SnapAlign::None) {
                stops_x.push_back(snap_offset(inline_align, area_x, area_w, port_w));
            }
        }
        if (type.axis == SnapAxis::Y || type.axis == SnapAxis::Both) {
            if (block_align != SnapAlign::None) {
                stops_y.push_back(snap_offset(block_align, area_y, area_h, port_h));
            }
        }
    }
    if (type.axis == SnapAxis::X || type.axis == SnapAxis::Both) {
        x = choose_snap(x, stops_x, type.strict, port_w);
    }
    if (type.axis == SnapAxis::Y || type.axis == SnapAxis::Both) {
        y = choose_snap(y, stops_y, type.strict, port_h);
    }
}


layout::Box* nearest_scrollable(layout::Box* start, float scrollbar_thickness) {
    for (layout::Box* box = start; box != nullptr; box = box->parent) {
        if (box->style_ptr == nullptr || box->node == nullptr) {
            continue;
        }
        const layout::OverflowMode y = box->style().overflow_y;
        const layout::OverflowMode x = box->style().overflow_x;
        if (y != layout::OverflowMode::Auto && y != layout::OverflowMode::Scroll &&
            x != layout::OverflowMode::Auto && x != layout::OverflowMode::Scroll) {
            continue;
        }
        float view_w = -1.f;
        float view_h = -1.f;
        const ScrollRange range = scroll_range(*box, view_w, view_h);
        (void)scrollbar_thickness;
        if (range.max_x > range.min_x + 0.01f || range.max_y > range.min_y + 0.01f) {
            return box;
        }
    }
    return nullptr;
}

float align_scroll(ScrollAlign align, float target_start, float target_size, float port_start,
                   float port_size) {
    const float target_end = target_start + target_size;
    const float port_end = port_start + port_size;
    switch (align) {
        case ScrollAlign::Start:
            return target_start;
        case ScrollAlign::End:
            return target_end - port_size;
        case ScrollAlign::Center:
            return target_start - (port_size - target_size) * 0.5f;
        case ScrollAlign::Nearest:
            if (target_start >= port_start && target_end <= port_end) {
                return port_start;
            }
            if (target_start < port_start) {
                return target_start;
            }
            return target_end - port_size;
    }
    return port_start;
}

}  // namespace input_detail

void Interaction::note_scroll(html::Node* node) {
    if (node == nullptr || !scroll_.scroll_pending_set_.insert(node).second) {
        return;
    }
    scroll_.scroll_pending_.push_back(node);
}
void Interaction::flush_scroll_events() {
    const std::vector<html::Node*> pending = std::move(scroll_.scroll_pending_);
    scroll_.scroll_pending_.clear();
    scroll_.scroll_pending_set_.clear();
    for (html::Node* node : pending) {
        if (node == nullptr) {
            continue;
        }
        fire_from(node, "scroll");
    }
}
bool Interaction::apply_scroll(layout::Box& box, float x, float y, float view_w, float view_h) {
    if ((view_w < 0.f || view_h < 0.f) && host_.layout != nullptr) {
        float document_w = view_w;
        float document_h = view_h;
        if (document_scroll_view(*host_.layout, box, host_.scrollbar_thickness, document_w,
                                 document_h)) {
            if (view_w < 0.f) {
                view_w = document_w;
            }
            if (view_h < 0.f) {
                view_h = document_h;
            }
        }
    }
    if (!set_scroll(box, x, y, view_w, view_h)) {
        return false;
    }
    note_scroll(box.node);
    return true;
}
layout::Box* Interaction::scroller_from(layout::Box* hit) const {
    if (layout::Box* found = nearest_scroller(hit)) {
        return found;
    }
    if (host_.layout == nullptr || host_.layout->root() == nullptr) {
        return nullptr;
    }
    layout::Box* root = host_.layout->root();
    float view_w = -1.f;
    float view_h = -1.f;
    document_scroll_view(*host_.layout, *root, host_.scrollbar_thickness, view_w, view_h);
    const ScrollRange range = scroll_range(*root, view_w, view_h);
    if (range.max_y > range.min_y + 0.01f || range.max_x > range.min_x + 0.01f) {
        return root;
    }
    return nullptr;
}
bool Interaction::apply_wheel(layout::Box* box, float delta_x, float delta_y) {
    if (box == nullptr || box->node == nullptr) {
        return false;
    }
    const float before_x = box->node->scroll_x;
    const float before_y = box->node->scroll_y;
    bool moved = apply_scroll(*box, before_x + delta_x, before_y + delta_y);
    float rest_x = delta_x - (box->node->scroll_x - before_x);
    float rest_y = delta_y - (box->node->scroll_y - before_y);
    if (std::fabs(rest_x) < 0.01f && std::fabs(rest_y) < 0.01f) {
        return moved;
    }
    // CSS Overscroll: `contain` / `none` keep unused wheel delta from chaining to an ancestor.
    auto axis_blocks = [](html::Node* node, const char* longhand, const char* shorthand,
                          bool has_rest) {
        if (!has_rest || node == nullptr) {
            return false;
        }
        auto blocks = [](const std::string* v) {
            if (v == nullptr) {
                return false;
            }
            std::string s = *v;
            for (char& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            // Two-value shorthand: take the first token for both axes when longhand is absent.
            const auto sp = s.find_first_of(" \t");
            if (sp != std::string::npos) {
                s = s.substr(0, sp);
            }
            return s == "contain" || s == "none";
        };
        if (const std::string* v = node->style(longhand)) {
            return blocks(v);
        }
        return blocks(node->style(shorthand));
    };
    if (axis_blocks(box->node, "overscroll-behavior-x", "overscroll-behavior",
                    std::fabs(rest_x) >= 0.01f)) {
        rest_x = 0.f;
    }
    if (axis_blocks(box->node, "overscroll-behavior-y", "overscroll-behavior",
                    std::fabs(rest_y) >= 0.01f)) {
        rest_y = 0.f;
    }
    if (std::fabs(rest_x) < 0.01f && std::fabs(rest_y) < 0.01f) {
        return moved;
    }
    layout::Box* parent = scroller_from(box->parent);
    if (parent != nullptr && parent != box) {
        moved = apply_wheel(parent, rest_x, rest_y) || moved;
    }
    return moved;
}
bool Interaction::wheel(float x, float y, float delta_x, float delta_y, int delta_mode,
                        Wheel mods) {
    if (host_.layout == nullptr) {
        return false;
    }
    layout::Box* hit = host_.layout->hit_test(x, y);
    if (delta_mode == 1 && hit != nullptr && hit->style_ptr != nullptr) {
        const float line = std::max(1.0f, hit->style().font_size);
        delta_x = line_mode_pixels(line, delta_x);
        delta_y = line_mode_pixels(line, delta_y);
    }
    // Handler before default scroll (EVENT.md section 7 / HOST PR-17). Target walk starts at the
    // hit under the pointer, same shape as click / drop.
    mods.delta_x = delta_x;
    mods.delta_y = delta_y;
    html::Node* target = handler_owner(node_of(hit), "wheel");
    const bool prevented = target != nullptr && fire(target, "wheel", nullptr, nullptr, &mods);
    if (prevented) {
        return true;
    }
    // A wheel that moved nothing (the end of a list, or a target with no scroll range) has no
    // placement to publish; `sync_world` would find an empty mark set and place the whole tree
    // (docs/PERF_REFACTOR.md stage P).
    if (apply_wheel(scroller_from(hit), delta_x, delta_y) || !host_.layout->world_is_current()) {
        host_.layout->sync_world();
        if (host_.document != nullptr) {
            settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
        }
    }
    return false;
}

namespace {

bool box_scrolls(const layout::Box& box, bool is_root) {
    if (box.node == nullptr) {
        return false;
    }
    if (is_root) {
        return true;
    }
    if (box.style_ptr == nullptr) {
        return false;
    }
    const auto scrolls = [](layout::OverflowMode mode) {
        return mode == layout::OverflowMode::Auto || mode == layout::OverflowMode::Scroll;
    };
    return scrolls(box.style().overflow_x) || scrolls(box.style().overflow_y);
}

bool clamp_scrollers(layout::Box& box, layout::LayoutEngine& layout, float thickness, bool is_root) {
    bool moved = false;
    if (box_scrolls(box, is_root)) {
        float view_w = -1.f;
        float view_h = -1.f;
        if (is_root) {
            document_scroll_view(layout, box, thickness, view_w, view_h);
        }
        const ScrollRange range = scroll_range(box, view_w, view_h);
        const float next_x = std::min(range.max_x, std::max(range.min_x, box.node->scroll_x));
        const float next_y = std::min(range.max_y, std::max(range.min_y, box.node->scroll_y));
        if (std::fabs(next_x - box.node->scroll_x) > 0.01f ||
            std::fabs(next_y - box.node->scroll_y) > 0.01f) {
            set_scroll(box, next_x, next_y, view_w, view_h);
            moved = true;
        }
    }
    for (layout::Box* child : box.children) {
        if (child != nullptr) {
            moved = clamp_scrollers(*child, layout, thickness, false) || moved;
        }
    }
    return moved;
}

}  // namespace

void settle_content_visibility(layout::LayoutEngine& layout, html::Document& doc,
                               float scrollbar_thickness) {
    if (layout.root() == nullptr || !layout.uses_content_visibility()) {
        return;
    }
    // Two passes is the same fuse the rest of layout uses. The first sizes boxes against the
    // scroll this frame published. If that shrinks the scroll height past the offset, the clamp
    // moves the viewport and the second pass decides relevance again.
    for (int pass = 0; pass < 2; ++pass) {
        layout.invalidate_content_visibility();
        layout.layout(doc, layout.viewport());
        if (layout.root() == nullptr) {
            return;
        }
        if (!clamp_scrollers(*layout.root(), layout, scrollbar_thickness, true)) {
            break;
        }
        layout.sync_world();
    }
}

}  // namespace input
}  // namespace zui
