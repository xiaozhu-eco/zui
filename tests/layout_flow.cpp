#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"
#include "layout/paint_map.h"
#include "paint/display_list.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.6f; }

zui::layout::Box* box_of(zui::layout::LayoutEngine& layout, zui::html::Document& doc,
                         const char* id) {
    return layout.principal_box(zui::html::find(doc, id));
}

/// The local y of a box's first text run, or NaN when the box painted no text.
float run_y(zui::layout::Box* box) {
    if (box == nullptr || box->geo.runs.empty()) {
        return std::nanf("");
    }
    return box->geo.runs.front().y;
}

}  // namespace

// A definite width larger than the containing block is cut by max-width: 100%.
TEST(LayoutFlow, MaxWidthPercentClampsALongerWidth) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/max_width_percent.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{800.0f, 400.0f});
    const zui::layout::Box* box = box_of(layout, doc, "box");
    if (box == nullptr) {
        FAIL() << "missing #box\n";
    }
    if (!near(box->geo.width, 400.0f)) {
        FAIL() << "max-width 100% did not clamp 660px to the 400px containing block: "
               << box->geo.width << "\n";
    }
}

// A span that wraps inside a definite-width paragraph has to grow that paragraph.
// The span's own border box covers every line; the paragraph used to stay one line-height
// and the next sibling overlapped the second line.
TEST(LayoutFlow, WrappedInlineGrowsItsBlock) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/inline_wrap_block.html", doc,
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
    layout.layout(doc, zui::layout::Viewport{800.0f, 400.0f});
    const zui::layout::Box* paragraph = box_of(layout, doc, "p");
    const zui::layout::Box* span = box_of(layout, doc, "s");
    if (paragraph == nullptr || span == nullptr) {
        FAIL() << "missing boxes\n";
    }
    if (span->geo.height < 36.0f) {
        FAIL() << "span did not wrap: h=" << span->geo.height << " w=" << span->geo.width << "\n";
    }
    if (paragraph->geo.height + 0.5f < span->geo.height) {
        FAIL() << "paragraph stayed shorter than its wrapped span: p=" << paragraph->geo.height
               << " span=" << span->geo.height << "\n";
    }
    std::cout << "plain wrap: span h=" << span->geo.height << " paragraph h=" << paragraph->geo.height
              << " (one font box is under 36; paragraph covers the span)\n";
}

// Same wrap, but min-height: 0 takes the paragraph off the plain-block fast path.
// The full finish path still has to cover the span's line stack.
TEST(LayoutFlow, WrappedInlineGrowsANonPlainBlock) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/inline_wrap_block.html", doc,
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
    layout.layout(doc, zui::layout::Viewport{800.0f, 400.0f});
    const zui::layout::Box* paragraph = box_of(layout, doc, "p2");
    const zui::layout::Box* span = box_of(layout, doc, "s2");
    if (paragraph == nullptr || span == nullptr) {
        FAIL() << "missing boxes\n";
    }
    if (span->geo.height < 36.0f) {
        FAIL() << "span did not wrap: h=" << span->geo.height << " w=" << span->geo.width << "\n";
    }
    if (paragraph->geo.height + 0.5f < span->geo.height) {
        FAIL() << "paragraph stayed shorter than its wrapped span: p=" << paragraph->geo.height
               << " span=" << span->geo.height << "\n";
    }
    std::cout << "nonplain wrap: span h=" << span->geo.height << " paragraph h="
              << paragraph->geo.height
              << " (one font box is under 36; paragraph covers the span)\n";
}

// A wrapped inline with a sibling is absorbed into the parent IFC and reflowed at
// the parent's width. The span's own 80px stack must not become the parent's height.
TEST(LayoutFlow, AbsorbedInlineDoesNotInflateTheParent) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/absorbed_inline.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{800.0f, 400.0f});
    const zui::layout::Box* host = box_of(layout, doc, "host");
    const zui::layout::Box* span = box_of(layout, doc, "s");
    const zui::layout::Box* sib = box_of(layout, doc, "sib");
    if (host == nullptr || span == nullptr || sib == nullptr) {
        FAIL() << "missing boxes\n";
    }
    if (!span->inline_text_owned_by_parent) {
        FAIL() << "span was not absorbed; h=" << span->geo.height << " w=" << span->geo.width
               << "\n";
    }
    // Forty 16px CJK glyphs wrap to about eight lines at 80px (~160px) and to two
    // lines at the parent's 400px. The inflated parent is the 80px stack.
    if (host->geo.height >= 100.0f) {
        FAIL() << "absorbed 80px wrap inflated #host: h=" << host->geo.height
               << " span=" << span->geo.height << "\n";
    }
    if (host->geo.height < 20.0f) {
        FAIL() << "host lost its line stack: h=" << host->geo.height << "\n";
    }
}

TEST(LayoutFlow, CollapseAndFloats) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flow.html", doc, error)) {
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
    // Layout geometry, including boxes below the fold: the paint walk culls those (LAYOUT.md
    // section 9 step B), so map every box before reading rectangles.
    zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());

    auto* a = box_of(layout, doc, "a");
    auto* b = box_of(layout, doc, "b");
    auto* host = box_of(layout, doc, "box");
    if (a == nullptr || b == nullptr || host == nullptr) {
        FAIL() << "missing margin boxes\n";
    }
    if (!near(a->geo.y, 31.0f) || !near(b->geo.y, 81.0f) ||
        !near(host->geo.content_height, 110.0f)) {
        // `geo.y` is measured from the parent's *border* box, and `#box` has a 1px top border, so
        // the content-box-relative 30/80 are 31/81 here. The browser reports #a at y=39 and #b at
        // y=89 with #box at y=8 - the same 31/81 (measured with the box-compare harness).
        FAIL() << "sibling collapse a.y=" << a->geo.y << " b.y=" << b->geo.y
               << " host.h=" << host->geo.content_height << "\n";
    }

    auto* inner = box_of(layout, doc, "inner");
    auto* child = box_of(layout, doc, "child");
    if (inner == nullptr || child == nullptr) {
        FAIL() << "missing parent-child boxes\n";
    }
    if (!near(child->geo.y, 0.0f) || !near(inner->geo.height, 10.0f) ||
        !near(inner->geo.y, 21.0f)) {
        // #child's own box coincides with #inner's, so its offset inside #inner stays 0; #inner
        // sits 20px below #outer's content edge, which is 21px below its border box.
        FAIL() << "parent collapse child.y=" << child->geo.y << " inner.h=" << inner->geo.height
               << " inner.y=" << inner->geo.y << "\n";
    }

    auto* p1 = box_of(layout, doc, "p1");
    auto* p2 = box_of(layout, doc, "p2");
    if (p1 == nullptr || p2 == nullptr) {
        FAIL() << "missing through boxes\n";
    }
    if (!near(p1->geo.y, 11.0f) || !near(p2->geo.y, 61.0f)) {
        // Same 1px-border offset: #gap's content box starts 1px below its border box.
        FAIL() << "through collapse p1.y=" << p1->geo.y << " p2.y=" << p2->geo.y << "\n";
    }

    auto* s1 = box_of(layout, doc, "s1");
    auto* s2 = box_of(layout, doc, "s2");
    auto* s3 = box_of(layout, doc, "s3");
    if (s1 == nullptr || s2 == nullptr || s3 == nullptr) {
        FAIL() << "missing flex items\n";
    }
    if (!(s1->geo.width < 80.0f && s2->geo.x < 100.0f && s3->geo.x < 180.0f &&
          s1->geo.width + s2->geo.width + s3->geo.width < 240.0f)) {
        FAIL() << "flex max-content s1=" << s1->geo.width << " s2.x=" << s2->geo.x
               << " s3.x=" << s3->geo.x << "\n";
    }

    auto* wide = box_of(layout, doc, "wide");
    auto* minc = box_of(layout, doc, "minc");
    if (wide == nullptr || minc == nullptr) {
        FAIL() << "missing grid tracks\n";
    }
    if (!(wide->geo.width > minc->geo.width + 4.0f)) {
        FAIL() << "grid tracks wide=" << wide->geo.width << " min=" << minc->geo.width << "\n";
    }

    auto* pic = box_of(layout, doc, "pic");
    auto* attr = box_of(layout, doc, "attr");
    auto* cssimg = box_of(layout, doc, "cssimg");
    if (pic == nullptr || attr == nullptr || cssimg == nullptr) {
        FAIL() << "missing images\n";
    }
    if (!near(pic->geo.width, 1.0f) || !near(pic->geo.height, 1.0f) ||
        !near(attr->geo.width, 32.0f) || !near(attr->geo.height, 32.0f) ||
        !near(cssimg->geo.width, 20.0f) || !near(cssimg->geo.height, 20.0f)) {
        FAIL() << "img pic=" << pic->geo.width << "x" << pic->geo.height
               << " attr=" << attr->geo.width << "x" << attr->geo.height
               << " css=" << cssimg->geo.width << "x" << cssimg->geo.height << "\n";
    }

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list, 800, 600);
    int images = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::DrawImage) {
            ++images;
        }
    }
    if (images < 3) {
        FAIL() << "draw image count " << images << "\n";
    }

    auto* n1 = box_of(layout, doc, "n1");
    auto* n2 = box_of(layout, doc, "n2");
    auto* maxw = box_of(layout, doc, "maxw");
    auto* minw = box_of(layout, doc, "minw");
    auto* fitabs = box_of(layout, doc, "fitabs");
    auto* word = box_of(layout, doc, "word");
    auto* clip = box_of(layout, doc, "clip");
    auto* sp = box_of(layout, doc, "sp");
    if (n1 == nullptr || n2 == nullptr || maxw == nullptr || minw == nullptr || fitabs == nullptr ||
        word == nullptr || clip == nullptr || sp == nullptr) {
        FAIL() << "missing extra boxes\n";
    }
    if (!near(n2->geo.y, -9.0f)) {
        // #neg has a 1px top border and `geo.y` is measured from the parent's border box: the
        // browser puts #n2 at 333 with #neg at 342, i.e. -9, not the content-relative -10.
        FAIL() << "negative collapse n2.y=" << n2->geo.y << "\n";
    }
    if (!near(maxw->geo.width, 140.0f) || !near(minw->geo.width, 90.0f)) {
        FAIL() << "keyword width max=" << maxw->geo.width << " min=" << minw->geo.width << "\n";
    }
    if (!near(fitabs->geo.width, 70.0f) || !near(fitabs->geo.x, 10.0f)) {
        FAIL() << "abs fit w=" << fitabs->geo.width << " x=" << fitabs->geo.x << "\n";
    }
    if (!near(word->geo.width, 180.0f) || !near(clip->geo.width, 80.0f)) {
        FAIL() << "flex floor word=" << word->geo.width << " clip=" << clip->geo.width << "\n";
    }
    if (!near(sp->geo.width, 100.0f)) {
        FAIL() << "span track " << sp->geo.width << "\n";
    }
    auto* spw = box_of(layout, doc, "spw");
    auto* cap = box_of(layout, doc, "cap");
    if (spw == nullptr || cap == nullptr) {
        FAIL() << "missing span width or caption\n";
    }
    if (!near(spw->geo.width, 116.0f)) {
        FAIL() << "spanned width " << spw->geo.width << "\n";
    }
    if (cap->geo.width < 50.0f || cap->geo.height < 24.0f || cap->geo.height > 60.0f) {
        FAIL() << "br caption " << cap->geo.width << "x" << cap->geo.height << "\n";
    }
    auto* fl = box_of(layout, doc, "fl");
    auto* flp = box_of(layout, doc, "flp");
    auto* fr = box_of(layout, doc, "fr");
    auto* ccl = box_of(layout, doc, "ccl");
    auto* mgb = box_of(layout, doc, "mgb");
    auto* hang = box_of(layout, doc, "hang");
    auto* hold = box_of(layout, doc, "hold");
    auto* pa = box_of(layout, doc, "pa");
    auto* pb = box_of(layout, doc, "pb");
    auto* db = box_of(layout, doc, "db");
    zui::layout::Box* fl_text = nullptr;
    if (flp != nullptr) {
        for (zui::layout::Box* ch : flp->children) {
            if (ch->role == zui::layout::BoxRole::Text) {
                fl_text = ch;
            }
        }
    }
    if (fl == nullptr || flp == nullptr || fr == nullptr || ccl == nullptr || mgb == nullptr ||
        hang == nullptr || hold == nullptr || pa == nullptr || pb == nullptr || db == nullptr ||
        fl_text == nullptr || flp->geo.runs.empty()) {
        FAIL() << "missing float boxes\n";
    }
    if (!near(fl->geo.x, 0.0f) || !near(fl->geo.y, 0.0f) || !near(flp->geo.width, 180.0f) ||
        flp->geo.runs[0].x < 59.0f) {
        // LAYOUT.md section 4: the runs live on the inline formatting context container
        // (`#flp`), not on the leaf Text box, so the line offset is read from `#flp`.
        FAIL() << "float left x=" << fl->geo.x << " line=" << flp->geo.runs[0].x
               << " p=" << flp->geo.width << "\n";
    }
    if (!near(fr->geo.x, 120.0f)) {
        FAIL() << "float right x=" << fr->geo.x << "\n";
    }
    if (!near(ccl->geo.y, 28.0f)) {
        FAIL() << "clear y=" << ccl->geo.y << "\n";
    }
    if (!near(mgb->geo.y, 50.0f)) {
        FAIL() << "margin across float y=" << mgb->geo.y << "\n";
    }
    auto* flhost = box_of(layout, doc, "flhost");
    // A float's bottom edge is included in its parent's height only when the parent is a BFC
    // root (`#hold { overflow: hidden }` -> 28). A plain block collapses to its in-flow content:
    // `#hang` holds only a float, so it is 0, and `#flhost` follows `#flp` at 24 while the 28px
    // float overflows. A browser reports exactly these (measured with the box-compare harness).
    if (flhost == nullptr || !near(hang->geo.height, 0.0f) || !near(hold->geo.height, 28.0f) ||
        !near(flhost->geo.height, 24.0f)) {
        FAIL() << "hang h=" << hang->geo.height << " hold h=" << hold->geo.height
               << " flhost h=" << (flhost ? flhost->geo.height : -1.0f) << "\n";
    }
    if (!near(pa->geo.x, 0.0f) || !near(pb->geo.x, 50.0f) || !near(pb->geo.y, 0.0f) ||
        !near(db->geo.y, 16.0f)) {
        FAIL() << "pack pb=" << pb->geo.x << "," << pb->geo.y << " drop=" << db->geo.y << "\n";
    }

    zui::html::Document page;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/flow/flow.html", page, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine page_css;
    if (!page_css.load_sheets(page, error)) {
        FAIL() << error << "\n";
    }
    page_css.cascade(page);
    zui::layout::LayoutEngine page_layout(metrics);
    page_layout.layout(page, zui::layout::Viewport{1100.0f, 720.0f});
    // Layout geometry, including boxes below the fold: the paint walk culls those (LAYOUT.md
    // section 9 step B), so map every box before reading rectangles.
    zui::layout::map_paint_every_box(page_layout.paint_map_session(), *page_layout.root());
    struct Found {
        zui::layout::Box* across = nullptr;
        void walk(zui::layout::Box* b) {
            if (b == nullptr) {
                return;
            }
            if (b->node != nullptr && b->node->class_name.find("across") != std::string::npos) {
                across = b;
            }
            for (zui::layout::Box* ch : b->children) {
                walk(ch);
            }
        }
    } found;
    found.walk(page_layout.root());
    auto* page_cap = page_layout.principal_box(zui::html::find(page, "cap"));
    if (found.across == nullptr || !near(found.across->geo.width, 116.0f)) {
        FAIL() << "example span " << (found.across ? found.across->geo.width : -1.0f) << "\n";
    }
    int text_lines = 0;
    float line_w = 0;
    if (page_cap != nullptr) {
        for (zui::layout::Box* ch : page_cap->children) {
            if (ch->role == zui::layout::BoxRole::Text && ch->geo.width > 1.0f) {
                ++text_lines;
                line_w = std::max(line_w, ch->geo.width);
            }
        }
    }
    if (page_cap == nullptr || text_lines != 2 || line_w < 40.0f || page_cap->geo.width < 40.0f ||
        page_cap->geo.height < 24.0f || page_cap->geo.height > 60.0f) {
        FAIL() << "example br lines=" << text_lines << " line_w=" << line_w
               << " box=" << (page_cap ? page_cap->geo.width : -1.0f) << "x"
               << (page_cap ? page_cap->geo.height : -1.0f) << "\n";
    }
}

TEST(LayoutFlow, IntrinsicAndBrokenImage) {
    zui::html::Document page;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/flow/flow.html", page, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine page_css;
    if (!page_css.load_sheets(page, error)) {
        FAIL() << error << "\n";
    }
    page_css.cascade(page);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine page_layout(metrics);
    page_layout.layout(page, zui::layout::Viewport{1100.0f, 720.0f});
    // This test asserts on *layout* geometry, including boxes below the fold. A frame's paint walk
    // culls those (LAYOUT.md section 9 step B) and leaves them without world coordinates, so the
    // geometry is mapped for every box here.
    zui::layout::map_paint_every_box(page_layout.paint_map_session(), *page_layout.root());
    struct Sizes {
        float minline = -1;
        float maxline = -1;
        float card = -1;
        float missing = -1;
        int captions = 0;
        void walk(zui::layout::Box* b) {
            if (b == nullptr) {
                return;
            }
            if (b->node != nullptr) {
                if (b->node->id == "cap") {
                    int lines = 0;
                    for (zui::layout::Box* ch : b->children) {
                        if (ch->role == zui::layout::BoxRole::Text && ch->geo.width > 1.f) {
                            ++lines;
                        }
                    }
                    captions = lines;
                }
                const std::string& cls = b->node->class_name;
                if (cls.find("minline") != std::string::npos) {
                    minline = b->geo.width;
                    if (b->parent != nullptr && b->parent->parent != nullptr) {
                        card = b->parent->parent->geo.width;
                    }
                }
                if (cls.find("maxline") != std::string::npos) {
                    maxline = b->geo.width;
                }
                if (b->node->tag == "img") {
                    auto it = b->node->attrs.find("src");
                    if (it != b->node->attrs.end() &&
                        it->second.find("missing") != std::string::npos) {
                        missing = b->geo.width;
                    }
                }
            }
            for (zui::layout::Box* ch : b->children) {
                walk(ch);
            }
        }
    } sizes;
    sizes.walk(page_layout.root());
    auto* cap = page_layout.principal_box(zui::html::find(page, "cap"));
    float cn_min_y = -1;
    float cn_max_y = -1;
    float cn_max_x = -1;
    float mm_second = -1;
    float mm_width = -1;
    struct PlaceWalk {
        float* cn_min_y;
        float* cn_max_y;
        float* cn_max_x;
        float* mm_second;
        float* mm_width;
        void walk(zui::layout::Box* b) {
            if (b == nullptr || b->node == nullptr && b->children.empty()) {
                return;
            }
            if (b->node != nullptr) {
                const std::string& cls = b->node->class_name;
                if (cls.find("minline") != std::string::npos && b->geo.width < 30.f) {
                    *cn_min_y = b->geo.y;
                }
                if (cls.find("maxline") != std::string::npos && b->geo.width < 160.f &&
                    b->geo.width > 40.f) {
                    *cn_max_y = b->geo.y;
                    *cn_max_x = b->geo.x;
                }
                if (cls.find("mm") != std::string::npos) {
                    *mm_width = b->geo.width;
                    int i = 0;
                    for (zui::layout::Box* ch : b->children) {
                        if (ch != nullptr && ch->node != nullptr &&
                            ch->node->class_name.find("cell") != std::string::npos) {
                            if (i == 1) {
                                *mm_second = ch->geo.width;
                            }
                            ++i;
                        }
                    }
                }
            }
            for (zui::layout::Box* ch : b->children) {
                walk(ch);
            }
        }
    } places{&cn_min_y, &cn_max_y, &cn_max_x, &mm_second, &mm_width};
    places.walk(page_layout.root());
    if (cap == nullptr || cap->geo.y < -1.f || cap->geo.x < 40.f) {
        FAIL() << "caption beside image y=" << (cap ? cap->geo.y : -999)
               << " x=" << (cap ? cap->geo.x : -999) << "\n";
    }
    if (cn_max_y < 0.f || cn_max_y > cn_min_y + 30.f || cn_max_x < 10.f) {
        FAIL() << "cjk maxline x=" << cn_max_x << " y=" << cn_max_y << " min_y=" << cn_min_y
               << "\n";
    }
    if (mm_second < 40.f || (mm_width > 0.f && mm_second > mm_width * 0.55f)) {
        FAIL() << "minmax second=" << mm_second << " grid=" << mm_width << "\n";
    }
    if (sizes.captions != 2 || sizes.minline < 8.f || sizes.minline > 40.f ||
        sizes.maxline < 80.f || sizes.card < 120.f || sizes.missing < 0.f || sizes.missing > 8.f) {
        FAIL() << "cap_lines=" << sizes.captions << " minline=" << sizes.minline
               << " maxline=" << sizes.maxline << " card=" << sizes.card
               << " missing=" << sizes.missing << "\n";
    }
}

TEST(LayoutFlow, NowrapHasNoVerticalBar) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/checks/checks.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{1100.0f, 720.0f});
    zui::layout::Box* nowrap = nullptr;
    struct Find {
        zui::layout::Box** out;
        void walk(zui::layout::Box* b) {
            if (b == nullptr) {
                return;
            }
            if (b->node != nullptr && b->node->class_name.find("nowrap") != std::string::npos) {
                *out = b;
            }
            for (zui::layout::Box* ch : b->children) {
                walk(ch);
            }
        }
    } find{&nowrap};
    find.walk(layout.root());
    if (nowrap == nullptr) {
        FAIL() << "nowrap box missing\n";
        return;
    }
    const float client_h = nowrap->geo.height - nowrap->geo.border.vertical();
    const float client_w = nowrap->geo.width - nowrap->geo.border.horizontal();
    if (nowrap->geo.scroll_height > client_h + 0.5f) {
        FAIL() << "vertical bar scroll_h=" << nowrap->geo.scroll_height << " client=" << client_h
               << "\n";
    }
    if (nowrap->geo.scroll_width <= client_w + 0.5f) {
        FAIL() << "nowrap did not overflow horizontally scroll_w=" << nowrap->geo.scroll_width
               << " client=" << client_w << "\n";
    }
}

/// HTML button layout: Chromium centres a `<button>`'s content in its content box - its UA sheet
/// asks for `align-items: center`, and Blink's `LayoutNGButton::AlignButtonChildren` moves the
/// anonymous child block, for a definite `height` and for a grid area the item was stretched into.
/// A `<div>` with the same declarations keeps its line at the top. Measured against Chrome: the
/// calendar's `.btn { height: 34px }` label sat 5px above the middle without this.
///
/// The calendar's month cell is a button too, and its row stretches it, so it needs a child that
/// fills the content box (`.cell-inner`) to keep the numbers at the top edge - the last case here.
TEST(LayoutFlow, ButtonContentIsCentred) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/button_center.html", doc, error)) {
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

    auto* button = box_of(layout, doc, "btn");
    auto* plain = box_of(layout, doc, "div");
    auto* area_button = box_of(layout, doc, "grid-btn");
    auto* area_div = box_of(layout, doc, "grid-div");
    auto* wrapper = box_of(layout, doc, "inner");
    if (button == nullptr || plain == nullptr || area_button == nullptr || area_div == nullptr ||
        wrapper == nullptr) {
        FAIL() << "missing boxes\n";
        return;
    }
    // The fixture is one 20px line in a 60px box: centring moves the line 20px down, top alignment
    // leaves it at the content top, so the two runs differ by the leftover half. Every box in the
    // fixture has zero padding and border, so a run's local y is measured from its content top.
    const float centred = 20.0f;
    const float to_button = run_y(button) - run_y(plain);
    const float to_area = run_y(area_button) - run_y(area_div);
    const float to_wrapper = run_y(wrapper) - run_y(plain);
    if (std::fabs(to_button - centred) > 0.6f) {
        FAIL() << "button content offset " << to_button << " != " << centred << "\n";
    }
    if (std::fabs(to_area - centred) > 0.6f) {
        FAIL() << "stretched grid button content offset " << to_area << " != " << centred << "\n";
    }
    if (std::fabs(to_wrapper) > 0.6f) {
        FAIL() << "a full-height wrapper did not hold the line at the top: " << to_wrapper << "\n";
    }
}

/// The baseline an atomic inline box puts on its line, per kind (`source/layout/baseline.h`).
///
/// The engine used to report the block-end edge of the border box for every atomic inline that
/// had no line box of its own, which is one of the three rules below and not the other two.
/// `tests/fixtures/atomic_baseline.html` is one line per case; the table is Chrome's answer for
/// that page (headless Edge, 360x900, the same client size), and it covers the whole class:
///
///  - an inline-block is its **last** line box (CSS 2.1 section 10.8.3), and its bottom **margin**
///    edge when it has no line box or its `overflow` is not `visible`;
///  - an inline-grid / inline-flex is the **first** item of the first row / first line, using that
///    item's own baseline or the block-end edge of its border box, whatever its `overflow`;
///  - an inline-table takes its first row, a replaced box its bottom margin edge, and a form
///    widget (a checkbox, which the engine lays out itself) its border box;
///  - a ruby container takes its base, not the annotation stacked above it.
///
/// Every case also asserts the child's own height, so a block-size mistake cannot pass as a
/// baseline one. The `<select>` case of the fixture is left out: the engine's control is 20px tall
/// where Chrome's is 19, which is a widget-sizing question, not a baseline one.
TEST(LayoutFlow, AtomicInlineBaselineKind) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/atomic_baseline.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{360.0f, 900.0f});

    struct Case {
        int index;     // `p<index>` holds the line, `b<index>` is the box under test
        float line;    // the parent's height: its one line box
        float offset;  // the box's top below the line's top
        float height;  // the box's own border-box height
    };
    const Case cases[] = {
        {1, 22.0f, 0.0f, 22.0f},  {2, 32.0f, 4.0f, 28.0f},  {3, 26.0f, 0.0f, 22.0f},
        {4, 26.0f, 4.0f, 22.0f},  {5, 30.0f, 0.0f, 28.0f},  {6, 26.0f, 0.0f, 16.0f},
        {7, 32.0f, 0.0f, 28.0f},  {8, 20.0f, 4.0f, 14.0f},  {9, 26.0f, 4.0f, 22.0f},
        {10, 20.0f, 4.0f, 14.0f}, {11, 20.0f, 4.0f, 14.0f}, {12, 26.0f, 4.0f, 22.0f},
        {13, 38.0f, 0.0f, 28.0f}, {14, 22.0f, 3.0f, 13.0f}, {15, 32.0f, 4.0f, 28.0f},
        {16, 32.0f, 4.0f, 28.0f}, {17, 26.0f, 0.0f, 16.0f}, {18, 20.0f, 4.0f, 14.0f},
        {19, 23.0f, 0.0f, 22.0f}, {20, 22.0f, 8.0f, 14.0f}, {21, 22.0f, 8.0f, 14.0f},
        {22, 32.0f, 0.0f, 22.0f}, {23, 23.0f, 0.0f, 22.0f}, {24, 30.0f, 8.0f, 22.0f},
        {25, 23.0f, 0.0f, 22.0f}, {26, 22.0f, 0.0f, 22.0f}, {27, 22.0f, 0.0f, 22.0f},
        {28, 22.0f, 0.0f, 22.0f}, {29, 20.0f, 0.0f, 10.0f},
    };
    for (const Case& probe : cases) {
        const std::string parent_id = "p" + std::to_string(probe.index);
        const std::string box_id = "b" + std::to_string(probe.index);
        auto* parent = box_of(layout, doc, parent_id.c_str());
        auto* box = box_of(layout, doc, box_id.c_str());
        if (parent == nullptr || box == nullptr) {
            ADD_FAILURE() << "case " << probe.index << ": missing box\n";
            continue;
        }
        const float line = parent->geo.height;
        const float offset = box->geo.world_y - parent->geo.world_y;
        const float height = box->geo.height;
        if (!near(line, probe.line) || !near(offset, probe.offset) || !near(height, probe.height)) {
            ADD_FAILURE() << "case " << probe.index << " (" << box_id << "): line " << line
                          << " != " << probe.line << ", offset " << offset << " != " << probe.offset
                          << ", height " << height << " != " << probe.height << "\n";
        }
    }
}

TEST(LayoutFlow, InlineCodePaddingDoesNotOverlapFollowingText) {
    // Regression for zmd welcome CJK overlap after gray `<code>` chips: the line
    // walker used to keep one ShapedRun across a non-atomic inline chrome piece, so
    // text after the chip painted at the chip's x.
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/inline_code_gap.html", doc, error)) {
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
    zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());

    auto* line = box_of(layout, doc, "line");
    auto* body = box_of(layout, doc, "body");
    auto* chip = box_of(layout, doc, "chip");
    if (line == nullptr || body == nullptr || chip == nullptr) {
        FAIL() << "missing line/body/chip boxes\n";
    }
    if (chip->geo.width < 8.f) {
        FAIL() << "chip too narrow: " << chip->geo.width << "\n";
    }

    // The body span owns the Chinese runs (a sole inline stays one piece). The chip owns
    // its mono run.
    zui::layout::Box* cjk_box = body->geo.runs.empty() ? line : body;
    float after_x = std::nanf("");
    for (const auto& run : cjk_box->geo.runs) {
        // UTF-8 for U+540E (后)
        if (run.text.find("\xe5\x90\x8e") != std::string::npos) {
            after_x = cjk_box->geo.world_x + run.x;
            break;
        }
    }
    if (!(after_x == after_x)) {
        FAIL() << "no run containing 后; body runs=" << body->geo.runs.size()
               << " line runs=" << line->geo.runs.size() << "\n";
    }
    const float chip_right = chip->geo.world_x + chip->geo.width;
    // Following CJK must start at/after the chip's border edge (small float slack).
    if (after_x + 0.5f < chip_right) {
        FAIL() << "后 overlaps chip: after_x=" << after_x << " chip=[" << chip->geo.world_x
               << "," << chip_right << "] chip.w=" << chip->geo.width
               << " line.runs=" << line->geo.runs.size() << "\n";
    }

    // The chip's alphabetic baseline is the same line as the surrounding CJK.
    // Half-leading on the chip's own strut used to drop the mono glyphs.
    auto baseline_of = [](const zui::layout::Box* box, const zui::layout::TextRun& run) {
        return box->geo.world_y + run.y + run.ascent;
    };
    float cjk_base = std::nanf("");
    for (const auto& run : cjk_box->geo.runs) {
        if (run.text.find("\xe5\x89\x8d") != std::string::npos) {  // 前
            cjk_base = baseline_of(cjk_box, run);
            break;
        }
    }
    float chip_base = std::nanf("");
    for (const auto& run : chip->geo.runs) {
        if (!run.text.empty()) {
            chip_base = baseline_of(chip, run);
            break;
        }
    }
    if (!(cjk_base == cjk_base) || !(chip_base == chip_base)) {
        FAIL() << "missing baselines cjk=" << cjk_base << " chip=" << chip_base << "\n";
    } else if (std::fabs(cjk_base - chip_base) > 0.5f) {
        const auto& cr = chip->geo.runs.empty() ? chip->geo.runs.front() : chip->geo.runs.front();
        (void)cr;
        FAIL() << "code baseline " << chip_base << " != CJK baseline " << cjk_base
               << " chip.y=" << chip->geo.world_y << " chip.h=" << chip->geo.height
               << " chip.run0 y=" << (chip->geo.runs.empty() ? -1.f : chip->geo.runs.front().y)
               << " ascent=" << (chip->geo.runs.empty() ? -1.f : chip->geo.runs.front().ascent)
               << " cjk.y=" << cjk_box->geo.world_y
               << " cjk.run0 y=" << (cjk_box->geo.runs.empty() ? -1.f : cjk_box->geo.runs.front().y)
               << " ascent=" << (cjk_box->geo.runs.empty() ? -1.f : cjk_box->geo.runs.front().ascent)
               << " pad.t=" << chip->geo.padding.top
               << " place.b=" << chip->placement.cb_offset.block_offset
               << " chip.base=" << chip->geo.baseline
               << " cjk.base=" << cjk_box->geo.baseline
               << "\n";
    }
}

TEST(LayoutFlow, NestedZeroChromeSpanDoesNotForceWrap) {
    // A selection highlight is `display:inline` with no padding. Wrapping most of a
    // one-line paragraph in it must keep the prefix on that same line (CSS 2.1 §9.4.2).
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/inline_highlight_wrap.html", doc,
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
    zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());

    auto* p = box_of(layout, doc, "p");
    auto* chip = box_of(layout, doc, "chip");
    if (p == nullptr) {
        FAIL() << "missing #p\n";
    }
    if (p->geo.height > 40.f) {
        FAIL() << "highlight span forced a second line: p.h=" << p->geo.height << "\n";
    }
    if (chip == nullptr || chip->geo.width < 8.f) {
        FAIL() << "missing padded chip inside the highlight\n";
    }
}
