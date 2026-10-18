#include "ext/plugin_export.h"
#include "ext/slots.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace zui {
namespace {

bool newer_version(std::string_view current, std::string_view next) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    const std::string a(current);
    const std::string b(next);
    const long left = std::strtol(a.c_str(), &end_a, 10);
    const long right = std::strtol(b.c_str(), &end_b, 10);
    if (end_a != a.c_str() && *end_a == '\0' && end_b != b.c_str() && *end_b == '\0') {
        return right > left;
    }
    return next > current;
}

class LocalUpdate final : public UpdateService {
public:
    plugin::UpdateReport apply(std::string_view manifest_path, std::string_view current_version,
                               std::string_view dest_dir) override {
        plugin::UpdateReport report;
        std::ifstream in{std::filesystem::path(std::string(manifest_path))};
        if (!in) {
            return report;
        }
        std::string version;
        std::string payload;
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, 8, "version=") == 0) {
                version = line.substr(8);
            } else if (line.compare(0, 8, "payload=") == 0) {
                payload = line.substr(8);
            }
        }
        if (version.empty() || !newer_version(current_version, version)) {
            report.version = version;
            return report;
        }
        report.newer = true;
        report.version = version;
        const std::filesystem::path manifest{std::string(manifest_path)};
        const std::filesystem::path source = manifest.parent_path() / payload;
        const std::filesystem::path dest = std::filesystem::path(std::string(dest_dir)) / source.filename();
        std::error_code error;
        std::filesystem::create_directories(dest.parent_path(), error);
        std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return report;
        }
        report.applied = true;
        report.payload = dest.string();
        return report;
    }
};

}  // namespace

void zui_plugin_update_register(ExtensionRegistry& registry) {
    registry.add_update(std::make_unique<LocalUpdate>());
}

}  // namespace zui
