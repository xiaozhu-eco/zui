#include "image/image.h"

#include "image/driver.h"
#include "image/svg/gpu_hook.h"

#include <fstream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace image {
namespace {

struct Owned {
    Bitmap bitmap;
    std::shared_ptr<std::vector<std::uint8_t>> pixels;
};

std::unordered_map<std::string, Owned>& cache() {
    static std::unordered_map<std::string, Owned> c;
    return c;
}

std::uint64_t& decode_count_ref() {
    static std::uint64_t count = 0;
    return count;
}

std::unordered_map<std::string, std::pair<int, int>>& intrinsic_memo() {
    static std::unordered_map<std::string, std::pair<int, int>> memo;
    return memo;
}

// Suffix of the filename only. "dir.name/photo.JPEG" is "JPEG"; a dot in a
// directory does not count. image::load maps this string onto a Driver.
std::string suffix_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t base = slash == std::string::npos ? 0 : slash + 1;
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < base || dot + 1 >= path.size()) {
        return {};
    }
    return path.substr(dot + 1);
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end <= 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(end));
    in.read(reinterpret_cast<char*>(bytes.data()), end);
    return static_cast<bool>(in);
}

}  // namespace

const Bitmap* load(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    auto& c = cache();
    auto found = c.find(path);
    if (found != c.end()) {
        return found->second.bitmap.rgba != nullptr ? &found->second.bitmap : nullptr;
    }
    const Driver* driver = DriverManager::instance().find(suffix_of(path));
    std::vector<std::uint8_t> bytes;
    Raster raster;
    std::string error;
    if (driver == nullptr || !read_file(path, bytes) ||
        !driver->decode_file(path, bytes.data(), bytes.size(), raster, error) ||
        raster.rgba.empty()) {
        c.emplace(path, Owned{});
        return nullptr;
    }
    Owned owned;
    owned.pixels = std::make_shared<std::vector<std::uint8_t>>(std::move(raster.rgba));
    owned.bitmap.width = raster.width;
    owned.bitmap.height = raster.height;
    owned.bitmap.rgba = owned.pixels->data();
    auto inserted = c.emplace(path, std::move(owned));
    ++decode_count_ref();
    return &inserted.first->second.bitmap;
}

const Bitmap* load_fitted(const std::string& path, int max_width, int max_height) {
    if (path.empty() || max_width <= 0 || max_height <= 0 || suffix_of(path) != "svg") {
        return load(path);
    }
    const std::string key = path + "#" + std::to_string(max_width) + "x" + std::to_string(max_height);
    auto& c = cache();
    auto found = c.find(key);
    if (found != c.end()) {
        return found->second.bitmap.rgba != nullptr ? &found->second.bitmap : nullptr;
    }
    const Driver* driver = DriverManager::instance().find("svg");
    std::vector<std::uint8_t> bytes;
    Raster raster;
    std::string error;
    svg_decode_set_fit(max_width, max_height);
    const bool decoded = driver != nullptr && read_file(path, bytes) &&
                         driver->decode_file(path, bytes.data(), bytes.size(), raster, error) &&
                         !raster.rgba.empty();
    svg_decode_set_fit(0, 0);
    if (!decoded) {
        c.emplace(key, Owned{});
        return nullptr;
    }
    Owned owned;
    owned.pixels = std::make_shared<std::vector<std::uint8_t>>(std::move(raster.rgba));
    owned.bitmap.width = raster.width;
    owned.bitmap.height = raster.height;
    owned.bitmap.rgba = owned.pixels->data();
    auto inserted = c.emplace(key, std::move(owned));
    ++decode_count_ref();
    return &inserted.first->second.bitmap;
}

bool intrinsic_size(const std::string& path, int& width, int& height) {
    width = 0;
    height = 0;
    if (path.empty()) {
        return false;
    }
    auto& memo = intrinsic_memo();
    auto known = memo.find(path);
    if (known != memo.end()) {
        width = known->second.first;
        height = known->second.second;
        return width > 0 && height > 0;
    }
    if (suffix_of(path) != "svg") {
        const Bitmap* bitmap = load(path);
        if (bitmap == nullptr || bitmap->width <= 0 || bitmap->height <= 0) {
            return false;
        }
        width = bitmap->width;
        height = bitmap->height;
        memo.emplace(path, std::make_pair(width, height));
        return true;
    }
    const Driver* driver = DriverManager::instance().find("svg");
    std::vector<std::uint8_t> bytes;
    Raster raster;
    std::string error;
    svg_decode_set_probe(true);
    const bool decoded = driver != nullptr && read_file(path, bytes) &&
                         driver->decode_file(path, bytes.data(), bytes.size(), raster, error);
    svg_decode_set_probe(false);
    int probed_w = 0;
    int probed_h = 0;
    svg_decode_probed(probed_w, probed_h);
    if (!decoded || probed_w <= 0 || probed_h <= 0) {
        return false;
    }
    width = probed_w;
    height = probed_h;
    memo.emplace(path, std::make_pair(width, height));
    return true;
}

void invalidate(const std::string& path) {
    if (path.empty()) {
        return;
    }
    auto& c = cache();
    c.erase(path);
    const std::string prefix = path + "#";
    for (auto it = c.begin(); it != c.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            it = c.erase(it);
        } else {
            ++it;
        }
    }
    intrinsic_memo().erase(path);
}

std::uint64_t decode_count() { return decode_count_ref(); }

}  // namespace image
}  // namespace zui
