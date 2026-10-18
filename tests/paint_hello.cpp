#include <cmath>
#include <gtest/gtest.h>


#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"
#include "paint/display_list.h"
#include "text/font_chain.h"

#include "log/log.h"

#include <cstdint>
#include <string>

TEST(PaintHello, FillsTextAndClips) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/hello/hello.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});
    if (layout.root() == nullptr) {
        FAIL() << "layout root missing\n";
    }

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);

    int fills = 0;
    int texts = 0;
    int clips = 0;
    int clip_depth = 0;
    int max_clip_depth = 0;
    bool saw_blue = false;
    bool saw_hello = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip) {
            ++clips;
            ++clip_depth;
            if (clip_depth > max_clip_depth) {
                max_clip_depth = clip_depth;
            }
        } else if (it.op == zui::paint::DisplayOp::PopClip) {
            --clip_depth;
        } else if (it.op == zui::paint::DisplayOp::FillRect) {
            ++fills;
            if (it.color.r == 0x2f && it.color.g == 0x6f && it.color.b == 0xeb) {
                saw_blue = true;
            }
        }
        if (it.op == zui::paint::DisplayOp::DrawText) {
            ++texts;
            if (it.text.find("Save") != std::string::npos ||
                it.text.find("Home") != std::string::npos) {
                saw_hello = true;
            }
        }
    }
    ZUI_FILE_INFO("fills={} texts={} clips={}", fills, texts, clips);
    if (clips < 1 || clip_depth != 0) {
        FAIL() << "overflow clip missing or unbalanced\n";
    }
    if (!saw_blue) {
        FAIL() << "missing primary button fill\n";
    }
    if (!saw_hello) {
        FAIL() << "missing Hello text\n";
    }
}

TEST(PaintHello, PlaceholderUsesItsOwnStyle) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/placeholder_color.html", doc,
                              error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 200.0f});
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);

    int seen = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText || it.text.empty()) {
            continue;
        }
        ++seen;
        if (it.color.r != 0x8a || it.color.g != 0x7f || it.color.b != 0x72) {
            FAIL() << "placeholder colour " << static_cast<int>(it.color.r) << ","
                   << static_cast<int>(it.color.g) << "," << static_cast<int>(it.color.b)
                   << " weight " << it.font_weight << " px " << it.font_px << "\n";
        }
        if (it.font_weight != 400) {
            FAIL() << "placeholder weight " << it.font_weight << "\n";
        }
        if (std::fabs(it.font_px - 16.0f) > 0.1f) {
            FAIL() << "placeholder font size " << it.font_px << "\n";
        }
    }
    if (seen == 0) {
        FAIL() << "placeholder text was not painted\n";
    }
}

/// Typing into a text control has to replace the placeholder run. `append_element_text` builds
/// that run from the generated `::placeholder` node, so until `BoxTree::sync_text` swapped the run
/// over, the placeholder kept painting and the typed value was never drawn at all.
TEST(PaintHello, TypingReplacesThePlaceholderRun) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/placeholder_color.html", doc,
                              error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.0f, 200.0f};
    layout.layout(doc, viewport);

    zui::html::Node* q = zui::html::find(doc, "q");
    if (q == nullptr) {
        FAIL() << "input#q missing\n";
    }
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);

    std::string placeholder;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::DrawText && !it.text.empty()) {
            placeholder = it.text;
        }
    }
    if (placeholder.empty()) {
        FAIL() << "the placeholder run was not painted\n";
    }

    // What App's typing path does: write the value onto the node, re-sync the run, re-lay out.
    q->value = "ab";
    q->text = q->value;
    layout.sync_text(q);
    layout.layout(doc, viewport);
    painter.paint(*layout.root(), list);

    bool painted_value = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText || it.text.empty()) {
            continue;
        }
        if (it.text == placeholder) {
            FAIL() << "the placeholder is still painted after typing\n";
        }
        if (it.text != "ab") {
            continue;
        }
        painted_value = true;
        // The run has to carry the element's own style now: `::placeholder` is #8a7f72 / 400 and
        // the control is #112233 / 700.
        if (it.color.r != 0x11 || it.color.g != 0x22 || it.color.b != 0x33) {
            FAIL() << "the typed value kept the placeholder colour\n";
        }
        if (it.font_weight != 700) {
            FAIL() << "the typed value kept the placeholder weight " << it.font_weight << "\n";
        }
    }
    if (!painted_value) {
        FAIL() << "the typed value was not painted\n";
    }
}

/// A text control's caret is the font box of the text on its line, not the control's content box:
/// Chromium sizes it from the font metrics (LayoutNG's `InlineBoxHeight`), so `height: 36px`
/// with `line-height: 36px` around a 16px font is a 21px caret whose top sits one half-leading
/// below the content top - measured in Chromium 150 at 1100x720 with Microsoft YaHei. The paint
/// runs have to keep carrying that box, or the caret falls back to the 34px content box.
TEST(PaintHello, TextRunCarriesTheFontBoxTheCaretNeeds) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/caret_metrics.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 200.0f});

    zui::html::Node* q = zui::html::find(doc, "q");
    ASSERT_NE(q, nullptr);
    zui::layout::Box* box = layout.principal_box(q);
    ASSERT_NE(box, nullptr);
    ASSERT_FALSE(box->geo.runs.empty()) << "the placeholder run is missing\n";
    const zui::layout::TextRun& run = box->geo.runs.front();

    // The same rounded font metrics the line's strut was built from.
    zui::text::FontChain& chain = metrics.chain_for("Microsoft YaHei", 400, false);
    chain.load(static_cast<std::uint32_t>(' '), 16.f, false);
    const float font_box = std::round(chain.ascent()) + std::round(chain.descent());
    ASSERT_GT(font_box, 0.f);
    EXPECT_NEAR(run.font_ascent + run.font_descent, font_box, 0.01f);
    EXPECT_LT(run.font_ascent + run.font_descent, box->geo.content_height)
        << "a caret as tall as the content box is the bug this field exists to avoid";
    // `line-height: 36px` centres the font box in the line, half-leading on the ascent.
    EXPECT_NEAR(run.ascent - run.font_ascent, std::floor((36.f - font_box) * 0.5f), 0.01f);
}
