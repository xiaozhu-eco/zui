#ifndef ZUI_TESTS_APP_INTERACT_BROWSER_ORACLE_H
#define ZUI_TESTS_APP_INTERACT_BROWSER_ORACLE_H

// Checked-in readback from tests/app_interact/browser_oracle.py. The expected strings are what
// the browser showed for the same action. A missing file is a failure: do not fill it from the
// engine.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace zui::test {

inline nlohmann::json load_browser_oracle(std::string& error) {
    const std::filesystem::path path =
        std::filesystem::path(ZUI_SOURCE_DIR) / "tests" / "app_interact" / "browser_oracle.json";
    std::ifstream in(path);
    if (!in) {
        error = "missing browser oracle " + path.string();
        return {};
    }
    try {
        return nlohmann::json::parse(in);
    } catch (const std::exception& ex) {
        error = ex.what();
        return {};
    }
}

}  // namespace zui::test

#endif
