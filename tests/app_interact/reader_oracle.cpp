// Plays one reader interaction scenario against the native session and writes the
// same observation records the browser side produces. tests/interact/reader_oracle.py
// feeds both hosts the scenario file and diffs the records.
//
// Usage: zui_reader_oracle --scenario <file.json> --out <state.json>

#include "reader_harness.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

namespace {

nlohmann::json observe(zui::App& app, const nlohmann::json& query) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& item : query) {
        const std::string id = item.value("id", "");
        nlohmann::json node = nlohmann::json::object();
        for (const auto& field : item.value("fields", nlohmann::json::array())) {
            const std::string name = field.get<std::string>();
            node[name] = app.document().get(id, name);
        }
        out[id] = std::move(node);
    }
    return out;
}

void settle(reader_test::ReaderHarness& harness) {
    harness.settle([] { return false; }, 24);
}

bool play(reader_test::ReaderHarness& harness, const nlohmann::json& scenario,
          nlohmann::json& marks) {
    const nlohmann::json query = scenario.value("query", nlohmann::json::array());
    marks["boot"] = observe(harness.app, query);
    for (const auto& action : scenario.value("actions", nlohmann::json::array())) {
        const std::string op = action.value("do", "");
        if (op == "click") {
            const std::string id = action.value("id", "");
            if (!id.empty()) {
                zui::frame::Access::click_id(harness.app, id);
            }
            settle(harness);
        } else if (op == "key") {
            const std::string key = action.value("key", "");
            zui::frame::Access::key(harness.app, key);
            settle(harness);
        } else if (op == "mark") {
            marks[action.value("name", "")] = observe(harness.app, query);
        } else {
            std::cerr << "unknown action " << op << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenario_path;
    std::string out_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--scenario" && i + 1 < argc) {
            scenario_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        }
    }
    if (scenario_path.empty() || out_path.empty()) {
        std::cerr << "usage: zui_reader_oracle --scenario <file> --out <file>\n";
        return 2;
    }
    std::ifstream in(scenario_path);
    if (!in) {
        std::cerr << "cannot read " << scenario_path << "\n";
        return 2;
    }
    const nlohmann::json scenario = nlohmann::json::parse(in, nullptr, false);
    if (scenario.is_discarded()) {
        std::cerr << "invalid scenario json\n";
        return 2;
    }
    reader_test::ReaderHarness harness;
    nlohmann::json marks = nlohmann::json::object();
    if (!play(harness, scenario, marks)) {
        return 1;
    }
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "cannot write " << out_path << "\n";
        return 2;
    }
    out << marks.dump(2) << "\n";
    return 0;
}
