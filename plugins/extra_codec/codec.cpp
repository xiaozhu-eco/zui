#include "ext/plugin_export.h"
#include "image/driver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zui {

ZUI_PLUGIN_API void zui_plugin_extra_codec_register(image::DriverManager& manager);

namespace {

class ExtraDriver final : public image::Driver {
public:
    const char* name() const override { return "extra"; }

    std::vector<std::string> suffixes() const override { return {"zfx"}; }

    bool decode(const std::uint8_t*, std::size_t, image::Raster& out, std::string& error) const override {
        out.width = 1;
        out.height = 1;
        out.rgba = {9, 8, 7, 255};
        error.clear();
        return true;
    }

    bool decode_file(const std::string& path, const std::uint8_t* bytes, std::size_t size, image::Raster& out,
                     std::string& error) const override {
        if (path.empty() && (bytes == nullptr || size == 0)) {
            error = "empty extra";
            return false;
        }
        return decode(bytes, size, out, error);
    }

    bool encode(const image::Raster&, std::vector<std::uint8_t>&, std::string& error) const override {
        error = "extra";
        return false;
    }
};

}  // namespace

void zui_plugin_extra_codec_register(image::DriverManager& manager) {
    manager.add(std::make_unique<ExtraDriver>());
}

}  // namespace zui
