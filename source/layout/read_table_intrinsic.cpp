#include "layout/intern_style.h"
#include "layout/style_literal.h"

#include "layout/cache/key_policy.h"

#include "layout/intern_grid.h"

#include "layout/style_table.h"

#include "css/values.h"
#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "layout/intern_detail.h"

namespace zui {
namespace layout {
namespace intern_detail {

bool table_row_display(const std::string& display, const std::string& tag) {
    return display == "table-row" || display == "table-row-group" ||
           display == "table-header-group" || display == "table-footer-group" || tag == "tr" ||
           tag == "thead" || tag == "tbody" || tag == "tfoot";
}

bool table_column_display(const std::string& display, const std::string& tag) {
    return display == "table-column" || display == "table-column-group" || tag == "col" ||
           tag == "colgroup";
}

std::vector<std::string> split_ws_simple(std::string_view raw) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

// The display value as a table part, with the HTML tags as the fallback when the author did not
// restate the display (an author `display: block` on a `<tr>` really does win).
TablePart table_part_of(const html::Node* node) {
    const std::string* d = style_literal(node, "display");
    const std::string display = d != nullptr ? *d : std::string();
    if (display == "table" || display == "inline-table") {
        return display == "table" ? TablePart::Table : TablePart::InlineTable;
    }
    if (display == "table-row-group") {
        return TablePart::RowGroup;
    }
    if (display == "table-header-group") {
        return TablePart::HeaderGroup;
    }
    if (display == "table-footer-group") {
        return TablePart::FooterGroup;
    }
    if (display == "table-row") {
        return TablePart::Row;
    }
    if (display == "table-cell") {
        return TablePart::Cell;
    }
    if (display == "table-column") {
        return TablePart::Column;
    }
    if (display == "table-column-group") {
        return TablePart::ColumnGroup;
    }
    if (display == "table-caption") {
        return TablePart::Caption;
    }
    if (!display.empty() && display != "block" && display != "inline" &&
        display != "inline-block") {
        return TablePart::None;
    }
    // No display value of its own: the tag decides, which is what the UA sheet would have said.
    const std::string& tag = node->tag;
    if (tag == "table") {
        return TablePart::Table;
    }
    if (tag == "thead") {
        return TablePart::HeaderGroup;
    }
    if (tag == "tbody") {
        return TablePart::RowGroup;
    }
    if (tag == "tfoot") {
        return TablePart::FooterGroup;
    }
    if (tag == "tr") {
        return TablePart::Row;
    }
    if (tag == "td" || tag == "th") {
        return TablePart::Cell;
    }
    if (tag == "col") {
        return TablePart::Column;
    }
    if (tag == "colgroup") {
        return TablePart::ColumnGroup;
    }
    if (tag == "caption") {
        return TablePart::Caption;
    }
    return TablePart::None;
}

void read_table(LayoutStyle& style, const html::Node* node) {
    if (style.table_part == TablePart::None) {
        style.table_part = table_part_of(node);
    }
    if (const std::string* collapse = style_literal(node, "border-collapse")) {
        style.border_collapse = *collapse == "collapse";
    }
    // `border-spacing: <inline> [<block>]`; the second value defaults to the first.
    if (const std::string* spacing = style_literal(node, "border-spacing")) {
        const auto parts = split_ws_simple(*spacing);
        if (!parts.empty()) {
            const css::ResolvedLength first =
                resolve_with_viewport(parts[0], font_size_px(node), root_font_size_px(node));
            if (first.valid && !first.is_percent) {
                style.border_spacing_inline = first.px;
            }
        }
        const std::string second =
            parts.size() >= 2 ? parts[1] : (parts.empty() ? std::string() : parts[0]);
        if (!second.empty()) {
            const css::ResolvedLength parsed =
                resolve_with_viewport(second, font_size_px(node), root_font_size_px(node));
            if (parsed.valid && !parsed.is_percent) {
                style.border_spacing_block = parsed.px;
            }
        }
    }
    if (const std::string* layout = style_literal(node, "table-layout")) {
        style.table_fixed = *layout == "fixed";
    }
    if (const std::string* side = style_literal(node, "caption-side")) {
        style.caption_below = *side == "bottom";
    }
    // `colspan` / `rowspan` are content, not cascade: they ride on the node's attributes.
    if (node->tag == "td" || node->tag == "th") {
        if (const auto it = node->attrs.find("colspan"); it != node->attrs.end()) {
            style.table_colspan = std::max(1, std::atoi(it->second.c_str()));
        }
        if (const auto it = node->attrs.find("rowspan"); it != node->attrs.end()) {
            style.table_rowspan = std::max(1, std::atoi(it->second.c_str()));
        }
    }
    if (style.table_part == TablePart::Column || style.table_part == TablePart::ColumnGroup) {
        if (const auto it = node->attrs.find("span"); it != node->attrs.end()) {
            style.table_span = std::max(1, std::atoi(it->second.c_str()));
        }
    }
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// One `<length>` (or `none`) of a `contain-intrinsic-size` value. `none` is the "no intrinsic
/// size" answer and comes back as -1; a percentage is not in that value's grammar (CSS Sizing
/// section 5.3), so it is rejected rather than resolved.
bool read_intrinsic_value(const std::string& token, float font_size, float root_font_size,
                          float* out) {
    if (token == "none") {
        *out = -1.f;
        return true;
    }
    const css::ResolvedLength resolved = resolve_with_viewport(token, font_size, root_font_size);
    if (!resolved.valid || resolved.is_percent) {
        return false;
    }
    *out = resolved.px;
    return true;
}

/// Whitespace-separated, lower-cased tokens of one `contain-intrinsic-size` value.
std::vector<std::string> intrinsic_tokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i > start) {
            tokens.push_back(lower_ascii(text.substr(start, i - start)));
        }
    }
    return tokens;
}

/// The leading `auto`, if it is there. It asks for the element's **last remembered size**, which
/// this engine does not keep: the declaration then means its fallback length. Reading the keyword
/// with `strtof` is what made `contain-intrinsic-size: auto 38px` a zero placeholder.
void eat_intrinsic_auto(const std::vector<std::string>& tokens, std::size_t* index) {
    if (*index < tokens.size() && tokens[*index] == "auto") {
        ++*index;
    }
}

/// One axis: `auto? [ none | <length> ]` (CSS Sizing section 5.3), the whole value of a longhand.
bool read_intrinsic_longhand(std::string_view text, float font_size, float root_font_size,
                             float* out) {
    const std::vector<std::string> tokens = intrinsic_tokens(text);
    std::size_t i = 0;
    eat_intrinsic_auto(tokens, &i);
    if (i + 1 != tokens.size()) {
        return false;
    }
    return read_intrinsic_value(tokens[i], font_size, root_font_size, out);
}

/// The shorthand: `auto? [ none | <length> ]{1,2}`. One value sets both axes, two set the inline
/// axis and then the block axis.
bool read_intrinsic_pair(std::string_view text, float font_size, float root_font_size,
                         float* inline_px, float* block_px) {
    const std::vector<std::string> tokens = intrinsic_tokens(text);
    std::size_t i = 0;
    eat_intrinsic_auto(tokens, &i);
    const std::size_t count = tokens.size() - i;
    if (count < 1 || count > 2) {
        return false;
    }
    float first = -1.f;
    if (!read_intrinsic_value(tokens[i], font_size, root_font_size, &first)) {
        return false;
    }
    float second = first;
    if (count == 2 && !read_intrinsic_value(tokens[i + 1], font_size, root_font_size, &second)) {
        return false;
    }
    *inline_px = first;
    *block_px = second;
    return true;
}

/// `contain-intrinsic-size` and its two longhands. A longhand that is present settles its own axis,
/// and the shorthand then has nothing left to say - the engine reads the computed map, which no
/// longer knows which declaration came last, so "the longhand wins" is the rule it can keep.
void read_intrinsic(LayoutStyle& style, const html::Node* node) {
    if (node == nullptr) {
        return;
    }
    const float font = font_size_px(node);
    const float root = root_font_size_px(node);
    float inline_px = -1.f;
    float block_px = -1.f;
    const std::string* width = style_literal(node, "contain-intrinsic-width");
    const std::string* height = style_literal(node, "contain-intrinsic-height");
    if (width != nullptr) {
        read_intrinsic_longhand(*width, font, root, &inline_px);
    }
    if (height != nullptr) {
        read_intrinsic_longhand(*height, font, root, &block_px);
    }
    if (width == nullptr && height == nullptr) {
        if (const std::string* shorthand = style_literal(node, "contain-intrinsic-size")) {
            read_intrinsic_pair(*shorthand, font, root, &inline_px, &block_px);
        }
    }
    style.contain_intrinsic_inline = inline_px;
    style.contain_intrinsic_block = block_px;
}

}  // namespace intern_detail
}  // namespace layout
}  // namespace zui
