#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.5f; }

float origin_x(const zui::layout::Box& p) {
    return p.geo.world_x + p.geo.border.left + p.geo.padding.left - p.geo.scroll_x;
}

float origin_y(const zui::layout::Box& p) {
    return p.geo.world_y + p.geo.border.top + p.geo.padding.top - p.geo.scroll_y;
}

// `geo.x` / `geo.y` hold the border-box start inside the padding box of the box's *containing
// block*, which is not always the DOM parent: for these absolutely positioned boxes it is the
// initial containing block (the root). A translate shift has to be measured against that frame.
// A browser puts #pct at (100, 110) and #px at (18, 16) - measured with the box-compare harness -
// which is exactly what the frame below yields.
const zui::layout::Box& containing_block(const zui::layout::Box& box) {
    return box.abs_containing_block != nullptr ? *box.abs_containing_block : *box.parent;
}

float shift_x(const zui::layout::Box& box) {
    return box.geo.world_x - (containing_block(box).geo.world_x + box.geo.x);
}

float shift_y(const zui::layout::Box& box) {
    return box.geo.world_y - (containing_block(box).geo.world_y + box.geo.y);
}

}  // namespace

TEST(LayoutTranslate, PercentAndUsed) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/translate.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{400.0f, 300.0f};
    layout.layout(doc, viewport);

    zui::html::Node* pct_n = zui::html::find(doc, "pct");
    zui::html::Node* kid_n = zui::html::find(doc, "kid");
    zui::html::Node* px_n = zui::html::find(doc, "px");
    zui::layout::Box* pct = layout.principal_box(pct_n);
    zui::layout::Box* kid = layout.principal_box(kid_n);
    zui::layout::Box* px = layout.principal_box(px_n);
    if (pct == nullptr || kid == nullptr || px == nullptr || pct->parent == nullptr ||
        px->parent == nullptr) {
        FAIL() << "missing boxes\n";
    }
    if (!near(pct->geo.width, 200.0f) || !near(pct->geo.height, 100.0f)) {
        FAIL() << "pct border box " << pct->geo.width << "x" << pct->geo.height << "\n";
    }
    if (!near(shift_x(*pct), 0.5f * pct->geo.width) ||
        !near(shift_y(*pct), 1.1f * pct->geo.height)) {
        FAIL() << "percent translate shift " << shift_x(*pct) << "," << shift_y(*pct) << "\n";
    }
    if (!near(kid->geo.world_y, origin_y(*pct) + kid->geo.y)) {
        FAIL() << "child did not follow percent translate: kid.y=" << kid->geo.world_y
               << " parent content=" << origin_y(*pct) << "\n";
    }
    if (!near(px->geo.width, 40.0f) || !near(px->geo.height, 30.0f) || !near(shift_x(*px), 8.0f) ||
        !near(shift_y(*px), -4.0f)) {
        FAIL() << "px translate shift " << shift_x(*px) << "," << shift_y(*px) << " box "
               << px->geo.width << "x" << px->geo.height << "\n";
    }

    const float layout_x = pct->geo.x;
    const float layout_y = pct->geo.y;
    const float hit_x = pct->geo.world_x + pct->geo.width * 0.5f;
    const float hit_y = pct->geo.world_y + pct->geo.height * 0.5f;
    if (layout.hit_test(hit_x, hit_y) != pct) {
        FAIL() << "missed translated box at " << hit_x << "," << hit_y << "\n";
    }
    zui::layout::Box* stale = layout.hit_test(origin_x(*pct->parent) + layout_x + 1.0f,
                                              origin_y(*pct->parent) + layout_y + 1.0f);
    if (stale == pct || stale == kid) {
        FAIL() << "untranslated corner still hits the shifted box\n";
    }

    pct_n->used.flags |= zui::html::VisualUsed::kTransform;
    pct_n->used.tx = 0;
    pct_n->used.ty = 0;
    pct_n->used.tx_pct = false;
    pct_n->used.ty_pct = false;
    layout.sync_world();
    if (!near(shift_x(*pct), 0.0f) || !near(shift_y(*pct), 0.0f) || !near(pct->geo.x, layout_x) ||
        !near(pct->geo.y, layout_y)) {
        FAIL() << "used identity did not replace stylesheet translate\n";
    }

    pct_n->used.ty = 110.0f;
    pct_n->used.ty_pct = true;
    layout.sync_world();
    if (!near(shift_y(*pct), 1.1f * pct->geo.height) || !near(pct->geo.y, layout_y)) {
        FAIL() << "used percent overlay shift " << shift_y(*pct) << "\n";
    }
    if (!near(kid->geo.world_y, origin_y(*pct) + kid->geo.y)) {
        FAIL() << "child missed used overlay\n";
    }
}

// CSS Transforms 1 section 4: the function list is applied about `transform-origin`, whose initial
// value is `50% 50%`. Rotated *painting* is the documented subset, but the used origin is what the
// engine folds into the box's world origin, so a 100x60 box rotated 90 degrees about `(cx, cy)`
// starts at `(cx + cy, cy - cx)` - one value per named origin, and `turn` / `grad` have to reach
// the same angle `deg` does.
TEST(LayoutTranslate, TransformOriginAndAngleUnits) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transform_origin.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{400.0f, 900.0f});

    // `(cx, cy)` per case, for the 100x60 box the fixture gives every id.
    struct Case {
        const char* id;
        float cx;
        float cy;
    };
    const Case cases[] = {
        {"tl", 0.0f, 0.0f},      // `left top`
        {"tr", 100.0f, 0.0f},    // `right top`
        {"br", 100.0f, 60.0f},   // `right bottom`
        {"bottom", 50.0f, 60.0f},  // a single vertical keyword leaves x at `center`
        {"turn", 100.0f, 0.0f},    // `0.25turn` is 90 degrees
        {"grad", 100.0f, 0.0f},    // `100grad` is 90 degrees
        {"pct", 100.0f, 0.0f},     // `100% 0`
    };
    for (const Case& c : cases) {
        zui::html::Node* node = zui::html::find(doc, c.id);
        if (node == nullptr) {
            FAIL() << "fixture is missing #" << c.id << "\n";
            continue;
        }
        zui::layout::Box* box = layout.principal_box(node);
        if (box == nullptr || box->parent == nullptr) {
            FAIL() << "#" << c.id << " has no box\n";
            continue;
        }
        const float expected_x = c.cx + c.cy;
        const float expected_y = c.cy - c.cx;
        if (!near(shift_x(*box), expected_x) || !near(shift_y(*box), expected_y)) {
            FAIL() << "#" << c.id << " origin shift " << shift_x(*box) << "," << shift_y(*box)
                   << " expected " << expected_x << "," << expected_y << "\n";
        }
    }
}
