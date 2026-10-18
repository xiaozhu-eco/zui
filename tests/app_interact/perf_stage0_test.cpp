// docs/PERF_REFACTOR.md section 5, stages 0 and 1: split the four interaction costs apart, lock the
// numbers the current implementation produces, and hold R2 (a non-layout property does not lay
// out). The page is written to the temp directory (no application id, no CSS file in the repo);
// every expectation below is the number this run measured, with the branch it means written next to
// it. Release skips the work-unit ones because FrameWork is zero there (docs/PERF.md section 6.1).

#include "handler_rig.h"
#include "log/log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <cstdint>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Exactly the four levers of the stage-0 table, plus a scroll container: a paint-only class, a
// hover rule, a theme custom property on `html`, a native <select>, and 40 rows in an
// `overflow:auto` list.
const char* kStage0Page =
    "<!DOCTYPE html><html class=\"theme-paper\"><head><style>"
    ":root{--ink:rgb(17,34,51);--paper:rgb(255,255,255)}"
    "html.theme-ink{--ink:rgb(176,48,48)}"
    "body{margin:0;font-size:16px}"
    ".stage{width:600px;height:400px;background:var(--paper)}"
    ".tile{width:120px;height:60px;background-color:rgb(238,238,238);color:var(--ink)}"
    ".tile.on{background-color:rgb(43,108,176)}"
    ".tile.round{border-radius:12px}"
    ".tile.deep.on{box-shadow:inset 0 0 6px rgb(0,0,0)}"
    ".plate{width:80px;height:40px;background-color:var(--ink)}"
    ".btn{width:100px;height:32px;background-color:rgb(221,221,221)}"
    ".btn:hover{background-color:rgb(255,204,0)}"
    "select{width:120px;height:28px}"
    ".list{width:200px;height:120px;overflow:auto}"
    ".item{height:20px;background-color:rgb(250,250,250)}"
    ".far{margin-top:260px;width:60px;height:40px;background-color:rgb(20,20,20)}"
    ".far.on{background-color:rgb(0,90,200)}"
    ".overlay{position:fixed;left:0;top:0;width:80px;height:80px;"
    "background-color:rgb(9,9,9);display:none}"
    ".overlay.show{display:block}"
    "</style></head><body><div class=\"stage\">"
    "<div id=\"tile\" class=\"tile\">t</div>"
    "<div id=\"deep\" class=\"tile deep\">d</div>"
    "<div id=\"plate\" class=\"plate\"></div>"
    "<div id=\"btn\" class=\"btn\">b</div>"
    "<select id=\"pick\"><option>a</option><option>b</option><option>c</option></select>"
    "<div id=\"far\" class=\"far\"></div>"
    "<div id=\"overlay\" class=\"overlay\"></div>"
    "<div id=\"list\" class=\"list\">";

std::string stage0_html() {
    std::string html = kStage0Page;
    for (int i = 0; i < 40; ++i) {
        html += "<div class=\"item\">row " + std::to_string(i) + "</div>";
    }
    html += "</div></div></body></html>\n";
    return html;
}

// One visible leaf and one leaf under `display:none`: the write to the hidden leaf is the capability
// L baseline (a hidden subtree must produce no box-tree work).
const char* kHiddenPage =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0}"
    ".host{display:none}"
    ".leaf{width:10px;height:10px;background-color:rgb(1,2,3)}"
    ".leaf.wide{width:40px}"
    "</style></head><body>"
    "<div id=\"visible\"><div id=\"vleaf\" class=\"leaf\">v</div></div>"
    "<div id=\"host\" class=\"host\"><div id=\"hleaf\" class=\"leaf\">h</div></div>"
    "</body></html>\n";

// Capability G: `#leaf` has a fixed box and `overflow: hidden`, so an inner box that grows *inside*
// it leaves the leaf's own contribution to `#holder` unchanged; one that grows past the leaf does
// not. `#holder` is fixed too, so neither case moves the holder's contribution to the stage.
const char* kContributionPage =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0}"
    ".stage{width:300px;height:100px}"
    ".holder{width:200px;height:24px;overflow:hidden}"
    ".leaf{display:block;width:60px;height:20px;overflow:hidden}"
    ".inner{display:block;width:40px;height:6px;background-color:rgb(1,2,3)}"
    ".inner.wide{width:50px}"
    ".inner.huge{width:120px}"
    "</style></head><body>"
    "<div id=\"stage\" class=\"stage\"><div id=\"holder\" class=\"holder\">"
    "<div id=\"leaf\" class=\"leaf\"><div id=\"inner\" class=\"inner\"></div></div></div></div>"
    "</body></html>\n";

// Capability G, the branch that has to climb: the leaf does *not* clip here, so an inner box that
// grows past the leaf moves the leaf's ink, and the leaf's contribution to the holder really does
// change. The holder still clips, so the chain stops one level further up than the fixture above.
const char* kGrowPage =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0}"
    ".stage{width:300px;height:100px}"
    ".holder{width:200px;height:24px;overflow:hidden}"
    ".leaf{display:block;width:60px;height:20px}"
    ".inner{display:block;width:40px;height:6px;background-color:rgb(1,2,3)}"
    ".inner.huge{width:120px}"
    "</style></head><body>"
    "<div id=\"stage\" class=\"stage\"><div id=\"holder\" class=\"holder\">"
    "<div id=\"leaf\" class=\"leaf\"><div id=\"inner\" class=\"inner\"></div></div></div></div>"
    "</body></html>\n";

/// A container whose font size changes as one class: every row inherits a new computed style, so the
/// cascade hands `mark` one box per row plus the leaf in a single frame. Each row is `1em` tall, so
/// the rows grow with the font and the leaf's contribution to the holder really changes - the
/// all-changed form of capability G, where the contribution walk has no early stop to find.
std::string inherited_font_html() {
    std::string html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0;font-size:10px}"
        ".stage{width:300px}"
        ".holder{width:200px}"
        ".leaf{display:block;font-size:10px}"
        ".leaf.big{font-size:20px}"
        ".row{display:block;height:1em;background-color:rgb(1,2,3)}"
        "</style></head><body>"
        "<div id=\"stage\" class=\"stage\"><div id=\"holder\" class=\"holder\">"
        "<div id=\"leaf\" class=\"leaf\">";
    for (int i = 0; i < 12; ++i) {
        html += "<div class=\"row\"></div>";
    }
    html += "</div></div></div></body></html>\n";
    return html;
}

/// A windowed rig on the stage-0 page: the windowed pump is the path that paints, so the fixtures
/// can read the paint units too. `load()` leaves the page settled at a quiet frame.
struct Stage0Page {
    zui::test::HandlerRig rig;

    bool load(const char* html = nullptr) {
        zui::perf::FrameWork ignored;
        return load_cold(html, ignored);
    }

    /// Like `load`, but hands back the accounting window of the frame that ran the first (cold)
    /// cascade: that is the R0 a later stage is measured against.
    bool load_cold(const char* html, zui::perf::FrameWork& cold) {
        const fs::path dir = zui::test::scratch_dir("perf-stage0");
        fs::create_directories(dir);
        const fs::path page = dir / "page.html";
        std::ofstream out(page);
        out << (html != nullptr ? std::string(html) : stage0_html());
        out.close();
        if (!rig.app.document().load(page.string())) {
            return false;
        }
        if (!rig.windowed() || !rig.pump()) {
            return false;
        }
        cold = zui::perf::last_frame_work();
        rig.settle();
        return true;
    }
};

/// The numbers below are what this fixture, on this Debug build, measured when the case was
/// written (2026-10-05). They are baselines, not contract: a stage that changes one of them
/// updates the number and says which invariant moved ([`PERF.md`] section 6.5).
///
/// `flow_remeasures` is not asserted on a frame that skipped `layout()`: the unit is the last
/// `LayoutContext::end_frame()` snapshot, so a frame that never laid out still reports the previous
/// layout's count (measured 1 here - it comes from the load frame, not from this one).

}  // namespace

TEST(PerfStage0, PaintOnlyClassDoesNotLayOut) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    ASSERT_TRUE(page.rig.app.document().add_class("tile", "on"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        (void)zui::frame::Access::box_rect(page.rig.app, "tile", x, y, w, h);
        ZUI_FILE_INFO("[stage3] tile=({:.0f},{:.0f},{:.0f},{:.0f})", x, y, w, h);
        (void)zui::frame::Access::box_rect(page.rig.app, "far", x, y, w, h);
        ZUI_FILE_INFO("[stage3] far=({:.0f},{:.0f},{:.0f},{:.0f}) rects={}", x, y, w, h,
                      static_cast<unsigned long long>(work.damage_rects));
    }
    // Measured 1 / 1 / 1 / 62: the write's closure is the tile, the rule runs once, one key is
    // built, and the paint walk covers the 62 boxes the page has.
    EXPECT_EQ(work.cascade_visited, 1u) << "the closure of a class write on the tile is the tile";
    EXPECT_EQ(work.cascade_rule_runs, 1u);
    EXPECT_EQ(work.cascade_share_keys, 1u);
    // Capability E keeps the tiles the damage did not touch. The select is its own listing; the
    // tile does not meet that listing's scrollport, so the narrow walk replays it instead of
    // entering its text. Twelve boxes reach the erased tile, and the frame visits the root once
    // more to replay the rebuilt listing.
    EXPECT_EQ(work.painted_walked, 13u) << "capability E: the walk covers the erased tile only";
    // Branch of the stage-0 table: `laid_out == 0`, so the 384 ms of the section 1 snapshot is not
    // a scheduling input; stage 1 only keeps this fixture.
    EXPECT_EQ(work.laid_out, 0u) << "a paint-only class must not lay out (R2)";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_EQ(work.damage_rects, 1u) << "one rectangle until capability C";
}

TEST(PerfStage0, HoverOnOneButtonStaysInsideItsSubtree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "btn", x, y, w, h));
    page.rig.window().inject(zui::backends::TextInputFrame{});
    page.rig.window().pointer_.x = x + w * 0.5f;
    page.rig.window().pointer_.y = y + h * 0.5f;
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 1: entering `.btn:hover` cascades the button itself, not the page. Branch of the
    // stage-0 table: `cascade_visited` equals the button subtree, so stage 1b is not started.
    EXPECT_EQ(work.cascade_visited, 1u) << "the hover closure stops at the button";
    EXPECT_EQ(work.cascade_rule_runs, 1u);
    EXPECT_EQ(work.laid_out, 0u);
}

TEST(PerfStage0, ThemeColorClassDoesNotLayOut) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    auto text_color = [](zui::test::HandlerRig& rig, std::string_view text) {
        for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
            if (item.text == text) {
                return item.color;
            }
        }
        return zui::paint::Color::transparent();
    };
    const zui::paint::Color ink_before = text_color(page.rig, "t");
    ASSERT_TRUE(page.rig.app.document().set_theme("ink"));
    ASSERT_TRUE(page.rig.pump());
    const zui::paint::Color ink_after = text_color(page.rig, "t");
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 56 visited / 56 keys / 17 rule runs / 4 recomputed / 15 painted. `share_keys ==
    // visited` is expected (one key per visited node today). R0 is 17: the in-frame ShareCache
    // merges siblings, and a position bit no selector tests stays out of that key, so the last
    // `.item` and the last `<option>` join the middle sibling instead of running the rules again.
    // The first sibling stays apart because its previous element tag differs.
    // Recomputed is 4, not one map per visited node. A custom property stays on the element that
    // declared it. `html.theme-ink` changes `--ink`; html and the three nodes that substitute it
    // (both `.tile`s and `.plate`) are the maps that move. The tile text still takes the new ink.
    // Branch: `laid_out == 0`, so no classification or `geometry_dirty` repair is scheduled.
    // The theme damage is the html border box. A nested listing is invalidated on its scrollport
    // clamped to the scrollable overflow, not on the retained-margin clip, so the list (it does
    // not use `--ink`) replays and the walk stops at that container.
    EXPECT_EQ(work.laid_out, 0u) << "a colour-only theme must not lay out (R2)";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.cascade_visited, 56u);
    EXPECT_EQ(work.cascade_share_keys, 56u);
    EXPECT_EQ(work.cascade_rule_runs, 17u) << "R0 for this fixture; stage 2 requires rule_runs < R0";
    EXPECT_EQ(work.cascade_recomputed, 4u);
    EXPECT_EQ(work.painted_walked, 15u);
    EXPECT_FALSE(ink_after.r == ink_before.r && ink_after.g == ink_before.g &&
                 ink_after.b == ink_before.b && ink_after.a == ink_before.a)
        << "the tile took the new --ink";
    EXPECT_EQ(ink_after.r, 176);
    EXPECT_EQ(ink_after.g, 48);
    EXPECT_EQ(ink_after.b, 48);
    EXPECT_EQ(ink_after.a, 255);
}

TEST(PerfStage0, ScrollOnlyDoesNotLayOut) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    page.rig.app.document().set("list", "scroll_top", "60");
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 0 restyled / 0 laid / 1 placement / 78 world boxes / 3 records kept. Today the
    // placement walk starts at the root and covers the page (78 boxes); capability P's exit is
    // that `world_walked` covers only the scrolled container and what the scroll moved.
    EXPECT_EQ(work.cascade_visited, 0u) << "scrolling restyles nothing";
    EXPECT_EQ(work.laid_out, 0u) << "scrolling lays out nothing";
    EXPECT_EQ(work.world_base_gen, 1u) << "one placement this frame";
    // 44 = the path from the root to the scroller (4 boxes) plus the 40 item boxes. Before stage P
    // this was 78: the walk started at the root and visited the plate/button/select/far boxes that
    // the scroll cannot move. A published item's text is shifted by the same delta as the item
    // (a 60px scroll moves the first item from 525 to 465, and its text follows) instead of being
    // mapped again. Offscreen items keep that delta as lag.
    EXPECT_EQ(work.world_walked, 44u) << "stage P: only the scrolled subtree is re-placed";
    EXPECT_EQ(work.paint_records, 3u);
    EXPECT_EQ(work.paint_reused, 2u);
}

TEST(PerfStage0, WriteUnderDisplayNoneDoesNoBoxWork) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kHiddenPage));
    ASSERT_TRUE(page.rig.app.document().add_class("hleaf", "wide"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 1 / 0 / 0 / 0, with a layout-affecting class (`width`): a write inside
    // `display:none` already produces no box-tree work today, so this part of capability L is not
    // the open gap. What stage L still has to fix is the "no box, not none/contents" host that
    // climbs to the nearest boxed ancestor, and the I4-root dedup - both need a model-host fixture.
    EXPECT_EQ(work.cascade_visited, 1u) << "the hidden leaf is still cascaded";
    EXPECT_EQ(work.laid_out, 0u);
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u) << "a hidden subtree generates no box work";
}

namespace {

/// The border radius the painter used for the fill that covers `id`'s border box, or -1 when no
/// item covers it. This is the "look at the pixels" half of R2 without a raster: the display list
/// the renderer received is what the GPU would draw.
float painted_radius(zui::test::HandlerRig& rig, const char* id) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h)) {
        return -1.f;
    }
    for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (std::abs(item.rect.x - x) > 0.5f || std::abs(item.rect.y - y) > 0.5f ||
            std::abs(item.rect.w - w) > 0.5f || std::abs(item.rect.h - h) > 0.5f) {
            continue;
        }
        return item.radius;
    }
    return -1.f;
}

/// True when the window named `id` among the boxes it relowered / interned / laid.
bool touched(const zui::perf::FrameWork& work, std::string_view id) {
    for (std::size_t i = 0; i < work.touched_id_count; ++i) {
        if (work.touched_ids[i] == id) {
            return true;
        }
    }
    return false;
}

/// The colour of the painted text run whose characters are @p text.
zui::paint::Color painted_text_color(zui::test::HandlerRig& rig, std::string_view text) {
    for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
        if (item.text == text) {
            return item.color;
        }
    }
    return zui::paint::Color::transparent();
}

bool same_color(const zui::paint::Color& a, const zui::paint::Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/// The colour of the background fill the painter drew for `id`'s border box, or transparent when
/// no item covers it (the same probe `painted_radius` uses).
zui::paint::Color painted_fill_color(zui::test::HandlerRig& rig, const char* id) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h)) {
        return zui::paint::Color::transparent();
    }
    for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (std::abs(item.rect.x - x) > 0.5f || std::abs(item.rect.y - y) > 0.5f ||
            std::abs(item.rect.w - w) > 0.5f || std::abs(item.rect.h - h) > 0.5f) {
            continue;
        }
        return item.color;
    }
    return zui::paint::Color::transparent();
}

/// True when the display list the renderer received holds any item with this colour.
bool painted_any_color(zui::test::HandlerRig& rig, zui::paint::Color want) {
    for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
        if (same_color(item.color, want)) {
            return true;
        }
    }
    return false;
}

/// Two display items are the same drawing when these fields agree (the clip the painter pushed is a
/// `PushClip` item, so a moved clip shows up in the sequence too).
bool same_item(const zui::paint::DisplayItem& a, const zui::paint::DisplayItem& b) {
    return a.op == b.op && a.rect.x == b.rect.x && a.rect.y == b.rect.y && a.rect.w == b.rect.w &&
           a.rect.h == b.rect.h && a.color.r == b.color.r && a.color.g == b.color.g &&
           a.color.b == b.color.b && a.color.a == b.color.a && a.radius == b.radius &&
           a.stroke_width == b.stroke_width && a.text == b.text;
}

/// The items of a frame that can reach its pixels. docs/PERF_REFACTOR.md capability E replays a
/// retained listing through the clip it was recorded with, which drops items that lie outside that
/// clip - the plain walk still emits them and the raster clips them away. R5 is about pixels, so a
/// comparison between the two paths drops the off-clip items from both sides: clip/group operations
/// and text (whose rectangle is a pen origin, not an extent) always stay, everything else has to
/// reach the viewport.
std::vector<zui::paint::DisplayItem> visible_items(
    const std::vector<zui::paint::DisplayItem>& in, float viewport_w, float viewport_h) {
    auto touches = [](zui::paint::Rect a, zui::paint::Rect b, float slack) {
        return a.x - slack < b.x + b.w && a.y - slack < b.y + b.h && a.x + a.w + slack > b.x &&
               a.y + a.h + slack > b.y;
    };
    std::vector<zui::paint::DisplayItem> out;
    out.reserve(in.size());
    // The clip the items are painted through, as the walk and the replay both build it. An item
    // that cannot reach it is culled by `append_shifted` on the replay and clipped away by the
    // raster on the plain walk, so it is not part of the comparison either way.
    std::vector<zui::paint::Rect> stack;
    zui::paint::Rect running{0.f, 0.f, viewport_w, viewport_h};
    for (const zui::paint::DisplayItem& item : in) {
        switch (item.op) {
            case zui::paint::DisplayOp::PushClip: {
                stack.push_back(running);
                const zui::paint::Rect clipped =
                    zui::paint::intersect_rect(running, item.rect);
                running = clipped;
                out.push_back(item);
                continue;
            }
            case zui::paint::DisplayOp::PopClip:
                if (!stack.empty()) {
                    running = stack.back();
                    stack.pop_back();
                } else {
                    running = zui::paint::Rect{0.f, 0.f, viewport_w, viewport_h};
                }
                out.push_back(item);
                continue;
            case zui::paint::DisplayOp::DrawText:
            case zui::paint::DisplayOp::BeginFilter:
            case zui::paint::DisplayOp::EndFilter:
            case zui::paint::DisplayOp::BeginTransform:
            case zui::paint::DisplayOp::EndTransform:
                if (item.op != zui::paint::DisplayOp::DrawText) {
                    out.push_back(item);
                    continue;
                }
                // A text item's rectangle is the pen origin, so give its glyphs room to reach the
                // clip before dropping it: a pen just outside can still paint inside.
                if (touches(item.rect, running, std::max(1.f, item.font_px))) {
                    out.push_back(item);
                }
                continue;
            default:
                break;
        }
        float slack = 0.f;
        if (item.op == zui::paint::DisplayOp::StrokeRect) {
            slack = std::max(1.0f, item.stroke_width);
        } else if (item.op == zui::paint::DisplayOp::ShadowRect && item.blur > 0.f) {
            slack = item.blur * 1.5f;
        }
        if (touches(item.rect, running, slack)) {
            out.push_back(item);
        }
    }
    return out;
}

/// R5 (docs/PERF_REFACTOR.md section 3): both paint paths must put the same visible items on the
/// wire, in the same order.
void expect_same_visible_items(const std::vector<zui::paint::DisplayItem>& full,
                               const std::vector<zui::paint::DisplayItem>& selective,
                               float viewport_w = 1100.f, float viewport_h = 720.f) {
    const std::vector<zui::paint::DisplayItem> a = visible_items(full, viewport_w, viewport_h);
    const std::vector<zui::paint::DisplayItem> b = visible_items(selective, viewport_w, viewport_h);
    ASSERT_EQ(a.size(), b.size()) << "the replay must produce the same visible items";
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(same_item(a[i], b[i])) << "item " << i;
    }
}

/// The escape hatch of the selective paint path (`paint/painter.cpp`): `0` keeps the old wholesale
/// drop, anything else (including unset) uses the localized damage.
void set_selective_damage(bool on) {
    _putenv_s("ZUI_PERF_DAMAGE", on ? "1" : "0");
}

/// Run `action` on a fresh page with the selective path in the given mode and return the items the
/// renderer was handed. Both runs load the same bytes into a fresh scratch directory.
template <typename Fn>
std::vector<zui::paint::DisplayItem> painted_items(bool selective, const std::string& html,
                                                   Fn&& action) {
    set_selective_damage(selective);
    Stage0Page page;
    std::vector<zui::paint::DisplayItem> items;
    if (page.load(html.c_str())) {
        action(page);
        items = page.rig.renderer().last_items;
    }
    set_selective_damage(true);  // back to the default for whatever runs next
    return items;
}

}  // namespace

TEST(PerfStage1, PaintOnlyRadiusReinternsWithoutLayout) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    ASSERT_TRUE(page.rig.pump());
    // Before: the fixture's tile has square corners and the painter drew them that way.
    EXPECT_FLOAT_EQ(painted_radius(page.rig, "tile"), 0.f);

    ASSERT_TRUE(page.rig.app.document().add_class("tile", "round"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // R2: the changed property (`border-radius`) is not in `affects_layout`, so the frame stores
    // nothing and builds nothing. docs/PERF.md section 8 item 9 is the trap this checks: paint
    // reads the radius out of the box's interned LayoutStyle, so a frame that skipped layout also
    // had to re-intern the box, or the corners would keep the old shape.
    EXPECT_EQ(work.laid_out, 0u) << "a radius must not lay out (R2)";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_FLOAT_EQ(painted_radius(page.rig, "tile"), 12.f) << "the painter sees the new radius";
}

TEST(PerfStageG, AbsorbedChildChangeDoesNotStoreTheParent) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kContributionPage));
    // `#inner` widens inside `#leaf`'s fixed box, so `#leaf`'s contribution to `#holder` is the same
    // whatever the inner box did. `mark` compares that contribution and stops at `#leaf`; the frame
    // re-lays `#inner` and `#leaf`, and `#holder` answers from its cache.
    ASSERT_TRUE(page.rig.app.document().add_class("inner", "wide"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 2 / 0 / 0 with `touched_ids == {inner, leaf}`: the mark walk compared the leaf's
    // contribution with the snapshot the holder recorded, found it equal, and stopped. The holder
    // answers from its cache. This is the exit condition of capability G, and it holds on the
    // current engine - `LayoutInvalidation::mark` did the compare, `resolve_for_invalidation`
    // stored the leaf, and the holder's `result_stale` then saw a re-stored child.
    EXPECT_EQ(work.laid_out, 2u) << "the inner box and the leaf it moved inside; not the holder";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_TRUE(touched(work, "inner"));
    EXPECT_TRUE(touched(work, "leaf"));
    EXPECT_FALSE(touched(work, "holder")) << "the holder's contribution did not change";
    EXPECT_FALSE(touched(work, "stage"));
}

TEST(PerfStageG, ChildThatGrowsItsParentStoresTheParent) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kGrowPage));
    // The leaf does not clip, so `#inner` growing past it moves the leaf's ink and `#holder` has to
    // re-lay. The holder's own box is fixed and it does clip, so its contribution to the stage does
    // not move and the chain stops there again. The clipping-leaf form of this fixture is the stage
    // W test below: there the wall stops the climb one level lower.
    ASSERT_TRUE(page.rig.app.document().add_class("inner", "huge"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 3 / 0 / 0, `touched_ids == {inner, leaf, holder}`: the leaf's ink moved, so the
    // holder re-lays; the holder's own box is fixed, so the chain stops there and the stage hits.
    EXPECT_EQ(work.laid_out, 3u) << "inner, leaf and the holder it overflowed; not the stage";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_TRUE(touched(work, "holder"));
    EXPECT_FALSE(touched(work, "stage")) << "the stage's contribution did not change";
}

TEST(PerfStageG, ManyChangedBoxesCompareEachAncestorOnce) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(inherited_font_html().c_str()));
    // One class on `#leaf` moves the font size, so the rows inherit a new computed style and the
    // cascade's `layout` set names thirteen boxes in one frame. Each row's `height:1em` follows the
    // font, so the leaf's contribution to the holder moves too and the chain above cannot stop: this
    // is the all-changed form of capability G. The contribution compare is what re-lays a subject's
    // subtree through `resolve_for_invalidation`, so without a verdict per frame the walk pays it
    // once per marked box - the reader's font-size step re-walked `#flow_wrap` 345 times for one
    // class change.
    ASSERT_TRUE(page.rig.app.document().add_class("leaf", "big"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 31: the load frame stores the page's 17 boxes, the frame that took the class stores
    // its 13 changed ones (the four above answer from their cache) and the write's eager
    // invalidation stores one ancestor. Before the verdict the same fixture stored 67; the extra 36
    // were the ancestors re-derived once per marked box. `laid_out` is 17 both ways, which is why
    // this gate reads the store count and not `laid.boxes`.
    EXPECT_EQ(work.layout_stores, 31u) << "each changed box and each ancestor it moves, once";
    EXPECT_EQ(work.laid_out, 17u) << "the distinct boxes are the page; the stores are the cost";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
}

TEST(PerfStageW, TheWallStopsTheCoupling) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kContributionPage));
    // docs/PERF.md I5: a box whose `overflow` is not `visible` is a relayout wall. `#inner` grows
    // past `#leaf`, so the leaf's *scrollWidth* really moves - and the holder must not care, because
    // the leaf clips what it lays out. Before stage W the leaf's raw ink travelled up, the holder's
    // recorded ink moved with it, and `#holder` re-laid for a box it cannot paint.
    ASSERT_TRUE(page.rig.app.document().add_class("inner", "huge"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 2 / 0 / 0 with `touched_ids == {inner, leaf}`: the change stayed inside the wall, and
    // the same fixture with the leaf's `overflow` left visible re-lays the holder (3) above.
    EXPECT_EQ(work.laid_out, 2u) << "inner and the leaf; the wall stops the climb";
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_TRUE(touched(work, "inner"));
    EXPECT_TRUE(touched(work, "leaf"));
    EXPECT_FALSE(touched(work, "holder")) << "a clipped descendant cannot move the holder";
    EXPECT_FALSE(touched(work, "stage"));
    // The wall did not swallow the change: the leaf still reports the wider content, which is what
    // its own scroll area is for (and what the painter clips).
    EXPECT_NEAR(std::stof(zui::frame::Access::measure(page.rig.app, "leaf", "scroll_width")), 120.f,
                1.f)
        << "the leaf's own scrollWidth grew; only the boxes above it are unaffected";
}

TEST(PerfStage3, TwoDistantEditsKeepTwoDamageRects) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    ASSERT_TRUE(page.rig.app.document().add_class("tile", "on"));
    ASSERT_TRUE(page.rig.app.document().add_class("far", "on"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        (void)zui::frame::Access::box_rect(page.rig.app, "tile", x, y, w, h);
        ZUI_FILE_INFO("[stage3] tile=({:.0f},{:.0f},{:.0f},{:.0f})", x, y, w, h);
        (void)zui::frame::Access::box_rect(page.rig.app, "far", x, y, w, h);
        ZUI_FILE_INFO("[stage3] far=({:.0f},{:.0f},{:.0f},{:.0f}) rects={}", x, y, w, h,
                      static_cast<unsigned long long>(work.damage_rects));
    }
    // Measured 2: two paint-only edits far apart stay two rectangles instead of collapsing into one
    // bounding box (docs/PERF_REFACTOR.md capability C).
    EXPECT_EQ(work.damage_rects, 2u) << "distant edits do not merge";
    EXPECT_EQ(work.laid_out, 0u);
}

TEST(PerfStage3, SelectiveDamageMatchesTheFullWalk) {
    const auto action = [](Stage0Page& page) {
        (void)page.rig.app.document().add_class("tile", "on");
        (void)page.rig.pump();
    };
    const std::vector<zui::paint::DisplayItem> full = painted_items(false, stage0_html(), action);
    const std::vector<zui::paint::DisplayItem> selective = painted_items(true, stage0_html(), action);
    ASSERT_FALSE(full.empty());
    expect_same_visible_items(full, selective);
}

TEST(PerfStage3, FixedLayerClosedLeavesNoResidue) {
    // Open a `position: fixed` layer, close it, then damage something else. The retained listings
    // cannot be kept for an anchored subtree, so this is the shape that used to replay the layer.
    const auto action = [](Stage0Page& page) {
        (void)page.rig.app.document().add_class("overlay", "show");
        (void)page.rig.pump();
        (void)page.rig.app.document().remove_class("overlay", "show");
        (void)page.rig.pump();
        (void)page.rig.app.document().add_class("tile", "on");
        (void)page.rig.pump();
    };
    const std::vector<zui::paint::DisplayItem> full = painted_items(false, stage0_html(), action);
    const std::vector<zui::paint::DisplayItem> selective = painted_items(true, stage0_html(), action);
    ASSERT_FALSE(full.empty());
    expect_same_visible_items(full, selective);
}

// ---------------------------------------------------------------------------------------------
// docs/PERF_REFACTOR.md stage 3b (capability E): a page with a single scrolling record - the
// document element - so a local change has to re-walk only the tiles it erased. Two `display:flex`
// rows of ten 100x100 boxes: row A at y 0, row B at y 300. Both sit inside the 1100x720 viewport,
// and the `kTilePx = 256` grid puts each row in a different tile row, with three boxes per tile
// column. Nothing here scrolls, so the document element is the only listing.
std::string tile_strip_html() {
    std::string html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0}"
        ".row{display:flex}"
        ".far{margin-top:200px}"
        ".box{width:100px;height:100px;background-color:rgb(238,238,238)}"
        ".box.on{background-color:rgb(43,108,176)}"
        "</style></head><body><div class=\"row\">";
    for (int i = 0; i < 10; ++i) {
        html += "<div id=\"a" + std::to_string(i) + "\" class=\"box\"></div>";
    }
    html += "</div><div class=\"row far\">";
    for (int i = 0; i < 10; ++i) {
        html += "<div id=\"b" + std::to_string(i) + "\" class=\"box\"></div>";
    }
    html += "</div></body></html>\n";
    return html;
}

TEST(PerfStage3b, OneBackgroundChangeWalksOnlyItsTiles) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(tile_strip_html().c_str(), cold));
    const std::uint64_t whole_page = cold.painted_walked;
    ASSERT_TRUE(page.rig.app.document().add_class("a0", "on"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    ZUI_FILE_INFO("[stage3b] whole={} one={} rects={}", whole_page, work.painted_walked,
                  work.damage_rects);
    EXPECT_EQ(work.damage_rects, 1u);
    EXPECT_EQ(work.laid_out, 0u);
    // The boxes that reach the erased tile (the document element, `body`, the row, and the three
    // boxes the tile covers) plus the container the frame visits to replay the listing.
    EXPECT_EQ(work.painted_walked, 7u);
    EXPECT_LT(work.painted_walked, whole_page);
}

TEST(PerfStage3b, TwoDistantEditsWalkTwoTileGroups) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(tile_strip_html().c_str(), cold));
    const std::uint64_t whole_page = cold.painted_walked;
    ASSERT_TRUE(page.rig.app.document().add_class("a0", "on"));
    ASSERT_TRUE(page.rig.app.document().add_class("b0", "on"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    ZUI_FILE_INFO("[stage3b] whole={} two={} rects={}", whole_page, work.painted_walked,
                  work.damage_rects);
    EXPECT_EQ(work.damage_rects, 2u) << "row A and row B are different tiles";
    // Two tile rows: the shared ancestors once each, plus the three boxes per tile column.
    EXPECT_EQ(work.painted_walked, 11u) << "two tile groups, not the whole listing";
    EXPECT_LT(work.painted_walked, whole_page);
}

// ---------------------------------------------------------------------------------------------
// docs/PERF_REFACTOR.md stage 2 (capability A): the cross-frame cascade memo.
// (The stage 4 probe sits at the end of this file.)
//
// The fixture is a theme class on a subtree (`#themed`), not on `html`: the class write changes
// `--ink` there, so the nodes that read it re-substitute and everything outside the subtree keeps
// its memo entry untouched. `body { counter-reset }` is what makes the style closure the whole
// document even for a subtree write (docs/PERF.md section 2.4: an author counter can observe every
// later sibling), so the control node outside `#themed` is still walked and can be counted.
const char* kStage2Page =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0;font-size:16px;counter-reset:sec}"
    "#themed{--ink:rgb(17,34,51);margin-top:120px}"
    "#themed.theme-ink{--ink:rgb(176,48,48)}"
    ".card{color:var(--ink)}"
    ".title{color:var(--ink)}"
    ".note{color:var(--ink)}"
    ".plain{color:rgb(1,2,3)}"
    ".card:hover .title{background-color:rgb(255,204,0)}"
    "</style></head><body>"
    "<div id=\"themed\"><div id=\"card\" class=\"card\">"
    "<span id=\"title\" class=\"title\">t</span><span id=\"twin\" class=\"title\">u</span>"
    "</div><span id=\"note\" class=\"note\">n</span></div>"
    "<div id=\"control\" class=\"plain\">c</div>"
    "</body></html>\n";

TEST(PerfStage2, ThemeColorClassSkipsTheRuleList) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    // `document().load` cascades at 0×0 and fills the memo. The first viewport pump reuses those
    // entries when no `@media` group flips, so this frame is not a second full rule run.
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(kStage2Page, cold));
    ASSERT_TRUE(page.rig.pump());
    const zui::paint::Color title_before = painted_text_color(page.rig, "t");
    ASSERT_TRUE(page.rig.app.document().add_class("themed", "theme-ink"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork first = zui::perf::last_frame_work();
    const zui::paint::Color title_after = painted_text_color(page.rig, "t");
    ASSERT_TRUE(page.rig.app.document().remove_class("themed", "theme-ink"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    // A colour-only class write: no box work either way.
    EXPECT_EQ(second.laid_out, 0u) << "a colour-only theme must not lay out (R2)";
    EXPECT_EQ(second.boxes_created, 0u);
    // The class write changes the class of `#themed` alone, so exactly one node has to run its rule
    // list; the count is far below the cold R0 measured above.
    EXPECT_EQ(second.cascade_visited, 10u) << "the author counter makes the closure the document";
    EXPECT_EQ(second.cascade_rule_runs, 1u) << "only #themed re-matched";
    (void)cold;
    // The nodes that read `--ink` re-substituted; the ones outside `#themed` answered from their
    // memo entry without substituting at all (`#control` among them).
    EXPECT_EQ(second.cascade_resubsts, 4u) << "#card, #title, #twin and #note read the property";
    EXPECT_EQ(second.cascade_memo_hits, 5u) << "html/head/style/body and the #control node";
    EXPECT_EQ(second.cascade_key_evals, 0u) << "the key build calls no selector";
    EXPECT_LE(second.cascade_key_evals, second.cascade_visited);
    // Stage 1 re-interns every box a Paint diff reaches. The five diffs are `#themed`, `#card`,
    // `#title`, `#twin` and `#note`. `#themed` also owns the anonymous block around the inline
    // `#note`, and each of the three spans owns its text run. Those derived boxes carry the font
    // the painter reads, so they are interned with the element (PERF.md section 4). 5 + 1 + 3 = 9.
    EXPECT_EQ(second.style_interned, 9u)
        << "paint diffs plus the text and anonymous boxes they own";
    EXPECT_EQ(second.cascade_env_gen_bumps, 0u) << "no media / env input moved";
    // The pixels moved for the themed subtree and stayed put for the control.
    EXPECT_FALSE(same_color(title_after, title_before)) << "the title took the new --ink";
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "c"), zui::paint::Color{1, 2, 3, 255}))
        << "the control's own colour must not move";
}

/// One class recolours the whole page, but only a few of its rows declare a transition. A theme
/// change does exactly this: the stylesheet swap lands every node with a changed interpolable
/// property in `cr.paint`, and the job of `Transitions::sync` is to start the transitions the page
/// asked for - not to derive a specified value for all eighteen properties of every node it walks.
std::string transition_claim_html(int rows) {
    std::string html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0}"
        ".row{display:block;height:8px}"
        ".row.tx{transition: background-color 100ms linear}"
        "</style></head><body id=\"page\">";
    for (int i = 0; i < rows; ++i) {
        html += i < 4 ? "<div class=\"row tx\"></div>" : "<div class=\"row\"></div>";
    }
    html += "</body></html>\n";
    return html;
}

/// A page whose colours come from a theme stylesheet, plus the two packs the fixture swaps. Every
/// row declares no colour of its own, so a theme swap re-styles all of them.
void write_transition_claim_page(const fs::path& dir, int rows) {
    std::ofstream page(dir / "page.html");
    page << transition_claim_html(rows);
    page.close();
    std::ofstream a(dir / "themea.css");
    a << "body{color:rgb(1,1,1)}.row{background-color:rgb(10,10,10)}";
    a.close();
    std::ofstream b(dir / "themeb.css");
    b << "body{color:rgb(2,2,2)}.row{background-color:rgb(200,40,40)}";
    b.close();
}

/// A scroll container that exists from the first frame and a list of rows that arrives later - the
/// shape every application has when it fills a list (the reader's contents, a chat log, zdb's
/// results). The rows opt into `content-visibility: auto` with an intrinsic size, which is what
/// lets the engine skip the ones outside the scrollport.
const char* kCvListPage =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0}"
    ".list{width:300px;height:200px;overflow:auto}"
    ".row{content-visibility:auto;contain-intrinsic-size:auto 20px;display:block;height:20px}"
    "</style></head><body><div class=\"list\" id=\"list\">"
    "<template><div class=\"row\">{{text}}</div></template>"
    "</div></body></html>\n";

class RowModel : public zui::ListModel {
public:
    size_t rows = 0;

    size_t row_count() const override { return rows; }

    std::string value(size_t row, std::string_view key) const override {
        if (key == "text") {
            return "row " + std::to_string(row);
        }
        return {};
    }
};

TEST(PerfStage2, NewRowsUnderContentVisibilitySkipOffscreenOnTheFirstLayout) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kCvListPage));
    RowModel rows;
    rows.rows = 500;
    ASSERT_TRUE(page.rig.app.document().set_model("list", rows));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Every row is 20px in a 200px scrollport, so 90 rows cover the viewport plus the 1.5x relevance
    // margin; the other 410 are skipped and their text boxes are never laid out. Measured 1003
    // distinct boxes and 2003 stores before the in-pass decision (`flow/blocks.cpp`), 593 / 1183
    // after. The rows themselves are still built: skipping belongs to layout, not to the box tree.
    EXPECT_EQ(work.boxes_created, 1000u) << "500 rows and their text boxes; the tree is not skipped";
    EXPECT_EQ(work.laid_out, 593u) << "the rows near the scrollport plus one box per skipped row";
    EXPECT_EQ(work.layout_stores, 1183u) << "a skipped row stores its placeholder, not its text";
}

TEST(PerfStage2, TransitionFeedClaimsOnlyTransitionedProperties) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A theme swap is what makes this measurable: the class write above rides the cascade memo, so
    // only the rows whose colour actually moved reach `commit_computed`. Swapping the stylesheet
    // invalidates every memo entry, so all of them do - the shape the studio's theme button has.
    const auto claim = [](int rows) {
        const fs::path dir = zui::test::scratch_dir("perf-stage0");
        fs::create_directories(dir);
        write_transition_claim_page(dir, rows);
        Stage0Page page;
        EXPECT_TRUE(page.rig.app.document().load((dir / "page.html").string()));
        EXPECT_TRUE(page.rig.windowed());
        EXPECT_TRUE(page.rig.pump());
        page.rig.settle();
        EXPECT_TRUE(page.rig.app.document().set_theme("themea"));
        EXPECT_TRUE(page.rig.pump());
        EXPECT_TRUE(page.rig.app.document().set_theme("themeb"));
        EXPECT_TRUE(page.rig.pump());
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork small = claim(8);
    const zui::perf::FrameWork large = claim(200);
    // Every row recolours and four of them declare `transition: background-color`. The feed pays two
    // specified-value derivations per (node, property) it derives, so the four transitioning rows
    // cost 8 whether the page has 8 rows or 200. Measured with the property check where it used to
    // be - after the derivation - the same fixture counted 324 and 7236 (nine and 201 nodes in
    // `cr.paint`, times eighteen properties, times two maps); the studio's 80x80 grid measured 244k
    // derivations for 22 transitions that actually started, 75.7 ms of a 94 ms theme frame.
    EXPECT_EQ(small.transition_values, 8u) << "four transitioned rows x one property x two maps";
    EXPECT_EQ(large.transition_values, small.transition_values)
        << "the feed must not scale with the rows that declare no transition";
    EXPECT_EQ(small.laid_out, 0u) << "a colour-only class must not lay out (R2)";
    EXPECT_EQ(small.boxes_created, 0u);
}

TEST(PerfStage2, HoveringTheAncestorCompoundIsNotAReuse) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(kStage2Page, cold));
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    // Hover the *second* title, so `#title` itself is never the hovered element: the only way its
    // background can move is the ancestor compound `.card:hover`.
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "twin", x, y, w, h));
    EXPECT_FALSE(painted_any_color(page.rig, zui::paint::Color{255, 204, 0, 255}))
        << "the hover background is not painted before the pointer arrives";
    page.rig.window().inject(zui::backends::TextInputFrame{});
    page.rig.window().pointer_.x = x + w * 0.5f;
    page.rig.window().pointer_.y = y + h * 0.5f;
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // `.card:hover .title` is an ancestor compound: entering hover changes `.card`'s hover bit,
    // which is part of what the key hashes for `.title`. `#title` itself is never hovered, so a
    // reuse of its stored computed would leave it transparent; the yellow paint proves it re-ran.
    EXPECT_TRUE(painted_any_color(page.rig, zui::paint::Color{255, 204, 0, 255}))
        << "the ancestor compound's hover has to reach #title and #twin";
    EXPECT_EQ(work.laid_out, 0u);
}

TEST(PerfStage2, PrecedingSiblingChangeIsNotAReuse) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `.a + .b`: the key of `#b` has to carry what the sibling combinator can see on `#a`.
    const char* page_html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0;counter-reset:sec}"
        ".a{color:rgb(1,2,3)}"
        ".b{color:rgb(4,5,6)}"
        ".a + .b{color:rgb(7,8,9)}"
        "</style></head><body>"
        "<span id=\"a\" class=\"a\">a</span><span id=\"b\" class=\"b\">b</span>"
        "</body></html>\n";
    Stage0Page page;
    ASSERT_TRUE(page.load(page_html));
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "b"), zui::paint::Color{7, 8, 9, 255}));
    // `#a` stops matching `.a`, so `.a + .b` stops matching `#b`.
    ASSERT_TRUE(page.rig.app.document().remove_class("a", "a"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "b"), zui::paint::Color{4, 5, 6, 255}))
        << "the sibling combinator result moved with the preceding sibling";
    EXPECT_GE(work.cascade_rule_runs, 1u);
}

TEST(PerfStage2, ViewportSizeWithoutAMediaFlipReusesTheMemo) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kStage2Page));
    ASSERT_TRUE(page.rig.pump());
    page.rig.window().width_ += 40;
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Width is not an `env()` input. This page has no `@media`, so the stored maps stay valid.
    EXPECT_EQ(work.cascade_env_gen_bumps, 0u);
    EXPECT_GT(work.cascade_memo_hits, 0u)
        << "visited=" << work.cascade_visited << " misses=" << work.cascade_memo_misses
        << " resubsts=" << work.cascade_resubsts << " rounds=" << work.cascade_rounds
        << " keys=" << work.cascade_share_keys;
    EXPECT_EQ(work.cascade_rule_runs, 0u);
}

TEST(PerfStage2, MediaQueryFlipRestylesTheMatchedNode) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const char* html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0}"
        "#control{color:rgb(1,2,3)}"
        "#other{color:rgb(4,5,6)}"
        "@media (max-width: 900px) { #control { color: rgb(9,0,0); } }"
        "</style></head><body>"
        "<div id=\"control\">c</div><div id=\"other\">o</div>"
        "</body></html>\n";
    Stage0Page page;
    ASSERT_TRUE(page.load(html));
    // `windowed()` establishes 1100×720, which is outside the query. Cross the breakpoint
    // explicitly: 800px admits the rule, 1100px drops it.
    page.rig.window().width_ = 800;
    ASSERT_TRUE(page.rig.pump());
    const zui::paint::Color loaded = painted_text_color(page.rig, "c");
    EXPECT_TRUE(same_color(loaded, zui::paint::Color{9, 0, 0, 255}))
        << "800px matches max-width: 900px, got "
        << static_cast<int>(loaded.r) << "," << static_cast<int>(loaded.g) << ","
        << static_cast<int>(loaded.b);
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "o"), zui::paint::Color{4, 5, 6, 255}));
    page.rig.window().width_ = 1100;
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "c"), zui::paint::Color{1, 2, 3, 255}))
        << "the flipped query drops #control back to its unconditional colour";
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "o"), zui::paint::Color{4, 5, 6, 255}))
        << "a node the query does not name keeps its colour";
    EXPECT_EQ(work.cascade_env_gen_bumps, 0u);
    EXPECT_GT(work.cascade_memo_hits, 0u);
    EXPECT_GE(work.cascade_rule_runs, 1u);
}

namespace {

/// Rows of one text field, for the `nth-last-child()` fixture below.
struct Stage2Rows : zui::ListModel {
    std::vector<std::string> rows;
    size_t row_count() const override { return rows.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "text" ? rows[row] : std::string{};
    }
};

}  // namespace

TEST(PerfStage2, AddingASiblingIsNotAReuse) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `:nth-last-child(2)` counts from the end, so the key has to carry how many siblings follow:
    // appending a row moves the match to another row without changing any node's own tag or class.
    const char* page_html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0;counter-reset:sec}"
        ".row{color:rgb(1,2,3)}"
        ".row:nth-last-child(2){color:rgb(9,9,9)}"
        "</style></head><body>"
        "<ul id=\"host\"><template><li class=\"row\">{{text}}</li></template></ul>"
        "</body></html>\n";
    Stage0Page page;
    ASSERT_TRUE(page.load(page_html));
    Stage2Rows model;
    model.rows = {"a", "b", "c"};
    ASSERT_TRUE(page.rig.app.document().set_model("host", model));
    ASSERT_TRUE(page.rig.pump());
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "b"), zui::paint::Color{9, 9, 9, 255}))
        << "b is the second from the end in a three-row list";
    model.rows = {"a", "b", "c", "d"};
    ASSERT_TRUE(page.rig.app.document().set_model("host", model));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "b"), zui::paint::Color{1, 2, 3, 255}))
        << "b is no longer second from the end";
    EXPECT_TRUE(same_color(painted_text_color(page.rig, "c"), zui::paint::Color{9, 9, 9, 255}))
        << "c is the second from the end now";
    EXPECT_GT(work.cascade_rule_runs, 0u);
}

TEST(PerfStage2, ReplacingTheSheetsDropsEveryEntry) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load(kStage2Page));
    ASSERT_TRUE(page.rig.app.document().set_theme("plain"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // `set_theme` swaps the theme sheet: the sheet generation moves, so no entry from the previous
    // generation may answer and the whole document re-matches.
    EXPECT_EQ(work.cascade_memo_hits, 0u) << "another sheet generation invalidates every key";
    EXPECT_GT(work.cascade_rule_runs, 1u);
}

namespace {

// Stage P: the page is a column of static rows that fills the viewport (so a whole-tree placement
// really does visit all of them), a small `overflow:auto` pane whose own subtree is a fraction of
// the page, and a fixed holder whose content can change without the holder's contribution moving.
// The two fixtures below are the two branches of capability P's exit condition: a scroll marks the
// pane, a local layout change marks what the layout stored.
std::string stagep_html() {
    std::string html =
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0;font-size:8px}"
        ".body_row{width:300px;height:8px;background-color:rgb(240,240,240)}"
        ".pane{width:200px;height:48px;overflow:auto}"
        ".item{height:8px;background-color:rgb(250,250,250)}"
        ".holder{width:300px;height:40px;overflow:hidden}"
        ".leaf{display:block;width:60px;height:20px;overflow:hidden}"
        ".inner{display:block;width:40px;height:6px;background-color:rgb(1,2,3)}"
        ".inner.wide{width:50px}"
        "</style></head><body>";
    for (int i = 0; i < 70; ++i) {
        html += "<div class=\"body_row\">r</div>";
    }
    html += "<div id=\"pane\" class=\"pane\">";
    for (int i = 0; i < 20; ++i) {
        html += "<div class=\"item\" id=\"item" + std::to_string(i) + "\">i</div>";
    }
    html += "</div><div id=\"holder\" class=\"holder\">"
            "<div id=\"leaf\" class=\"leaf\"><div id=\"inner\" class=\"inner\"></div></div>"
            "</div></body></html>\n";
    return html;
}

bool rect_of(zui::test::HandlerRig& rig, const char* id, float& x, float& y, float& w, float& h) {
    return zui::frame::Access::box_rect(rig.app, id, x, y, w, h);
}

}  // namespace

TEST(PerfStageP, ScrollPlacesOnlyTheScrolledSubtree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(stagep_html().c_str(), cold));
    float before_x = 0.f, before_y = 0.f, before_w = 0.f, before_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "item0", before_x, before_y, before_w, before_h));
    float far_x = 0.f, far_y = 0.f, far_w = 0.f, far_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "inner", far_x, far_y, far_w, far_h));
    const std::uint64_t page_boxes = cold.world_walked;
    ASSERT_TRUE(page.rig.app.document().set("pane", "scroll_top", "48"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    float after_x = 0.f, after_y = 0.f, after_w = 0.f, after_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "item0", after_x, after_y, after_w, after_h));
    // The scroll moved the rows (the observation reads the placement the frame published), and it
    // did not touch the rest of the page.
    EXPECT_NEAR(after_y, before_y - 48.f, 0.5f) << "the scrolled rows keep their world position";
    EXPECT_EQ(work.world_base_gen, 1u);
    EXPECT_EQ(work.laid_out, 0u);
    // Exit condition: the walk covers the pane and the rows it moved, not the 70 body rows above
    // it. Measured 186 for the cold frame (the whole page) and 23 here: html, body and the pane,
    // plus the pane's 20 rows. Each row's text is shifted by the same delta as the row, the same
    // way `PerfStage0.ScrollOnlyDoesNotLayOut` does not remap a published item's text.
    EXPECT_LT(work.world_walked, page_boxes) << "a scroll is not a whole-tree placement";
    EXPECT_EQ(work.world_walked, 23u) << "the pane and the rows inside it, plus the path to it";
}

TEST(PerfStageP, LocalLayoutChangeDoesNotPlaceThePage) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(stagep_html().c_str(), cold));
    float row_x = 0.f, row_y = 0.f, row_w = 0.f, row_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "item0", row_x, row_y, row_w, row_h));
    const std::uint64_t page_boxes = cold.world_walked;
    // `width` is a layout property, and the leaf holds its own overflow: the change is inside the
    // holder's contribution, so the layout recomputes the leaf's chain and stops.
    ASSERT_TRUE(page.rig.app.document().add_class("inner", "wide"));
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    float inner_x = 0.f, inner_y = 0.f, inner_w = 0.f, inner_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "inner", inner_x, inner_y, inner_w, inner_h));
    float after_row_x = 0.f, after_row_y = 0.f, after_row_w = 0.f, after_row_h = 0.f;
    ASSERT_TRUE(rect_of(page.rig, "item0", after_row_x, after_row_y, after_row_w, after_row_h));
    EXPECT_NEAR(inner_w, 50.f, 0.5f) << "the leaf really was laid out again";
    EXPECT_NEAR(after_row_y, row_y, 0.5f) << "a box outside the change keeps its world rectangle";
    // Exit condition: a leaf whose parent origin did not move is not a whole-page placement.
    // Measured 186 for the cold frame and 5 here: html, body, holder, leaf, inner.
    EXPECT_LT(work.world_walked, page_boxes) << "a local layout is not a whole-tree placement";
    EXPECT_EQ(work.world_walked, 5u) << "the path to the leaf and the leaf's own subtree";
}

TEST(PerfStageP, ViewportChangeStillPlacesTheWholeTree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    zui::perf::FrameWork cold;
    ASSERT_TRUE(page.load_cold(stagep_html().c_str(), cold));
    const std::uint64_t page_boxes = cold.world_walked;
    // A resized viewport moves the culling window and the root's available rectangle for every box;
    // no store and no scroll marks that, so the placement input has to be the viewport itself.
    page.rig.window().width_ += 40;
    page.rig.window().height_ += 40;
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // Measured 186 both before and after: a resize places the whole tree (marks or not).
    EXPECT_GE(work.world_walked, page_boxes) << "a resize places the whole tree, marks or not";
}

// ---------------------------------------------------------------------------------------------
// docs/PERF_REFACTOR.md stage 4 (capability B): the `<select>` popup is the first top-layer
// member. The exit condition (R3) is *about the base page*: opening and closing must leave the base
// subtree's `laid_out` at 0, keep the base listings `reused`, leave `base_generation` alone, bump
// `overlay_generation` instead, and let `painted_walked` count the popup's own boxes.
//
// The numbers below are today's, measured on this fixture in Debug. They are the baseline the
// stage has to move, locked first so the change is interceptable (docs/PERF.md section 6.4: 先能被
// 拦截，再谈修好). The popup writes its geometry into each option's inline `style`, so opening it
// is a box-tree edit: the whole world is re-placed (`base=1`), the paint stamp goes full
// (`reused=0`), and the frame walks the page (`painted=71`). A culled box stops the paint walk,
// so offscreen list rows do not add their text. None of the five exit-condition numbers is met
// yet; the test names each one.
TEST(PerfStage4, SelectPopupLocksTheBasePage) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Stage0Page page;
    ASSERT_TRUE(page.load());
    const zui::perf::FrameWork open = page.rig.click_work("pick");
    ZUI_FILE_INFO("[stage4] open laid={} created={} painted={} base={} overlay={} items={}",
                  open.laid_out, open.boxes_created, open.painted_walked, open.world_base_gen,
                  open.world_overlay_gen, open.paint_items);
    // Exit condition: the base page's laid_out == 0. Today the popup drags 13 boxes through layout.
    EXPECT_EQ(open.laid_out, 13u);
    // Exit condition: the popup's own box creation is the right cost (3 options + their text boxes).
    EXPECT_EQ(open.boxes_created, 7u);
    // Exit condition: painted_walked counts the popup only. Today the page walk is 71, and closing
    // it is 64: the popup adds the same 7 boxes either way.
    EXPECT_EQ(open.painted_walked, 71u);
    // Exit condition: base_generation does not move; overlay_generation does.
    EXPECT_EQ(open.world_base_gen, 1u);
    EXPECT_EQ(open.world_overlay_gen, 0u);
    // Exit condition: the base page's Record is reused. Today the full stamp drop re-records them.
    EXPECT_EQ(open.paint_reused, 0u);
    EXPECT_EQ(open.paint_records, 3u);
    // capability B, slice 2 (landed): a top-layer member is painted by the pass after the document
    // walk, so no listing holds one. The open frame's listings hold 82 items against the closed
    // page's 80 - the control's own record grows by two items in its open state. The popup's three
    // rows contribute none; before the slice they were walked inside the document listing, and a
    // regression that let them back in would show up here as the rows' own drawing items.
    EXPECT_EQ(open.paint_items, 82u) << "the popup's rows must stay out of the listings";

    const zui::perf::FrameWork close = page.rig.click_work("pick");
    ZUI_FILE_INFO("[stage4] close laid={} created={} painted={} base={} overlay={} items={}",
                  close.laid_out, close.boxes_created, close.painted_walked, close.world_base_gen,
                  close.world_overlay_gen, close.paint_items);
    // Closing destroys the popup's boxes instead of creating them, and re-lays the control.
    EXPECT_EQ(close.laid_out, 9u);
    EXPECT_EQ(close.boxes_created, 0u);
    EXPECT_EQ(close.painted_walked, 64u);
    EXPECT_EQ(close.world_base_gen, 1u);
    EXPECT_EQ(close.world_overlay_gen, 0u);
    EXPECT_EQ(close.paint_reused, 0u);
    EXPECT_EQ(close.paint_items, 80u) << "closing the popup leaves the listings where they were";
}
