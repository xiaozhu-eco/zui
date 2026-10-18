#include "layout/intern_detail.h"
#include "layout/style_literal.h"

namespace zui {
namespace layout {
namespace intern_detail {

void read_flex(LayoutStyle& style, const html::Node* node) {
    if (node == nullptr) {
        return;
    }
    if (const std::string* direction = style_literal(node, "flex-direction")) {
        if (*direction == "row-reverse") {
            style.flex_direction = FlexDirection::RowReverse;
        } else if (*direction == "column") {
            style.flex_direction = FlexDirection::Column;
        } else if (*direction == "column-reverse") {
            style.flex_direction = FlexDirection::ColumnReverse;
        }
    }
    if (const std::string* wrap = style_literal(node, "flex-wrap")) {
        if (*wrap == "wrap") {
            style.flex_wrap = FlexWrap::Wrap;
        } else if (*wrap == "wrap-reverse") {
            style.flex_wrap = FlexWrap::WrapReverse;
        }
    }
    if (const std::string* grow = style_literal(node, "flex-grow")) {
        style.flex_grow = std::strtof(grow->c_str(), nullptr);
    }
    if (const std::string* shrink = style_literal(node, "flex-shrink")) {
        style.flex_shrink = std::strtof(shrink->c_str(), nullptr);
    }
    if (const std::string* basis = style_literal(node, "flex-basis")) {
        if (*basis == "content" || *basis == "max-content" || *basis == "min-content") {
            style.flex_basis_auto = false;
            style.flex_basis_content = true;
        } else if (*basis != "auto") {
            const ParsedLength parsed =
                parse_length_kept(style, LengthProperty::FlexBasis, node, basis, true);
            style.flex_basis_auto = !parsed.valid || parsed.is_auto;
            style.flex_basis_percent = parsed.valid && parsed.is_percent && !parsed.is_auto;
            style.flex_basis_px = parsed.valid && !parsed.is_auto ? parsed.value : 0;
        }
    }
    if (const std::string* items = style_literal(node, "align-items")) {
        if (*items == "flex-start" || *items == "start") {
            style.align_items = FlexAlign::Start;
        } else if (*items == "flex-end" || *items == "end") {
            style.align_items = FlexAlign::End;
        } else if (*items == "center") {
            style.align_items = FlexAlign::Center;
        } else if (*items == "stretch") {
            style.align_items = FlexAlign::Stretch;
        } else if (*items == "baseline" || *items == "first baseline") {
            style.align_items = FlexAlign::Baseline;
        } else if (*items == "last baseline") {
            style.align_items = FlexAlign::LastBaseline;
        }
    }
    if (const std::string* align = style_literal(node, "align-self")) {
        if (*align == "flex-start" || *align == "start") {
            style.align_self = FlexAlign::Start;
        } else if (*align == "flex-end" || *align == "end") {
            style.align_self = FlexAlign::End;
        } else if (*align == "center") {
            style.align_self = FlexAlign::Center;
        } else if (*align == "stretch") {
            style.align_self = FlexAlign::Stretch;
        } else if (*align == "baseline" || *align == "first baseline") {
            style.align_self = FlexAlign::Baseline;
        } else if (*align == "last baseline") {
            style.align_self = FlexAlign::LastBaseline;
        }
    }
    // justify-self is the inline-axis sibling of align-self (CSS Box Alignment). left and right
    // map to start and end for a horizontal writing mode.
    if (const std::string* justify = style_literal(node, "justify-self")) {
        if (*justify == "flex-start" || *justify == "start" || *justify == "left") {
            style.justify_self = FlexAlign::Start;
        } else if (*justify == "flex-end" || *justify == "end" || *justify == "right") {
            style.justify_self = FlexAlign::End;
        } else if (*justify == "center") {
            style.justify_self = FlexAlign::Center;
        } else if (*justify == "stretch") {
            style.justify_self = FlexAlign::Stretch;
        } else if (*justify == "baseline" || *justify == "first baseline") {
            style.justify_self = FlexAlign::Baseline;
        } else if (*justify == "last baseline") {
            style.justify_self = FlexAlign::LastBaseline;
        }
    }
    if (const std::string* pack = style_literal(node, "justify-content")) {
        style.justify_content = parse_pack(*pack);
    }
    if (const std::string* order = style_literal(node, "order")) {
        style.flex_order = std::atoi(order->c_str());
    }
    const ParsedLength column_gap = parse_length_kept(
        style, LengthProperty::ColumnGap, node, style_literal(node, "column-gap"), false);
    if (column_gap.valid && !column_gap.is_auto) {
        style.column_gap_percent = column_gap.is_percent;
        style.column_gap = column_gap.value;
    }
    // CSS Multi-column Layout section 3. `columns` is the shorthand for the pair, in either order.
    auto apply_column_count = [&style, node](const std::string* value) {
        if (value == nullptr || *value == "auto") {
            style.column_count = 0;
            return;
        }
        const long parsed = std::strtol(value->c_str(), nullptr, 10);
        style.column_count = parsed >= 1 ? static_cast<int>(parsed) : 0;
    };
    auto apply_column_width = [&style, node](const std::string* value) {
        if (value == nullptr || *value == "auto") {
            style.column_width_auto = true;
            style.column_width = 0;
            return;
        }
        const ParsedLength parsed = parse_length(node, value, false);
        if (!parsed.valid || parsed.is_auto || parsed.is_percent) {
            style.column_width_auto = true;
            style.column_width = 0;
            return;
        }
        style.column_width_auto = false;
        style.column_width = parsed.value;
    };
    apply_column_count(style_literal(node, "column-count"));
    apply_column_width(style_literal(node, "column-width"));
    if (const std::string* shorthand = style_literal(node, "columns")) {
        // `<column-width> || <column-count>`, in either order. The length is a plain px here; the
        // longhands above already carry everything the cascade can put on them.
        std::string token;
        const std::string raw = *shorthand;
        for (std::size_t i = 0; i <= raw.size(); ++i) {
            const bool end = i == raw.size();
            if (!end && !std::isspace(static_cast<unsigned char>(raw[i]))) {
                token.push_back(raw[i]);
                continue;
            }
            if (token.empty()) {
                continue;
            }
            if (token == "auto") {
                token.clear();
                continue;
            }
            if (token.front() >= '0' && token.front() <= '9' &&
                token.find('.') == std::string::npos && token.find("px") == std::string::npos) {
                style.column_count = std::max(1, std::atoi(token.c_str()));
            } else {
                const std::size_t unit = token.find("px");
                const float value = std::strtof(token.c_str(), nullptr);
                if (value > 0.f && unit != std::string::npos) {
                    style.column_width_auto = false;
                    style.column_width = value;
                }
            }
            token.clear();
        }
    }
    if (const std::string* fill = style_literal(node, "column-fill")) {
        style.column_fill_auto = *fill == "auto";
    }
    if (const std::string* span = style_literal(node, "column-span")) {
        style.column_span_all = *span == "all";
    }
    const ParsedLength row_gap =
        parse_length_kept(style, LengthProperty::RowGap, node, style_literal(node, "row-gap"), false);
    if (row_gap.valid && !row_gap.is_auto) {
        style.row_gap_percent = row_gap.is_percent;
        style.row_gap = row_gap.value;
    }
    auto parse_break_between = [](const std::string* raw) {
        if (raw == nullptr) {
            return BreakBetween::Auto;
        }
        if (*raw == "avoid" || *raw == "avoid-page" || *raw == "avoid-column") {
            return BreakBetween::Avoid;
        }
        if (*raw == "always" || *raw == "page" || *raw == "left" || *raw == "right" ||
            *raw == "recto" || *raw == "verso") {
            return BreakBetween::Always;
        }
        if (*raw == "column") {
            return BreakBetween::Column;
        }
        return BreakBetween::Auto;
    };
    // page-break-* maps onto break-* (CSS Fragmentation 3); prefer the modern longhand when both
    // are present.
    const std::string* break_before = style_literal(node, "break-before");
    if (break_before == nullptr) {
        break_before = style_literal(node, "page-break-before");
    }
    style.break_before = parse_break_between(break_before);
    const std::string* break_after = style_literal(node, "break-after");
    if (break_after == nullptr) {
        break_after = style_literal(node, "page-break-after");
    }
    style.break_after = parse_break_between(break_after);
    const std::string* break_inside = style_literal(node, "break-inside");
    if (break_inside == nullptr) {
        break_inside = style_literal(node, "page-break-inside");
    }
    if (break_inside != nullptr && (*break_inside == "avoid" || *break_inside == "avoid-page" ||
                                    *break_inside == "avoid-column")) {
        style.break_inside = BreakInside::Avoid;
    }
    auto parse_keep_lines = [](const std::string* raw, int fallback) {
        if (raw == nullptr || raw->empty()) {
            return fallback;
        }
        char* end = nullptr;
        const long parsed = std::strtol(raw->c_str(), &end, 10);
        if (end == raw->c_str() || parsed < 1 || parsed > 1000) {
            return fallback;
        }
        return static_cast<int>(parsed);
    };
    style.orphans = parse_keep_lines(style_literal(node, "orphans"), 2);
    style.widows = parse_keep_lines(style_literal(node, "widows"), 2);
}


}  // namespace intern_detail
}  // namespace layout
}  // namespace zui
