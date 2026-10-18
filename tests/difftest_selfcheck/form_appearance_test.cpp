// Native form appearance for radio, select, range, progress, and file.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// P1: radio native chrome + closed <select> chevron; appearance:none suppresses both.
TEST(DifftestSelfCheck, FormAppearanceRadioSelect) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "form-appearance.html")) << error;

    auto prop = [&](const char* id, const char* key) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr || n->computed == nullptr) {
            return std::string("<missing>");
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string("<absent>") : std::string(it->second);
    };
    EXPECT_EQ(prop("radio-none", "appearance"), "none");
    EXPECT_EQ(prop("sel-none", "appearance"), "none");

    zui::html::Node* radio_on = zui::html::find(scene.doc, "radio-on");
    zui::html::Node* radio_off = zui::html::find(scene.doc, "radio-off");
    ASSERT_NE(radio_on, nullptr);
    ASSERT_NE(radio_off, nullptr);
    EXPECT_TRUE(radio_on->checked);
    EXPECT_FALSE(radio_off->checked);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto count_near = [&](zui::layout::Box* box, bool require_round) {
        int n = 0;
        if (box == nullptr) {
            return 0;
        }
        const float x0 = box->geo.world_x - 1.f;
        const float y0 = box->geo.world_y - 1.f;
        const float x1 = box->geo.world_x + box->geo.width + 1.f;
        const float y1 = box->geo.world_y + box->geo.height + 1.f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect &&
                it.op != zui::paint::DisplayOp::StrokeRect) {
                continue;
            }
            const float cx = it.rect.x + it.rect.w * 0.5f;
            const float cy = it.rect.y + it.rect.h * 0.5f;
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            if (require_round && it.radii.max_component() < 2.f) {
                continue;
            }
            ++n;
        }
        return n;
    };

    zui::layout::Box* on = scene.box("radio-on");
    zui::layout::Box* off = scene.box("radio-off");
    zui::layout::Box* none = scene.box("radio-none");
    zui::layout::Box* sel = scene.box("sel");
    zui::layout::Box* sel_none = scene.box("sel-none");
    ASSERT_NE(on, nullptr);
    ASSERT_NE(off, nullptr);
    ASSERT_NE(none, nullptr);
    ASSERT_NE(sel, nullptr);
    ASSERT_NE(sel_none, nullptr);

    EXPECT_GE(count_near(on, true), 2) << "checked radio paints round face + inner dot";
    EXPECT_GE(count_near(off, true), 1) << "unchecked radio paints round face";
    EXPECT_EQ(count_near(none, true), 0) << "appearance:none radio has no native round chrome";

    // Chevron segments are small dark FillRects in the end padding gutter.
    int chevron = 0;
    const float gutter_x0 = sel->geo.world_x + sel->geo.width - sel->geo.padding.right - 2.f;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (it.rect.w > 4.f || it.rect.h > 4.f) {
            continue;
        }
        if (it.color.r > 80 || it.color.g > 80 || it.color.b > 80) {
            continue;
        }
        const float cx = it.rect.x + it.rect.w * 0.5f;
        const float cy = it.rect.y + it.rect.h * 0.5f;
        if (cx >= gutter_x0 && cy >= sel->geo.world_y && cy <= sel->geo.world_y + sel->geo.height) {
            ++chevron;
        }
    }
    EXPECT_GE(chevron, 4) << "closed select paints a chevron in the end gutter";

    int chevron_none = 0;
    const float none_gutter =
        sel_none->geo.world_x + sel_none->geo.width - sel_none->geo.padding.right - 2.f;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (it.rect.w > 4.f || it.rect.h > 4.f) {
            continue;
        }
        if (it.color.r > 80 || it.color.g > 80 || it.color.b > 80) {
            continue;
        }
        const float cx = it.rect.x + it.rect.w * 0.5f;
        const float cy = it.rect.y + it.rect.h * 0.5f;
        if (cx >= none_gutter && cy >= sel_none->geo.world_y &&
            cy <= sel_none->geo.world_y + sel_none->geo.height) {
            ++chevron_none;
        }
    }
    EXPECT_EQ(chevron_none, 0) << "appearance:none select has no chevron";
}

// P1: range track/thumb and progress fill; appearance:none suppresses native chrome.
TEST(DifftestSelfCheck, FormAppearanceRangeProgress) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "form-range-progress.html")) << error;

    EXPECT_TRUE(zui::html::is_range(zui::html::find(scene.doc, "range-native")));
    EXPECT_TRUE(zui::html::is_progress(zui::html::find(scene.doc, "prog-native")));

    auto prop = [&](const char* id, const char* key) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr || n->computed == nullptr) {
            return std::string("<missing>");
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string("<absent>") : std::string(it->second);
    };
    EXPECT_EQ(prop("range-none", "appearance"), "none");
    EXPECT_EQ(prop("prog-none", "appearance"), "none");

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto count_fill = [&](zui::layout::Box* box) {
        int n = 0;
        if (box == nullptr) {
            return 0;
        }
        const float x0 = box->geo.world_x - 1.f;
        const float y0 = box->geo.world_y - 1.f;
        const float x1 = box->geo.world_x + box->geo.width + 1.f;
        const float y1 = box->geo.world_y + box->geo.height + 1.f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect &&
                it.op != zui::paint::DisplayOp::StrokeRect) {
                continue;
            }
            const float cx = it.rect.x + it.rect.w * 0.5f;
            const float cy = it.rect.y + it.rect.h * 0.5f;
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            ++n;
        }
        return n;
    };

    auto accent_fill_width = [&](zui::layout::Box* box) {
        float w = 0.f;
        if (box == nullptr) {
            return w;
        }
        const float x0 = box->geo.world_x - 1.f;
        const float y0 = box->geo.world_y - 1.f;
        const float x1 = box->geo.world_x + box->geo.width + 1.f;
        const float y1 = box->geo.world_y + box->geo.height + 1.f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect) {
                continue;
            }
            // Accent blue used by native range thumb / progress fill.
            if (it.color.r > 60 || it.color.g < 90 || it.color.b < 200) {
                continue;
            }
            const float cx = it.rect.x + it.rect.w * 0.5f;
            const float cy = it.rect.y + it.rect.h * 0.5f;
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            w = std::max(w, it.rect.w);
        }
        return w;
    };

    zui::layout::Box* range_n = scene.box("range-native");
    zui::layout::Box* range_none = scene.box("range-none");
    zui::layout::Box* prog_n = scene.box("prog-native");
    zui::layout::Box* prog_none = scene.box("prog-none");
    zui::layout::Box* prog_empty = scene.box("prog-empty");
    ASSERT_NE(range_n, nullptr);
    ASSERT_NE(range_none, nullptr);
    ASSERT_NE(prog_n, nullptr);
    ASSERT_NE(prog_none, nullptr);
    ASSERT_NE(prog_empty, nullptr);

    EXPECT_GE(count_fill(range_n), 2) << "native range paints track + thumb";
    EXPECT_LE(count_fill(range_none), 1) << "appearance:none range has no track/thumb chrome";

    const float fill_w = accent_fill_width(prog_n);
    EXPECT_GT(fill_w, 60.f) << "75% progress fill should be wider than half of 120px";
    EXPECT_LT(fill_w, 120.f);
    EXPECT_LE(accent_fill_width(prog_none), 0.5f) << "appearance:none progress has no fill chrome";
    EXPECT_LE(accent_fill_width(prog_empty), 0.5f) << "indeterminate progress has no fill";
}

// P1: file input button + filename; appearance:none suppresses native chrome.
TEST(DifftestSelfCheck, FormAppearanceFile) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "form-file.html")) << error;

    EXPECT_TRUE(zui::html::is_file(zui::html::find(scene.doc, "file-empty")));
    zui::html::Node* named = zui::html::find(scene.doc, "file-named");
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->value, "report.pdf");

    auto prop = [&](const char* id, const char* key) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr || n->computed == nullptr) {
            return std::string("<missing>");
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string("<absent>") : std::string(it->second);
    };
    EXPECT_EQ(prop("file-none", "appearance"), "none");

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto count_near = [&](zui::layout::Box* box) {
        int n = 0;
        if (box == nullptr) {
            return 0;
        }
        const float x0 = box->geo.world_x - 1.f;
        const float y0 = box->geo.world_y - 1.f;
        const float x1 = box->geo.world_x + box->geo.width + 1.f;
        const float y1 = box->geo.world_y + box->geo.height + 1.f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect &&
                it.op != zui::paint::DisplayOp::StrokeRect &&
                it.op != zui::paint::DisplayOp::DrawText) {
                continue;
            }
            float cx = 0.f;
            float cy = 0.f;
            if (it.op == zui::paint::DisplayOp::DrawText) {
                cx = it.rect.x;
                cy = it.rect.y;
            } else {
                cx = it.rect.x + it.rect.w * 0.5f;
                cy = it.rect.y + it.rect.h * 0.5f;
            }
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            ++n;
        }
        return n;
    };

    auto has_text = [&](zui::layout::Box* box, std::string_view needle) {
        if (box == nullptr) {
            return false;
        }
        const float x0 = box->geo.world_x - 2.f;
        const float y0 = box->geo.world_y - 2.f;
        const float x1 = box->geo.world_x + box->geo.width + 2.f;
        const float y1 = box->geo.world_y + box->geo.height + 2.f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::DrawText) {
                continue;
            }
            if (it.rect.x < x0 || it.rect.y < y0 || it.rect.x > x1 || it.rect.y > y1) {
                continue;
            }
            if (it.text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    zui::layout::Box* empty = scene.box("file-empty");
    zui::layout::Box* named_b = scene.box("file-named");
    zui::layout::Box* none = scene.box("file-none");
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(named_b, nullptr);
    ASSERT_NE(none, nullptr);

    EXPECT_GE(count_near(empty), 3) << "native empty file paints button + labels";
    EXPECT_TRUE(has_text(empty, "Choose File"));
    EXPECT_TRUE(has_text(empty, "No file chosen"));
    EXPECT_TRUE(has_text(named_b, "report.pdf"));
    EXPECT_FALSE(has_text(none, "Choose File")) << "appearance:none suppresses button label";
    EXPECT_FALSE(has_text(none, "No file chosen"));
    EXPECT_FALSE(has_text(none, "hidden.txt"));
    // Author background may still paint; native button face + dual labels must be gone.
    EXPECT_LT(count_near(none), count_near(empty))
        << "appearance:none file paints fewer chrome items than native";
}
