#include "ext/plugin_export.h"
#include "ext/slots.h"
#include "image/driver.h"
#include "image/image.h"

#include <zui/app.h>
#include <zui/host_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace zui {
ZUI_PLUGIN_API void zui_plugin_extra_codec_register(image::DriverManager& manager);
ZUI_PLUGIN_API void zui_plugin_view_commit_register(ExtensionRegistry& registry);
}  // namespace zui

namespace {

struct SlotBoot {
    SlotBoot() {
        zui::zui_plugin_view_commit_register(zui::ExtensionRegistry::instance());
        zui::image::DriverManager& images = zui::image::DriverManager::instance();
        zui::zui_plugin_extra_codec_register(images);
    }
};

const SlotBoot kSlotBoot;

class AppCommit final : public zui::DocumentCommit {
public:
    explicit AppCommit(zui::Document& document) : document_(document) {}

    bool set(std::string_view id, std::string_view field, std::string_view value) override {
        return document_.set(id, field, value);
    }
    bool add_class(std::string_view id, std::string_view name) override {
        return document_.add_class(id, name);
    }
    bool remove_class(std::string_view id, std::string_view name) override {
        return document_.remove_class(id, name);
    }
    void bind(std::string_view name, std::function<void(zui::Event&)> handler) override {
        document_.bind(name, std::move(handler));
    }
    bool set_model(std::string_view id, const zui::ListModel& model) override {
        return static_cast<bool>(document_.set_model(id, model));
    }

private:
    zui::Document& document_;
};

}  // namespace

TEST(PluginSlots, BuiltInCodecsRoundTripAndForeignSuffixDoesNotReplaceThem) {
    const zui::image::Driver* png = zui::image::DriverManager::instance().find("png");
    const zui::image::Driver* jpeg = zui::image::DriverManager::instance().find("jpeg");
    const zui::image::Driver* svg = zui::image::DriverManager::instance().find("svg");
    const zui::image::Driver* extra = zui::image::DriverManager::instance().find("zfx");
    ASSERT_NE(png, nullptr);
    ASSERT_NE(jpeg, nullptr);
    ASSERT_NE(svg, nullptr);
    ASSERT_NE(extra, nullptr);
    EXPECT_STREQ(png->name(), "PNG");
    EXPECT_STREQ(jpeg->name(), "JPEG");
    EXPECT_STREQ(svg->name(), "SVG");
    EXPECT_NE(extra, png);

    zui::image::Raster src;
    src.width = 2;
    src.height = 1;
    src.rgba = {10, 20, 30, 255, 40, 50, 60, 128};
    std::vector<std::uint8_t> encoded;
    std::string error;
    ASSERT_TRUE(png->encode(src, encoded, error)) << error;
    zui::image::Raster decoded;
    ASSERT_TRUE(png->decode(encoded.data(), encoded.size(), decoded, error)) << error;
    EXPECT_EQ(decoded.rgba, src.rgba);

    src.rgba.assign(8 * 8 * 4, 0);
    src.width = 8;
    src.height = 8;
    for (int i = 0; i < 64; ++i) {
        src.rgba[static_cast<std::size_t>(i) * 4 + 0] = 220;
        src.rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    encoded.clear();
    ASSERT_TRUE(jpeg->encode(src, encoded, error)) << error;
    ASSERT_TRUE(jpeg->decode(encoded.data(), encoded.size(), decoded, error)) << error;
    EXPECT_EQ(decoded.width, 8);
    EXPECT_EQ(decoded.height, 8);
    EXPECT_EQ(decoded.rgba.size(), src.rgba.size());

    const char* rect =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#ff0000\"/></svg>";
    ASSERT_TRUE(svg->decode(reinterpret_cast<const std::uint8_t*>(rect), std::strlen(rect), decoded, error))
        << error;
    EXPECT_EQ(decoded.width, 4);
    EXPECT_EQ(decoded.height, 4);
    EXPECT_EQ(decoded.rgba.size(), 64u);

    const std::uint8_t foreign[] = {1, 2, 3};
    ASSERT_TRUE(extra->decode(foreign, sizeof(foreign), decoded, error)) << error;
    ASSERT_EQ(decoded.rgba.size(), 4u);
    EXPECT_EQ(decoded.rgba[0], 9);
    EXPECT_EQ(decoded.rgba[1], 8);
    EXPECT_EQ(decoded.rgba[2], 7);
    EXPECT_EQ(decoded.rgba[3], 255);
    EXPECT_STREQ(zui::image::DriverManager::instance().find("png")->name(), "PNG");
}

TEST(PluginSlots, ViewCommitWritesThroughTheDocument) {
    const std::filesystem::path page = std::filesystem::temp_directory_path() / "zui-view-commit.html";
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><body><p id=\"plug\">old</p></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    AppCommit commit(app.document());
    zui::ExtensionRegistry::instance().mount_views(commit);
    EXPECT_EQ(app.document().get("plug", "text"), "from-view");
}

TEST(PluginSlots, OpenerWithoutPluginDoesNotRun) {
    zui::App app(0, nullptr);
    const zui::HostServices services = app.host().services();
    zui::plugin::OpenerSpec spec;
    spec.app_name = "Album";
    spec.prog_id = "ZuiNoPlugin";
    spec.executable = "C:\\apps\\zui_album.exe";
    spec.extension = ".png";
    spec.mime = "image/png";
    spec.scheme = "zui-album";
    const zui::plugin::OpenerReport registered = services.register_opener(spec);
    EXPECT_FALSE(registered.ran);
    EXPECT_FALSE(registered.ok);
    EXPECT_EQ(registered.os_error, 0u);
    EXPECT_TRUE(registered.desktop_entry.empty());
    zui::plugin::OpenerSpec by_ext;
    by_ext.extension = ".png";
    const zui::plugin::OpenerReport queried = services.query_opener(by_ext);
    EXPECT_FALSE(queried.ran);
    EXPECT_FALSE(queried.ok);
    const zui::plugin::OpenerReport removed = services.unregister_opener(spec);
    EXPECT_FALSE(removed.ran);
    EXPECT_FALSE(removed.ok);
    const zui::plugin::OpenerReport launched = services.launch_opener(spec, "photo.png");
    EXPECT_FALSE(launched.ran);
    EXPECT_FALSE(launched.ok);
}

TEST(PluginSlots, HostClipboardWithoutWindowAndDataDir) {
    zui::App app(0, nullptr);
    const zui::ClipboardText text = app.host().clipboard_text();
    EXPECT_EQ(text.status, zui::ClipboardStatus::Unsupported);
    const std::filesystem::path dir = app.host().user_data_dir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(std::filesystem::is_directory(dir));
}
