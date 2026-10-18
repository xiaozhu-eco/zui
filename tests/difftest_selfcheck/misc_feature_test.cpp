// Selection, marker placement, remote stylesheets, and grid baselines.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

#include "text/font_chain.h"

#include <filesystem>

using namespace difftest_selfcheck;

// ::selection S1: cascade contribution, highlight inheritance, paired defaults / currentColor.
TEST(DifftestSelfCheck, SelectionCascade) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "selection-cascade.html")) << error;

    auto sel = [&](const char* id) -> const zui::html::SelectionStyle* {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        return n != nullptr ? n->selection.get() : nullptr;
    };
    auto expect_rgba = [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a,
                          std::uint8_t er, std::uint8_t eg, std::uint8_t eb, std::uint8_t ea,
                          const char* what) {
        EXPECT_EQ(r, er) << what << " r";
        EXPECT_EQ(g, eg) << what << " g";
        EXPECT_EQ(b, eb) << what << " b";
        EXPECT_EQ(a, ea) << what << " a";
    };

    const zui::html::SelectionStyle* both = sel("both");
    ASSERT_NE(both, nullptr);
    EXPECT_TRUE(both->flags & zui::html::SelectionStyle::kHasFg);
    EXPECT_TRUE(both->flags & zui::html::SelectionStyle::kHasBg);
    EXPECT_TRUE(both->flags & zui::html::SelectionStyle::kAuthorFg);
    EXPECT_TRUE(both->flags & zui::html::SelectionStyle::kAuthorBg);
    expect_rgba(both->fg_r, both->fg_g, both->fg_b, both->fg_a, 0, 0, 0, 255, "both fg");
    expect_rgba(both->bg_r, both->bg_g, both->bg_b, both->bg_a, 255, 255, 0, 255, "both bg");

    // #outer author bg + colour; #inner cascades green and inherits red bg (no chain break).
    const zui::html::SelectionStyle* outer = sel("outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_TRUE(outer->flags & zui::html::SelectionStyle::kAuthorBg);
    EXPECT_TRUE(outer->flags & zui::html::SelectionStyle::kAuthorFg);
    expect_rgba(outer->bg_r, outer->bg_g, outer->bg_b, outer->bg_a, 255, 0, 0, 255, "outer bg");
    expect_rgba(outer->fg_r, outer->fg_g, outer->fg_b, outer->fg_a, 0, 128, 0, 255, "outer fg");

    const zui::html::SelectionStyle* inner = sel("inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_TRUE(inner->flags & zui::html::SelectionStyle::kAuthorFg);
    EXPECT_TRUE(inner->flags & zui::html::SelectionStyle::kAuthorBg)
        << "bg must inherit from parent highlight";
    expect_rgba(inner->fg_r, inner->fg_g, inner->fg_b, inner->fg_a, 0, 128, 0, 255, "inner fg");
    expect_rgba(inner->bg_r, inner->bg_g, inner->bg_b, inner->bg_a, 255, 0, 0, 255, "inner bg");

    // Bg-only on #blue-p: fg is each element's own currentColor (not a shared parent fill).
    const zui::html::SelectionStyle* blue = sel("blue-p");
    ASSERT_NE(blue, nullptr);
    EXPECT_TRUE(blue->flags & zui::html::SelectionStyle::kAuthorBg);
    EXPECT_FALSE(blue->flags & zui::html::SelectionStyle::kAuthorFg);
    expect_rgba(blue->fg_r, blue->fg_g, blue->fg_b, blue->fg_a, 0, 0, 255, 255, "blue-p fg");
    expect_rgba(blue->bg_r, blue->bg_g, blue->bg_b, blue->bg_a, 0, 128, 0,
                static_cast<std::uint8_t>(0.4f * 255.f + 0.5f), "blue-p bg");

    const zui::html::SelectionStyle* red_em = sel("red-em");
    ASSERT_NE(red_em, nullptr);
    EXPECT_TRUE(red_em->flags & zui::html::SelectionStyle::kAuthorBg) << "child inherits author bg";
    EXPECT_FALSE(red_em->flags & zui::html::SelectionStyle::kAuthorFg);
    expect_rgba(red_em->fg_r, red_em->fg_g, red_em->fg_b, red_em->fg_a, 255, 0, 0, 255,
                "red-em fg from own color");
    expect_rgba(red_em->bg_r, red_em->bg_g, red_em->bg_b, red_em->bg_a, 0, 128, 0,
                static_cast<std::uint8_t>(0.4f * 255.f + 0.5f), "red-em inherited bg");

    // Illegal ::selection props must not change the element's own used layout style.
    zui::html::Node* probe = zui::html::find(scene.doc, "layout-probe");
    ASSERT_NE(probe, nullptr);
    ASSERT_NE(probe->computed, nullptr);
    auto margin = probe->computed->find("margin-left");
    auto width = probe->computed->find("width");
    auto display = probe->computed->find("display");
    if (margin != probe->computed->end()) {
        EXPECT_NE(margin->second, "40px");
    }
    ASSERT_NE(width, probe->computed->end());
    EXPECT_EQ(width->second, "100px");
    if (display != probe->computed->end()) {
        EXPECT_NE(display->second, "none");
    }
    zui::layout::Box* probe_box = scene.box("layout-probe");
    ASSERT_NE(probe_box, nullptr);
    EXPECT_NEAR(probe_box->geo.width, 100.f, 0.5f);

    // No author on the chain → UA-pair sentinel.
    EXPECT_EQ(sel("plain"), nullptr);

    // parse_css_color system colours used by the UA pair / author Highlight keywords.
    auto hi = zui::css::parse_css_color("Highlight");
    auto ht = zui::css::parse_css_color("HighlightText");
    ASSERT_TRUE(hi.has_value());
    ASSERT_TRUE(ht.has_value());
    expect_rgba(hi->r, hi->g, hi->b, hi->a, 0x00, 0x78, 0xD7, 255, "Highlight");
    expect_rgba(ht->r, ht->g, ht->b, ht->a, 255, 255, 255, 255, "HighlightText");
}

// ::selection S2–S3: SelectionRange inject + path-b TextRun mapping + paint overlay colours.
TEST(DifftestSelfCheck, SelectionPaint) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "selection-paint.html")) << error;

    zui::html::Node* sample = zui::html::find(scene.doc, "sample");
    ASSERT_NE(sample, nullptr);
    ASSERT_EQ(sample->text, "ABCDEFGH");
    ASSERT_NE(sample->selection, nullptr);
    EXPECT_TRUE(sample->selection->flags & zui::html::SelectionStyle::kAuthorBg);
    EXPECT_TRUE(sample->selection->flags & zui::html::SelectionStyle::kAuthorFg);
    EXPECT_EQ(sample->selection->bg_r, 0);
    EXPECT_EQ(sample->selection->bg_g, 128);
    EXPECT_EQ(sample->selection->bg_b, 0);
    EXPECT_EQ(sample->selection->fg_r, 0);
    EXPECT_EQ(sample->selection->fg_g, 0);
    EXPECT_EQ(sample->selection->fg_b, 0);

    // S2: clamp / order / empty / clear + paint dirty.
    const std::uint64_t stamp0 = zui::html::paint_stamp(scene.doc);
    zui::html::set_selection(scene.doc, sample, 10, 2);  // unordered + past end
    EXPECT_EQ(scene.doc.selection.node, sample);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{2});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{8});
    EXPECT_GT(zui::html::paint_stamp(scene.doc), stamp0);

    zui::html::set_selection(scene.doc, sample, 3, 3);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));
    EXPECT_EQ(scene.doc.selection.node, sample);

    const std::uint64_t stamp1 = zui::html::paint_stamp(scene.doc);
    zui::html::clear_selection(scene.doc);
    EXPECT_EQ(scene.doc.selection.node, nullptr);
    EXPECT_GT(zui::html::paint_stamp(scene.doc), stamp1);

    // Path (b): ASCII single-run maps onto node->text with glyph advances.
    zui::layout::Box* sample_box = scene.box("sample");
    ASSERT_NE(sample_box, nullptr);
    const zui::layout::TextRun* mapped = nullptr;
    for (const zui::layout::TextRun& run : sample_box->geo.runs) {
        if (!run.glyph_advances.empty() && run.text == sample->text) {
            mapped = &run;
            break;
        }
    }
    // Runs may live on an anonymous text child whose parent paints; search the subtree.
    if (mapped == nullptr) {
        for (const zui::layout::Box* child : sample_box->children) {
            if (child == nullptr) {
                continue;
            }
            for (const zui::layout::TextRun& run : child->geo.runs) {
                if (!run.glyph_advances.empty() && run.text == sample->text) {
                    mapped = &run;
                    break;
                }
            }
            if (mapped != nullptr) {
                break;
            }
            // Parent IFC owns the runs.
        }
        if (mapped == nullptr) {
            for (const zui::layout::TextRun& run : sample_box->geo.runs) {
                if (run.text == sample->text) {
                    mapped = &run;
                    break;
                }
            }
        }
    }
    ASSERT_NE(mapped, nullptr) << "expected a TextRun for sample text";
    EXPECT_EQ(mapped->byte_begin, std::size_t{0});
    ASSERT_FALSE(mapped->glyph_advances.empty())
        << "path-b mapping should copy glyph advances when shaped text == node->text";
    EXPECT_EQ(mapped->glyph_advances.size(), sample->text.size());
    float advance_sum = 0.f;
    for (float a : mapped->glyph_advances) {
        advance_sum += a;
    }
    EXPECT_GT(advance_sum, 0.f);

    auto paint_with_selection = [&](zui::html::Node* node, std::size_t a, std::size_t b) {
        zui::html::set_selection(scene.doc, node, a, b);
        zui::paint::DisplayList list;
        zui::paint::Painter painter;
        painter.set_retained(false);
        painter.set_selection(scene.doc.selection.node, scene.doc.selection.start,
                              scene.doc.selection.end);
        painter.paint(*scene.layout->root(), list, 800, 600);
        return list;
    };

    // Partial select "CDE" (bytes [2,5)).
    {
        const zui::paint::DisplayList list = paint_with_selection(sample, 2, 5);
        bool saw_sel_bg = false;
        bool saw_sel_fg = false;
        bool saw_unselected = false;
        for (const auto& it : list.items()) {
            if (it.op == zui::paint::DisplayOp::FillRect && it.color.r == 0 && it.color.g == 128 &&
                it.color.b == 0) {
                saw_sel_bg = true;
                EXPECT_GT(it.rect.w, 0.f);
                // Highlight height is the line box (`line_block`), not the em square.
                EXPECT_FLOAT_EQ(it.rect.h, mapped->line_block);
            }
            if (it.op == zui::paint::DisplayOp::DrawText) {
                if (it.text == "CDE" && it.color.r == 0 && it.color.g == 0 && it.color.b == 0) {
                    saw_sel_fg = true;
                }
                if ((it.text == "AB" || it.text == "FGH") && it.color.b == 255) {
                    saw_unselected = true;
                }
            }
        }
        EXPECT_TRUE(saw_sel_bg) << "author ::selection background missing";
        EXPECT_TRUE(saw_sel_fg) << "selected substring not drawn in selection colour";
        EXPECT_TRUE(saw_unselected) << "unselected slices should keep element colour";
    }

    // Empty range paints no overlay.
    {
        const zui::paint::DisplayList list = paint_with_selection(sample, 2, 2);
        for (const auto& it : list.items()) {
            if (it.op == zui::paint::DisplayOp::FillRect) {
                EXPECT_FALSE(it.color.r == 0 && it.color.g == 128 && it.color.b == 0)
                    << "empty selection must not paint highlight";
            }
            if (it.op == zui::paint::DisplayOp::DrawText &&
                it.text.find('C') != std::string::npos) {
                EXPECT_EQ(it.color.b, 255) << "empty selection keeps normal colour";
            }
        }
    }

    // No author on #plain → UA Highlight / HighlightText when selected.
    zui::html::Node* plain = zui::html::find(scene.doc, "plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->selection.get(), nullptr);
    {
        const zui::paint::DisplayList list = paint_with_selection(plain, 0, plain->text.size());
        bool saw_ua_bg = false;
        bool saw_ua_fg = false;
        for (const auto& it : list.items()) {
            if (it.op == zui::paint::DisplayOp::FillRect && it.color.r == 0x00 &&
                it.color.g == 0x78 && it.color.b == 0xD7) {
                saw_ua_bg = true;
            }
            if (it.op == zui::paint::DisplayOp::DrawText && it.text == plain->text &&
                it.color.r == 255 && it.color.g == 255 && it.color.b == 255) {
                saw_ua_fg = true;
            }
        }
        EXPECT_TRUE(saw_ua_bg) << "UA Highlight background";
        EXPECT_TRUE(saw_ua_fg) << "UA HighlightText foreground";
    }

    zui::html::clear_selection(scene.doc);
}

// ::marker M4: outside markers hang with inline-end at the content-box start; list text keeps
// the UA padding-left gutter; hanging ink does not grow scrollWidth.
TEST(DifftestSelfCheck, MarkerOutsidePlacement) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-outside.html")) << error;

    zui::layout::Box* li = scene.box("a");
    ASSERT_NE(li, nullptr);
    zui::layout::Box* marker = nullptr;
    zui::layout::Box* text = nullptr;
    for (zui::layout::Box* child : li->children) {
        if (child == nullptr) {
            continue;
        }
        if (child->generated == zui::html::PseudoElement::Marker) {
            marker = child;
        } else if (child->role == zui::layout::BoxRole::Text) {
            text = child;
        }
    }
    ASSERT_NE(marker, nullptr);
    ASSERT_NE(text, nullptr);

    // UA / fixture padding-left:40px: the list item's content starts at x=40 in the ul.
    EXPECT_NEAR(li->geo.world_x, 40.f, 0.5f) << "list item border box should sit after padding";
    EXPECT_NEAR(text->geo.world_x, 40.f, 0.5f) << "outside marker must not push the text";

    const float marker_end = marker->geo.world_x + marker->geo.content_width;
    // Prefer border-box width when content_width is unset on a freshly placed marker.
    const float marker_end_border =
        marker->geo.world_x +
        (marker->geo.content_width > 0.f
             ? marker->geo.content_width
             : (marker->cache != nullptr && marker->cache->pinned_final() != nullptr
                    ? marker->cache->pinned_final()->geometry.border_box_inline
                    : 0.f));
    EXPECT_NEAR(marker_end_border, 40.f, 1.f)
        << "marker inline-end should meet the content start; world_x=" << marker->geo.world_x
        << " end=" << marker_end_border << " content_end=" << marker_end;

    zui::layout::Box* scroller = scene.box("scroll");
    ASSERT_NE(scroller, nullptr);
    const float client_w = scroller->geo.width - scroller->geo.border.horizontal();
    EXPECT_LE(scroller->geo.scroll_width, client_w + 0.5f)
        << "outside marker must not inflate scrollWidth; scroll=" << scroller->geo.scroll_width
        << " client=" << client_w;
}

// B1: http(s) <link rel=stylesheet> is applied from the URL cache (WinHTTP fills it in-process;
// the dedicated difftest spins a localhost fixture for a live fetch).
TEST(DifftestSelfCheck, RemoteStylesheetFetch) {
    const char* url = "http://127.0.0.1:18765/remote-selfcheck.css";
    zui::css::cache_url_for_test(url, "#probe { width: 120px; height: 40px; }");

    // A scratch document, not a fixture: the fixture directory is checked in and must stay clean,
    // so the page is written beside the system temp files and removed on the way out.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui_remote_stylesheet_tmp.html";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out) << path.string();
        out << "<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"" << url
            << "\"></head><body><div id=\"probe\"></div></body></html>";
    }

    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    zui::layout::Box* probe = scene.box("probe");
    ASSERT_NE(probe, nullptr);
    EXPECT_NEAR(probe->geo.width, 120.f, 0.5f);
    EXPECT_NEAR(probe->geo.height, 40.f, 0.5f);

    std::string cached;
    EXPECT_TRUE(zui::css::fetch_url(url, cached));
    EXPECT_NE(cached.find("120px"), std::string::npos);

    std::error_code remove_ec;
    std::filesystem::remove(path, remove_ec);
}

// B2/A4: grid align-items:last baseline shares max(block−last) from the area end (flex parity).
TEST(DifftestSelfCheck, GridLastBaseline) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "grid-last-baseline.html")) << error;
    scene.layout->sync_world();

    zui::layout::Box* a = scene.box("a");
    zui::layout::Box* b = scene.box("b");
    zui::layout::Box* fa = scene.box("fa");
    zui::layout::Box* fb = scene.box("fb");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(fa, nullptr);
    ASSERT_NE(fb, nullptr);

    const zui::layout::LayoutResult* ar = Scene::result(a);
    const zui::layout::LayoutResult* br = Scene::result(b);
    ASSERT_NE(ar, nullptr);
    ASSERT_NE(br, nullptr);
    ASSERT_TRUE(std::isfinite(ar->geometry.baselines.last));
    ASSERT_TRUE(std::isfinite(br->geometry.baselines.last));

    // Shared last baseline: border-start + last coincide within the 100px row.
    const float a_last = a->placement.cb_offset.block_offset + ar->geometry.baselines.last;
    const float b_last = b->placement.cb_offset.block_offset + br->geometry.baselines.last;
    EXPECT_NEAR(a_last, b_last, 0.75f)
        << "last baselines must share one edge; a=" << a_last << " b=" << b_last;

    // Distinct from first-baseline packing: last-baseline sits nearer the row end.
    EXPECT_GT(a->placement.cb_offset.block_offset, fa->placement.cb_offset.block_offset + 5.f)
        << "last-baseline #a should sit below first-baseline #fa";
    EXPECT_GT(b->placement.cb_offset.block_offset, fb->placement.cb_offset.block_offset + 5.f)
        << "last-baseline #b should sit below first-baseline #fb";

    // Taller item still ends at the area end (from_end is max).
    EXPECT_NEAR(a->placement.cb_offset.block_offset + ar->geometry.border_box_block, 100.f, 1.f)
        << "max from_end item packs to the row end";
}

// A local @font-face url() is registered before layout, without env.json preloading that family.
TEST(DifftestSelfCheck, FontFaceLocalFile) {
    const std::filesystem::path font =
        std::filesystem::path(fixture("font-face.html")).parent_path().parent_path() / "fonts" /
        "NotoSans-Regular.ttf";
    ASSERT_TRUE(std::filesystem::is_regular_file(font));
    zui::text::register_embedded_font("Zui Known", font.string());

    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "font-face.html")) << error;

    const std::string registered = zui::text::embedded_font_path("Zui Face");
    EXPECT_NE(registered.find("NotoSans-Regular.ttf"), std::string::npos) << registered;

    zui::layout::Box* face = scene.box("face");
    zui::layout::Box* known = scene.box("known");
    ASSERT_NE(face, nullptr);
    ASSERT_NE(known, nullptr);
    EXPECT_NEAR(face->geo.width, known->geo.width, 1.f / 64.f);
    EXPECT_GT(face->geo.width, 40.f);
}

// CSS Grid §9: a subgrid's items occupy the parent tracks it spans. Column tracks are
// 120px / 1fr / 120px with a 16px gap inside a 400px grid. Row tracks are 36px / 36px
// with a 10px gap.
TEST(DifftestSelfCheck, GridSubgridTracks) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "grid-subgrid.html")) << error;

    const auto box = [&](const char* id) {
        zui::layout::Box* found = scene.box(id);
        EXPECT_NE(found, nullptr) << id;
        return found;
    };
    zui::layout::Box* h1 = box("h1");
    zui::layout::Box* h2 = box("h2");
    zui::layout::Box* h3 = box("h3");
    zui::layout::Box* a = box("a");
    zui::layout::Box* b = box("b");
    zui::layout::Box* c = box("c");
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);
    ASSERT_NE(h3, nullptr);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    constexpr float kTol = 1.f / 64.f;
    EXPECT_NEAR(h1->geo.width, 120.f, kTol);
    EXPECT_NEAR(h3->geo.width, 120.f, kTol);
    EXPECT_NEAR(h2->geo.width, 128.f, kTol);
    EXPECT_NEAR(a->geo.world_x, h1->geo.world_x, kTol);
    EXPECT_NEAR(a->geo.width, h1->geo.width, kTol);
    EXPECT_NEAR(b->geo.world_x, h2->geo.world_x, kTol);
    EXPECT_NEAR(b->geo.width, h2->geo.width, kTol);
    EXPECT_NEAR(c->geo.world_x, h3->geo.world_x, kTol);
    EXPECT_NEAR(c->geo.width, h3->geo.width, kTol);
    EXPECT_NEAR(a->geo.world_y, h1->geo.world_y + h1->geo.height, kTol);

    zui::layout::Box* r1 = box("r1");
    zui::layout::Box* r2 = box("r2");
    zui::layout::Box* side = box("side");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(side, nullptr);
    EXPECT_NEAR(r1->geo.height, 36.f, kTol);
    EXPECT_NEAR(r2->geo.height, 36.f, kTol);
    EXPECT_NEAR(r2->geo.world_y, r1->geo.world_y + 46.f, kTol);
    EXPECT_NEAR(side->geo.world_y, r1->geo.world_y, kTol);
    EXPECT_NEAR(side->geo.height, 82.f, kTol);
    EXPECT_NEAR(r1->geo.width, 200.f, kTol);
}

// CSS Overflow: scrollbar-gutter:stable reserves one classic bar (15px) on a flex or grid
// even when the content fits. both-edges reserves that thickness on the start edge too.
// An overflowing stable pane still reserves one bar, not two.
TEST(DifftestSelfCheck, ScrollbarGutterStable) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scrollbar-gutter.html")) << error;

    const auto box = [&](const char* id) {
        zui::layout::Box* found = scene.box(id);
        EXPECT_NE(found, nullptr) << id;
        return found;
    };
    zui::layout::Box* flex = box("flex");
    zui::layout::Box* fa = box("fa");
    zui::layout::Box* flexb = box("flexb");
    zui::layout::Box* fb = box("fb");
    zui::layout::Box* grid = box("grid");
    zui::layout::Box* ga = box("ga");
    zui::layout::Box* gridb = box("gridb");
    zui::layout::Box* gb = box("gb");
    zui::layout::Box* over = box("over");
    zui::layout::Box* oa = box("oa");
    ASSERT_NE(flex, nullptr);
    ASSERT_NE(fa, nullptr);
    ASSERT_NE(flexb, nullptr);
    ASSERT_NE(fb, nullptr);
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(ga, nullptr);
    ASSERT_NE(gridb, nullptr);
    ASSERT_NE(gb, nullptr);
    ASSERT_NE(over, nullptr);
    ASSERT_NE(oa, nullptr);

    constexpr float kTol = 1.f / 64.f;
    constexpr float kBar = 15.f;
    EXPECT_NEAR(fa->geo.width, 220.f - kBar, kTol);
    EXPECT_NEAR(fa->geo.world_x, flex->geo.world_x, kTol);
    EXPECT_NEAR(ga->geo.width, 220.f - kBar, kTol);
    EXPECT_NEAR(ga->geo.world_x, grid->geo.world_x, kTol);
    EXPECT_NEAR(fb->geo.width, 220.f - 2.f * kBar, kTol);
    EXPECT_NEAR(fb->geo.world_x, flexb->geo.world_x + kBar, kTol);
    EXPECT_NEAR(gb->geo.width, 220.f - 2.f * kBar, kTol);
    EXPECT_NEAR(gb->geo.world_x, gridb->geo.world_x + kBar, kTol);
    EXPECT_NEAR(oa->geo.width, 220.f - kBar, kTol);
    EXPECT_NEAR(oa->geo.world_x, over->geo.world_x, kTol);
    EXPECT_NEAR(oa->geo.height, 80.f, kTol);
}

// font-display periods match the pinned browser: block and auto hide for 3s then swap forever,
// swap shows the fallback immediately, fallback hides for 100ms then swaps for 3s, and optional
// hides for 100ms and then keeps the fallback. A file present at registration is used.
TEST(DifftestSelfCheck, FontDisplayTimeline) {
    struct Reset {
        ~Reset() { zui::text::font_display_test_clock(-1); }
    } reset;

    const std::filesystem::path font =
        std::filesystem::path(fixture("font-face.html")).parent_path().parent_path() / "fonts" /
        "NotoSans-Regular.ttf";
    ASSERT_TRUE(std::filesystem::is_regular_file(font));
    std::string url = font.generic_string();
    for (char& c : url) {
        if (c == '\\') {
            c = '/';
        }
    }
    auto face = [&](const char* family, const char* display) {
        return std::string("@font-face{font-family:\"") + family + "\";src:url(\"" + url +
               "\") format(\"truetype\");font-display:" + display + ";}";
    };
    zui::text::font_display_test_clock(0);
    // No whitespace between rules: the scanner must still see every @font-face.
    zui::text::register_font_faces(face("DispBlock", "block") + face("DispSwap", "swap") +
                                        face("DispFallback", "fallback") +
                                        face("DispOptional", "optional"),
                                    {});
    for (const char* family : {"DispBlock", "DispSwap", "DispFallback", "DispOptional"}) {
        const std::string path = zui::text::embedded_font_path(family);
        EXPECT_NE(path.find("NotoSans-Regular"), std::string::npos) << family << " -> [" << path << "]";
    }

    using zui::text::FontDisplayPhase;
    auto phase = [](const char* family) { return zui::text::font_display_phase(family); };

    zui::text::font_display_test_arm("DispBlock", 0, 0, false);
    zui::text::font_display_test_clock(1000);
    EXPECT_EQ(phase("DispBlock"), FontDisplayPhase::Invisible);
    zui::text::font_display_test_clock(3000);
    EXPECT_EQ(phase("DispBlock"), FontDisplayPhase::Fallback);
    zui::text::font_display_test_arm("DispBlock", 0, 4000, true);
    zui::text::font_display_test_clock(4000);
    EXPECT_EQ(phase("DispBlock"), FontDisplayPhase::Use);

    zui::text::font_display_test_arm("DispSwap", 0, 0, false);
    zui::text::font_display_test_clock(0);
    EXPECT_EQ(phase("DispSwap"), FontDisplayPhase::Fallback);
    zui::text::font_display_test_arm("DispSwap", 0, 5000, true);
    zui::text::font_display_test_clock(5000);
    EXPECT_EQ(phase("DispSwap"), FontDisplayPhase::Use);

    zui::text::font_display_test_arm("DispFallback", 0, 0, false);
    zui::text::font_display_test_clock(50);
    EXPECT_EQ(phase("DispFallback"), FontDisplayPhase::Invisible);
    zui::text::font_display_test_clock(200);
    EXPECT_EQ(phase("DispFallback"), FontDisplayPhase::Fallback);
    zui::text::font_display_test_arm("DispFallback", 0, 200, true);
    EXPECT_EQ(phase("DispFallback"), FontDisplayPhase::Use);
    zui::text::font_display_test_arm("DispFallback", 0, 4000, true);
    zui::text::font_display_test_clock(4000);
    EXPECT_EQ(phase("DispFallback"), FontDisplayPhase::Fallback);

    zui::text::font_display_test_arm("DispOptional", 0, 0, false);
    zui::text::font_display_test_clock(50);
    EXPECT_EQ(phase("DispOptional"), FontDisplayPhase::Invisible);
    zui::text::font_display_test_clock(200);
    EXPECT_EQ(phase("DispOptional"), FontDisplayPhase::Fallback);
    zui::text::font_display_test_arm("DispOptional", 0, 50, true);
    zui::text::font_display_test_clock(200);
    EXPECT_EQ(phase("DispOptional"), FontDisplayPhase::Use);
    zui::text::font_display_test_arm("DispOptional", 0, 200, true);
    EXPECT_EQ(phase("DispOptional"), FontDisplayPhase::Fallback);

    EXPECT_EQ(phase("No Such Face"), FontDisplayPhase::Use);

    zui::text::font_display_test_clock(200);
    zui::text::font_display_test_arm("DispOptional", 0, 0, false);
    {
        zui::text::FontChain skipped;
        ASSERT_TRUE(skipped.init("DispOptional", 400, false));
        const zui::text::LoadedGlyph glyph =
            skipped.load(static_cast<std::uint32_t>('M'), 48.f, false, 400, false, true);
        EXPECT_EQ(glyph.path.find("NotoSans-Regular"), std::string::npos) << glyph.path;
    }
    zui::text::font_display_test_arm("DispOptional", 0, 0, true);
    {
        zui::text::FontChain used;
        ASSERT_TRUE(used.init("DispOptional", 400, false));
        const zui::text::LoadedGlyph glyph =
            used.load(static_cast<std::uint32_t>('M'), 48.f, false, 400, false, true);
        EXPECT_NE(glyph.path.find("NotoSans-Regular"), std::string::npos) << glyph.path;
    }
}
