#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/request.h"
#include "layout/engine.h"

#include "log/log.h"

#include <cmath>
#include <string>
#include <vector>

TEST(LayoutFlex, AutoHeight) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_auto_height.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});

    auto* sidebar = layout.principal_box(zui::html::find(doc, "sidebar"));
    auto* content = layout.principal_box(zui::html::find(doc, "content"));
    auto* grid = layout.principal_box(zui::html::find(doc, "grid"));
    auto* head = layout.principal_box(zui::html::find(doc, "head"));
    auto* row0 = layout.principal_box(zui::html::find(doc, "row0"));
    if (sidebar == nullptr || content == nullptr || grid == nullptr || head == nullptr ||
        row0 == nullptr) {
        FAIL() << "missing boxes\n";
    }

    ZUI_FILE_INFO("sidebar {}x{} world=({},{})", sidebar->geo.width, sidebar->geo.height,
                  sidebar->geo.world_x, sidebar->geo.world_y);
    ZUI_FILE_INFO("content {}x{} world=({},{})", content->geo.width, content->geo.height,
                  content->geo.world_x, content->geo.world_y);
    ZUI_FILE_INFO("grid {}x{} content={}x{}", grid->geo.width, grid->geo.height,
                  grid->geo.content_width, grid->geo.content_height);
    ZUI_FILE_INFO("content {}x{}", content->geo.content_width, content->geo.content_height);
    ZUI_FILE_INFO("head {}x{} world_y={}", head->geo.width, head->geo.height, head->geo.world_y);
    ZUI_FILE_INFO("row0 {}x{} world_y={}", row0->geo.width, row0->geo.height, row0->geo.world_y);

    if (std::fabs(sidebar->geo.width - 180.0f) > 1.0f) {
        FAIL() << "sidebar crushed: " << sidebar->geo.width << "\n";
    }
    if (content->geo.world_x + 1.0f < sidebar->geo.world_x + sidebar->geo.width) {
        FAIL() << "content overlaps sidebar: content.x=" << content->geo.world_x
               << " sidebar.right=" << (sidebar->geo.world_x + sidebar->geo.width) << "\n";
    }
    if (head->geo.height > 80.0f) {
        FAIL() << "flex height:auto filled the 360px grid: head.h=" << head->geo.height << "\n";
    }
    if (row0->geo.world_y < head->geo.world_y + head->geo.height - 1.0f) {
        FAIL() << "row overlaps head\n";
    }
    auto* tall = layout.principal_box(zui::html::find(doc, "tall"));
    if (tall == nullptr) {
        FAIL() << "missing tall box\n";
    }
    if (row0->children.empty()) {
        FAIL() << "row0 has no cell boxes\n";
    }
    // `.row` is a block-level `display: flex` container, so its own border box is as wide as the
    // grid's content box (605px at 800x600) - the browser reports the same (measured with the
    // box-compare harness). The 20 `flex-wrap: nowrap` cells are what overflows, so the overflow
    // has to be read off the last cell, not off the container's width.
    zui::layout::Box* last_cell = row0->children.back();
    const float last_right = last_cell->geo.world_x + last_cell->geo.width;
    const float row_right = row0->geo.world_x + row0->geo.width;
    if (last_right <= row_right + 1.0f) {
        FAIL() << "nowrap row did not overflow the grid: last cell right=" << last_right
               << " row right=" << row_right << " cell w=" << last_cell->geo.width << "\n";
    }
    if (tall->geo.height < 1900.0f) {
        FAIL() << "tall child was crushed: height=" << tall->geo.height << "\n";
    }
    const float row_overflow = last_right - row0->geo.world_x;
    if (grid->geo.width + 2.0f >= row_overflow) {
        FAIL() << "grid grew to the nowrap row: grid width=" << grid->geo.width
               << " row content=" << row_overflow << "\n";
    }
    if (grid->geo.height + 2.0f >= tall->geo.height) {
        FAIL() << "grid grew to the tall child: grid height=" << grid->geo.height
               << " tall=" << tall->geo.height << "\n";
    }
    if (content->geo.width + 2.0f >= row_overflow) {
        FAIL() << "outer box grew to the grid row: content width=" << content->geo.width
               << " row content=" << row_overflow << "\n";
    }
    if (content->geo.height + 2.0f >= tall->geo.height) {
        FAIL() << "outer box grew to the tall child: content height=" << content->geo.height
               << " tall=" << tall->geo.height << "\n";
    }
}

TEST(LayoutFlex, MinContentAsk) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_auto_height.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.0f, 600.0f};
    layout.layout(doc, viewport);

    auto* sidebar = layout.principal_box(zui::html::find(doc, "sidebar"));
    if (sidebar == nullptr || sidebar->parent == nullptr) {
        FAIL() << "missing sidebar/workspace\n";
    }
    zui::layout::Box* workspace = sidebar->parent;
    if (workspace->cache == nullptr || workspace->cache->pinned_final() == nullptr) {
        FAIL() << "workspace has no pinned final result\n";
    }
    bool saw_flex_base = false;
    bool saw_min_content = false;
    bool saw_final = false;
    for (const auto& req : workspace->cache->pinned_final()->used_requests) {
        switch (req.child_key.space.request.kind) {
            case zui::layout::RequestKind::FlexBase:
                saw_flex_base = true;
                break;
            case zui::layout::RequestKind::MinContent:
                saw_min_content = true;
                break;
            case zui::layout::RequestKind::Final:
                saw_final = true;
                break;
            default:
                break;
        }
    }
    if (!saw_flex_base) {
        FAIL() << "flex used_requests missing FlexBase\n";
    }
    if (!saw_min_content) {
        FAIL() << "flex used_requests missing MinContent (auto min-size path)\n";
    }
    if (!saw_final) {
        FAIL() << "flex used_requests missing Final\n";
    }
}

TEST(LayoutFlex, MinContentDiffersFromFlexBase) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_min_content.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});

    auto* item = layout.principal_box(zui::html::find(doc, "item"));
    auto* clipped = layout.principal_box(zui::html::find(doc, "clipped"));
    if (item == nullptr || item->parent == nullptr || clipped == nullptr ||
        clipped->parent == nullptr) {
        FAIL() << "missing item/clipped boxes\n";
    }
    zui::layout::Box* row = item->parent;
    zui::layout::Box* row_clip = clipped->parent;
    if (row->cache == nullptr || row->cache->pinned_final() == nullptr ||
        row_clip->cache == nullptr || row_clip->cache->pinned_final() == nullptr) {
        FAIL() << "row has no pinned final result\n";
    }

    float flex_base = -1.f;
    float min_content = -1.f;
    bool saw_min_content = false;
    for (const auto& req : row->cache->pinned_final()->used_requests) {
        if (req.child != item) {
            continue;
        }
        switch (req.child_key.space.request.kind) {
            case zui::layout::RequestKind::FlexBase:
                flex_base = req.snapshot.geometry.border_box_inline;
                break;
            case zui::layout::RequestKind::MinContent:
                min_content = req.snapshot.geometry.border_box_inline;
                saw_min_content = true;
                break;
            default:
                break;
        }
    }
    if (!saw_min_content) {
        FAIL() << "visible overflow item missing MinContent ask\n";
    }
    if (flex_base < 0.f || min_content < 0.f) {
        FAIL() << "missing FlexBase/MinContent geometry flex_base=" << flex_base
               << " min_content=" << min_content << "\n";
    }
    ZUI_FILE_INFO("item FlexBase={} MinContent={}", flex_base, min_content);
    // Longest unbreakable word is much narrower than the full unwrapped phrase.
    if (!(min_content + 8.0f < flex_base)) {
        FAIL() << "MinContent should be clearly smaller than FlexBase: min=" << min_content
               << " base=" << flex_base << "\n";
    }
    // Automatic min size floors the used width under flex shrink in a narrow row.
    if (item->geo.width + 1.0f < min_content) {
        FAIL() << "item shrunk below MinContent: used=" << item->geo.width << " min=" << min_content
               << "\n";
    }

    bool clipped_min = false;
    for (const auto& req : row_clip->cache->pinned_final()->used_requests) {
        if (req.child == clipped &&
            req.child_key.space.request.kind == zui::layout::RequestKind::MinContent) {
            clipped_min = true;
            break;
        }
    }
    if (clipped_min) {
        FAIL() << "overflow:hidden item should not receive MinContent ask\n";
    }
}

// B2: overflow:hidden items skip MinContent; the second measure ask is FlexHypothetical at the
// clamped main size (flex-basis 40px floored by min-width 100px).
TEST(LayoutFlex, FlexHypotheticalAsk) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_hypothetical.html", doc,
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
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});

    auto* item = layout.principal_box(zui::html::find(doc, "item"));
    if (item == nullptr || item->parent == nullptr) {
        FAIL() << "missing item\n";
    }
    zui::layout::Box* row = item->parent;
    if (row->cache == nullptr || row->cache->pinned_final() == nullptr) {
        FAIL() << "row has no pinned final result\n";
    }

    float flex_base = -1.f;
    float flex_hypo = -1.f;
    float hypo_avail = -1.f;
    bool saw_min = false;
    for (const auto& req : row->cache->pinned_final()->used_requests) {
        if (req.child != item) {
            continue;
        }
        switch (req.child_key.space.request.kind) {
            case zui::layout::RequestKind::FlexBase:
                flex_base = req.snapshot.geometry.border_box_inline;
                break;
            case zui::layout::RequestKind::FlexHypothetical:
                flex_hypo = req.snapshot.geometry.border_box_inline;
                if (req.child_key.space.available.inline_size.definite) {
                    hypo_avail = req.child_key.space.available.inline_size.px;
                }
                break;
            case zui::layout::RequestKind::MinContent:
                saw_min = true;
                break;
            default:
                break;
        }
    }
    if (saw_min) {
        FAIL() << "overflow:hidden item must not get MinContent (slot reserved for FlexHypothetical)\n";
    }
    if (flex_base < 0.f || flex_hypo < 0.f) {
        FAIL() << "missing FlexBase/FlexHypothetical geometry base=" << flex_base
               << " hypo=" << flex_hypo << "\n";
    }
    ZUI_FILE_INFO("item FlexBase={} FlexHypothetical={} hypo_avail={} used={}", flex_base, flex_hypo,
                  hypo_avail, item->geo.width);
    // flex-basis 40px + min-width 100px → hypothetical main size 100 before grow in a 300px row.
    if (hypo_avail < 99.f || hypo_avail > 101.f) {
        FAIL() << "FlexHypothetical ask available inline should be the clamped hypo (~100): "
               << hypo_avail << "\n";
    }
    // Grow fills the row; used width stays within max-width 180.
    if (item->geo.width + 1.0f < 100.f) {
        FAIL() << "item used width below min-width floor: " << item->geo.width << "\n";
    }
    if (item->geo.width > 181.f) {
        FAIL() << "item used width above max-width: " << item->geo.width << "\n";
    }
}

// A column container's cross axis is its inline axis, and a stretched item with an auto inline
// size is measured at the container's inner inline size (Flexbox section 9.4 steps 9-10). This
// fixture's container resolves to the body's width, so both asks see the same definite width: the
// phrase does not wrap in either and the two block sizes agree. They differ only with an indefinite
// cross size, which is what the MinContent ask exists for in the first place.
TEST(LayoutFlex, ColumnMinContentAtDefiniteCrossSize) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_min_content.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});

    auto* item = layout.principal_box(zui::html::find(doc, "col-item"));
    if (item == nullptr || item->parent == nullptr) {
        FAIL() << "missing col-item\n";
    }
    zui::layout::Box* col = item->parent;
    if (col->cache == nullptr || col->cache->pinned_final() == nullptr) {
        FAIL() << "col has no pinned final result\n";
    }

    float flex_base = -1.f;
    float min_content = -1.f;
    bool saw_min_content = false;
    for (const auto& req : col->cache->pinned_final()->used_requests) {
        if (req.child != item) {
            continue;
        }
        switch (req.child_key.space.request.kind) {
            case zui::layout::RequestKind::FlexBase:
                flex_base = req.snapshot.geometry.border_box_block;
                break;
            case zui::layout::RequestKind::MinContent:
                min_content = req.snapshot.geometry.border_box_block;
                saw_min_content = true;
                if (req.child_key.space.request.axis != zui::layout::Axis::Block) {
                    FAIL() << "column MinContent ask should use Axis::Block\n";
                }
                break;
            default:
                break;
        }
    }
    if (!saw_min_content) {
        FAIL() << "column item missing MinContent ask\n";
    }
    if (flex_base < 0.f || min_content < 0.f) {
        FAIL() << "missing FlexBase/MinContent block geometry flex_base=" << flex_base
               << " min_content=" << min_content << "\n";
    }
    ZUI_FILE_INFO("col-item FlexBase.block={} MinContent.block={}", flex_base, min_content);
    // Both asks measure the item at the container's inner cross size, so the two agree here.
    if (min_content > flex_base + 4.0f || min_content + 4.0f < flex_base) {
        FAIL() << "column MinContent block should match FlexBase at a definite cross size: min="
               << min_content << " base=" << flex_base << "\n";
    }
    if (item->geo.height + 1.0f < min_content) {
        FAIL() << "column item shrunk below MinContent: used=" << item->geo.height
               << " min=" << min_content << "\n";
    }
}

TEST(LayoutFlex, ContainerMinContentNowrapSumVsWrapMax) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_min_content.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});

    auto* nowrap_box = layout.principal_box(zui::html::find(doc, "inner-nowrap"));
    auto* wrap_box = layout.principal_box(zui::html::find(doc, "inner-wrap"));
    if (nowrap_box == nullptr || wrap_box == nullptr || nowrap_box->parent == nullptr) {
        FAIL() << "missing inner nowrap/wrap boxes\n";
    }
    zui::layout::Box* probe = nowrap_box->parent;
    if (probe->cache == nullptr || probe->cache->pinned_final() == nullptr) {
        FAIL() << "probe has no pinned final result\n";
    }

    float nowrap_min = -1.f;
    float wrap_min = -1.f;
    for (const auto& req : probe->cache->pinned_final()->used_requests) {
        if (req.child_key.space.request.kind != zui::layout::RequestKind::MinContent) {
            continue;
        }
        if (req.child == nowrap_box) {
            nowrap_min = req.snapshot.geometry.border_box_inline;
        } else if (req.child == wrap_box) {
            wrap_min = req.snapshot.geometry.border_box_inline;
        }
    }
    if (nowrap_min < 0.f || wrap_min < 0.f) {
        FAIL() << "missing container MinContent asks nowrap=" << nowrap_min << " wrap=" << wrap_min
               << "\n";
    }
    ZUI_FILE_INFO("container MinContent nowrap={} wrap={}", nowrap_min, wrap_min);
    // Fixed 100px + 80px + 10px gap. Nowrap must sum; wrap must be the largest item (+ no gap
    // between lines when each item is alone). A naive "always sum" algorithm fails this case.
    const float sum_expect = 100.0f + 80.0f + 10.0f;
    const float max_expect = 100.0f;
    if (std::fabs(nowrap_min - sum_expect) > 2.0f) {
        FAIL() << "nowrap MinContent should be sum of items+gap: got=" << nowrap_min
               << " expect~=" << sum_expect << "\n";
    }
    if (std::fabs(wrap_min - max_expect) > 2.0f) {
        FAIL() << "wrap MinContent should be max item, not sum: got=" << wrap_min
               << " expect~=" << max_expect << "\n";
    }
    if (!(wrap_min + 20.0f < nowrap_min)) {
        FAIL() << "wrap MinContent must be clearly smaller than nowrap sum\n";
    }
}

/// `flex: 1 1 calc(100% - 64px)` is the reader's narrow search. The basis has to be that
/// calc, so the search wraps onto its own row instead of shrinking to 1px beside the brand.
/// The resume control is a flex button with `width: calc(100% - 32px)` and has to fill the
/// shelf. `repeat(auto-fill, minmax(132px, 1fr))` at 400px fits two tracks, and the card
/// button stretches to its track.
TEST(LayoutFlex, CalcBasisWrapsAndPercentageWidth) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_calc_basis.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    const zui::html::Node* finder_node = zui::html::find(doc, "finder");
    const std::string* basis = finder_node != nullptr ? finder_node->style("flex-basis") : nullptr;
    if (basis == nullptr || basis->find("calc(100% - 64px)") == std::string::npos) {
        FAIL() << "flex-basis lost the calc: " << (basis != nullptr ? *basis : std::string("<none>"))
               << "\n";
    }
    const zui::html::Node* resume_node = zui::html::find(doc, "resume");
    const std::string* resume_width =
        resume_node != nullptr ? resume_node->style("width") : nullptr;
    if (resume_width == nullptr || resume_width->find("calc(100% - 32px)") == std::string::npos) {
        FAIL() << "resume width: " << (resume_width != nullptr ? *resume_width : std::string("<none>"))
               << "\n";
    }
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{400.0f, 800.0f};
    layout.layout(doc, viewport);
    layout.layout(doc, viewport);

    auto box_of = [&](const char* id) -> zui::layout::Box* {
        zui::layout::Box* box = layout.principal_box(zui::html::find(doc, id));
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
        }
        return box;
    };
    zui::layout::Box* brand = box_of("brand");
    zui::layout::Box* finder = box_of("finder");
    zui::layout::Box* theme = box_of("theme");
    zui::layout::Box* resume = box_of("resume");
    zui::layout::Box* books = box_of("books");
    zui::layout::Box* wrap = box_of("wrap");
    zui::layout::Box* card = box_of("card");
    zui::layout::Box* wrap2 = box_of("wrap2");
    if (brand == nullptr || finder == nullptr || theme == nullptr || resume == nullptr ||
        books == nullptr || wrap == nullptr || card == nullptr || wrap2 == nullptr) {
        return;
    }
    ZUI_FILE_INFO(
        "brand {}x{} @{} finder {}x{} @{} theme {} @{} resume {} books {} wrap {} @{} card {} "
        "wrap2 {} @{}",
        brand->geo.width, brand->geo.height, brand->geo.world_y, finder->geo.width,
        finder->geo.height, finder->geo.world_y, theme->geo.width, theme->geo.world_y,
        resume->geo.width, books->geo.width, wrap->geo.width, wrap->geo.world_y, card->geo.width,
        wrap2->geo.width, wrap2->geo.world_y);
    if (finder->geo.world_y <= brand->geo.world_y + 1.0f) {
        FAIL() << "search stayed on the brand row, y=" << finder->geo.world_y << "\n";
    }
    if (finder->geo.width < 300.0f) {
        FAIL() << "search did not take the calc basis: " << finder->geo.width << "\n";
    }
    if (theme->geo.world_y <= finder->geo.world_y + 1.0f) {
        FAIL() << "theme stayed on the search row, y=" << theme->geo.world_y << "\n";
    }
    if (resume->geo.width < 340.0f) {
        FAIL() << "resume calc width collapsed: " << resume->geo.width << "\n";
    }
    if (books->geo.width < 360.0f) {
        FAIL() << "books grid did not fill the shelf: " << books->geo.width << "\n";
    }
    if (std::fabs(wrap->geo.world_y - wrap2->geo.world_y) > 1.0f) {
        FAIL() << "auto-fill stacked the cards: y " << wrap->geo.world_y << " / "
               << wrap2->geo.world_y << "\n";
    }
    if (wrap->geo.width < 150.0f || wrap->geo.width > 250.0f) {
        FAIL() << "track width " << wrap->geo.width << " is not one of two 1fr columns\n";
    }
    if (std::fabs(card->geo.width - wrap->geo.width) > 2.0f) {
        FAIL() << "card button did not stretch to the track: card " << card->geo.width
               << " wrap " << wrap->geo.width << "\n";
    }
}

TEST(LayoutFlex, ButtonShrinksToFit) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/button_fit.html", doc, error)) {
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
    layout.layout(doc, zui::layout::Viewport{1000.0f, 800.0f});

    auto width_of = [&](const char* id) {
        zui::layout::Box* box = layout.principal_box(zui::html::find(doc, id));
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
            return 0.0f;
        }
        return box->geo.width;
    };
    const float flex_btn = width_of("flex_btn");
    const float flex_div = width_of("flex_div");
    const float full_btn = width_of("full_btn");
    const float block_btn = width_of("block_btn");
    const float block_div = width_of("block_div");
    const float item_btn = width_of("item_btn");
    ZUI_FILE_INFO("flex_btn={} flex_div={} full_btn={} block_btn={} block_div={} item_btn={}",
                  flex_btn, flex_div, full_btn, block_btn, block_div, item_btn);
    if (!(flex_div > 700.0f)) {
        FAIL() << "flex div should stretch: " << flex_div << "\n";
    }
    if (!(flex_btn > 40.0f && flex_btn < 700.0f)) {
        FAIL() << "flex button should shrink to its content: " << flex_btn << "\n";
    }
    zui::layout::Box* where = layout.principal_box(zui::html::find(doc, "flex_where"));
    zui::layout::Box* button = layout.principal_box(zui::html::find(doc, "flex_btn"));
    if (where == nullptr || button == nullptr) {
        FAIL() << "missing flex button children\n";
    } else {
        const float where_right = where->geo.world_x + where->geo.width;
        const float button_right = button->geo.world_x + button->geo.width;
        if (where->geo.width < 40.0f || where_right > button_right + 1.0f) {
            FAIL() << "flex:1 column spilled out of the button: where=" << where->geo.width
                   << " right " << where_right << " button right " << button_right << "\n";
        }
    }
    if (!(full_btn > 700.0f)) {
        FAIL() << "width:100% button should still stretch: " << full_btn << "\n";
    }
    if (!(block_div > 700.0f)) {
        FAIL() << "block div should stretch: " << block_div << "\n";
    }
    if (!(block_btn > 20.0f && block_btn < 360.0f)) {
        FAIL() << "block button should shrink to its content: " << block_btn << "\n";
    }
    if (std::fabs(item_btn - 500.0f) > 2.0f) {
        FAIL() << "flex item button should stretch to the column: " << item_btn << "\n";
    }
}


/// CSS Flexbox section 8.1 "Aligning with auto margins": positive free space goes to the auto
/// margins before justify-content / align-self, so `margin-left: auto` pushes an item to the end
/// and a pair of auto margins centers it. `example/applications/calendar`'s `.top-actions` is the
/// page that found this missing: it sat at the position its content ended.
TEST(LayoutFlex, AutoMargins) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_auto_margin.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{1000.0f, 600.0f};
    layout.layout(doc, viewport);
    layout.layout(doc, viewport);

    auto* bar = layout.principal_box(zui::html::find(doc, "bar"));
    auto* lead = layout.principal_box(zui::html::find(doc, "lead"));
    auto* push = layout.principal_box(zui::html::find(doc, "push"));
    auto* push2 = layout.principal_box(zui::html::find(doc, "push2"));
    auto* center_row = layout.principal_box(zui::html::find(doc, "center-row"));
    auto* cbox = layout.principal_box(zui::html::find(doc, "cbox"));
    auto* center_col = layout.principal_box(zui::html::find(doc, "center-col"));
    auto* vbox = layout.principal_box(zui::html::find(doc, "vbox"));
    if (bar == nullptr || lead == nullptr || push == nullptr || push2 == nullptr ||
        center_row == nullptr || cbox == nullptr || center_col == nullptr || vbox == nullptr) {
        FAIL() << "missing boxes\n";
    }
    ZUI_FILE_INFO("lead {} push {} push2 {} cbox {},{} {}x{} vbox {} {}x{}", lead->geo.world_x,
                  push->geo.world_x, push2->geo.world_x, cbox->geo.world_x, cbox->geo.world_y,
                  cbox->geo.width, cbox->geo.height, vbox->geo.world_y, vbox->geo.width,
                  vbox->geo.height);

    // 1000px row: 100 + 100 + 200 of content leaves 600px of free space for two auto margins,
    // so each takes 300. `justify-content: flex-end` must NOT add anything on top.
    if (std::fabs(lead->geo.world_x - 0.0f) > 1.0f) {
        FAIL() << "lead moved: " << lead->geo.world_x << "\n";
    }
    if (std::fabs(push->geo.world_x - 400.0f) > 1.0f) {
        FAIL() << "push x: " << push->geo.world_x << " want 400\n";
    }
    if (std::fabs((push2->geo.world_x + push2->geo.width) - 1000.0f) > 1.0f) {
        FAIL() << "push2 right edge: " << (push2->geo.world_x + push2->geo.width) << " want 1000\n";
    }

    // `margin: auto` centers on both axes and, per CSS Box Alignment section 5.2, defeats
    // `align-items: stretch` - the box keeps its 50px height instead of filling the 100px line.
    if (std::fabs(cbox->geo.width - 50.0f) > 1.0f || std::fabs(cbox->geo.height - 50.0f) > 1.0f) {
        FAIL() << "cbox stretched: " << cbox->geo.width << "x" << cbox->geo.height << "\n";
    }
    if (std::fabs(cbox->geo.world_x - (center_row->geo.world_x + 175.0f)) > 1.0f) {
        FAIL() << "cbox x: " << cbox->geo.world_x << "\n";
    }
    if (std::fabs(cbox->geo.world_y - (center_row->geo.world_y + 25.0f)) > 1.0f) {
        FAIL() << "cbox y: " << cbox->geo.world_y << "\n";
    }

    // A column container centers the same way on its main (block) axis.
    if (std::fabs(vbox->geo.world_y - (center_col->geo.world_y + 85.0f)) > 1.5f) {
        FAIL() << "vbox y: " << vbox->geo.world_y << " col y " << center_col->geo.world_y << "\n";
    }
}

/// A flex item's text used to answer every Final ask with its max-content advance, so the
/// container's min-content target (one glyph, or one word) never matched and the two-round
/// ceiling fired. The published box is still the max-content size: the long label stays
/// wider than the one-letter label, and the pass converges.
TEST(LayoutFlex, TextItemFinalMatchesTarget) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/flex_text_item.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.0f, 200.0f};
    layout.layout(doc, viewport);
    layout.layout(doc, viewport);

    if (!layout.non_convergence().empty()) {
        FAIL() << "flex text item did not converge: " << layout.non_convergence().front().reason
               << "\n";
    }
    auto* long_label = layout.principal_box(zui::html::find(doc, "long"));
    auto* short_label = layout.principal_box(zui::html::find(doc, "short"));
    if (long_label == nullptr || short_label == nullptr) {
        FAIL() << "missing labels\n";
    }
    ZUI_FILE_INFO("long {} short {}", long_label->geo.width, short_label->geo.width);
    // "Import photos" at 15px plus 24px of padding and 2px of border is well past 80.
    // A min-content pin would collapse it toward the longest word, still above the
    // one-letter label, so the gap has to be the full phrase.
    if (long_label->geo.width < 80.0f) {
        FAIL() << "long label shrank to " << long_label->geo.width << "\n";
    }
    if (long_label->geo.width < short_label->geo.width + 40.0f) {
        FAIL() << "long " << long_label->geo.width << " short " << short_label->geo.width << "\n";
    }
}

/// Auto-inset abspos keeps the static position. A flex container adds the used margin
/// (`margin: -1px` → -1 at the padding edge of `#app`, and 2 inside `#toggle`'s 3px
/// padding). A grid with `place-items: center` aligns that 1px box in the padding box:
/// 17.5px into a 36×32 button, plus the button's place in the padded flex row.
TEST(LayoutFlex, AbsStaticMarginAndGridCenter) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/abs_static_place.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{900.0f, 400.0f};
    layout.layout(doc, viewport);
    layout.layout(doc, viewport);

    auto* gone = layout.principal_box(zui::html::find(doc, "gone"));
    auto* hid = layout.principal_box(zui::html::find(doc, "hid"));
    auto* sr = layout.principal_box(zui::html::find(doc, "sr"));
    auto* sr2 = layout.principal_box(zui::html::find(doc, "sr2"));
    auto* file = layout.principal_box(zui::html::find(doc, "file"));
    auto* side = layout.principal_box(zui::html::find(doc, "side"));
    auto* screen = layout.principal_box(zui::html::find(doc, "screen"));
    auto* padabs = layout.principal_box(zui::html::find(doc, "padabs"));
    auto* bar = layout.principal_box(zui::html::find(doc, "bar"));
    auto* fit = layout.principal_box(zui::html::find(doc, "fit"));
    auto* fitfile = layout.principal_box(zui::html::find(doc, "fitfile"));
    if (gone == nullptr || hid == nullptr || sr == nullptr || sr2 == nullptr || file == nullptr ||
        side == nullptr || screen == nullptr || padabs == nullptr || bar == nullptr ||
        fit == nullptr || fitfile == nullptr) {
        FAIL() << "missing boxes\n";
    }
    ZUI_FILE_INFO("gone {},{} hid {},{} sr {},{} sr2 {},{}",
                  gone->placement.cb_offset.inline_offset, gone->placement.cb_offset.block_offset,
                  hid->placement.cb_offset.inline_offset, hid->placement.cb_offset.block_offset,
                  sr->placement.cb_offset.inline_offset, sr->placement.cb_offset.block_offset,
                  sr2->placement.cb_offset.inline_offset, sr2->placement.cb_offset.block_offset);

    auto near = [](float actual, float want, const char* name) {
        if (std::fabs(actual - want) > 0.6f) {
            ADD_FAILURE() << name << " " << actual << " want " << want << "\n";
        }
    };
    near(gone->placement.cb_offset.inline_offset, -1.0f, "gone x");
    near(gone->placement.cb_offset.block_offset, -1.0f, "gone y");
    near(hid->placement.cb_offset.inline_offset, 2.0f, "hid x");
    near(hid->placement.cb_offset.block_offset, 2.0f, "hid y");
    near(sr->placement.cb_offset.inline_offset, 20.5f, "sr x");
    near(sr->placement.cb_offset.block_offset, 18.5f, "sr y");
    near(sr2->placement.cb_offset.inline_offset, 56.5f, "sr2 x");
    near(sr2->placement.cb_offset.block_offset, 18.5f, "sr2 y");
    // `.tools` is 199px, `justify-content: flex-end`. The 1px box with margin -1px
    // sits on the content end (199), not at the start (-1). An in-flow button before
    // it must not move that static position.
    near(file->placement.cb_offset.inline_offset, 199.0f, "file x");
    // `#fit` is width:auto, a flex item of `#bar`. Its content box is the button, and
    // the static position has to be recorded after that width exists. The 1px box
    // starts on `#fit`'s right edge (the same 200px gap as zdb `#file-open`).
    if (fit->geo.width < 30.f) {
        ADD_FAILURE() << "fit collapsed: " << fit->geo.width << "\n";
    }
    const float fitfile_want = bar->placement.cb_offset.inline_offset +
                               fit->placement.cb_offset.inline_offset + fit->geo.width;
    near(fitfile->placement.cb_offset.inline_offset, fitfile_want, "fitfile x");
    // Block label inside the flex shell: padding 12px is added once, then margin -1px.
    // `#side` has no padding, so the label's border edge is the side's border edge.
    near(screen->placement.cb_offset.inline_offset,
         side->placement.cb_offset.inline_offset + 11.0f, "screen x");
    near(screen->placement.cb_offset.block_offset,
         side->placement.cb_offset.block_offset + 11.0f, "screen y");
    // The padded box is itself the containing block. Two layouts must agree on one
    // padding, not the previous pin plus this pass.
    near(padabs->placement.cb_offset.inline_offset, 11.0f, "padabs x");
    near(padabs->placement.cb_offset.block_offset, 11.0f, "padabs y");
}

namespace {

bool lay_form_gaps(zui::html::Document& doc, zui::css::Engine& css, zui::layout::LayoutEngine& layout,
                   std::string& error) {
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/form_gaps.html", doc, error)) {
        return false;
    }
    if (!css.load_sheets(doc, error)) {
        return false;
    }
    css.cascade(doc);
    const zui::layout::Viewport viewport{800.0f, 600.0f};
    layout.layout(doc, viewport);
    layout.layout(doc, viewport);
    return true;
}

zui::html::Node* hit_node(zui::layout::LayoutEngine& layout, float x, float y) {
    for (zui::layout::Box* box = layout.hit_test(x, y); box != nullptr; box = box->parent) {
        if (box->node == nullptr) {
            continue;
        }
        if (box->node->tag.size() >= 2 && box->node->tag[0] == ':' && box->node->tag[1] == ':') {
            continue;
        }
        return box->node;
    }
    return nullptr;
}

bool box_center(zui::layout::LayoutEngine& layout, zui::html::Node* node, float& x, float& y) {
    zui::layout::Box* box = node == nullptr ? nullptr : layout.principal_box(node);
    if (box == nullptr || box->geo.width < 1.f || box->geo.height < 1.f) {
        return false;
    }
    x = box->geo.world_x + box->geo.width * 0.5f;
    y = box->geo.world_y + box->geo.height * 0.5f;
    return true;
}

}  // namespace

/// A content-box text field clips overflow, so its automatic minimum is a content
/// size of 0. The border box still cannot be smaller than padding plus border.
/// Asking for 0 and getting the chrome back used to trip the two-round ceiling.
TEST(LayoutFlex, ClippedInputChromeFloor) {
    zui::html::Document doc;
    std::string error;
    zui::css::Engine css;
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    if (!lay_form_gaps(doc, css, layout, error)) {
        FAIL() << error << "\n";
    }
    if (!layout.non_convergence().empty()) {
        FAIL() << "clipped flex input did not converge: "
               << layout.non_convergence().front().reason << "\n";
    }
    auto* zero = layout.principal_box(zui::html::find(doc, "zero"));
    auto* fill = layout.principal_box(zui::html::find(doc, "fill"));
    if (zero == nullptr || fill == nullptr) {
        FAIL() << "missing inputs\n";
    }
    ZUI_FILE_INFO("zero {} fill {}", zero->geo.width, fill->geo.width);
    // UA chrome is 2px border plus 2px padding on each side (8px). The 20-ch
    // preferred width is far larger; the floor must not be that preferred width.
    if (zero->geo.width < 4.f || zero->geo.width > 48.f) {
        FAIL() << "zero width " << zero->geo.width << "\n";
    }
    if (fill->geo.width < 200.f) {
        FAIL() << "fill width " << fill->geo.width << "\n";
    }
}

/// Radio keeps the value attribute (default "on" only when the attribute is
/// absent). Checkbox stays on the frozen true/false payload. A click on
/// `<a href>` opens unless the handler calls prevent_default, and
/// pointer-events inherits so a `none` container is not a hit.
TEST(FormGaps, RadioValuePointerEventsAndLinks) {
    zui::html::Document doc;
    std::string error;
    zui::css::Engine css;
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    if (!lay_form_gaps(doc, css, layout, error)) {
        FAIL() << error << "\n";
    }

    auto* green = zui::html::find(doc, "green");
    auto* clay = zui::html::find(doc, "clay");
    auto* bare = zui::html::find(doc, "bare");
    auto* empty = zui::html::find(doc, "empty");
    auto* box = zui::html::find(doc, "box");
    auto* kid = zui::html::find(doc, "kid");
    auto* live = zui::html::find(doc, "live");
    if (green == nullptr || clay == nullptr || bare == nullptr || empty == nullptr ||
        box == nullptr || kid == nullptr || live == nullptr) {
        FAIL() << "missing controls\n";
    }
    EXPECT_TRUE(green->checked);
    EXPECT_EQ(green->value, "green");
    EXPECT_FALSE(clay->checked);
    EXPECT_EQ(clay->value, "clay");
    EXPECT_EQ(bare->value, "on");
    EXPECT_TRUE(empty->value.empty());
    EXPECT_TRUE(box->checked);
    EXPECT_EQ(box->value, "true");

    const std::string* kid_events = kid->style("pointer-events");
    const std::string* live_events = live->style("pointer-events");
    ASSERT_NE(kid_events, nullptr);
    ASSERT_NE(live_events, nullptr);
    EXPECT_EQ(*kid_events, "none");
    EXPECT_EQ(*live_events, "auto");

    float kx = 0, ky = 0, lx = 0, ly = 0, bx = 0, by = 0;
    ASSERT_TRUE(box_center(layout, kid, kx, ky));
    ASSERT_TRUE(box_center(layout, live, lx, ly));
    ASSERT_TRUE(box_center(layout, zui::html::find(doc, "plain"), bx, by));
    zui::html::Node* kid_hit = hit_node(layout, kx, ky);
    if (kid_hit != nullptr) {
        EXPECT_NE(kid_hit->id, "kid");
        EXPECT_NE(kid_hit->id, "dead");
    }
    zui::html::Node* live_hit = hit_node(layout, lx, ly);
    ASSERT_NE(live_hit, nullptr);
    EXPECT_EQ(live_hit->id, "live");
    zui::html::Node* plain_hit = hit_node(layout, bx, by);
    ASSERT_NE(plain_hit, nullptr);
    EXPECT_EQ(plain_hit->id, "plain");

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    std::vector<std::string> opened;
    zui::input::InteractionHost host;
    host.document = &doc;
    host.layout = &layout;
    host.metrics = &metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.open_uri = [&](std::string_view uri) {
        opened.emplace_back(uri);
        return true;
    };
    host.dispatch = [&](zui::Event& event) {
        if (event.name() == "on_stay") {
            event.prevent_default();
        }
    };
    zui::input::Interaction interaction(std::move(host));
    auto click = [&](zui::html::Node* node) {
        float x = 0;
        float y = 0;
        if (node == nullptr || !box_center(layout, node, x, y)) {
            ADD_FAILURE() << "no box for click\n";
            return;
        }
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };
    click(clay);
    EXPECT_TRUE(clay->checked);
    EXPECT_EQ(clay->value, "clay");
    EXPECT_FALSE(green->checked);
    EXPECT_EQ(green->value, "green");
    click(box);
    EXPECT_FALSE(box->checked);
    EXPECT_EQ(box->value, "false");

    click(zui::html::find(doc, "ext"));
    click(zui::html::find(doc, "stay"));
    click(zui::html::find(doc, "frag"));
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], "mailto:a@b.c");
}
