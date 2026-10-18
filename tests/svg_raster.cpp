#include "image/svg/render/svg_probe.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error;
};

Pixels rasterize(const char* svg) {
    Pixels out;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(svg);
    if (!zui::image::svg::probe_svg_raster(bytes, std::strlen(svg), out.width, out.height, out.rgba, out.error)) {
        out.width = 0;
        out.height = 0;
        out.rgba.clear();
    }
    return out;
}

void require_device(const Pixels& pixels) {
    if (pixels.error == "vulkan") {
        GTEST_SKIP() << "Vulkan device was not created";
    }
}

std::string grid(const Pixels& pixels) {
    std::ostringstream os;
    os << pixels.error << " " << pixels.width << "x" << pixels.height << "\n";
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width) +
                                   static_cast<std::size_t>(x)) *
                                  4;
            os << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(pixels.rgba[i])
               << std::setw(2) << static_cast<int>(pixels.rgba[i + 1]) << std::setw(2)
               << static_cast<int>(pixels.rgba[i + 2]) << std::setw(2) << static_cast<int>(pixels.rgba[i + 3])
               << ' ';
        }
        os << '\n';
    }
    return os.str();
}

void expect_pixel(const Pixels& pixels, int x, int y, int r, int g, int b, int a) {
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width) + static_cast<std::size_t>(x)) * 4;
    ASSERT_LT(i + 3, pixels.rgba.size()) << grid(pixels);
    EXPECT_EQ(static_cast<int>(pixels.rgba[i + 0]), r) << "x=" << x << " y=" << y << "\n" << grid(pixels);
    EXPECT_EQ(static_cast<int>(pixels.rgba[i + 1]), g) << "x=" << x << " y=" << y << "\n" << grid(pixels);
    EXPECT_EQ(static_cast<int>(pixels.rgba[i + 2]), b) << "x=" << x << " y=" << y << "\n" << grid(pixels);
    EXPECT_EQ(static_cast<int>(pixels.rgba[i + 3]), a) << "x=" << x << " y=" << y << "\n" << grid(pixels);
}

void expect_solid(const Pixels& pixels, int r, int g, int b, int a) {
    ASSERT_FALSE(pixels.rgba.empty()) << pixels.error;
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            expect_pixel(pixels, x, y, r, g, b, a);
        }
    }
}

}  // namespace

TEST(SvgRaster, FullRedRect) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<rect width=\"8\" height=\"8\" fill=\"#ff0000\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_solid(pixels, 255, 0, 0, 255);
}

TEST(SvgRaster, LeftHalfGreen) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<rect width=\"4\" height=\"8\" fill=\"#00ff00\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            expect_pixel(pixels, x, y, 0, 255, 0, 255);
        }
        for (int x = 4; x < 8; ++x) {
            expect_pixel(pixels, x, y, 0, 0, 0, 0);
        }
    }
}

TEST(SvgRaster, CircleCoversCenterNotCorner) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<circle cx=\"4\" cy=\"4\" r=\"3\" fill=\"#0000ff\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_pixel(pixels, 4, 4, 0, 0, 255, 255);
    // Pixel [4,5] x [1,2] straddles the radius. 15 of 16 samples are inside.
    expect_pixel(pixels, 4, 1, 0, 0, 255, 239);
    expect_pixel(pixels, 0, 0, 0, 0, 0, 0);
    expect_pixel(pixels, 4, 0, 0, 0, 0, 0);
}

TEST(SvgRaster, VerticalEdgeCoverage) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<rect width=\"4.25\" height=\"8\" fill=\"#ff0000\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_pixel(pixels, 3, 4, 255, 0, 0, 255);
    // One of four sample columns sits left of x=4.25, so coverage is 1/4.
    expect_pixel(pixels, 4, 4, 255, 0, 0, 64);
    expect_pixel(pixels, 5, 4, 0, 0, 0, 0);
}

TEST(SvgRaster, StrokeEdgeCoverage) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<line x1=\"0\" y1=\"4\" x2=\"8\" y2=\"4\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"0.5\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_pixel(pixels, 4, 3, 255, 0, 0, 64);
    expect_pixel(pixels, 4, 4, 255, 0, 0, 64);
    expect_pixel(pixels, 4, 2, 0, 0, 0, 0);
    expect_pixel(pixels, 4, 5, 0, 0, 0, 0);
}

TEST(SvgRaster, HorizontalStroke) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<line x1=\"0\" y1=\"4\" x2=\"8\" y2=\"4\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"2\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_pixel(pixels, 4, 3, 255, 0, 0, 255);
    expect_pixel(pixels, 4, 4, 255, 0, 0, 255);
    expect_pixel(pixels, 4, 2, 0, 0, 0, 0);
    expect_pixel(pixels, 4, 5, 0, 0, 0, 0);
}

TEST(SvgRaster, GroupOpacityHalvesAlpha) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<g opacity=\"0.5\"><rect width=\"8\" height=\"8\" fill=\"#ff0000\"/></g>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    expect_solid(pixels, 255, 0, 0, 128);
}

TEST(SvgRaster, HorizontalGradient) {
    const Pixels pixels = rasterize(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<defs><linearGradient id=\"g\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect width=\"8\" height=\"8\" fill=\"url(#g)\"/>"
        "</svg>");
    require_device(pixels);
    ASSERT_TRUE(pixels.error.empty()) << pixels.error;
    ASSERT_EQ(pixels.width, 8);
    ASSERT_EQ(pixels.height, 8);
    // t = (x + 0.5) / 8. x=0 → 15.9375 blue, x=7 → 239.0625 blue.
    expect_pixel(pixels, 0, 4, 239, 0, 16, 255);
    expect_pixel(pixels, 7, 4, 16, 0, 239, 255);
}
