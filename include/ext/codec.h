#ifndef ZUI_EXT_CODEC_H
#define ZUI_EXT_CODEC_H

// Public image-codec catalog. A plugin receives the catalog the core passes in
// and calls add. This header depends only on the raster value.

#include <zui/raster.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace image {

class DriverManager;

// One Driver owns one codec family. The same driver may claim several suffixes
// (JPEG claims both "jpg" and "jpeg"). image::load selects a driver from the
// file suffix. It does not sniff the bytes.
class Driver {
public:
    virtual ~Driver() = default;
    virtual const char* name() const = 0;
    /// Lowercase suffixes without a leading dot.
    virtual std::vector<std::string> suffixes() const = 0;
    virtual bool decode(const std::uint8_t* bytes, std::size_t size, Raster& out,
                        std::string& error) const = 0;
    /// `path` is the file the bytes were read from. There is no default that drops it.
    virtual bool decode_file(const std::string& path, const std::uint8_t* bytes, std::size_t size,
                             Raster& out, std::string& error) const = 0;
    virtual bool encode(const Raster& image, std::vector<std::uint8_t>& out,
                        std::string& error) const = 0;
};

/// Suffix catalog the core passes to a codec plugin.
/// `add` is not virtual, so a subclass cannot replace the table.
/// The first registration for a suffix wins. A default-constructed catalog is not
/// something the process reads: only `DriverManager` can construct one.
class ImageCodecs {
public:
    /// True when this driver claimed at least one suffix that was still free.
    /// A suffix that is already claimed stays with the first driver.
    bool add(std::unique_ptr<Driver> driver) {
        if (driver == nullptr) {
            return false;
        }
        const std::vector<std::string> listed = driver->suffixes();
        std::vector<std::string> claimed;
        claimed.reserve(listed.size());
        for (const std::string& suffix : listed) {
            const std::string key = normalize(suffix);
            if (key.empty() || find_key(key) != nullptr) {
                continue;
            }
            claimed.push_back(key);
        }
        if (claimed.empty()) {
            return false;
        }
        const Driver* raw = driver.get();
        drivers_.push_back(std::move(driver));
        for (const std::string& key : claimed) {
            by_suffix_.emplace_back(key, raw);
        }
        return true;
    }

    /// `suffix` may be "png", ".JPG", or "jpeg". Unknown suffixes return null.
    const Driver* find(std::string_view suffix) const { return find_key(normalize(suffix)); }

protected:
    ~ImageCodecs() = default;

private:
    friend class DriverManager;
    struct Key {
        explicit Key() = default;
    };

    explicit ImageCodecs(Key) {}

    static std::string normalize(std::string_view suffix) {
        std::string key(suffix);
        if (!key.empty() && key.front() == '.') {
            key.erase(key.begin());
        }
        for (char& c : key) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return key;
    }

    const Driver* find_key(const std::string& key) const {
        if (key.empty()) {
            return nullptr;
        }
        for (const auto& entry : by_suffix_) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return nullptr;
    }

    std::vector<std::unique_ptr<Driver>> drivers_;
    std::vector<std::pair<std::string, const Driver*>> by_suffix_;
};

}  // namespace image
}  // namespace zui

#endif
