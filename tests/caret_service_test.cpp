#include "layout/caret/caret_service.h"
#include "layout/caret/caret_types.h"
#include "layout/caret/inline_lines.h"

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/line_box.h"
#include "layout/engine.h"
#include "layout/paint_map.h"
#include "input/edit_model.h"
#include "input/interaction.h"
#include "paint/display_list.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

using zui::layout::Box;
using zui::layout::CaretAffinity;
using zui::layout::CaretHit;
using zui::layout::CaretRect;
using zui::layout::EditMapping;
using zui::layout::LayoutEngine;
using zui::layout::get_rect;
using zui::layout::hit_test;
using zui::layout::inline_lines;
using zui::layout::line_of;
using zui::layout::make_edit_mapping;

bool load_doc(LayoutEngine& layout, zui::html::Document& doc, const char* path, float vw,
              float vh) {
    std::string error;
    zui::css::Engine css;
    if (!zui::html::load_file(path, doc, error)) {
        return false;
    }
    if (!css.load_sheets(doc, error)) {
        return false;
    }
    css.cascade(doc);
    layout.layout(doc, zui::layout::Viewport{vw, vh});
    if (layout.root() != nullptr) {
        zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());
    }
    return true;
}

}  // namespace

TEST(CaretService, MultiRunSameVisualLineHitAndRect) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html", 800.f,
                         200.f));

    zui::html::Node* line_n = zui::html::find(doc, "line");
    ASSERT_NE(line_n, nullptr);
    Box* box = layout.principal_box(line_n);
    ASSERT_NE(box, nullptr);

    const auto* lines = inline_lines(*box);
    ASSERT_NE(lines, nullptr);
    ASSERT_FALSE(lines->lines.empty());
    ASSERT_EQ(lines->lines.size(), 1u) << "expected a single visual line";
    const zui::layout::LineBox& line = lines->lines.front();
    const float line_font_box = line.font_ascent + line.font_descent;
    ASSERT_GT(line_font_box, 0.f);

    const std::string edit = "aabb";
    const EditMapping mapping = make_edit_mapping(*box, edit);
    ASSERT_GE(mapping.spans.size(), 2u)
        << "expected mappable spans for both style runs (plain + bold)";

    const std::size_t line_i_a = line_of(*box, mapping, 1, CaretAffinity::Downstream);
    const std::size_t line_i_b = line_of(*box, mapping, 3, CaretAffinity::Downstream);
    ASSERT_NE(line_i_a, static_cast<std::size_t>(-1));
    EXPECT_EQ(line_i_a, line_i_b) << "offsets in both style runs must share one LineBox";
    EXPECT_EQ(line_i_a, 0u);

    for (std::size_t off = 0; off <= edit.size(); ++off) {
        const CaretRect rect = get_rect(*box, mapping, off, CaretAffinity::Downstream);
        ASSERT_TRUE(rect.ok) << "offset " << off;
        EXPECT_NEAR(rect.height, line_font_box, 0.05f)
            << "caret height must be the LineBox font box, not a single TextRun guess; off=" << off;
    }

    float prev_x = -1.e9f;
    for (std::size_t off = 0; off <= edit.size(); ++off) {
        const CaretRect rect = get_rect(*box, mapping, off, CaretAffinity::Downstream);
        ASSERT_TRUE(rect.ok);
        EXPECT_GE(rect.x + 0.05f, prev_x) << "get_rect x not monotonic at offset " << off;
        prev_x = rect.x;
    }

    const CaretRect at0 = get_rect(*box, mapping, 0, CaretAffinity::Downstream);
    const CaretRect at_end = get_rect(*box, mapping, edit.size(), CaretAffinity::Downstream);
    ASSERT_TRUE(at0.ok);
    ASSERT_TRUE(at_end.ok);
    ASSERT_GT(at_end.x, at0.x);

    std::size_t prev_off = 0;
    const float y = at0.y + at0.height * 0.5f;
    const int samples = 8;
    for (int s = 0; s <= samples; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(samples);
        const float x = at0.x + (at_end.x - at0.x) * t;
        const CaretHit hit = hit_test(*box, mapping, x, y);
        ASSERT_TRUE(hit.ok) << "hit_test failed at sample " << s;
        EXPECT_GE(hit.offset, prev_off) << "hit_test offsets not monotonic";
        prev_off = hit.offset;
        EXPECT_EQ(line_of(*box, mapping, hit.offset, hit.affinity), line_i_a);
    }
}

TEST(CaretService, EmptyPlaceholderHasCaret) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_metrics.html", 800.f,
                         200.f));

    zui::html::Node* q = zui::html::find(doc, "q");
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(q->value.empty()) << "fixture search field starts empty (placeholder painted)";
    Box* box = layout.principal_box(q);
    ASSERT_NE(box, nullptr);
    const auto* lines = inline_lines(*box);
    ASSERT_NE(lines, nullptr);
    ASSERT_FALSE(lines->lines.empty());
    const float font_box = lines->lines.front().font_ascent + lines->lines.front().font_descent;
    ASSERT_GT(font_box, 0.f);

    const EditMapping mapping = make_edit_mapping(*box, q->value);
    ASSERT_FALSE(mapping.spans.empty()) << "empty value uses the line strut, not the placeholder";
    EXPECT_EQ(mapping.spans.front().byte_begin, 0u);
    EXPECT_EQ(mapping.spans.front().byte_end, 0u);

    const CaretRect rect = get_rect(*box, mapping, 0, CaretAffinity::Downstream);
    ASSERT_TRUE(rect.ok);
    EXPECT_NEAR(rect.height, font_box, 0.05f);
    EXPECT_GE(rect.x, box->geo.world_x);
    EXPECT_LE(rect.x, box->geo.world_x + box->geo.width);
    EXPECT_GE(rect.y, box->geo.world_y - 0.5f);
    EXPECT_LE(rect.y + rect.height, box->geo.world_y + box->geo.height + 1.f);

    const zui::input::CaretRect via_input = zui::input::caret_rect(*box, metrics, q->value, 0);
    EXPECT_NEAR(via_input.x, rect.x, 0.5f);
    EXPECT_NEAR(via_input.y, rect.y, 0.5f);
    EXPECT_NEAR(via_input.height, rect.height, 0.5f);
    EXPECT_GT(via_input.height, 0.f);

    const CaretHit hit = hit_test(*box, mapping, box->geo.world_x + box->geo.width * 0.5f,
                                  box->geo.world_y + box->geo.height * 0.5f);
    ASSERT_TRUE(hit.ok);
    EXPECT_EQ(hit.offset, 0u);
}

TEST(CaretService, EmptyEditorClickPaintsCaret) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_empty.html", 800.f,
                         600.f));

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &doc;
    host.layout = &layout;
    host.metrics = &metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    zui::input::Interaction interaction(host);

    const char* ids[] = {"in-ph", "in-empty", "ta-ph", "ta-empty"};
    for (const char* id : ids) {
        zui::html::Node* node = zui::html::find(doc, id);
        ASSERT_NE(node, nullptr) << id;
        EXPECT_TRUE(node->value.empty()) << id;
        Box* box = layout.principal_box(node);
        ASSERT_NE(box, nullptr) << id;
        const auto* lines = inline_lines(*box);
        ASSERT_NE(lines, nullptr) << id;
        ASSERT_FALSE(lines->lines.empty()) << id;
        const float font_box = lines->lines.front().font_ascent + lines->lines.front().font_descent;
        ASSERT_GT(font_box, 0.f) << id;

        const EditMapping mapping = make_edit_mapping(*box, node->value);
        ASSERT_FALSE(mapping.spans.empty()) << id;
        const CaretRect service = get_rect(*box, mapping, 0, CaretAffinity::Downstream);
        ASSERT_TRUE(service.ok) << id;
        EXPECT_NEAR(service.height, font_box, 0.05f) << id;
        EXPECT_GE(service.x, box->geo.world_x) << id;
        EXPECT_LE(service.x, box->geo.world_x + box->geo.width) << id;
        EXPECT_GE(service.y, box->geo.world_y - 0.5f) << id;
        EXPECT_LE(service.y + service.height, box->geo.world_y + box->geo.height + 1.f) << id;

        const zui::input::CaretRect via_input =
            zui::input::caret_rect(*box, metrics, node->value, 0);
        EXPECT_NEAR(via_input.x, service.x, 0.5f) << id;
        EXPECT_NEAR(via_input.y, service.y, 0.5f) << id;
        EXPECT_GT(via_input.height, 0.f) << id;

        const float cx = box->geo.world_x + box->geo.width * 0.5f;
        const float cy = box->geo.world_y + box->geo.height * 0.5f;
        const CaretHit hit = hit_test(*box, mapping, cx, cy);
        ASSERT_TRUE(hit.ok) << id;
        EXPECT_EQ(hit.offset, 0u) << id;

        interaction.pointer_button("mousePressed", cx, cy, "left", 1);
        EXPECT_EQ(focused, node) << id;
        EXPECT_EQ(node->caret, 0) << id;

        zui::paint::DisplayList list;
        zui::paint::Painter painter;
        painter.set_caret(node, static_cast<std::size_t>(std::max(node->caret, 0)),
                          zui::paint::Color{20, 80, 200, 255}, true);
        ASSERT_NE(layout.root(), nullptr);
        painter.paint(*layout.root(), list, 800.f, 600.f);
        const zui::paint::Painter::CaretBar bar = painter.last_caret_rect();
        ASSERT_TRUE(bar.ok) << id;
        EXPECT_GT(bar.h, 0.f) << id;
        EXPECT_NEAR(bar.x, service.x, 0.75f) << id;
        EXPECT_NEAR(bar.y, service.y, 0.75f) << id;
        EXPECT_NEAR(bar.h, service.height, 0.75f) << id;

        bool filled = false;
        for (const zui::paint::DisplayItem& item : list.items()) {
            if (item.op != zui::paint::DisplayOp::FillRect) {
                continue;
            }
            if (std::fabs(item.rect.w - 1.f) > 0.2f || item.rect.h < 1.f) {
                continue;
            }
            if (std::fabs(item.rect.x - bar.x) < 0.6f && std::fabs(item.rect.y - bar.y) < 0.6f) {
                filled = true;
                break;
            }
        }
        EXPECT_TRUE(filled) << id << " paint list has no caret fill";
    }
}

TEST(CaretService, InputCaretRectMatchesService) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html", 800.f,
                         200.f));

    zui::html::Node* line_n = zui::html::find(doc, "line");
    ASSERT_NE(line_n, nullptr);
    Box* box = layout.principal_box(line_n);
    ASSERT_NE(box, nullptr);

    // Multi-style same visual line: pre-service IFC cover failed (geo.runs empty on the parent),
    // so input::caret_rect used a content-edge measure fallback that disagreed with the service.
    const std::string edit = "aabb";
    const EditMapping mapping = make_edit_mapping(*box, edit);
    ASSERT_GE(mapping.spans.size(), 2u);

    for (std::size_t off : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
        const CaretRect service = get_rect(*box, mapping, off, CaretAffinity::Downstream);
        ASSERT_TRUE(service.ok) << "service offset " << off;
        const zui::input::CaretRect via_input = zui::input::caret_rect(*box, metrics, edit, off);
        EXPECT_NEAR(via_input.x, service.x, 0.5f) << "x off=" << off;
        EXPECT_NEAR(via_input.y, service.y, 0.5f) << "y off=" << off;
        EXPECT_NEAR(via_input.height, service.height, 0.5f) << "height off=" << off;
        EXPECT_NEAR(via_input.height, service.height, 0.5f);
        EXPECT_GT(via_input.height, 0.f);
    }

    // Soft-wrapped textarea: input::caret_rect must track the service on every laid-out line.
    zui::html::Document ta_doc;
    LayoutEngine ta_layout(metrics);
    ASSERT_TRUE(load_doc(ta_layout, ta_doc,
                         ZUI_SOURCE_DIR "/tests/fixtures/caret_textarea_wrap.html", 800.f, 400.f));
    zui::html::Node* ta = zui::html::find(ta_doc, "ta");
    ASSERT_NE(ta, nullptr);
    ASSERT_FALSE(ta->value.empty());
    Box* ta_box = ta_layout.principal_box(ta);
    ASSERT_NE(ta_box, nullptr);
    const EditMapping ta_map = make_edit_mapping(*ta_box, ta->value);
    ASSERT_FALSE(ta_map.spans.empty()) << "textarea value should map";
    for (std::size_t off = 0; off <= ta->value.size(); off = off == 0 ? 1 : off * 2) {
        if (off > ta->value.size()) {
            off = ta->value.size();
        }
        const CaretRect service = get_rect(*ta_box, ta_map, off, CaretAffinity::Downstream);
        if (!service.ok) {
            continue;
        }
        const zui::input::CaretRect via_input =
            zui::input::caret_rect(*ta_box, metrics, ta->value, off);
        EXPECT_NEAR(via_input.x, service.x, 0.75f) << "ta x off=" << off;
        EXPECT_NEAR(via_input.y, service.y, 0.75f) << "ta y off=" << off;
        EXPECT_NEAR(via_input.height, service.height, 0.75f) << "ta h off=" << off;
        if (off == ta->value.size()) {
            break;
        }
    }
}

namespace {

struct SoftWrapProbe {
    LayoutEngine* layout = nullptr;
    zui::html::Document* doc = nullptr;
    Box* box = nullptr;
    std::string text;
    EditMapping mapping;
};

bool load_soft_wrap(SoftWrapProbe& p, zui::layout::FreeTypeTextMetrics& metrics) {
    p.doc = new zui::html::Document();
    p.layout = new LayoutEngine(metrics);
    if (!load_doc(*p.layout, *p.doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_soft_wrap.html", 400.f,
                  300.f)) {
        return false;
    }
    zui::html::Node* ta = zui::html::find(*p.doc, "ta");
    if (ta == nullptr) {
        return false;
    }
    p.text = ta->value;
    p.box = p.layout->principal_box(ta);
    if (p.box == nullptr) {
        return false;
    }
    p.mapping = make_edit_mapping(*p.box, p.text);
    return !p.mapping.spans.empty();
}

void free_soft_wrap(SoftWrapProbe& p) {
    delete p.layout;
    delete p.doc;
    p.layout = nullptr;
    p.doc = nullptr;
}

/// First byte that is both the end of one visual line and the start of the next (soft wrap).
bool find_soft_wrap_offset(const EditMapping& mapping, std::size_t& out_offset,
                           std::size_t& out_line_before) {
    for (const auto& a : mapping.spans) {
        for (const auto& b : mapping.spans) {
            if (b.line_index == a.line_index + 1 && b.byte_begin == a.byte_end &&
                a.byte_end > 0 && a.byte_end < (mapping.text != nullptr ? mapping.text->size() : 0)) {
                out_offset = a.byte_end;
                out_line_before = a.line_index;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST(CaretService, AffinitySoftWrapSameOffset) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    SoftWrapProbe p;
    ASSERT_TRUE(load_soft_wrap(p, metrics)) << "soft-wrap fixture failed to map";
    const auto* lines = inline_lines(*p.box);
    ASSERT_NE(lines, nullptr);
    ASSERT_GE(lines->lines.size(), 2u) << "expected soft wrap to produce 2+ visual lines; spans="
                                       << p.mapping.spans.size();

    std::size_t wrap = 0;
    std::size_t line_before = 0;
    ASSERT_TRUE(find_soft_wrap_offset(p.mapping, wrap, line_before))
        << "no shared soft-wrap byte between adjacent LineBoxes";

    const std::size_t up_line = line_of(*p.box, p.mapping, wrap, CaretAffinity::Upstream);
    const std::size_t down_line = line_of(*p.box, p.mapping, wrap, CaretAffinity::Downstream);
    EXPECT_EQ(up_line, line_before);
    EXPECT_EQ(down_line, line_before + 1u);
    EXPECT_NE(up_line, down_line);

    const CaretRect up = get_rect(*p.box, p.mapping, wrap, CaretAffinity::Upstream);
    const CaretRect down = get_rect(*p.box, p.mapping, wrap, CaretAffinity::Downstream);
    ASSERT_TRUE(up.ok);
    ASSERT_TRUE(down.ok);
    EXPECT_LT(up.y + 0.5f, down.y) << "Upstream must paint on the previous visual line";
    // Upstream sits at the end of the previous line; Downstream at the start of the next.
    EXPECT_GT(up.x, down.x) << "Upstream x should be toward the previous line's end";

    // Hit-test on the previous line near its end → Upstream affinity at the wrap offset.
    const CaretHit hit_up = hit_test(*p.box, p.mapping, up.x - 0.5f, up.y + up.height * 0.5f);
    ASSERT_TRUE(hit_up.ok);
    EXPECT_EQ(hit_up.offset, wrap);
    EXPECT_EQ(hit_up.affinity, CaretAffinity::Upstream);

    // Hit-test on the next line near its start → Downstream affinity.
    const CaretHit hit_dn = hit_test(*p.box, p.mapping, down.x + 0.5f, down.y + down.height * 0.5f);
    ASSERT_TRUE(hit_dn.ok);
    EXPECT_EQ(hit_dn.offset, wrap);
    EXPECT_EQ(hit_dn.affinity, CaretAffinity::Downstream);

    free_soft_wrap(p);
}

/// Soft-wrapped <p><span> (zmd paper body): a click on the second visual line must not resolve
/// to an offset that paints on the first line (lines_owner geometry + multi-line mapping).
TEST(CaretService, SpanSoftWrapHitPicksSecondLine) {
    zui::layout::FreeTypeTextMetrics metrics;
    zui::html::Document* doc = new zui::html::Document();
    LayoutEngine* layout = new LayoutEngine(metrics);
    ASSERT_TRUE(load_doc(*layout, *doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_span_soft_wrap.html",
                         320.f, 240.f));
    zui::html::Node* body = zui::html::find(*doc, "body");
    zui::html::Node* blk = zui::html::find(*doc, "blk");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(blk, nullptr);
    Box* body_box = layout->principal_box(body);
    Box* blk_box = layout->principal_box(blk);
    ASSERT_NE(body_box, nullptr);
    ASSERT_NE(blk_box, nullptr);
    const std::string edit = body->text.empty() ? [&] {
        std::string out;
        for (const auto& ch : body->children) {
            if (ch && ch->tag == "#text") {
                out += ch->text;
            }
        }
        if (out.empty()) {
            out = body->text;
        }
        return out;
    }()
                                                 : body->text;
    // Prefer concatenating descendant text, including nested marks, over the element's own text.
    std::string edit2;
    {
        std::function<void(const zui::html::Node*)> walk = [&](const zui::html::Node* n) {
            if (n == nullptr) {
                return;
            }
            if (n->tag == "#text") {
                edit2 += n->text;
                return;
            }
            if (n->children.empty()) {
                edit2 += n->text;
                return;
            }
            for (const auto& c : n->children) {
                walk(c.get());
            }
        };
        walk(body);
        if (edit2.empty()) {
            edit2 = body->text;
        }
    }
    // Product path is text_offset_at on the <p>. A wrapping sole-child span keeps its own
    // line boxes (CSS 2.1 §10.6.1: the inline's used height is the font box, so the <p>
    // stays one line box); the Range↔line-box mapping still walks those inner lines.
    const Box* map_box = blk_box;
    EditMapping mapping = make_edit_mapping(*blk_box, edit2);
    auto line_count = [](const EditMapping& m) {
        std::size_t n = 0;
        for (const auto& s : m.spans) {
            n = std::max(n, s.line_index + 1);
        }
        return n;
    };
    if (mapping.spans.empty() || line_count(mapping) < 2) {
        map_box = body_box;
        mapping = make_edit_mapping(*body_box, edit2);
    }
    ASSERT_FALSE(mapping.spans.empty()) << "edit2_len=" << edit2.size();
    std::size_t max_li = 0;
    for (const auto& s : mapping.spans) {
        max_li = std::max(max_li, s.line_index);
    }
    ASSERT_GE(max_li, 1u) << "expected soft wrap; spans=" << mapping.spans.size()
                          << " edit=" << edit2;

    // Click the mid of a span that already maps to visual line 1. 0.85 of the <p>'s
    // border-box height is still the first fragment when the inline overflows.
    float y = map_box->geo.world_y + map_box->geo.height * 0.85f;
    float x = map_box->geo.world_x + map_box->geo.width * 0.85f;
    bool on_line1 = false;
    for (const auto& s : mapping.spans) {
        if (s.line_index < 1) {
            continue;
        }
        const float top = map_box->geo.world_y + s.local_y + s.ascent - s.font_ascent;
        const float height = s.font_ascent + s.font_descent;
        y = top + height * 0.5f;
        x = map_box->geo.world_x + s.local_x + 4.f;
        on_line1 = true;
        break;
    }
    ASSERT_TRUE(on_line1);
    const CaretHit hit = hit_test(*map_box, mapping, x, y);
    ASSERT_TRUE(hit.ok);
    std::size_t hit_line = static_cast<std::size_t>(-1);
    for (const auto& s : mapping.spans) {
        if (hit.offset >= s.byte_begin && hit.offset <= s.byte_end) {
            if (hit_line == static_cast<std::size_t>(-1) || s.line_index > 0) {
                hit_line = s.line_index;
            }
        }
    }
    // Shared soft-wrap offset may appear on both lines; prefer Downstream (line 1+).
    for (const auto& s : mapping.spans) {
        if (s.byte_begin == hit.offset && s.line_index > 0) {
            hit_line = s.line_index;
        }
    }
    EXPECT_GE(hit_line, 1u) << "click on lower band resolved to line 0; offset=" << hit.offset;

    const CaretRect first = get_rect(*map_box, mapping, 0, CaretAffinity::Downstream);
    const CaretRect rect = get_rect(*map_box, mapping, hit.offset, hit.affinity);
    ASSERT_TRUE(rect.ok);
    ASSERT_TRUE(first.ok);
    EXPECT_GE(rect.y, first.y + first.height * 0.5f)
        << "caret rect still on first line band; y=" << rect.y << " first=" << first.y;

    delete layout;
    delete doc;
}


TEST(CaretService, PreferredXVerticalMove) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    SoftWrapProbe p;
    ASSERT_TRUE(load_soft_wrap(p, metrics));
    const auto* lines = inline_lines(*p.box);
    ASSERT_NE(lines, nullptr);
    ASSERT_GE(lines->lines.size(), 2u);

    // Sit near the end of the first visual line (not at a wrap-only edge if possible).
    std::size_t start = 0;
    for (const auto& s : p.mapping.spans) {
        if (s.line_index == 0) {
            start = s.byte_end > 0 ? s.byte_end - 1 : s.byte_begin;
            // Prefer a byte strictly inside the line when possible.
            if (s.byte_end > s.byte_begin + 1) {
                start = s.byte_end - 1;
                while (start > s.byte_begin &&
                       (static_cast<unsigned char>((*p.mapping.text)[start]) & 0xc0) == 0x80) {
                    --start;
                }
            }
            break;
        }
    }
    const CaretRect origin = get_rect(*p.box, p.mapping, start, CaretAffinity::Downstream);
    ASSERT_TRUE(origin.ok);
    float preferred_x = origin.x;

    std::size_t at = start;
    at = line_step(*p.box, p.mapping, at, CaretAffinity::Downstream, +1, preferred_x);
    const CaretRect after = get_rect(*p.box, p.mapping, at, CaretAffinity::Downstream);
    ASSERT_TRUE(after.ok);
    EXPECT_NEAR(after.x, preferred_x, 10.f) << "first Down must land near preferred-x; off=" << at;
    EXPECT_FLOAT_EQ(preferred_x, origin.x) << "line_step must not overwrite a seeded preferred_x";
    // Second Down with the same sticky preferred-x (not reseeded from `after.x`).
    const float sticky = preferred_x;
    const std::size_t at2 = line_step(*p.box, p.mapping, at, CaretAffinity::Downstream, +1, preferred_x);
    EXPECT_FLOAT_EQ(preferred_x, sticky);
    if (at2 != at) {
        const CaretRect after2 = get_rect(*p.box, p.mapping, at2, CaretAffinity::Downstream);
        ASSERT_TRUE(after2.ok);
        // Allow clamping to a shorter line's end (x may be < preferred), but not drifting right.
        EXPECT_LE(after2.x, sticky + 10.f);
        EXPECT_GE(after2.x + 1.f, std::min(sticky, after2.x));
    }

    free_soft_wrap(p);
}

TEST(CaretService, LineEdgeHomeEnd) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    SoftWrapProbe p;
    ASSERT_TRUE(load_soft_wrap(p, metrics));
    ASSERT_GE(inline_lines(*p.box)->lines.size(), 2u);

    // Pick an offset on line 1 (not the first visual line).
    std::size_t mid = 0;
    std::size_t line_begin = 0;
    std::size_t line_end = 0;
    bool found = false;
    for (const auto& s : p.mapping.spans) {
        if (s.line_index == 1) {
            if (!found) {
                line_begin = s.byte_begin;
                line_end = s.byte_end;
                mid = s.byte_begin + (s.byte_end - s.byte_begin) / 2;
                found = true;
            } else {
                line_end = std::max(line_end, s.byte_end);
            }
        }
    }
    ASSERT_TRUE(found) << "need a second visual line";

    EXPECT_EQ(line_edge(*p.box, p.mapping, mid, false), line_begin);
    EXPECT_EQ(line_edge(*p.box, p.mapping, mid, true), line_end);
    // Soft-wrap Home must not jump to document start when the caret is on line 1+.
    EXPECT_NE(line_edge(*p.box, p.mapping, mid, false), 0u);

    free_soft_wrap(p);
}

namespace {

zui::input::InteractionHost caret_host(zui::html::Document& doc, LayoutEngine& layout,
                                       zui::layout::FreeTypeTextMetrics& metrics,
                                       zui::html::Node*& hovered, zui::html::Node*& focused,
                                       zui::html::Node*& scroll_drag) {
    zui::input::InteractionHost host;
    host.document = &doc;
    host.layout = &layout;
    host.metrics = &metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    return host;
}

void expect_painted_and_ime(LayoutEngine& layout, zui::layout::FreeTypeTextMetrics& metrics,
                            Box& box, zui::html::Node* node, const CaretRect& service,
                            CaretAffinity affinity, float vw, float vh, const char* what) {
    const std::size_t caret = static_cast<std::size_t>(std::max(node->caret, 0));
    const zui::input::CaretRect ime =
        zui::input::caret_rect(box, metrics, node->value, caret, affinity);
    EXPECT_NEAR(ime.x, service.x, 0.75f) << what;
    EXPECT_NEAR(ime.y, service.y, 0.75f) << what;
    EXPECT_NEAR(ime.height, service.height, 0.75f) << what;
    EXPECT_GT(ime.height, 0.f) << what;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_caret(node, caret, zui::paint::Color{20, 80, 200, 255}, true, affinity);
    ASSERT_NE(layout.root(), nullptr);
    painter.paint(*layout.root(), list, vw, vh);
    const zui::paint::Painter::CaretBar bar = painter.last_caret_rect();
    ASSERT_TRUE(bar.ok) << what;
    EXPECT_NEAR(bar.x, service.x, 0.75f) << what;
    EXPECT_NEAR(bar.y, service.y, 0.75f) << what;
    EXPECT_NEAR(bar.h, service.height, 0.75f) << what;

    bool filled = false;
    for (const zui::paint::DisplayItem& item : list.items()) {
        if (item.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (std::fabs(item.rect.w - 1.f) > 0.2f || item.rect.h < 1.f) {
            continue;
        }
        if (std::fabs(item.rect.x - bar.x) < 0.6f && std::fabs(item.rect.y - bar.y) < 0.6f) {
            filled = true;
            break;
        }
    }
    EXPECT_TRUE(filled) << what;
}

}  // namespace

/// Click and a vertical move each record a wrap side, and paint plus the IME rect use that side.
TEST(CaretService, WrapAffinityPaintsTheSideTheHitLanded) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_soft_wrap.html", 400.f,
                         300.f));
    zui::html::Node* ta = zui::html::find(doc, "ta");
    ASSERT_NE(ta, nullptr);
    Box* box = layout.principal_box(ta);
    ASSERT_NE(box, nullptr);
    const EditMapping mapping = make_edit_mapping(*box, ta->value);
    std::size_t wrap = 0;
    std::size_t line_before = 0;
    ASSERT_TRUE(find_soft_wrap_offset(mapping, wrap, line_before));
    const CaretRect up = get_rect(*box, mapping, wrap, CaretAffinity::Upstream);
    const CaretRect down = get_rect(*box, mapping, wrap, CaretAffinity::Downstream);
    ASSERT_TRUE(up.ok);
    ASSERT_TRUE(down.ok);
    ASSERT_LT(up.y + 0.5f, down.y);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        caret_host(doc, layout, metrics, hovered, focused, scroll_drag));

    interaction.pointer_button("mousePressed", up.x - 0.5f, up.y + up.height * 0.5f, "left", 1);
    EXPECT_EQ(focused, ta);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), wrap);
    EXPECT_EQ(interaction.edit_model().affinity, CaretAffinity::Upstream);
    expect_painted_and_ime(layout, metrics, *box, ta, up, interaction.edit_model().affinity, 400.f,
                           300.f, "upstream click");

    interaction.pointer_button("mousePressed", down.x + 0.5f, down.y + down.height * 0.5f, "left",
                               1);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), wrap);
    EXPECT_EQ(interaction.edit_model().affinity, CaretAffinity::Downstream);
    expect_painted_and_ime(layout, metrics, *box, ta, down, interaction.edit_model().affinity,
                           400.f, 300.f, "downstream click");

    // A Down from the start of the previous line lands on this wrap's next-line side.
    interaction.pointer_button("mousePressed", down.x + 0.5f, up.y + up.height * 0.5f, "left", 1);
    ASSERT_EQ(focused, ta);
    const std::size_t on_first = static_cast<std::size_t>(ta->caret);
    EXPECT_LT(on_first, wrap);
    zui::input::EditInput move_down;
    move_down.down = 1;
    interaction.apply_editing(move_down);
    const std::size_t landed = static_cast<std::size_t>(ta->caret);
    const CaretAffinity landed_aff = interaction.edit_model().affinity;
    const CaretRect moved = get_rect(*box, mapping, landed, landed_aff);
    ASSERT_TRUE(moved.ok);
    EXPECT_EQ(landed, wrap) << "down from the start of the line must land on the wrap";
    EXPECT_EQ(landed_aff, CaretAffinity::Downstream);
    EXPECT_NEAR(moved.y, down.y, 0.75f);
    EXPECT_GT(moved.y, up.y + 0.5f);
    expect_painted_and_ime(layout, metrics, *box, ta, moved, landed_aff, 400.f, 300.f,
                           "down from line start");

    // End of the wrapped line is the same byte as the next line's start. End must keep the
    // caret on the line it ended, which is the upstream side.
    interaction.pointer_button("mousePressed", up.x - 0.5f, up.y + up.height * 0.5f, "left", 1);
    zui::input::EditInput to_end;
    to_end.end = 1;
    interaction.apply_editing(to_end);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), wrap);
    EXPECT_EQ(interaction.edit_model().affinity, CaretAffinity::Upstream);
    expect_painted_and_ime(layout, metrics, *box, ta, up, interaction.edit_model().affinity, 400.f,
                           300.f, "end of wrapped line");
}

/// A line that is only a newline is a caret target for a click and for up/down.
TEST(CaretService, BlankLineHitAndVerticalMove) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_blank_line.html", 400.f,
                         300.f));
    zui::html::Node* ta = zui::html::find(doc, "ta");
    ASSERT_NE(ta, nullptr);
    Box* box = layout.principal_box(ta);
    ASSERT_NE(box, nullptr);
    const EditMapping mapping = make_edit_mapping(*box, ta->value);
    ASSERT_FALSE(mapping.spans.empty()) << "value=[" << ta->value << "]";

    const zui::layout::MappedSpan* blank = nullptr;
    for (const auto& span : mapping.spans) {
        if (span.byte_begin != span.byte_end) {
            continue;
        }
        bool before = false;
        bool after = false;
        for (const auto& other : mapping.spans) {
            if (other.byte_begin == other.byte_end) {
                continue;
            }
            if (other.line_index < span.line_index) {
                before = true;
            }
            if (other.line_index > span.line_index) {
                after = true;
            }
        }
        if (before && after) {
            blank = &span;
        }
    }
    ASSERT_NE(blank, nullptr) << "no blank line between text; value=[" << ta->value
                              << "] spans=" << mapping.spans.size();
    const std::size_t blank_at = blank->byte_begin;
    const CaretRect blank_rect = get_rect(*box, mapping, blank_at, CaretAffinity::Downstream);
    ASSERT_TRUE(blank_rect.ok);
    EXPECT_GT(blank_rect.height, 0.f);

    const CaretHit on_blank =
        hit_test(*box, mapping, blank_rect.x + 1.f, blank_rect.y + blank_rect.height * 0.5f);
    ASSERT_TRUE(on_blank.ok);
    EXPECT_EQ(on_blank.offset, blank_at);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        caret_host(doc, layout, metrics, hovered, focused, scroll_drag));
    interaction.pointer_button("mousePressed", blank_rect.x + 1.f,
                               blank_rect.y + blank_rect.height * 0.5f, "left", 1);
    EXPECT_EQ(focused, ta);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), blank_at);
    expect_painted_and_ime(layout, metrics, *box, ta, blank_rect, interaction.edit_model().affinity,
                           400.f, 300.f, "blank click");

    // Move to the text line above the blank, then Down lands on the blank line again.
    float above_y = blank_rect.y - blank_rect.height;
    for (const auto& span : mapping.spans) {
        if (span.line_index >= blank->line_index || span.byte_begin == span.byte_end) {
            continue;
        }
        const CaretRect above = get_rect(*box, mapping, span.byte_begin, CaretAffinity::Downstream);
        if (above.ok) {
            above_y = above.y + above.height * 0.5f;
            interaction.pointer_button("mousePressed", above.x + 0.5f, above_y, "left", 1);
            break;
        }
    }
    EXPECT_NE(static_cast<std::size_t>(ta->caret), blank_at);
    zui::input::EditInput down;
    down.down = 1;
    interaction.apply_editing(down);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), blank_at) << "down onto the blank line";
    const CaretRect after_down =
        get_rect(*box, mapping, blank_at, interaction.edit_model().affinity);
    ASSERT_TRUE(after_down.ok);
    EXPECT_NEAR(after_down.y, blank_rect.y, 0.75f);
    expect_painted_and_ime(layout, metrics, *box, ta, after_down, interaction.edit_model().affinity,
                           400.f, 300.f, "down onto blank");

    zui::input::EditInput down_again;
    down_again.down = 1;
    interaction.apply_editing(down_again);
    EXPECT_NE(static_cast<std::size_t>(ta->caret), blank_at);
    zui::input::EditInput up;
    up.up = 1;
    interaction.apply_editing(up);
    EXPECT_EQ(static_cast<std::size_t>(ta->caret), blank_at) << "up onto the blank line";
    const CaretRect after_up = get_rect(*box, mapping, blank_at, interaction.edit_model().affinity);
    ASSERT_TRUE(after_up.ok);
    EXPECT_NEAR(after_up.y, blank_rect.y, 0.75f);
    expect_painted_and_ime(layout, metrics, *box, ta, after_up, interaction.edit_model().affinity,
                           400.f, 300.f, "up onto blank");
}

/// Repeated spaces stay in a single-line field, and a caret past the clip is shifted back inside.
TEST(CaretService, SpacesAndWideFieldStayInTheClient) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_field.html", 480.f,
                         240.f));

    zui::html::Node* spaces = zui::html::find(doc, "spaces");
    ASSERT_NE(spaces, nullptr);
    EXPECT_EQ(spaces->value, "a   b");
    Box* spaces_box = layout.principal_box(spaces);
    ASSERT_NE(spaces_box, nullptr);
    EXPECT_EQ(spaces_box->style().white_space, zui::layout::WhiteSpace::Pre);
    EXPECT_EQ(spaces_box->style().overflow_x, zui::layout::OverflowMode::Clip);
    const EditMapping space_map = make_edit_mapping(*spaces_box, spaces->value);
    ASSERT_FALSE(space_map.spans.empty());
    std::vector<std::size_t> hits;
    const CaretRect origin = get_rect(*spaces_box, space_map, 0, CaretAffinity::Downstream);
    const CaretRect end = get_rect(*spaces_box, space_map, spaces->value.size(),
                                   CaretAffinity::Downstream);
    ASSERT_TRUE(origin.ok);
    ASSERT_TRUE(end.ok);
    const float y = origin.y + origin.height * 0.5f;
    for (int i = 0; i <= 24; ++i) {
        const float x = origin.x + (end.x - origin.x) * (static_cast<float>(i) / 24.f);
        const CaretHit hit = hit_test(*spaces_box, space_map, x, y);
        ASSERT_TRUE(hit.ok);
        if (hits.empty() || hits.back() != hit.offset) {
            hits.push_back(hit.offset);
        }
    }
    EXPECT_NE(std::find(hits.begin(), hits.end(), 2u), hits.end()) << "offset inside the spaces";
    EXPECT_NE(std::find(hits.begin(), hits.end(), 3u), hits.end()) << "second space";
    EXPECT_NE(std::find(hits.begin(), hits.end(), 4u), hits.end()) << "third space";

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        caret_host(doc, layout, metrics, hovered, focused, scroll_drag));
    interaction.pointer_button("mousePressed", origin.x + 0.5f, y, "left", 1);
    EXPECT_EQ(focused, spaces);
    for (int step = 0; step < 4; ++step) {
        const int before = spaces->caret;
        zui::input::EditInput right;
        right.right = 1;
        interaction.apply_editing(right);
        EXPECT_EQ(spaces->caret, before + 1) << "step " << step;
    }
    EXPECT_EQ(spaces->caret, 4);

    zui::html::Node* wide = zui::html::find(doc, "wide");
    ASSERT_NE(wide, nullptr);
    ASSERT_GT(wide->value.size(), 8u);
    Box* wide_box = layout.principal_box(wide);
    ASSERT_NE(wide_box, nullptr);
    EXPECT_EQ(wide_box->style().overflow_x, zui::layout::OverflowMode::Clip);
    // overflow:clip shows the padding box. The caret bar has to land in that box.
    const float client_left = wide_box->geo.world_x + wide_box->geo.border.left;
    const float client_right =
        client_left + std::max(0.f, wide_box->geo.width - wide_box->geo.border.horizontal());
    const EditMapping wide_map = make_edit_mapping(*wide_box, wide->value);
    ASSERT_FALSE(wide_map.spans.empty());
    const CaretRect past =
        get_rect(*wide_box, wide_map, wide->value.size(), CaretAffinity::Downstream);
    ASSERT_TRUE(past.ok);
    EXPECT_GT(past.x, client_right - 0.5f) << "end caret starts outside the clip";

    const CaretRect wide_origin = get_rect(*wide_box, wide_map, 0, CaretAffinity::Downstream);
    ASSERT_TRUE(wide_origin.ok);
    interaction.pointer_button("mousePressed", wide_origin.x + 0.5f,
                               wide_origin.y + wide_origin.height * 0.5f, "left", 1);
    EXPECT_EQ(focused, wide);
    zui::input::EditInput to_end;
    to_end.end = 1;
    interaction.apply_editing(to_end);
    EXPECT_EQ(static_cast<std::size_t>(wide->caret), wide->value.size());
    interaction.reveal_caret();
    EXPECT_GT(wide->scroll_x, 1.f);

    const CaretRect revealed = get_rect(*wide_box, make_edit_mapping(*wide_box, wide->value),
                                        wide->value.size(), interaction.edit_model().affinity);
    ASSERT_TRUE(revealed.ok);
    EXPECT_GE(revealed.x, client_left - 0.75f);
    EXPECT_LE(revealed.x + 1.f, client_right + 0.75f);
    expect_painted_and_ime(layout, metrics, *wide_box, wide, revealed,
                           interaction.edit_model().affinity, 480.f, 240.f, "revealed wide caret");

    bool shifted = false;
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list, 480.f, 240.f);
    for (const zui::paint::DisplayItem& item : list.items()) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.text.find("abc") == std::string::npos) {
            continue;
        }
        const float content_left = client_left + wide_box->geo.padding.left;
        EXPECT_NEAR(item.rect.x, content_left - wide->scroll_x, 1.5f);
        shifted = true;
        break;
    }
    EXPECT_TRUE(shifted) << "wide field glyphs did not follow scroll_x";

    const CaretHit at_end =
        hit_test(*wide_box, make_edit_mapping(*wide_box, wide->value), revealed.x, 
                 revealed.y + revealed.height * 0.5f);
    ASSERT_TRUE(at_end.ok);
    EXPECT_EQ(at_end.offset, wide->value.size());
}

/// RTL caret walks back along the line. Vertical text follows the inline axis when layout
/// produces one; otherwise the limit is printed and the pen is not faked.
TEST(CaretService, RtlAndVerticalFollowTheInlineAxis) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_direction.html", 400.f,
                         320.f));

    zui::html::Node* rtl = zui::html::find(doc, "rtl");
    ASSERT_NE(rtl, nullptr);
    ASSERT_FALSE(rtl->value.empty());
    Box* rtl_box = layout.principal_box(rtl);
    ASSERT_NE(rtl_box, nullptr);
    const EditMapping rtl_map = make_edit_mapping(*rtl_box, rtl->value);
    ASSERT_FALSE(rtl_map.spans.empty()) << "value bytes=" << rtl->value.size();
    bool reversed = false;
    for (const auto& span : rtl_map.spans) {
        reversed = reversed || span.reverse;
    }
    EXPECT_TRUE(reversed);
    const CaretRect at_start = get_rect(*rtl_box, rtl_map, 0, CaretAffinity::Downstream);
    const CaretRect at_end =
        get_rect(*rtl_box, rtl_map, rtl->value.size(), CaretAffinity::Downstream);
    ASSERT_TRUE(at_start.ok);
    ASSERT_TRUE(at_end.ok);
    EXPECT_GT(at_start.x, at_end.x + 2.f) << "logical start sits at the visual end";
    EXPECT_NEAR(at_start.y, at_end.y, 1.5f);
    const float line_left = std::min(at_start.x, at_end.x);
    const float line_right = std::max(at_start.x, at_end.x);
    EXPECT_GE(line_left, rtl_box->geo.world_x - 1.f);
    EXPECT_LE(line_right, rtl_box->geo.world_x + rtl_box->geo.width + 1.f);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        caret_host(doc, layout, metrics, hovered, focused, scroll_drag));
    interaction.pointer_button("mousePressed", at_start.x - 0.5f, at_start.y + at_start.height * 0.5f,
                               "left", 1);
    EXPECT_EQ(focused, rtl);
    EXPECT_EQ(rtl->caret, 0);
    expect_painted_and_ime(layout, metrics, *rtl_box, rtl, at_start, CaretAffinity::Downstream,
                           400.f, 320.f, "rtl start");

    zui::html::Node* vert = zui::html::find(doc, "vert");
    ASSERT_NE(vert, nullptr);
    Box* vert_box = layout.principal_box(vert);
    ASSERT_NE(vert_box, nullptr);
    // The fixture keeps "abc" on the div itself. There is no #text child.
    const std::string vert_text = vert->text;
    ASSERT_EQ(vert_text, "abc");
    const EditMapping vert_map = make_edit_mapping(*vert_box, vert_text);
    ASSERT_FALSE(vert_map.spans.empty());
    const CaretRect v0 = get_rect(*vert_box, vert_map, 0, CaretAffinity::Downstream);
    const CaretRect v1 = get_rect(*vert_box, vert_map, vert_text.size(), CaretAffinity::Downstream);
    ASSERT_TRUE(v0.ok);
    ASSERT_TRUE(v1.ok);
    EXPECT_GT(std::fabs(v0.y - v1.y), std::fabs(v0.x - v1.x));
    EXPECT_GT(std::fabs(v0.y - v1.y), 4.f);
    EXPECT_NEAR(v0.height, 1.f, 0.2f);
    EXPECT_GT(v0.width, 2.f);

    vert->value = vert_text;
    vert->caret = static_cast<int>(vert_text.size());
    zui::paint::DisplayList vlist;
    zui::paint::Painter vpainter;
    vpainter.set_caret(vert, vert_text.size(), zui::paint::Color{20, 80, 200, 255}, true);
    vpainter.paint(*layout.root(), vlist, 400.f, 320.f);
    const zui::paint::Painter::CaretBar vbar = vpainter.last_caret_rect();
    ASSERT_TRUE(vbar.ok);
    EXPECT_NEAR(vbar.h, 1.f, 0.25f);
    EXPECT_GT(vbar.w, 2.f);
    EXPECT_NEAR(vbar.y, v1.y, 0.75f);
    bool vfill = false;
    for (const zui::paint::DisplayItem& item : vlist.items()) {
        if (item.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (item.rect.h > 2.f || item.rect.w < 2.f) {
            continue;
        }
        if (std::fabs(item.rect.x - vbar.x) < 0.6f && std::fabs(item.rect.y - vbar.y) < 0.6f) {
            vfill = true;
            break;
        }
    }
    EXPECT_TRUE(vfill);
}

/// One fragment that cannot be aligned (the image) must not drop caret positions for the
/// edit-string text that follows it.
TEST(CaretService, ContenteditableKeepsCaretAfterUnalignedChild) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_ce.html", 480.f, 200.f));
    zui::html::Node* ce = zui::html::find(doc, "ce");
    ASSERT_NE(ce, nullptr);
    std::string edit;
    std::function<void(const zui::html::Node*)> walk = [&](const zui::html::Node* n) {
        if (n == nullptr) {
            return;
        }
        if (n != ce && n->tag == "#text") {
            edit += n->text;
            return;
        }
        if (n != ce && n->tag == "br") {
            edit.push_back('\n');
            return;
        }
        for (const auto& child : n->children) {
            walk(child.get());
        }
    };
    walk(ce);
    const std::size_t hello = edit.find("hello");
    const std::size_t world = edit.find("world");
    ASSERT_NE(hello, std::string::npos) << edit;
    ASSERT_NE(world, std::string::npos) << edit;
    ASSERT_LT(hello, world);

    Box* box = layout.principal_box(ce);
    ASSERT_NE(box, nullptr);
    const EditMapping mapping = make_edit_mapping(*box, edit);
    ASSERT_FALSE(mapping.spans.empty());

    auto covered = [&](std::size_t byte) {
        for (const auto& span : mapping.spans) {
            if (byte >= span.byte_begin && byte < span.byte_end) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(covered(hello)) << "hello dropped from [" << edit << "]";
    EXPECT_TRUE(covered(world)) << "world dropped after an unaligned fragment";
    const CaretRect world_rect = get_rect(*box, mapping, world, CaretAffinity::Downstream);
    ASSERT_TRUE(world_rect.ok);
    EXPECT_GT(world_rect.height, 0.f);

    ce->value = edit;
    ce->text = edit;
    ce->caret = static_cast<int>(world);
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_caret(ce, world, zui::paint::Color{20, 80, 200, 255}, true);
    painter.paint(*layout.root(), list, 480.f, 200.f);
    const zui::paint::Painter::CaretBar bar = painter.last_caret_rect();
    ASSERT_TRUE(bar.ok);
    EXPECT_NEAR(bar.x, world_rect.x, 0.75f);
    EXPECT_NEAR(bar.y, world_rect.y, 0.75f);
    const zui::input::CaretRect ime = zui::input::caret_rect(*box, metrics, edit, world);
    EXPECT_NEAR(ime.x, world_rect.x, 0.75f);
    EXPECT_NEAR(ime.y, world_rect.y, 0.75f);
}

TEST(CaretService, CompositionUpdateCommitCancel) {
    zui::input::EditModel edit;
    edit.text = "hello";
    edit.anchor = 5;
    edit.focus = 5;

    // Start + update: preview lands at focus.
    edit.begin_composition();
    ASSERT_TRUE(edit.composing.has_value());
    EXPECT_EQ(edit.composing->start, 5u);
    EXPECT_EQ(edit.composing->end, 5u);
    EXPECT_TRUE(edit.update_composition("ni"));
    ASSERT_TRUE(edit.composing.has_value());
    EXPECT_EQ(edit.text, "helloni");
    EXPECT_EQ(edit.composing->start, 5u);
    EXPECT_EQ(edit.composing->end, 7u);
    EXPECT_EQ(edit.composing->preview_text, "ni");
    EXPECT_EQ(edit.focus, 7u);
    EXPECT_EQ(edit.anchor, 7u);

    // Update replaces the open range.
    EXPECT_TRUE(edit.update_composition("nin"));
    EXPECT_EQ(edit.text, "hellonin");
    EXPECT_EQ(edit.composing->start, 5u);
    EXPECT_EQ(edit.composing->end, 8u);
    EXPECT_EQ(edit.focus, 8u);

    // Commit with the same preview keeps text and clears composing.
    EXPECT_TRUE(edit.commit_composition("nin"));
    EXPECT_FALSE(edit.composing.has_value());
    EXPECT_EQ(edit.text, "hellonin");
    EXPECT_EQ(edit.focus, 8u);

    // Fresh composition then cancel restores the pre-composition caret/text.
    edit.begin_composition();
    EXPECT_TRUE(edit.update_composition("hao"));
    EXPECT_EQ(edit.text, "helloninhao");
    EXPECT_TRUE(edit.cancel_composition());
    EXPECT_FALSE(edit.composing.has_value());
    EXPECT_EQ(edit.text, "hellonin");
    EXPECT_EQ(edit.focus, 8u);
    EXPECT_EQ(edit.anchor, 8u);

    // Selection collapse on begin: composing replaces the selected range.
    edit.text = "hello";
    edit.anchor = 0;
    edit.focus = 5;  // select "hello"
    edit.begin_composition();
    EXPECT_EQ(edit.text, "");
    ASSERT_TRUE(edit.composing.has_value());
    EXPECT_EQ(edit.composing->start, 0u);
    EXPECT_TRUE(edit.update_composition("Hi"));
    EXPECT_EQ(edit.text, "Hi");
    EXPECT_EQ(edit.composing->start, 0u);
    EXPECT_EQ(edit.composing->end, 2u);
    EXPECT_TRUE(edit.commit_composition("Hi"));
    EXPECT_EQ(edit.text, "Hi");
    EXPECT_FALSE(edit.composing.has_value());
}


TEST(CaretService, CeFormatRunInsertInherit) {
    zui::input::EditModel edit;
    edit.text = "ab";
    edit.load_format_bytes(std::string(2, static_cast<char>(1)));  // bold on both
    ASSERT_EQ(edit.format_runs.size(), 1u);
    EXPECT_EQ(edit.format_runs[0].style_bits, 1u);
    EXPECT_EQ(edit.format_runs[0].start, 0u);
    EXPECT_EQ(edit.format_runs[0].end, 2u);

    // Insert at focus=end inherits previous character mark (bold).
    edit.insert_formatted(2, "c");
    EXPECT_EQ(edit.text, "abc");
    ASSERT_EQ(edit.format_runs.size(), 1u);
    EXPECT_EQ(edit.format_runs[0].start, 0u);
    EXPECT_EQ(edit.format_runs[0].end, 3u);
    EXPECT_EQ(edit.format_runs[0].style_bits, 1u);
    EXPECT_EQ(edit.expand_format(), std::string(3, static_cast<char>(1)));

    // Insert at start (no previous char) stays plain for the new bytes; bold run shifts.
    edit.insert_formatted(0, "x");
    EXPECT_EQ(edit.text, "xabc");
    ASSERT_EQ(edit.format_runs.size(), 2u);
    EXPECT_EQ(edit.format_runs[0].style_bits, 0u);
    EXPECT_EQ(edit.format_runs[0].start, 0u);
    EXPECT_EQ(edit.format_runs[0].end, 1u);
    EXPECT_EQ(edit.format_runs[1].style_bits, 1u);
    EXPECT_EQ(edit.format_runs[1].start, 1u);
    EXPECT_EQ(edit.format_runs[1].end, 4u);
}

TEST(CaretService, CeFormatRunDeleteMerge) {
    zui::input::EditModel edit;
    edit.text = "aabbcc";
    // bold "aa", plain "bb", bold "cc"
    std::string styles;
    styles.append(2, static_cast<char>(1));
    styles.append(2, static_cast<char>(0));
    styles.append(2, static_cast<char>(1));
    edit.load_format_bytes(styles);
    ASSERT_EQ(edit.format_runs.size(), 3u);
    EXPECT_EQ(edit.format_runs[0].style_bits, 1u);
    EXPECT_EQ(edit.format_runs[1].style_bits, 0u);
    EXPECT_EQ(edit.format_runs[2].style_bits, 1u);

    // Delete plain middle "bb" -> adjacent bold runs merge.
    ASSERT_TRUE(edit.erase_formatted(2, 4));
    EXPECT_EQ(edit.text, "aacc");
    ASSERT_EQ(edit.format_runs.size(), 1u);
    EXPECT_EQ(edit.format_runs[0].start, 0u);
    EXPECT_EQ(edit.format_runs[0].end, 4u);
    EXPECT_EQ(edit.format_runs[0].style_bits, 1u);

    // Toggle italic on [1,3) then delete one italic byte; runs stay consistent.
    edit.toggle_style(1, 3, 2);  // italic bit
    ASSERT_GE(edit.format_runs.size(), 2u);
    ASSERT_TRUE(edit.erase_formatted(1, 2));
    EXPECT_EQ(edit.text.size(), 3u);
    // Re-expand must cover text; adjacent equal styles merged by compress.
    const std::string out = edit.expand_format();
    EXPECT_EQ(out.size(), edit.text.size());
    for (std::size_t i = 1; i < edit.format_runs.size(); ++i) {
        EXPECT_LT(edit.format_runs[i - 1].end, edit.format_runs[i].end);
        EXPECT_EQ(edit.format_runs[i - 1].end, edit.format_runs[i].start);
        EXPECT_FALSE(edit.format_runs[i - 1].style_bits == edit.format_runs[i].style_bits &&
                     edit.format_runs[i - 1].end == edit.format_runs[i].start)
            << "adjacent equal style_bits must be merged";
    }
}

/// zmd writing-view quote: padded blockquote, wrapping `.zmd-body` span, CJK + fullwidth
/// punctuation. A right-to-left drag must map increasing x onto increasing innerText offsets
/// (the screenshot of 欢迎.md was mirrored: press on the right highlighted 好的…).
TEST(CaretService, CjkQuoteHitGrowsWithX) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_cjk_quote.html", 720.f,
                         200.f));

    zui::html::Node* p = zui::html::find(doc, "mdb1");
    ASSERT_NE(p, nullptr);
    Box* box = layout.principal_box(p);
    ASSERT_NE(box, nullptr);
    const std::string edit = zui::layout::caret_edit_text(p);
    ASSERT_FALSE(edit.empty());
    const EditMapping mapping = make_edit_mapping(*box, edit);
    ASSERT_FALSE(mapping.spans.empty()) << "edit bytes=" << edit.size();

    bool reversed = false;
    for (const auto& span : mapping.spans) {
        reversed = reversed || span.reverse;
    }
    EXPECT_FALSE(reversed) << "horizontal-tb CJK is an LTR run";

    const CaretRect at0 = get_rect(*box, mapping, 0, CaretAffinity::Downstream);
    const CaretRect at_end = get_rect(*box, mapping, edit.size(), CaretAffinity::Downstream);
    ASSERT_TRUE(at0.ok);
    ASSERT_TRUE(at_end.ok);
    EXPECT_GT(at_end.x, at0.x + 8.f) << "logical start sits at the visual start; reverse="
                                     << reversed << " start_x=" << at0.x << " end_x=" << at_end.x;

    const float y = at0.y + at0.height * 0.5f;
    std::size_t prev = 0;
    const int samples = 8;
    for (int s = 0; s <= samples; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(samples);
        const float x = at0.x + (at_end.x - at0.x) * t;
        const CaretHit hit = hit_test(*box, mapping, x, y);
        ASSERT_TRUE(hit.ok) << "sample " << s;
        EXPECT_GE(hit.offset, prev) << "hit_test offsets run backwards at sample " << s
                                    << " x=" << x;
        prev = hit.offset;
    }

    const CaretHit left = hit_test(*box, mapping, at0.x + 2.f, y);
    const CaretHit right = hit_test(*box, mapping, at_end.x - 2.f, y);
    ASSERT_TRUE(left.ok);
    ASSERT_TRUE(right.ok);
    EXPECT_LT(left.offset, edit.size() / 2u);
    EXPECT_GT(right.offset, edit.size() / 2u);
    EXPECT_LT(left.offset, right.offset);

    // A click in the last eighth of the block box (the writing view aims at `mdbNl`) must still
    // land in the last quarter of the quote, even when the span is as wide as the column.
    const float box_right = box->geo.world_x + box->geo.width * 0.88f;
    const CaretHit far = hit_test(*box, mapping, box_right, y);
    ASSERT_TRUE(far.ok);
    EXPECT_GT(far.offset, edit.size() * 3 / 4)
        << "88% of the block mapped to byte " << far.offset << " / " << edit.size()
        << " box_w=" << box->geo.width << " glyph_end_x=" << at_end.x;
}

// A padded <code> chip is its own shaped run between the surrounding text. A hit on the
// chip must walk those glyphs, not stick on the byte where the text before the chip ends.
TEST(CaretService, PaddedCodeHitWalksTheChip) {
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::html::Document doc;
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load_doc(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/inline_code_hit.html", 800.f,
                         200.f));

    zui::html::Node* line = zui::html::find(doc, "line");
    zui::html::Node* chip = zui::html::find(doc, "chip");
    ASSERT_NE(line, nullptr);
    ASSERT_NE(chip, nullptr);
    Box* line_box = layout.principal_box(line);
    Box* chip_box = layout.principal_box(chip);
    ASSERT_NE(line_box, nullptr);
    ASSERT_NE(chip_box, nullptr);
    const std::string edit = zui::layout::caret_edit_text(line);
    std::string boxes;
    std::function<void(const Box*, int)> walk_boxes = [&](const Box* b, int depth) {
        if (b == nullptr || depth > 6) {
            return;
        }
        const zui::layout::LayoutResult* own =
            b->cache != nullptr ? b->cache->pinned_final() : nullptr;
        boxes += std::string(static_cast<std::size_t>(depth), ' ') + "box";
        if (b->node != nullptr) {
            boxes += std::string(" <") + b->node->tag + ">";
        }
        boxes += b->inline_text_owned_by_parent ? " owned" : "";
        boxes += " runs=";
        if (own == nullptr || own->lines == nullptr) {
            boxes += "none";
        } else {
            for (const auto& ln : own->lines->lines) {
                for (const auto& run : ln.runs) {
                    boxes += "[" + run.text + " o=" + std::to_string(run.origin) +
                             (run.has_origin ? "!" : "") + "]";
                }
            }
        }
        boxes += "\n";
        for (const Box* child : b->children) {
            walk_boxes(child, depth + 1);
        }
    };
    walk_boxes(line_box, 0);
    const std::size_t chip_at = edit.find("assets/");
    ASSERT_NE(chip_at, std::string::npos) << edit;
    const EditMapping mapping = make_edit_mapping(*line_box, edit);
    std::string dump;
    for (const auto& span : mapping.spans) {
        float adv = 0.f;
        for (float a : span.glyph_advances) {
            adv += a;
        }
        dump += "bytes " + std::to_string(span.byte_begin) + "-" + std::to_string(span.byte_end) +
                " x=" + std::to_string(span.local_x) + " adv=" + std::to_string(adv) +
                " line=" + std::to_string(span.line_index) + "\n";
    }
    dump += boxes;
    ASSERT_FALSE(mapping.spans.empty()) << dump;

    const float y = chip_box->geo.world_y + chip_box->geo.height * 0.5f;
    const float x_left = chip_box->geo.world_x + 6.f;
    const float x_right = chip_box->geo.world_x + chip_box->geo.width - 2.f;
    const CaretHit left = hit_test(*line_box, mapping, x_left, y);
    const CaretHit right = hit_test(*line_box, mapping, x_right, y);
    ASSERT_TRUE(left.ok) << dump;
    ASSERT_TRUE(right.ok) << dump;
    EXPECT_GE(left.offset, chip_at) << dump;
    EXPECT_LT(left.offset, chip_at + std::string("assets/").size()) << dump;
    EXPECT_GT(right.offset, left.offset) << "chip x " << x_left << ".." << x_right << "\n"
                                        << dump;
    EXPECT_LE(right.offset, chip_at + std::string("assets/").size()) << dump;
}
