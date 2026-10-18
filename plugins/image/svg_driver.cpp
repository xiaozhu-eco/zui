#include "image/driver.h"
#include "image/svg/parser/parser.h"
#include "image/svg/software_raster.h"
#include "image/svg/types/gpu_layout.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace image {
namespace {

int rounded(float value) {
    return static_cast<int>(std::lround(value));
}

class SvgDriver final : public Driver {
public:
    explicit SvgDriver(SvgGpuDecode gpu) : gpu_(gpu) {}

    const char* name() const override { return "SVG"; }

    std::vector<std::string> suffixes() const override { return {"svg"}; }

    bool decode(const std::uint8_t* bytes, std::size_t size, Raster& out, std::string& error) const override {
        return decode_file({}, bytes, size, out, error);
    }

    bool decode_file(const std::string& path, const std::uint8_t* bytes, std::size_t size, Raster& out,
                     std::string& error) const override {
        out = Raster{};
        if (bytes == nullptr || size == 0) {
            error = "svg root";
            return false;
        }
        svg::Document document;
        svg::Parser parser;
        const std::string_view xml(reinterpret_cast<const char*>(bytes), size);
        if (!parser.parse(xml, document, error)) {
            return false;
        }
        const int width = rounded(document.width);
        const int height = rounded(document.height);
        if (!dimensions_ok(width, height)) {
            error = "svg viewport";
            return false;
        }
        if (gpu_ != nullptr) {
            const int rendered = gpu_(&document, path, out, error);
            if (rendered != 0) {
                return rendered > 0;
            }
        }
        return svg::rasterize_software(document, out, error);
    }

    bool encode(const Raster&, std::vector<std::uint8_t>&, std::string& error) const override {
        error = svg::kSvgEncodeUnsupported;
        return false;
    }

private:
    SvgGpuDecode gpu_ = nullptr;
};

}  // namespace

void register_svg_driver(DriverManager& manager) {
    manager.add(std::make_unique<SvgDriver>(manager.svg_gpu()));
}

}  // namespace image
}  // namespace zui
