// Exhaustive interaction guard (docs/TEST_ARCHITECTURE.md 交互).
//
// `zui::frame::Access::interactive_ids` lists every element in a page that declares any handler
// attribute. `tests/interaction_census.json` is the checked-in inventory: one entry per page, each
// interactive id mapped to the test(s) that drive it. This guard asserts
//
//   1. the live interactive-id set equals the inventory (an added/removed handler fails loudly),
//   2. every id maps to at least one test, and every named test is registered in
//      tests/concern_manifest.json (so the id really is driven by a named case).
//
// Pages are loaded as plain documents (no session), so the inventory covers the static HTML and
// the `<template>` prototypes a model would instantiate. Session-generated rows reuse a template's
// id shape (`obj-{{index}}`), which the inventory records on the prototype line.

#include "app/frame_session.h"
#include "log/log.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path repo_root() { return fs::path(ZUI_SOURCE_DIR); }

/// The ids a page declares a handler on, sorted (the live truth `interactive_ids` returns).
std::vector<std::string> live_ids(const fs::path& page) {
    zui::App app(0, nullptr);
    if (!app.document().load(page.string())) {
        return {};
    }
    if (!zui::frame::Access::prepare_interaction(app)) {
        return {};
    }
    std::vector<std::string> ids = zui::frame::Access::interactive_ids(app);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

json load_json(const fs::path& path) {
    std::ifstream in(path);
    return json::parse(in);
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// `id` plus an `on*` attribute, in source order, from the raw page. The live set the engine
/// returns is still `interactive_ids`; this scan is only for controls that function cannot see.
std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> tags_of(
    const std::string& html) {
    static const std::regex tag_re(R"(<([A-Za-z][\w:-]*)\b([^<>]*)>)");
    static const std::regex attr_re(
        R"(([A-Za-z_:][\w:.-]*)\s*=\s*(\"[^\"]*\"|'[^']*'))");
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> tags;
    for (std::sregex_iterator it(html.begin(), html.end(), tag_re), end; it != end; ++it) {
        const std::string blob = (*it)[2].str();
        std::vector<std::pair<std::string, std::string>> attrs;
        for (std::sregex_iterator at(blob.begin(), blob.end(), attr_re), aend; at != aend; ++at) {
            std::string key = (*at)[1].str();
            std::string value = (*at)[2].str();
            for (char& ch : key) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            if (value.size() >= 2) {
                value = value.substr(1, value.size() - 2);
            }
            attrs.emplace_back(std::move(key), std::move(value));
        }
        tags.emplace_back((*it)[1].str(), std::move(attrs));
    }
    return tags;
}

bool is_handler_attr(const std::string& key) {
    return key.size() > 2 && key.compare(0, 2, "on") == 0;
}

std::vector<std::string> radio_ids(const std::string& html) {
    std::vector<std::string> ids;
    for (const auto& [tag, attrs] : tags_of(html)) {
        if (tag != "input" && tag != "INPUT") {
            std::string lower = tag;
            for (char& ch : lower) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            if (lower != "input") {
                continue;
            }
        }
        std::string ident;
        std::string type;
        for (const auto& [key, value] : attrs) {
            if (key == "id") {
                ident = value;
            } else if (key == "type") {
                type = value;
            }
        }
        for (char& ch : type) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        if (type == "radio" && !ident.empty()) {
            ids.push_back(ident);
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<std::string> unkeyed_handlers(const std::string& html) {
    std::vector<std::string> names;
    for (const auto& [tag, attrs] : tags_of(html)) {
        (void)tag;
        bool has_id = false;
        for (const auto& [key, value] : attrs) {
            if (key == "id" && !value.empty()) {
                has_id = true;
            }
        }
        if (has_id) {
            continue;
        }
        for (const auto& [key, value] : attrs) {
            if (is_handler_attr(key) && !value.empty()) {
                names.push_back(value);
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

struct CensusCase {
    std::string name;
    std::string body;
};

std::vector<CensusCase> test_bodies(const fs::path& dir) {
    std::vector<CensusCase> out;
    // A custom delimiter: the pattern itself contains )" .
    const std::regex mark(R"re(TEST\(AppInteract,\s*(\w+)\))re");
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
            continue;
        }
        const std::string text = read_text(entry.path());
        std::vector<std::pair<std::string, std::size_t>> marks;
        for (std::sregex_iterator it(text.begin(), text.end(), mark), end; it != end; ++it) {
            marks.emplace_back((*it)[1].str(), static_cast<std::size_t>(it->position()));
        }
        for (std::size_t i = 0; i < marks.size(); ++i) {
            const std::size_t end = i + 1 < marks.size() ? marks[i + 1].second : text.size();
            out.push_back(CensusCase{marks[i].first, text.substr(marks[i].second, end - marks[i].second)});
        }
    }
    return out;
}

bool asserts_readback(const std::string& body) {
    return body.find("EXPECT_") != std::string::npos || body.find("ASSERT_EQ") != std::string::npos ||
           body.find("ASSERT_NE") != std::string::npos || body.find("ASSERT_LT") != std::string::npos ||
           body.find("ASSERT_LE") != std::string::npos || body.find("ASSERT_GT") != std::string::npos ||
           body.find("ASSERT_GE") != std::string::npos || body.find("ASSERT_FALSE") != std::string::npos ||
           body.find("get(") != std::string::npos || body.find("visible(") != std::string::npos ||
           body.find("has_class") != std::string::npos || body.find("count_class") != std::string::npos ||
           body.find("class_texts") != std::string::npos || body.find("any_text") != std::string::npos;
}

const CensusCase* find_body(const std::vector<CensusCase>& bodies, const std::string& name) {
    for (const CensusCase& body : bodies) {
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

void expect_tests_assert(const std::string& page, const std::string& id, const json& tests,
                         const std::vector<std::string>& registered, const std::vector<CensusCase>& bodies) {
    ASSERT_FALSE(tests.empty()) << page << " id " << id << " maps to no test";
    bool any_registered = false;
    bool any_assertion = false;
    for (const json& test : tests) {
        const std::string bare = test.get<std::string>();
        const std::string full = "AppInteract." + bare;
        const bool found = std::find(registered.begin(), registered.end(), full) != registered.end();
        EXPECT_TRUE(found) << page << " id " << id << " names unregistered test " << full;
        any_registered = any_registered || found;
        const CensusCase* body = find_body(bodies, bare);
        if (body != nullptr && asserts_readback(body->body)) {
            any_assertion = true;
        } else {
            ADD_FAILURE() << page << " id " << id << " maps to " << full
                          << " which does not assert a response";
        }
    }
    EXPECT_TRUE(any_registered) << page << " id " << id << " has no registered test";
    EXPECT_TRUE(any_assertion) << page << " id " << id << " has no response assertion";
}

void expect_same_ids(const std::string& page, const std::string& kind, std::vector<std::string> live,
                     std::vector<std::string> expected) {
    std::sort(live.begin(), live.end());
    std::sort(expected.begin(), expected.end());
    for (const std::string& id : live) {
        if (std::find(expected.begin(), expected.end(), id) == expected.end()) {
            ADD_FAILURE() << page << " " << kind << " " << id << " is missing from the census";
        }
    }
    for (const std::string& id : expected) {
        if (std::find(live.begin(), live.end(), id) == live.end()) {
            ADD_FAILURE() << page << " " << kind << " " << id << " is not on the page";
        }
    }
    EXPECT_EQ(live, expected) << page << " " << kind << " set drifted from the census";
}

}  // namespace

TEST(AppInteract, DumpInteractionCensus) {
    const char* pages[] = {
        "example/applications/album/index.html",   "example/applications/calendar/index.html",
        "example/applications/chat/index.html",    "example/applications/contacts/index.html",
        "example/applications/kanban/index.html",  "example/applications/zdb/index.html",
        "example/applications/zmd/index.html",     "example/applications/reader/reader.html",
        "example/applications/share/index.html",
        "example/applications/file-explorer/index.html",
        "example/applications/particle/index.html",
        "example/applications/writer/index.html",
        "example/tests/hello/hello.html",          "example/tests/checks/checks.html",
        "example/tests/transition/transition.html", "example/tests/studio/studio.html",
        "example/tests/flow/flow.html",            "example/tests/style/style.html",
        "example/tests/table/table.html",          "example/tests/text/text.html",
    };
    for (const char* rel : pages) {
        const std::vector<std::string> ids = live_ids(repo_root() / rel);
        std::string line;
        for (const std::string& id : ids) {
            line += '"' + id + "\",";
        }
        ZUI_FILE_INFO("CENSUS {} :: {}", rel, line);
    }
}

TEST(AppInteract, InteractionCensusMatchesEveryPage) {
    const json census = load_json(repo_root() / "tests" / "interaction_census.json");
    const json manifest = load_json(repo_root() / "tests" / "concern_manifest.json");
    std::vector<std::string> registered;
    for (const json& entry : manifest.at("entries")) {
        registered.push_back(entry.at("name").get<std::string>());
    }
    const std::vector<CensusCase> bodies = test_bodies(repo_root() / "tests" / "app_interact");

    const fs::path applications = repo_root() / "example" / "applications";
    for (const fs::directory_entry& entry : fs::directory_iterator(applications)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string folder = entry.path().filename().string();
        const bool has_page = fs::is_regular_file(entry.path() / "index.html") ||
                              fs::is_regular_file(entry.path() / "reader.html");
        if (!has_page) {
            continue;
        }
        EXPECT_TRUE(census.contains(folder))
            << "example/applications page " << folder << " is missing from the census";
    }

    for (const auto& [name, page] : census.items()) {
        const std::string rel = page.at("file").get<std::string>();
        const fs::path file = repo_root() / rel;
        const bool radios = page.contains("controls") && page.at("controls") == "radio";
        const std::string html = radios || page.contains("unkeyed") || name == "share" ? read_text(file)
                                                                                        : std::string();

        std::vector<std::string> expected;
        for (const auto& [id, tests] : page.at("ids").items()) {
            expected.push_back(id);
        }

        if (radios) {
            const std::vector<std::string> live_radios = radio_ids(html);
            EXPECT_FALSE(live_radios.empty()) << name << " radio set is empty";
            expect_same_ids(name, "radio", live_radios, expected);
            expect_same_ids(name, "handler id", live_ids(file), {});
        } else {
            expect_same_ids(name, "handler id", live_ids(file), expected);
        }

        for (const auto& [id, tests] : page.at("ids").items()) {
            expect_tests_assert(name, id, tests, registered, bodies);
        }

        if (name == "share" || page.contains("unkeyed")) {
            const std::vector<std::string> live_unkeyed = unkeyed_handlers(html);
            std::vector<std::string> expected_unkeyed;
            const json unkeyed = page.contains("unkeyed") ? page.at("unkeyed") : json::object();
            for (const auto& [id, tests] : unkeyed.items()) {
                expected_unkeyed.push_back(id);
                expect_tests_assert(name, id, tests, registered, bodies);
            }
            EXPECT_FALSE(live_unkeyed.empty()) << name << " unkeyed handler set is empty";
            expect_same_ids(name, "unkeyed handler", live_unkeyed, expected_unkeyed);
        }
    }
}
