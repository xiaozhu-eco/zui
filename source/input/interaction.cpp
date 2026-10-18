#include "input/interaction.h"
#include "input/detail.h"

#include "html/selection.h"
#include "layout/caret/caret_service.h"
#include "layout/point.h"
#include "layout/place_dirty.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
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
bool is_content_editable(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    const auto it = node->attrs.find("contenteditable");
    if (it == node->attrs.end()) {
        return false;
    }
    return it->second != "false";
}

bool is_editable(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    if (is_content_editable(node)) {
        return true;
    }
    if (node->tag == "textarea") {
        return true;
    }
    if (node->tag != "input") {
        return false;
    }
    auto it = node->attrs.find("type");
    if (it == node->attrs.end() || it->second.empty()) {
        return true;
    }
    const std::string& type = it->second;
    // date / time stay typed text. number, color, and range are scalars, not a text caret.
    return type == "text" || type == "search" || type == "password" || type == "email" ||
           type == "url" || type == "tel" || type == "date" || type == "time";
}

bool is_rendered_for_focus(const html::Node* node) {
    // display:none hides the whole subtree. A <template>'s contents are not in the
    // document, so they are not tab stops (album's album-list prototype was taking
    // the Tab that Chrome gives to the first real album button). visibility:hidden
    // only removes the element itself; a visible descendant stays focusable.
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        if (n->tag == "template") {
            return false;
        }
        const std::string* display = n->style("display");
        if (display != nullptr && *display == "none") {
            // UA hides a dialog only while it is closed. `open` is written before this frame
            // flushes, so the computed `display:none` is stale and must not block the focus
            // the show-modal default moves inside (EVENT.md section 7).
            if (n->tag == "dialog" && n->attrs.find("open") != n->attrs.end()) {
                continue;
            }
            return false;
        }
    }
    const std::string* visibility = node->style("visibility");
    return visibility == nullptr || (*visibility != "hidden" && *visibility != "collapse");
}

bool is_focusable(const html::Node* node) {
    if (node == nullptr || node->disabled || !is_rendered_for_focus(node)) {
        return false;
    }
    if (node->tag == "a" || node->tag == "button" || node->tag == "textarea" ||
        node->tag == "select" || is_content_editable(node)) {
        return true;
    }
    if (node->tag == "input") {
        return true;
    }
    return node->attrs.find("tabindex") != node->attrs.end();
}

ScrollRange scroll_range(const layout::Box& box, float view_w, float view_h) {
    // Same view the scrollbar uses: the border box minus borders. scrollHeight includes padding,
    // so the client must too (a padding-sized client made a fitting line grow a bar).
    // Chrome reports scrollWidth/scrollHeight as integers, so the maximum offset is the
    // difference of those integers. A fractional extent (239.72) never reached the recorded end.
    const float view_x =
        view_w >= 0.f ? view_w : std::max(0.0f, box.geo.width - box.geo.border.horizontal());
    const float view_y = view_h >= 0.f ? view_h : view_height(box);
    const float extent_x = std::max(0.0f, std::round(box.geo.scroll_width) - std::round(view_x));
    const float extent_y = std::max(0.0f, std::round(box.geo.scroll_height) - std::round(view_y));
    ScrollRange range;
    const bool rtl = box.style_ptr != nullptr && box.style().direction == layout::Direction::Rtl;
    if (rtl) {
        range.min_x = -extent_x;
        range.max_x = 0;
    } else {
        range.min_x = 0;
        range.max_x = extent_x;
    }
    range.min_y = 0;
    range.max_y = extent_y;
    return range;
}

ClientRect client_rect(const layout::Box& box, float scrollbar_thickness) {
    ClientRect rect;
    rect.x = box.geo.world_x + box.geo.border.left;
    rect.y = box.geo.world_y + box.geo.border.top;
    rect.w = std::max(0.0f, box.geo.width - box.geo.border.horizontal());
    rect.h = std::max(0.0f, box.geo.height - box.geo.border.vertical());
    if (box.style_ptr == nullptr) {
        return rect;
    }
    const layout::ScrollbarV vertical = layout::vertical_scrollbar(box, scrollbar_thickness);
    const layout::ScrollbarH horizontal = layout::horizontal_scrollbar(box, scrollbar_thickness);
    if (vertical.visible) {
        rect.w = std::max(0.0f, rect.w - vertical.track_w);
    }
    if (horizontal.visible) {
        rect.h = std::max(0.0f, rect.h - horizontal.track_h);
    }
    return rect;
}

void document_client_size(float viewport_w, float viewport_h, float scroll_w, float scroll_h,
                          float scrollbar_thickness, float border_horizontal, float border_vertical,
                          float& out_w, float& out_h) {
    float width = viewport_w;
    float height = viewport_h;
    const bool wide = scroll_w > viewport_w + 0.5f;
    const bool tall = scroll_h > viewport_h + 0.5f;
    if (wide) {
        height = std::max(0.0f, height - scrollbar_thickness);
    }
    if (tall) {
        width = std::max(0.0f, width - scrollbar_thickness);
    }
    if (!wide && scroll_w > width + 0.5f) {
        height = std::max(0.0f, height - scrollbar_thickness);
    }
    if (!tall && scroll_h > height + 0.5f) {
        width = std::max(0.0f, width - scrollbar_thickness);
    }
    out_w = std::max(0.0f, width - border_horizontal);
    out_h = std::max(0.0f, height - border_vertical);
}

bool document_scroll_view(const layout::LayoutEngine& layout, const layout::Box& box,
                          float scrollbar_thickness, float& view_w, float& view_h) {
    if (layout.root() != &box) {
        return false;
    }
    document_client_size(layout.viewport().width, layout.viewport().height, box.geo.scroll_width,
                         box.geo.scroll_height, scrollbar_thickness, box.geo.border.horizontal(),
                         box.geo.border.vertical(), view_w, view_h);
    return true;
}

bool adjust_scroll_anchors(layout::Box& root) { return adjust_scroll_anchors_walk(root); }
bool set_scroll(layout::Box& box, float x, float y, float view_w, float view_h) {
    if (box.node == nullptr) {
        return false;
    }
    apply_scroll_snap(box, x, y, view_w, view_h);
    const ScrollRange range = scroll_range(box, view_w, view_h);
    const float next_x = std::min(range.max_x, std::max(range.min_x, x));
    const float next_y = std::min(range.max_y, std::max(range.min_y, y));
    const bool moved = std::fabs(next_x - box.node->scroll_x) > 0.01f ||
                       std::fabs(next_y - box.node->scroll_y) > 0.01f;
    box.node->scroll_x = next_x;
    box.node->scroll_y = next_y;
    // Reselect the scroll-anchoring node whenever the scroll offset is set (CSS Scroll Anchoring).
    remember_scroll_anchor(box);
    // docs/PERF_REFACTOR.md stage P: a scrolled container moves its whole subtree in the world and
    // nothing else. Marking it here is what lets the frame that follows place that subtree instead
    // of the document; `moved` is false for a no-op set, so a wheel at the end of a list marks
    // nothing.
    if (moved) {
        mark_place_dirty(box);
    }
    return moved;
}

namespace {

bool parse_scroll_length(std::string_view text, float basis, float& out) {
    std::size_t i = 0;
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    const std::size_t begin = i;
    if (text[i] == '+' || text[i] == '-') {
        ++i;
    }
    bool digits = false;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        digits = true;
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            digits = true;
            ++i;
        }
    }
    if (!digits) {
        return false;
    }
    const float number = std::strtof(std::string(text.substr(begin, i - begin)).c_str(), nullptr);
    if (i + 1 < text.size() && text[i] == 'p' && text[i + 1] == 'x') {
        i += 2;
    } else if (i < text.size() && text[i] == '%') {
        out = basis * number / 100.f;
        return true;
    } else if (i < text.size() && text[i] != ' ' && text[i] != '\t') {
        return false;
    }
    out = number;
    return true;
}

float scroll_side(const html::Node* node, const char* shorthand, const char* longhand, int side,
                  float basis) {
    if (node == nullptr) {
        return 0.f;
    }
    float direct = 0.f;
    if (const std::string* raw = node->style(longhand)) {
        if (parse_scroll_length(*raw, basis, direct)) {
            return direct;
        }
        return 0.f;
    }
    const std::string* box = node->style(shorthand);
    if (box == nullptr) {
        return 0.f;
    }
    float sides[4] = {0.f, 0.f, 0.f, 0.f};
    int count = 0;
    std::size_t i = 0;
    const std::string& text = *box;
    while (i < text.size() && count < 4) {
        while (i < text.size() &&
               (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        const std::size_t begin = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\n' &&
               text[i] != '\r') {
            ++i;
        }
        float value = 0.f;
        if (!parse_scroll_length(std::string_view(text).substr(begin, i - begin), basis, value)) {
            return 0.f;
        }
        sides[count++] = value;
    }
    if (count == 1) {
        return sides[0];
    }
    if (count == 2) {
        return side % 2 == 0 ? sides[0] : sides[1];
    }
    if (count == 3) {
        if (side == 0) {
            return sides[0];
        }
        if (side == 2) {
            return sides[2];
        }
        return sides[1];
    }
    if (count == 4 && side >= 0 && side < 4) {
        return sides[side];
    }
    return 0.f;
}

}  // namespace

bool scroll_into_view(layout::LayoutEngine& layout, html::Node& target, ScrollAlign block,
                      float scrollbar_thickness, html::Node** scrolled) {
    if (scrolled != nullptr) {
        *scrolled = nullptr;
    }
    layout::Box* target_box = layout.principal_box(&target);
    if (target_box == nullptr) {
        return false;
    }
    layout::Box* scroller = nearest_scrollable(target_box->parent, scrollbar_thickness);
    float view_w = -1.f;
    float view_h = -1.f;
    if (scroller == nullptr && layout.root() != nullptr) {
        document_scroll_view(layout, *layout.root(), scrollbar_thickness, view_w, view_h);
        const ScrollRange root_range = scroll_range(*layout.root(), view_w, view_h);
        if (root_range.max_y > 0.01f || root_range.max_x > root_range.min_x + 0.01f) {
            scroller = layout.root();
        } else {
            view_w = -1.f;
            view_h = -1.f;
        }
    } else if (scroller != nullptr) {
        document_scroll_view(layout, *scroller, scrollbar_thickness, view_w, view_h);
    }
    if (scroller == nullptr || scroller->node == nullptr) {
        return false;
    }
    ClientRect port;
    if (view_w >= 0.f && view_h >= 0.f) {
        port.x = 0.f;
        port.y = 0.f;
        port.w = view_w;
        port.h = view_h;
    } else {
        port = client_rect(*scroller, scrollbar_thickness);
    }
    // The target's content position. A culled subtree keeps no world rectangle - the walk parks it
    // at the sentinel - so a jump to a row the reader scrolled away from (an outline heading, a
    // focus ring, a find hit) has to read the placement chain instead: it is the same offset
    // `scroll_y` is measured in (the chain starts at the containing block's padding edge, CSS 2.1
    // section 10.1) and no walk touches it. The world rectangle stays the answer everywhere else:
    // a transform moves it, and the chain does not know about transforms.
    const bool parked = target_box->geo.culled || target_box->geo.world_y <= -1.0e9f ||
                        target_box->geo.world_x <= -1.0e9f;
    const auto content_start = [&](bool block) {
        if (parked) {
            float offset = 0.f;
            for (const layout::Box* box = target_box; box != nullptr && box != scroller;
                 box = box->parent) {
                offset += block ? box->placement.cb_offset.block_offset
                                : box->placement.cb_offset.inline_offset;
            }
            return offset;
        }
        const float world = block ? target_box->geo.world_y : target_box->geo.world_x;
        const float port_start = block ? port.y : port.x;
        const float scroll = block ? scroller->node->scroll_y : scroller->node->scroll_x;
        return (world - port_start) + scroll;
    };
    const float content_x = content_start(false);
    const float content_y = content_start(true);
    // CSSOM View: align the target's scroll-margin box with the scrollport inset by
    // scroll-padding. Percentages use the scrollport's size on that axis.
    const float margin_top = scroll_side(&target, "scroll-margin", "scroll-margin-top", 0, port.h);
    const float margin_right =
        scroll_side(&target, "scroll-margin", "scroll-margin-right", 1, port.w);
    const float margin_bottom =
        scroll_side(&target, "scroll-margin", "scroll-margin-bottom", 2, port.h);
    const float margin_left =
        scroll_side(&target, "scroll-margin", "scroll-margin-left", 3, port.w);
    const float pad_top =
        scroll_side(scroller->node, "scroll-padding", "scroll-padding-top", 0, port.h);
    const float pad_right =
        scroll_side(scroller->node, "scroll-padding", "scroll-padding-right", 1, port.w);
    const float pad_bottom =
        scroll_side(scroller->node, "scroll-padding", "scroll-padding-bottom", 2, port.h);
    const float pad_left =
        scroll_side(scroller->node, "scroll-padding", "scroll-padding-left", 3, port.w);
    const float x_view = std::max(0.f, port.w - pad_left - pad_right);
    const float y_view = std::max(0.f, port.h - pad_top - pad_bottom);
    const float next_x =
        align_scroll(block, content_x - margin_left,
                     target_box->geo.width + margin_left + margin_right,
                     scroller->node->scroll_x + pad_left, x_view) -
        pad_left;
    const float next_y =
        align_scroll(block, content_y - margin_top,
                     target_box->geo.height + margin_top + margin_bottom,
                     scroller->node->scroll_y + pad_top, y_view) -
        pad_top;
    if (!set_scroll(*scroller, next_x, next_y, view_w, view_h)) {
        return false;
    }
    if (scrolled != nullptr) {
        *scrolled = scroller->node;
    }
    return true;
}

float line_mode_pixels(float line_height_px, float delta_lines) {
    return delta_lines * line_height_px;
}

void Interaction::reveal_caret() {
    html::Node* node = text_.reveal_caret_;
    text_.reveal_caret_ = nullptr;
    if (node == nullptr || host_.layout == nullptr || host_.metrics == nullptr ||
        host_.document == nullptr) {
        return;
    }
    layout::Box* box = host_.layout->principal_box(node);
    if (box == nullptr || box->style_ptr == nullptr || box->node == nullptr) {
        return;
    }
    const std::size_t caret =
        std::min(static_cast<std::size_t>(std::max(node->caret, 0)), node->value.size());
    const layout::CaretAffinity affinity = text_.edit_.affinity;
    CaretRect bar = caret_rect(*box, *host_.metrics, node->value, caret, affinity);
    // One rect, one scrollport: the least scroll (CSSOM "nearest") that puts the caret's font box
    // back inside. A `view_w` / `view_h` of -1 means the box's own padding box is the port, which
    // is what a fixed-height textarea or a long `<input>` scrolls; the caller passes the viewport
    // for the root, whose border box is the content, not the port (`scroll_into_view` does the
    // same).
    const auto bring = [&](layout::Box& port_box, float view_w, float view_h, float rect_y,
                           float rect_h) {
        ClientRect port;
        if (view_w >= 0.f && view_h >= 0.f) {
            port.x = 0.f;
            port.y = 0.f;
            port.w = view_w;
            port.h = view_h;
        } else {
            port = client_rect(port_box, host_.scrollbar_thickness);
        }
        if (port.w <= 0.f || port.h <= 0.f || port_box.node == nullptr) {
            return false;
        }
        const float pad_top =
            scroll_side(port_box.node, "scroll-padding", "scroll-padding-top", 0, port.h);
        const float pad_bottom =
            scroll_side(port_box.node, "scroll-padding", "scroll-padding-bottom", 2, port.h);
        const float pad_left =
            scroll_side(port_box.node, "scroll-padding", "scroll-padding-left", 3, port.w);
        const float pad_right =
            scroll_side(port_box.node, "scroll-padding", "scroll-padding-right", 1, port.w);
        const float x_view = std::max(0.f, port.w - pad_left - pad_right);
        const float y_view = std::max(0.f, port.h - pad_top - pad_bottom);
        const float content_x = (bar.x - port.x) + port_box.node->scroll_x;
        const float content_y = (rect_y - port.y) + port_box.node->scroll_y;
        const float next_x = align_scroll(ScrollAlign::Nearest, content_x, 1.f,
                                          port_box.node->scroll_x + pad_left, x_view) -
                             pad_left;
        const float next_y = align_scroll(ScrollAlign::Nearest, content_y, rect_h,
                                          port_box.node->scroll_y + pad_top, y_view) -
                             pad_top;
        return apply_scroll(port_box, next_x, next_y, view_w, view_h);
    };
    const ScrollRange own_range = scroll_range(*box);
    const bool own_scrolls_y = own_range.max_y > 0.01f;
    if (own_scrolls_y || own_range.max_x > 0.01f) {
        if (bring(*box, -1.f, -1.f, bar.y, bar.height)) {
            // The control slid its own text: republish before the ancestor is judged, or the caret
            // rect still names the offset it had before.
            host_.layout->sync_world();
            bar = caret_rect(*box, *host_.metrics, node->value, caret, affinity);
        }
    }
    // A control that can scroll its own text has just done it, and that is where the caret is read
    // from - the page does not have to move for it. One that cannot (a textarea that grows with
    // its value has no own range on either axis) can only be revealed by an ancestor, which is
    // where a writing page lives. zmd's 写作 parks a 24px proxy textarea at the paper's top for
    // the keyboard: it scrolls internally, so the paper - which follows the caret itself - is left
    // alone.
    if (own_scrolls_y) {
        return;
    }
    layout::Box* scroller = nearest_scrollable(box->parent, host_.scrollbar_thickness);
    float view_w = -1.f;
    float view_h = -1.f;
    if (scroller == nullptr && host_.layout->root() != nullptr) {
        document_scroll_view(*host_.layout, *host_.layout->root(), host_.scrollbar_thickness,
                             view_w, view_h);
        const ScrollRange root_range = scroll_range(*host_.layout->root(), view_w, view_h);
        if (root_range.max_y > 0.01f || root_range.max_x > root_range.min_x + 0.01f) {
            scroller = host_.layout->root();
        } else {
            scroller = nullptr;
            view_w = -1.f;
            view_h = -1.f;
        }
    } else if (scroller != nullptr) {
        document_scroll_view(*host_.layout, *scroller, host_.scrollbar_thickness, view_w, view_h);
    }
    if (scroller == nullptr || scroller->node == nullptr) {
        return;
    }
    if (bring(*scroller, view_w, view_h, bar.y, bar.height)) {
        host_.layout->sync_world();
        settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
    }
}

void Interaction::tick_drag_scroll() {
    if (!text_.selecting_text_ || text_.select_node_ == nullptr || host_.layout == nullptr ||
        pointer_.press_state_.empty()) {
        return;
    }
    html::Node* origin = text_.select_node_;
    for (const auto& entry : pointer_.press_state_) {
        if (html::Node* captured = capture_for(entry.first)) {
            origin = captured;
            break;
        }
    }
    if (handler_owner(origin, "pointermove") != nullptr) {
        return;
    }
    ClientRect port;
    float view_w = -1.f;
    float view_h = -1.f;
    layout::Box* scroller = text_drag_port(port, view_w, view_h);
    if (scroller == nullptr) {
        return;
    }
    if (port.w <= 0.f || port.h <= 0.f) {
        return;
    }
    // The step for one axis: a pointer before the port scrolls back, one past its end scrolls
    // forward, and the further out it is the faster a browser scrolls. Inside, nothing moves.
    const auto step = [](float pointer, float start, float end) {
        if (pointer < start) {
            return -std::min(40.f, 4.f + (start - pointer) * 0.25f);
        }
        if (pointer > end) {
            return std::min(40.f, 4.f + (pointer - end) * 0.25f);
        }
        return 0.f;
    };
    const float dx = step(text_.drag_x_, port.x, port.x + port.w);
    const float dy = step(text_.drag_y_, port.y, port.y + port.h);
    if ((dx != 0.f || dy != 0.f) &&
        apply_scroll(*scroller, scroller->node->scroll_x + dx, scroller->node->scroll_y + dy,
                     view_w, view_h)) {
        host_.layout->sync_world();
        if (host_.document != nullptr) {
            settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
        }
    }
    // The text under the pointer moved with the port: extend to what is there now.
    update_text_drag_focus();
}

layout::Box* Interaction::text_drag_port(ClientRect& port, float& view_w, float& view_h) {
    if (text_.select_node_ == nullptr || host_.layout == nullptr) {
        return nullptr;
    }
    layout::Box* box = host_.layout->principal_box(text_.select_node_);
    if (box == nullptr || box->node == nullptr) {
        return nullptr;
    }
    view_w = -1.f;
    view_h = -1.f;
    // The drag scrolls the same port the caret reveal would: the control's own text when its box
    // scrolls, the nearest scroller that really has a range when it does not (a textarea that
    // grows with its value has none).
    const ScrollRange own = scroll_range(*box);
    if (own.max_y > 0.01f || own.max_x > 0.01f) {
        port = client_rect(*box, host_.scrollbar_thickness);
        return box->node != nullptr ? box : nullptr;
    }
    layout::Box* scroller = nearest_scrollable(box->parent, host_.scrollbar_thickness);
    if (scroller == nullptr && host_.layout->root() != nullptr) {
        document_scroll_view(*host_.layout, *host_.layout->root(), host_.scrollbar_thickness,
                             view_w, view_h);
        const ScrollRange root_range = scroll_range(*host_.layout->root(), view_w, view_h);
        if (root_range.max_y > 0.01f || root_range.max_x > root_range.min_x + 0.01f) {
            scroller = host_.layout->root();
        }
    }
    if (scroller == nullptr || scroller->node == nullptr) {
        return nullptr;
    }
    if (view_w >= 0.f && view_h >= 0.f) {
        port.x = 0.f;
        port.y = 0.f;
        port.w = view_w;
        port.h = view_h;
    } else {
        port = client_rect(*scroller, host_.scrollbar_thickness);
    }
    return scroller;
}

void Interaction::update_text_drag_focus() {
    if (!text_.selecting_text_ || text_.select_node_ == nullptr || host_.layout == nullptr) {
        return;
    }
    float x = text_.drag_x_;
    float y = text_.drag_y_;
    if (text_.selecting_tree_) {
        layout::Box* hit = host_.layout->hit_test(x, y);
        html::Node* n = node_of(hit);
        html::Node* host = n;
        if (host != nullptr && host->tag == "#text" && host->parent != nullptr) {
            host = host->parent;
        }
        if (hit == nullptr && host != nullptr) {
            hit = host_.layout->principal_box(host);
        }
        if (hit != nullptr && host != nullptr) {
            html::BoundaryPoint focus = layout::hit_point(*hit, host, x, y);
            if (focus.node == nullptr) {
                focus.node = host;
            }
            commit_boundary_selection(text_.range_anchor_point_, focus);
        }
        return;
    }
    layout::Box* box = host_.layout->principal_box(text_.select_node_);
    if (box == nullptr || box->style_ptr == nullptr) {
        return;
    }
    ClientRect port;
    float view_w = -1.f;
    float view_h = -1.f;
    if (text_drag_port(port, view_w, view_h) != nullptr) {
        const float left = std::max(port.x, box->geo.world_x);
        const float top = std::max(port.y, box->geo.world_y);
        const float right = std::min(port.x + port.w, box->geo.world_x + box->geo.width);
        const float bottom = std::min(port.y + port.h, box->geo.world_y + box->geo.height);
        x = std::min(std::max(x, left), std::max(left, right));
        y = std::min(std::max(y, top), std::max(top, bottom));
    }
    text_.preferred_x_ = std::numeric_limits<float>::quiet_NaN();
        text_.select_focus_ = hit_edit_index(text_.select_node_, x, y);
    commit_edit_selection(text_.select_node_, text_.select_anchor_, text_.select_focus_);
}

std::string dom_path(const html::Document& doc, const html::Node* node) {
    if (node == nullptr || doc.root == nullptr) {
        return {};
    }
    if (node == doc.root.get()) {
        return "0";
    }
    std::vector<const html::Node*> chain;
    for (const html::Node* cursor = node; cursor != nullptr && cursor != doc.root.get();
         cursor = cursor->parent) {
        if (cursor->tag.empty() || cursor->tag == "#text") {
            continue;
        }
        chain.push_back(cursor);
    }
    std::string path = "0";
    const html::Node* cursor = doc.root.get();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        int index = 0;
        bool found = false;
        for (const auto& child : cursor->children) {
            if (child->tag.empty() || child->tag == "#text") {
                continue;
            }
            if (child.get() == *it) {
                found = true;
                break;
            }
            ++index;
        }
        if (!found) {
            return {};
        }
        path += "/" + std::to_string(index);
        cursor = *it;
    }
    return path;
}

Interaction::Interaction(InteractionHost host)
    : host_(std::move(host)) {}

}  // namespace input
}  // namespace zui
