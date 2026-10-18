// The HTML widget preferred sizes: checkbox / radio, range, progress, file and a text input
// with `width: auto` (they are the boxes whose used size comes from the control, not the CSS).

#include "layout/box.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/shaped_run.h"
#include "layout/flow/detail.h"

#include <algorithm>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zui {
namespace layout {
namespace flow_detail {

void resolve_widget_size(Box& box, LayoutContext& cx, const LayoutStyle& style,
                         bool& width_auto, float& width_used, bool& height_auto,
                         float& height_used) {
    // A checkbox or radio with width/height auto uses the 13px widget. Specified
    // lengths, including an author class of 1px, stay as written.
    if (box.node != nullptr &&
        (zui::html::is_checkbox(box.node) || zui::html::is_radio(box.node))) {
        if (width_auto) {
            width_used = 13.f;
            width_auto = false;
        }
        if (height_auto) {
            height_used = 13.f;
            height_auto = false;
        }
    }
    // Range / progress / file preferred sizes when width/height are auto (Chromium-like).
    if (box.node != nullptr && zui::html::is_range(box.node)) {
        if (width_auto) {
            width_used = 129.f;
            width_auto = false;
        }
        if (height_auto) {
            height_used = 16.f;
            height_auto = false;
        }
    }
    if (box.node != nullptr && zui::html::is_progress(box.node)) {
        if (width_auto) {
            width_used = 160.f;
            width_auto = false;
        }
        if (height_auto) {
            height_used = 16.f;
            height_auto = false;
        }
    }
    if (box.node != nullptr && zui::html::is_file(box.node)) {
        if (width_auto) {
            width_used = 240.f;
            width_auto = false;
        }
        if (height_auto) {
            height_used = 22.f;
            height_auto = false;
        }
    }
    // HTML rendering: a text `<input>` with `width: auto` takes the preferred size from the
    // `size` attribute (default 20), including when `display` is block. Without this the field
    // stretches like a normal block and every undeclared text input is as wide as its CB.
    // A flex item still receives this as its definite width for `flex-basis: auto`; a later
    // `flex_inline` available size (the flex algorithm's resolved main size) overrides it.
    if (box.node != nullptr && width_auto && zui::html::is_text_field(box.node)) {
        width_used = cx.text_field_preferred_content(style, zui::html::text_field_size(box.node));
        width_auto = false;
    }
}

namespace {

// The shadow button uses the same UA rule as a push button (engine.cpp): border-box,
// padding 1px 6px, border 2px, and a 4px margin on the end side. Measured on Edge's
// `::-webkit-file-upload-button` from 12px through 20px; none of those lengths scale
// with the font.
constexpr float kButtonPadInline = 6.f;
constexpr float kButtonPadBlock = 1.f;
constexpr float kButtonBorder = 2.f;
constexpr float kButtonMarginEnd = 4.f;

struct FileLabels {
    const char* button = "Choose File";
    const char* empty = "No file chosen";
};

// The labels are the host UI language, which is what the capture browser shows. Simplified
// Chinese was measured on this Edge ("选择文件" / "未选择文件").
FileLabels file_labels() {
#ifdef _WIN32
    const LANGID lang = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(lang) == LANG_CHINESE) {
        const WORD sub = SUBLANGID(lang);
        if (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE) {
            return {"选择文件", "未选择文件"};
        }
        return {"選擇檔案", "未選擇檔案"};
    }
#else
    (void)0;
#endif
    return {};
}

bool file_appearance_native(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string* appearance = node->style("appearance");
    return appearance == nullptr || appearance->empty() || *appearance == "auto";
}

float shaped_width(LayoutContext& cx, const LayoutStyle& style, std::string_view text) {
    float width = 0.f;
    for (const ShapedRun& run : cx.shape(text, style, style.direction)) {
        width += run.advance_inline;
    }
    return width;
}

}  // namespace

Rect file_shadow_scroll(const Box& box, LayoutContext& cx, const LayoutStyle& style) {
    if (box.node == nullptr || !html::is_file(box.node) || !file_appearance_native(box.node)) {
        return {};
    }
    const FileLabels labels = file_labels();
    const std::string& filename =
        box.node->value.empty() ? std::string(labels.empty) : box.node->value;
    // The button keeps Arial (it does not inherit the input's family) at the input's size.
    // Its line box is the tallest face that draws the label, so CJK missing from Arial uses
    // the fallback face rather than Arial's own 17px box.
    LayoutStyle button = style;
    // The button does not inherit the input's family. On this Edge a glyph Arial lacks is drawn
    // with Noto Sans SC, and that face's win box is the line box. The generic fallback lists
    // YaHei first, which is one pixel short, so name Noto ahead of it for this label only.
    button.font_family = "Arial, \"Noto Sans SC\"";
    button.font_weight = 400;
    button.font_italic = false;
    button.line_height_kind = LineHeightKind::Normal;
    const float label_width = shaped_width(cx, button, labels.button);
    const float label_box = cx.text_font_box(button, labels.button);
    if (label_width <= 0.f || label_box <= 0.f) {
        return {};
    }
    const float button_inline =
        label_width + kButtonPadInline * 2.f + kButtonBorder * 2.f;
    const float button_block = label_box + kButtonPadBlock * 2.f + kButtonBorder * 2.f;
    const float name_width = shaped_width(cx, style, filename);
    const float name_block = used_line_height(style, cx.font_em(style));
    return Rect{0.f, 0.f, button_inline + kButtonMarginEnd + name_width,
                std::max(button_block, name_block)};
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
