#include "backends/group_alpha.h"

#include "paint/display_list.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

zui::paint::DisplayItem fill_rect(std::uint8_t alpha) {
    zui::paint::DisplayItem it;
    it.op = zui::paint::DisplayOp::FillRect;
    it.rect = zui::paint::Rect{1.f, 2.f, 3.f, 4.f};
    it.color = zui::paint::Color{9, 8, 7, alpha};
    zui::paint::GradientStop stop;
    stop.offset = 0.25f;
    stop.color = zui::paint::Color{1, 2, 3, alpha};
    it.gradient.stops.push_back(stop);
    return it;
}

zui::paint::DisplayItem begin_filter(float opacity) {
    zui::paint::DisplayItem it;
    it.op = zui::paint::DisplayOp::BeginFilter;
    it.filter.opacity = opacity;
    return it;
}

zui::paint::DisplayItem end_filter() {
    zui::paint::DisplayItem it;
    it.op = zui::paint::DisplayOp::EndFilter;
    return it;
}

std::vector<zui::paint::DisplayItem> replay(const std::vector<zui::paint::DisplayItem>& items) {
    std::vector<zui::paint::DisplayItem> drawn;
    zui::backends::GroupCursor cursor;
    zui::backends::submit_items(cursor, items, [&](const zui::paint::DisplayItem& it) {
        if (it.op == zui::paint::DisplayOp::FillRect) {
            drawn.push_back(it);
        }
    });
    return drawn;
}

}  // namespace

TEST(Host, UnmatchedBeginFilterScalesLaterPrimitives) {
    // 255 * 0.8 rounds to nearest the same way the GPU group factor does.
    constexpr std::uint8_t kOpaque = 255;
    constexpr std::uint8_t kScaled = 204;

    {
        const auto drawn =
            replay({begin_filter(0.8f), fill_rect(kOpaque), end_filter(), fill_rect(kOpaque)});
        ASSERT_EQ(drawn.size(), 2u);
        EXPECT_EQ(drawn[0].color.a, kScaled);
        EXPECT_EQ(drawn[0].gradient.stops.at(0).color.a, kScaled);
        EXPECT_EQ(drawn[1].color.a, kOpaque);
        EXPECT_EQ(drawn[1].gradient.stops.at(0).color.a, kOpaque);
    }
    {
        const auto drawn = replay({begin_filter(0.8f), fill_rect(kOpaque), fill_rect(kOpaque)});
        ASSERT_EQ(drawn.size(), 2u);
        EXPECT_EQ(drawn[0].color.a, kScaled);
        EXPECT_EQ(drawn[0].gradient.stops.at(0).color.a, kScaled);
        EXPECT_EQ(drawn[1].color.a, kScaled);
        EXPECT_EQ(drawn[1].gradient.stops.at(0).color.a, kScaled);
    }
    {
        const auto drawn = replay({begin_filter(0.8f), end_filter(), fill_rect(kOpaque)});
        ASSERT_EQ(drawn.size(), 1u);
        EXPECT_EQ(drawn[0].color.a, kOpaque);
        EXPECT_EQ(drawn[0].gradient.stops.at(0).color.a, kOpaque);
    }
}

TEST(Host, FilterBrightnessScalesRgbAndRestores) {
    zui::paint::DisplayItem bright;
    bright.op = zui::paint::DisplayOp::BeginFilter;
    bright.filter.brightness = 1.4f;
    zui::paint::DisplayItem dim;
    dim.op = zui::paint::DisplayOp::BeginFilter;
    dim.filter.brightness = 0.5f;
    zui::paint::DisplayItem fill;
    fill.op = zui::paint::DisplayOp::FillRect;
    fill.color = zui::paint::Color{124, 183, 255, 255};

    const auto drawn = replay({bright, fill, end_filter(), dim, fill, end_filter(), fill});
    ASSERT_EQ(drawn.size(), 3u);
    EXPECT_EQ(drawn[0].color.r, 174);
    EXPECT_EQ(drawn[0].color.g, 255);
    EXPECT_EQ(drawn[0].color.b, 255);
    EXPECT_EQ(drawn[0].color.a, 255);
    EXPECT_EQ(drawn[1].color.r, 62);
    EXPECT_EQ(drawn[1].color.g, 92);
    EXPECT_EQ(drawn[1].color.b, 128);
    EXPECT_EQ(drawn[2].color.r, 124);
    EXPECT_EQ(drawn[2].color.g, 183);
    EXPECT_EQ(drawn[2].color.b, 255);
}

TEST(Host, IsolateGroupKeepsAlphaAndEmitsMarkers) {
    zui::backends::GroupCursor cursor;
    cursor.isolate_groups = true;
    std::vector<zui::paint::DisplayOp> ops;
    std::vector<std::uint8_t> alphas;
    zui::backends::submit_items(
        cursor,
        {begin_filter(0.5f), fill_rect(255), end_filter(), fill_rect(255)},
        [&](const zui::paint::DisplayItem& it) {
            ops.push_back(it.op);
            if (it.op == zui::paint::DisplayOp::FillRect) {
                alphas.push_back(it.color.a);
            }
        });
    ASSERT_EQ(ops.size(), 4u);
    EXPECT_EQ(ops[0], zui::paint::DisplayOp::BeginFilter);
    EXPECT_EQ(ops[1], zui::paint::DisplayOp::FillRect);
    EXPECT_EQ(ops[2], zui::paint::DisplayOp::EndFilter);
    EXPECT_EQ(ops[3], zui::paint::DisplayOp::FillRect);
    ASSERT_EQ(alphas.size(), 2u);
    EXPECT_EQ(alphas[0], 255);
    EXPECT_EQ(alphas[1], 255);

    zui::paint::DisplayItem multiply;
    multiply.op = zui::paint::DisplayOp::BeginFilter;
    multiply.filter.blend = zui::paint::MixBlendMode::Multiply;
    ops.clear();
    zui::backends::GroupCursor blend_cursor;
    blend_cursor.isolate_groups = true;
    zui::backends::submit_items(blend_cursor, {multiply, fill_rect(255), end_filter()},
                                [&](const zui::paint::DisplayItem& it) { ops.push_back(it.op); });
    ASSERT_EQ(ops.size(), 3u);
    EXPECT_EQ(ops[0], zui::paint::DisplayOp::BeginFilter);
    EXPECT_EQ(ops[2], zui::paint::DisplayOp::EndFilter);
}

TEST(Host, IsolateBlurAndDropShadowEmitMarkers) {
    zui::paint::DisplayItem blur;
    blur.op = zui::paint::DisplayOp::BeginFilter;
    blur.filter.blur = 4.f;
    zui::paint::DisplayItem shadow;
    shadow.op = zui::paint::DisplayOp::BeginFilter;
    zui::paint::DropShadow drop;
    drop.dx = 6.f;
    drop.dy = 8.f;
    drop.blur = 2.f;
    shadow.filter.shadows.push_back(drop);

    auto markers = [](const zui::paint::DisplayItem& begin) {
        std::vector<zui::paint::DisplayOp> ops;
        std::vector<std::uint8_t> alphas;
        zui::backends::GroupCursor cursor;
        cursor.isolate_groups = true;
        zui::backends::submit_items(cursor, {begin, fill_rect(255), end_filter()},
                                    [&](const zui::paint::DisplayItem& it) {
                                        ops.push_back(it.op);
                                        if (it.op == zui::paint::DisplayOp::FillRect) {
                                            alphas.push_back(it.color.a);
                                        }
                                    });
        return std::pair<std::vector<zui::paint::DisplayOp>, std::vector<std::uint8_t>>{ops,
                                                                                         alphas};
    };
    {
        const auto [ops, alphas] = markers(blur);
        ASSERT_EQ(ops.size(), 3u);
        EXPECT_EQ(ops[0], zui::paint::DisplayOp::BeginFilter);
        EXPECT_EQ(ops[2], zui::paint::DisplayOp::EndFilter);
        ASSERT_EQ(alphas.size(), 1u);
        EXPECT_EQ(alphas[0], 255);
    }
    {
        const auto [ops, alphas] = markers(shadow);
        ASSERT_EQ(ops.size(), 3u);
        EXPECT_EQ(ops[0], zui::paint::DisplayOp::BeginFilter);
        ASSERT_EQ(alphas.size(), 1u);
        EXPECT_EQ(alphas[0], 255);
    }
}
