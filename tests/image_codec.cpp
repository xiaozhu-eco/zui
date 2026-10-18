#include "image/driver.h"
#include "image/image.h"
#include "image/svg/parser/parser.h"
#include "image/svg/scene.h"
#include "image/svg/types/gpu_layout.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "zui-image-codec";
    std::filesystem::create_directories(dir);
    return dir;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(out));
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(static_cast<bool>(out));
}

const zui::image::Driver* driver_for(const char* suffix) {
    const zui::image::Driver* found = zui::image::DriverManager::instance().find(suffix);
    EXPECT_NE(found, nullptr) << suffix;
    return found;
}

}  // namespace

TEST(ImageDriver, PngRoundTripUsesSuffix) {
    zui::image::Raster src;
    src.width = 2;
    src.height = 1;
    src.rgba = {10, 20, 30, 255, 40, 50, 60, 128};
    std::vector<std::uint8_t> encoded;
    std::string error;
    ASSERT_TRUE(driver_for("png")->encode(src, encoded, error)) << error;

    const std::filesystem::path path = scratch() / "sample.PNG";
    write_bytes(path, encoded);
    const std::string key = path.string();
    zui::image::invalidate(key);
    const zui::image::Bitmap* bitmap = zui::image::load(key);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(bitmap->width, 2);
    EXPECT_EQ(bitmap->height, 1);
    ASSERT_NE(bitmap->rgba, nullptr);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(bitmap->rgba[i], src.rgba[static_cast<std::size_t>(i)]) << i;
    }
}

TEST(ImageDriver, JpgAndJpegShareOneDriver) {
    const zui::image::Driver* jpg = driver_for("jpg");
    const zui::image::Driver* jpeg = driver_for(".JPEG");
    ASSERT_NE(jpg, nullptr);
    EXPECT_EQ(jpg, jpeg);
    EXPECT_STREQ(jpg->name(), "JPEG");

    zui::image::Raster src;
    src.width = 8;
    src.height = 8;
    src.rgba.assign(static_cast<std::size_t>(8 * 8 * 4), 0);
    for (int i = 0; i < 8 * 8; ++i) {
        src.rgba[static_cast<std::size_t>(i) * 4 + 0] = 220;
        src.rgba[static_cast<std::size_t>(i) * 4 + 1] = 30;
        src.rgba[static_cast<std::size_t>(i) * 4 + 2] = 40;
        src.rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    std::vector<std::uint8_t> encoded;
    std::string error;
    ASSERT_TRUE(jpg->encode(src, encoded, error)) << error;

    for (const char* name : {"flat.jpg", "flat.jpeg"}) {
        const std::filesystem::path path = scratch() / name;
        write_bytes(path, encoded);
        const std::string key = path.string();
        zui::image::invalidate(key);
        const zui::image::Bitmap* bitmap = zui::image::load(key);
        ASSERT_NE(bitmap, nullptr) << name;
        EXPECT_EQ(bitmap->width, 8);
        EXPECT_EQ(bitmap->height, 8);
        EXPECT_NEAR(static_cast<int>(bitmap->rgba[0]), 220, 8);
        EXPECT_NEAR(static_cast<int>(bitmap->rgba[1]), 30, 8);
        EXPECT_NEAR(static_cast<int>(bitmap->rgba[2]), 40, 8);
        EXPECT_EQ(bitmap->rgba[3], 255);
    }
}

TEST(ImageDriver, SuffixWinsOverBytes) {
    zui::image::Raster src;
    src.width = 1;
    src.height = 1;
    src.rgba = {1, 2, 3, 255};
    std::vector<std::uint8_t> encoded;
    std::string error;
    ASSERT_TRUE(driver_for("png")->encode(src, encoded, error)) << error;

    const std::filesystem::path path = scratch() / "really-png.jpg";
    write_bytes(path, encoded);
    const std::string key = path.string();
    zui::image::invalidate(key);
    EXPECT_EQ(zui::image::load(key), nullptr);
}

TEST(ImageDriver, UnknownSuffix) {
    const std::filesystem::path path = scratch() / "note.svg";
    write_bytes(path, {1, 2, 3, 4});
    const std::string key = path.string();
    zui::image::invalidate(key);
    EXPECT_EQ(zui::image::load(key), nullptr);
    EXPECT_EQ(zui::image::DriverManager::instance().find("gif"), nullptr);
    EXPECT_EQ(zui::image::DriverManager::instance().find("bmp"), nullptr);
}

TEST(ImageDriver, SvgDriverReportsBackendAndRefusesEncode) {
    const zui::image::Driver* svg = driver_for("svg");
    const zui::image::Driver* dotted = driver_for(".SVG");
    ASSERT_NE(svg, nullptr);
    EXPECT_EQ(svg, dotted);
    EXPECT_STREQ(svg->name(), "SVG");

    const char* rect =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#ff0000\"/>"
        "</svg>";
    zui::image::svg::Document document;
    std::string error;
    ASSERT_TRUE(zui::image::svg::Parser{}.parse(rect, document, error)) << error;
    EXPECT_EQ(document.width, 4.f);
    EXPECT_EQ(document.height, 4.f);
    EXPECT_EQ(document.shape_count(), 1);
    zui::image::svg::Scene scene;
    ASSERT_TRUE(scene.build(document, {}, error)) << error;
    ASSERT_EQ(scene.shapes.size(), 1u);
    EXPECT_EQ(scene.curves.size(), 4u);
    EXPECT_EQ(scene.curves[0].kind, 1);

    zui::image::Raster raster;
    EXPECT_TRUE(svg->decode(reinterpret_cast<const std::uint8_t*>(rect), std::strlen(rect), raster, error)) << error;
    EXPECT_EQ(raster.width, 4);
    EXPECT_EQ(raster.height, 4);
    EXPECT_EQ(raster.rgba.size(), 64u);

    error.clear();
    EXPECT_FALSE(svg->decode(reinterpret_cast<const std::uint8_t*>("nope"), 4, raster, error));
    EXPECT_STRNE(error.c_str(), zui::image::svg::kSvgUnsupported);

    std::vector<std::uint8_t> encoded;
    EXPECT_FALSE(svg->encode(raster, encoded, error));
    EXPECT_STREQ(error.c_str(), zui::image::svg::kSvgEncodeUnsupported);
}

TEST(ImageDriver, SvgGroupOpacityAndDefs) {
    const char* markup =
        "<svg width=\"8\" height=\"8\">"
        "<defs><rect id=\"hidden\" width=\"8\" height=\"8\" fill=\"#000000\"/></defs>"
        "<g opacity=\"0.5\"><rect width=\"8\" height=\"8\" fill=\"#00ff00\"/></g>"
        "</svg>";
    zui::image::svg::Document document;
    std::string error;
    ASSERT_TRUE(zui::image::svg::Parser{}.parse(markup, document, error)) << error;
    EXPECT_EQ(document.shape_count(), 1);
    zui::image::svg::Scene scene;
    ASSERT_TRUE(scene.build(document, {}, error)) << error;
    ASSERT_EQ(scene.commands.size(), 3u);
    EXPECT_EQ(scene.commands[0].op, zui::image::svg::CmdOp::Push);
    EXPECT_EQ(scene.commands[1].op, zui::image::svg::CmdOp::Shape);
    EXPECT_EQ(scene.commands[2].op, zui::image::svg::CmdOp::Pop);
    EXPECT_NEAR(scene.commands[0].opacity, 0.5f, 0.001f);
}

TEST(ImageDriver, AlbumFerryParses) {
    const std::string path = std::string(ZUI_SOURCE_DIR) + "/example/applications/album/pictures/ferry.svg";
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in)) << path;
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    zui::image::svg::Document document;
    std::string error;
    ASSERT_TRUE(zui::image::svg::Parser{}.parse(xml, document, error)) << error;
    EXPECT_EQ(document.width, 1200.f);
    EXPECT_EQ(document.height, 800.f);
    EXPECT_EQ(document.gradients.size(), 2u);
    EXPECT_GT(document.shape_count(), 0);
    zui::image::svg::Scene scene;
    ASSERT_TRUE(scene.build(document, path, error)) << error;
    EXPECT_EQ(scene.shapes.size(), static_cast<std::size_t>(document.shape_count()));
}

namespace {

class SuffixClaim final : public zui::image::Driver {
public:
    SuffixClaim(const char* driver_name, std::string suffix)
        : name_(driver_name), suffix_(std::move(suffix)) {}

    const char* name() const override { return name_; }
    std::vector<std::string> suffixes() const override { return {suffix_}; }
    bool decode(const std::uint8_t*, std::size_t, zui::image::Raster&, std::string& error) const override {
        error = "unused";
        return false;
    }
    bool decode_file(const std::string&, const std::uint8_t*, std::size_t, zui::image::Raster&,
                     std::string& error) const override {
        error = "unused";
        return false;
    }
    bool encode(const zui::image::Raster&, std::vector<std::uint8_t>&, std::string& error) const override {
        error = "unused";
        return false;
    }

private:
    const char* name_;
    std::string suffix_;
};

}  // namespace

TEST(ImageDriver, LaterRegistrationDoesNotReplaceAClaimedSuffix) {
    zui::image::DriverManager& catalog = zui::image::DriverManager::instance();
    const zui::image::Driver* png = catalog.find("png");
    ASSERT_NE(png, nullptr);
    EXPECT_FALSE(catalog.add(std::make_unique<SuffixClaim>("LATER", "png")));
    EXPECT_EQ(catalog.find("png"), png);
    EXPECT_STREQ(catalog.find("png")->name(), "PNG");

    EXPECT_TRUE(catalog.add(std::make_unique<SuffixClaim>("FIRST", "zclaim")));
    const zui::image::Driver* first = catalog.find("zclaim");
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->name(), "FIRST");
    EXPECT_FALSE(catalog.add(std::make_unique<SuffixClaim>("SECOND", "zclaim")));
    EXPECT_EQ(catalog.find("zclaim"), first);
    EXPECT_STREQ(catalog.find("zclaim")->name(), "FIRST");
}

TEST(ImageDriver, FixturePng) {
    const std::string path = std::string(ZUI_SOURCE_DIR) + "/tests/fixtures/dot.png";
    zui::image::invalidate(path);
    const zui::image::Bitmap* bitmap = zui::image::load(path);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_GT(bitmap->width, 0);
    EXPECT_GT(bitmap->height, 0);
    ASSERT_NE(bitmap->rgba, nullptr);
}
