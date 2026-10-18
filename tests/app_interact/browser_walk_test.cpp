// Replays tests/app_interact/browser_oracle.json "walk" on each example/applications
// session. The expected readback is the browser's; this file only measures the engine.
// Set ZUI_ENGINE_WALK to a directory to dump those measurements as {page}.json.

#include "browser_oracle.h"
#include "handler_rig.h"
#include "reader_harness.h"

#include "../../example/applications/album/cpp/session.h"
#include "../../example/applications/calendar/cpp/session.h"
#include "../../example/applications/chat/cpp/session.h"
#include "../../example/applications/contacts/cpp/session.h"
#include "../../example/applications/kanban/cpp/session.h"
#include "../../example/applications/particle/cpp/session.h"
#include "../../example/applications/share/cpp/session.h"
#include "../../example/applications/writer/cpp/session.h"
#include "../../example/applications/zdb/cpp/session.h"
#include "ui/view.h"
#ifndef ZUI_WITHOUT_ZMD
#include "../../example/applications/zmd/cpp/session.h"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kRadios[] = {
    "dir-code",    "dir-docs",   "dir-images", "dir-notes",   "dir-reports", "dir-root",
    "file-card",   "file-catalog", "file-coast", "file-ferry", "file-guide",  "file-lantern",
    "file-ledger", "file-mark",  "file-none",  "file-shelf",  "file-theme",  "view-details",
    "view-grid",   "view-list",
};

const char* kShown[] = {
    "listing-root", "listing-docs", "listing-notes", "listing-reports", "listing-images",
    "listing-code", "crumbs-root", "crumbs-docs", "crumbs-notes", "crumbs-reports",
    "crumbs-images", "crumbs-code", "count-root", "count-docs", "count-notes",
    "count-reports", "count-images", "count-code", "cols", "preview-empty", "pv-name-empty",
    "pv-name-guide", "pv-name-lantern", "pv-name-ferry", "pv-name-ledger", "pv-name-shelf",
    "pv-name-mark", "pv-name-coast", "pv-name-card", "pv-name-theme", "pv-name-catalog",
};

fs::path app_html(const char* folder, const char* file = "index.html") {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / folder / file;
}

std::string jstr(const nlohmann::json& obj, const char* key) {
    if (!obj.contains(key) || obj.at(key).is_null()) {
        return {};
    }
    if (obj.at(key).is_string()) {
        return obj.at(key).get<std::string>();
    }
    if (obj.at(key).is_number_integer()) {
        return std::to_string(obj.at(key).get<long long>());
    }
    return {};
}

std::string trim_ws(std::string_view text) {
    auto space = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    std::size_t begin = 0;
    while (begin < text.size() && space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string text_content(const std::vector<zui::frame::NodeCensus>& rows, int index) {
    std::string out = rows[static_cast<std::size_t>(index)].text;
    for (int i = index + 1; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)].parent == index) {
            out += text_content(rows, i);
        }
    }
    return out;
}

bool wide(const zui::frame::NodeCensus& row) { return row.w > 0.5f && row.h > 0.5f; }

bool has_id(const std::vector<zui::frame::NodeCensus>& rows, std::string_view id) {
    if (id.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!zui::test::inside_template(rows, i) && rows[i].id == id) {
            return true;
        }
    }
    return false;
}

int nth_class(const std::vector<zui::frame::NodeCensus>& rows, std::string_view token, int nth,
              bool only_wide) {
    int seen = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        const zui::frame::NodeCensus& row = rows[i];
        if (!zui::test::class_token(row.class_attr, token)) {
            continue;
        }
        if (only_wide && !wide(row)) {
            continue;
        }
        if (seen++ == nth) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string node_field(zui::test::HandlerRig& rig, const std::vector<zui::frame::NodeCensus>& rows,
                       int index, std::string_view field) {
    const zui::frame::NodeCensus& row = rows[static_cast<std::size_t>(index)];
    if (!row.id.empty()) {
        return rig.get(row.id, field);
    }
    if (field == "text") {
        if (!row.text.empty()) {
            return row.text;
        }
        for (std::size_t i = static_cast<std::size_t>(index) + 1; i < rows.size(); ++i) {
            if (rows[i].parent == index && rows[i].tag == "#text" && !rows[i].text.empty()) {
                return rows[i].text;
            }
        }
        return {};
    }
    if (field == "value") {
        return row.value;
    }
    if (field == "class") {
        return row.class_attr;
    }
    return {};
}

bool click_at(zui::test::HandlerRig& rig, float x, float y, float w, float h, bool top, bool seam) {
    const float cx = x + w * 0.5f;
    const float cy = top ? y + std::min(6.f, std::max(1.f, h * 0.15f)) : y + h * 0.5f;
    if (seam) {
        return zui::frame::Access::click_xy(rig.app, cx, cy);
    }
    return rig.at(cx, cy);
}

bool click_index(zui::test::HandlerRig& rig, const std::vector<zui::frame::NodeCensus>& rows, int index,
                 bool top, bool seam) {
    if (index < 0) {
        return false;
    }
    const zui::frame::NodeCensus& row = rows[static_cast<std::size_t>(index)];
    if (!wide(row)) {
        return false;
    }
    return click_at(rig, row.x, row.y, row.w, row.h, top, seam);
}

bool click_id_box(zui::test::HandlerRig& rig, std::string_view id, bool top, bool seam) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h) || w <= 0.5f || h <= 0.5f) {
        return false;
    }
    return click_at(rig, x, y, w, h, top, seam);
}

int find_text(const std::vector<zui::frame::NodeCensus>& rows, std::string_view cls, std::string_view text,
              bool exact) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i) || !wide(rows[i])) {
            continue;
        }
        if (!cls.empty() && !zui::test::class_token(rows[i].class_attr, cls)) {
            continue;
        }
        const std::string body = trim_ws(text_content(rows, static_cast<int>(i)));
        const bool hit = exact ? body == text : body.find(text) != std::string::npos;
        if (hit) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool click_labelled(zui::test::HandlerRig& rig, std::string_view text) {
    rig.observe();
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(rig.app);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        const zui::frame::NodeCensus& row = rows[i];
        if (row.tag != "label" && row.tag != "button" && row.tag != "span" && row.tag != "a") {
            continue;
        }
        if (!wide(row) || trim_ws(text_content(rows, static_cast<int>(i))) != text) {
            continue;
        }
        std::size_t hit = i;
        for (int parent = static_cast<int>(i); parent >= 0;
             parent = rows[static_cast<std::size_t>(parent)].parent) {
            if (rows[static_cast<std::size_t>(parent)].tag == "label") {
                hit = static_cast<std::size_t>(parent);
                break;
            }
        }
        const zui::frame::NodeCensus& target = wide(rows[hit]) ? rows[hit] : row;
        return rig.at(target.x + target.w * 0.5f, target.y + target.h * 0.5f);
    }
    return false;
}

bool page_has(const zui::App& app, std::string_view needle) {
    // browser_walk.py `any` is `body.innerText`: it skips display:none (and a hidden ancestor)
    // and it does not read input or textarea values. A zero box is the hidden case. A child can
    // keep a stale box after its parent collapses, so every ancestor has to be wide too.
    // `live_census` drops template rows and leaves `parent` pointing at the full census.
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(app);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        const zui::frame::NodeCensus& row = rows[i];
        if (row.tag == "input" || row.tag == "textarea" || row.tag == "option") {
            continue;
        }
        bool shown = true;
        int guard = 0;
        for (int p = static_cast<int>(i); p >= 0 && guard < 64; p = rows[static_cast<std::size_t>(p)].parent, ++guard) {
            if (!wide(rows[static_cast<std::size_t>(p)])) {
                shown = false;
                break;
            }
        }
        if (!shown) {
            continue;
        }
        if (row.text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool settled(zui::test::HandlerRig& rig, bool ok) {
    // A click handler may `defer` the rebuild that destroyed its own target. The browser
    // applies that rebuild before the read. Flush once; a second pump crashed share and writer.
    if (ok) {
        zui::frame::Access::flush_deferred(rig.app);
    }
    return ok;
}

const zui::frame::NodeCensus* find_id(const std::vector<zui::frame::NodeCensus>& rows, std::string_view id) {
    if (id.empty()) {
        return nullptr;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!zui::test::inside_template(rows, i) && rows[i].id == id) {
            return &rows[i];
        }
    }
    return nullptr;
}

bool form_control(const zui::frame::NodeCensus& row) {
    return row.tag == "input" || row.tag == "textarea" || row.tag == "select" || row.tag == "label";
}

/// browser_walk.py `field()` only understands these. Anything else is recorded as "".
bool browser_reads_field(std::string_view field) {
    return field == "text" || field == "value" || field == "checked" || field == "visible" ||
           field == "disabled" || field == "class" || field == "href" ||
           field.compare(0, 5, "data-") == 0 || field.compare(0, 5, "aria-") == 0;
}

std::string screen_of(const zui::App& app) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (row.id != "app") {
            continue;
        }
        std::size_t i = 0;
        while (i < row.class_attr.size()) {
            while (i < row.class_attr.size() && row.class_attr[i] == ' ') {
                ++i;
            }
            const std::size_t j = row.class_attr.find(' ', i);
            const std::string token = row.class_attr.substr(i, j == std::string::npos ? j : j - i);
            if (token.rfind("screen-", 0) == 0) {
                return token;
            }
            if (j == std::string::npos) {
                break;
            }
            i = j + 1;
        }
    }
    return {};
}

bool class_shown(const zui::App& app, std::string_view token) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (wide(row) && zui::test::class_token(row.class_attr, token)) {
            return true;
        }
    }
    return false;
}

nlohmann::json files_probe(zui::test::HandlerRig& rig) {
    nlohmann::json checked = nlohmann::json::object();
    for (const char* id : kRadios) {
        checked[id] = rig.get(id, "checked") == "true";
    }
    nlohmann::json shown = nlohmann::json::object();
    for (const char* name : kShown) {
        shown[name] = class_shown(rig.app, name);
    }
    return nlohmann::json{{"checked", std::move(checked)}, {"shown", std::move(shown)}};
}

std::string selected_name(const std::vector<zui::frame::NodeCensus>& rows) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        if (!zui::test::class_token(rows[i].class_attr, "obj") ||
            !zui::test::class_token(rows[i].class_attr, "is-selected")) {
            continue;
        }
        for (std::size_t c = 0; c < rows.size(); ++c) {
            if (zui::test::inside_template(rows, c) ||
                !zui::test::class_token(rows[c].class_attr, "obj-name")) {
                continue;
            }
            for (int p = rows[c].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
                if (p == static_cast<int>(i)) {
                    return trim_ws(text_content(rows, static_cast<int>(c)));
                }
            }
        }
    }
    return {};
}

nlohmann::json measure_reads(zui::test::HandlerRig& rig, const nlohmann::json& reads) {
    zui::frame::Access::flush_deferred(rig.app);
    rig.observe();
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(rig.app);
    nlohmann::json out = nlohmann::json::object();
    for (const nlohmann::json& spec : reads) {
        const std::string key = spec.contains("as") ? spec.at("as").get<std::string>() : spec.at("op").get<std::string>();
        const std::string op = spec.at("op").get<std::string>();
        if (op == "get" || op == "class_has") {
            const std::string id = jstr(spec, "id");
            int index = -1;
            if (!has_id(rows, id) && spec.contains("class") && spec.contains("nth")) {
                index = nth_class(rows, spec.at("class").get<std::string>(), spec.at("nth").get<int>(), true);
            }
            if (op == "get") {
                const std::string field = spec.at("field").get<std::string>();
                if (!browser_reads_field(field)) {
                    out[key] = "";
                } else if (has_id(rows, id)) {
                    out[key] = rig.get(id, field);
                } else if (index >= 0) {
                    out[key] = node_field(rig, rows, index, field);
                } else {
                    out[key] = "";
                }
            } else {
                const std::string token = spec.at("token").get<std::string>();
                if (index >= 0) {
                    out[key] = zui::test::class_token(rows[static_cast<std::size_t>(index)].class_attr, token);
                } else {
                    out[key] = rig.has_class(id, token);
                }
            }
        } else if (op == "count") {
            out[key] = zui::test::count_class(rig.app, spec.at("class").get<std::string>());
        } else if (op == "names") {
            nlohmann::json names = nlohmann::json::array();
            const std::string cls = spec.at("class").get<std::string>();
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (zui::test::inside_template(rows, i) ||
                    !zui::test::class_token(rows[i].class_attr, cls)) {
                    continue;
                }
                names.push_back(trim_ws(text_content(rows, static_cast<int>(i))));
            }
            out[key] = std::move(names);
        } else if (op == "any") {
            out[key] = page_has(rig.app, spec.at("text").get<std::string>());
        } else if (op == "has_text") {
            const std::string id = jstr(spec, "id");
            std::string body;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (!zui::test::inside_template(rows, i) && rows[i].id == id) {
                    body = text_content(rows, static_cast<int>(i));
                    break;
                }
            }
            out[key] = body.find(spec.at("text").get<std::string>()) != std::string::npos;
        } else if (op == "last_text") {
            const std::string cls = spec.at("class").get<std::string>();
            std::string last;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (zui::test::inside_template(rows, i) ||
                    !zui::test::class_token(rows[i].class_attr, cls)) {
                    continue;
                }
                last = trim_ws(text_content(rows, static_cast<int>(i)));
            }
            out[key] = last;
        } else if (op == "selected_name") {
            out[key] = selected_name(rows);
        } else if (op == "screen") {
            out[key] = screen_of(rig.app);
        } else if (op == "files") {
            out[key] = files_probe(rig);
        }
    }
    return out;
}

bool gate_ok(zui::test::HandlerRig& rig, const nlohmann::json& gate) {
    const std::string kind = gate.at("kind").get<std::string>();
    if (kind == "any") {
        return page_has(rig.app, gate.at("text").get<std::string>());
    }
    if (kind == "eq") {
        return rig.get(gate.at("id").get<std::string>(), gate.at("field").get<std::string>()) ==
               gate.at("value").get<std::string>();
    }
    if (kind == "contains") {
        const std::string field = gate.contains("field") ? gate.at("field").get<std::string>() : "text";
        return rig.get(gate.at("id").get<std::string>(), field).find(gate.at("text").get<std::string>()) !=
               std::string::npos;
    }
    if (kind == "min_len") {
        return zui::test::count_class(rig.app, gate.at("class").get<std::string>()) >= gate.at("n").get<int>();
    }
    if (kind == "screen") {
        return screen_of(rig.app) == gate.at("value").get<std::string>();
    }
    return false;
}

bool wait_gate(zui::test::HandlerRig& rig, const nlohmann::json& gate) {
    for (int i = 0; i < 48; ++i) {
        zui::frame::Access::flush_deferred(rig.app);
        rig.observe();
        if (gate_ok(rig, gate)) {
            return true;
        }
        if (!rig.pump()) {
            return false;
        }
    }
    return false;
}

void ensure_share_tree(const fs::path& root) {
    std::error_code ec;
    fs::create_directories(root / "folder", ec);
    auto write = [](const fs::path& path, std::size_t bytes) {
        std::error_code error;
        if (fs::is_regular_file(path, error) && fs::file_size(path, error) == bytes) {
            return;
        }
        std::ofstream out(path, std::ios::binary);
        const std::string block(bytes, 'x');
        out.write(block.data(), static_cast<std::streamsize>(block.size()));
    };
    write(root / "a.txt", 1200);
    write(root / "b.txt", 12);
}

struct WalkSession {
    zui::test::HandlerRig* rig = nullptr;
    share::Session* share = nullptr;
    particle::Session* particle = nullptr;
    reader::Session* reader = nullptr;
    // Keeps the application session alive until replay finishes. Bind callbacks hold its address.
    std::shared_ptr<void> keep;

    ~WalkSession() {
        if (particle != nullptr) {
            particle::stop(*particle);
        }
        if (reader != nullptr) {
            ++reader->open_token;
            ++reader->page.turn_token;
        }
        if (rig != nullptr) {
            zui::frame::Access::flush_deferred(rig->app);
        }
    }
};

bool share_act(WalkSession& session, const nlohmann::json& step) {
    const std::string op = step.at("op").get<std::string>();
    share::Session& share = *session.share;
    zui::test::HandlerRig& rig = *session.rig;
    if (op == "share_listing" || op == "share_reopen") {
        const fs::path dir(jstr(step, "dir"));
        ensure_share_tree(dir);
        if (!share.browser().open(dir.string())) {
            return false;
        }
        // The browser op replaces the file list, so previous `on` flags do not survive.
        share.browser().clear_selection();
        share.render_files();
        return rig.pump();
    }
    if (op == "share_peer") {
        share::Peer peer;
        peer.id = step.at("id").get<std::uint64_t>();
        peer.name = step.at("name").get<std::string>();
        peer.ip = step.at("ip").get<std::string>();
        peer.port = 0;
        share::net_stage_peer(peer);
        share.poll();
        return rig.pump();
    }
    if (op == "share_incoming") {
        share::IncomingOffer offer;
        offer.offer_id = 7;
        offer.from_name = step.at("from").get<std::string>();
        offer.from_ip = "10.0.0.9";
        offer.nfiles = step.at("nfiles").get<std::uint32_t>();
        offer.nbytes = step.at("nbytes").get<std::uint64_t>();
        share::net_stage_incoming(offer);
        share.poll();
        return rig.pump();
    }
    if (op == "share_screen") {
        share::XferView view;
        view.sending = true;
        view.peer_name = "客厅电脑";
        view.peer_ip = "10.0.0.8";
        view.total = 1000;
        view.done = 400;
        view.error = "连不上对方";
        const std::string name = step.at("screen").get<std::string>();
        if (name == "done") {
            view.phase = share::XferPhase::Done;
            view.done = 1000;
        } else if (name == "fail") {
            view.phase = share::XferPhase::Error;
        } else if (name == "transfer") {
            view.phase = share::XferPhase::Transferring;
        } else if (name == "wait") {
            view.phase = share::XferPhase::WaitingAccept;
        } else {
            return false;
        }
        share::net_stage_xfer(view);
        share.poll();
        return rig.pump();
    }
    return false;
}

bool act(WalkSession& session, const nlohmann::json& step) {
    zui::test::HandlerRig& rig = *session.rig;
    const std::string op = step.at("op").get<std::string>();
    if (op == "read") {
        return true;
    }
    if (op == "flush") {
        zui::frame::Access::flush_deferred(rig.app);
        return true;
    }
    if (op.rfind("share_", 0) == 0) {
        return session.share != nullptr && share_act(session, step);
    }
    rig.observe();
    const bool top = jstr(step, "at") == "top";
    const bool seam = step.contains("seam") && step.at("seam").is_boolean() && step.at("seam").get<bool>();
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(rig.app);
    if (op == "click" || op == "tap") {
        const std::string id = jstr(step, "id");
        const zui::frame::NodeCensus* node = find_id(rows, id);
        // `click` is HTMLElement.click(). Radios, checks, and labels need the hit path: a direct
        // activate does not toggle them. Everything else is activated even when a layer covers
        // the box or `at: top` names a scrolled-away edge. No extra pump: activate already flushed,
        // and a second frame crashed share and writer.
        if (op == "click" && !seam && node != nullptr && !form_control(*node) &&
            zui::frame::Access::activate_id(rig.app, id)) {
            return settled(rig, true);
        }
        if (has_id(rows, id) && click_id_box(rig, id, top, seam)) {
            return settled(rig, true);
        }
        // HTMLElement.click() fires on a display:none control. A zero box cannot be hit.
        if (op == "click" && has_id(rows, id) && zui::frame::Access::activate_id(rig.app, id)) {
            return settled(rig, true);
        }
        if (step.contains("class") && step.contains("nth")) {
            const int index = nth_class(rows, step.at("class").get<std::string>(), step.at("nth").get<int>(), true);
            return settled(rig, click_index(rig, rows, index, top, seam));
        }
        return false;
    }
    if (op == "click_text" || op == "tap_text") {
        const bool exact = step.contains("exact") && step.at("exact").is_boolean() && step.at("exact").get<bool>();
        const int index = find_text(rows, jstr(step, "class"), jstr(step, "text"), exact);
        if (op == "click_text" && index >= 0 && !rows[static_cast<std::size_t>(index)].id.empty() &&
            !form_control(rows[static_cast<std::size_t>(index)]) &&
            zui::frame::Access::activate_id(rig.app, rows[static_cast<std::size_t>(index)].id)) {
            return settled(rig, true);
        }
        return settled(rig, click_index(rig, rows, index, top, seam));
    }
    if (op == "click_labelled") {
        return settled(rig, click_labelled(rig, jstr(step, "text")));
    }
    if (op == "click_nth") {
        const int index = nth_class(rows, step.at("class").get<std::string>(), step.at("nth").get<int>(), true);
        return settled(rig, click_index(rig, rows, index, top, seam));
    }
    if (op == "click_within") {
        const std::string outer = step.at("outer").get<std::string>();
        const std::string text = step.at("text").get<std::string>();
        const std::string label = step.at("label").get<std::string>();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (zui::test::inside_template(rows, i) || !wide(rows[i]) ||
                !zui::test::class_token(rows[i].class_attr, outer)) {
                continue;
            }
            if (text_content(rows, static_cast<int>(i)).find(text) == std::string::npos) {
                continue;
            }
            for (std::size_t c = 0; c < rows.size(); ++c) {
                if (zui::test::inside_template(rows, c) || !wide(rows[c])) {
                    continue;
                }
                if (rows[c].tag != "label" && rows[c].tag != "button" && rows[c].tag != "span" &&
                    rows[c].tag != "a") {
                    continue;
                }
                if (trim_ws(text_content(rows, static_cast<int>(c))) != label) {
                    continue;
                }
                bool inside = false;
                for (int p = rows[c].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
                    if (p == static_cast<int>(i)) {
                        inside = true;
                        break;
                    }
                }
                if (!inside) {
                    continue;
                }
                return rig.at(rows[c].x + rows[c].w * 0.5f, rows[c].y + rows[c].h * 0.5f);
            }
        }
        return false;
    }
    if (op == "replace" || op == "commit" || op == "enter") {
        const std::string id = jstr(step, "id");
        const std::string value = jstr(step, "value");
        if (op == "commit") {
            if (!rig.type(id, "q")) {
                return false;
            }
            rig.app.document().set(id, "value", value);
            const std::string blur = jstr(step, "blur");
            if (!blur.empty()) {
                rig.app.document().focus(blur);
            }
            return rig.pump();
        }
        // The find field starts under a hidden bar. The browser still sets its value and fires
        // `input`; showing the bar is what makes the same keystroke focusable here.
        if (id == "find-q") {
            rig.app.document().set("findbar", "visible", "true");
            rig.pump();
        }
        if (value.empty()) {
            // Setting the value directly does not fire `input`. Backspace does, which is what
            // an empty `fireInput` delivers to the bound handler. Backspace deletes before the
            // caret, and a fresh focus can leave the caret at 0, so move to the end first.
            if (!rig.app.document().focus(id) || !rig.app.pump_frame()) {
                return rig.app.document().set(id, "value", "");
            }
            zui::backends::TextInputFrame to_end;
            to_end.end = 1;
            rig.window().inject(std::move(to_end));
            if (!rig.app.pump_frame()) {
                return rig.app.document().set(id, "value", "");
            }
            zui::backends::TextInputFrame frame;
            frame.backspace = 4096;
            rig.window().inject(std::move(frame));
            if (!rig.app.pump_frame()) {
                return false;
            }
            if (!rig.app.document().get(id, "value").empty()) {
                return rig.app.document().set(id, "value", "");
            }
            return true;
        }
        rig.app.document().set(id, "value", "");
        rig.pump();
        if (!rig.type(id, value)) {
            // A field with no box still accepts a programmatic value.
            if (!rig.app.document().set(id, "value", value)) {
                return false;
            }
        }
        if (op == "enter") {
            return rig.press(id, "Enter");
        }
        return true;
    }
    if (op == "press") {
        return rig.press(jstr(step, "id"), step.at("key").get<std::string>(),
                         step.contains("shift") && step.at("shift").get<bool>());
    }
    // A document-level key: the page accelerator (`<body onkeydown>`), not a control's own handler.
    if (op == "key") {
        return zui::frame::Access::key(rig.app, step.at("key").get<std::string>());
    }
    if (op == "wheel") {
        const double dy = step.contains("dy") ? step.at("dy").get<double>() : 0.0;
        const bool ctrl = step.contains("ctrl") && step.at("ctrl").is_boolean() && step.at("ctrl").get<bool>();
        // The oracle stores WheelEvent.deltaY. The window seam treats a positive notch as
        // wheel-up and negates it before the document (frame.cpp). Negate here so the document
        // sees that deltaY.
        return rig.wheel_window(jstr(step, "id"), static_cast<float>(-dy), ctrl);
    }
    if (op == "drag") {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        const std::string id = jstr(step, "id");
        if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h)) {
            return false;
        }
        const double dx = step.contains("dx") ? step.at("dx").get<double>() : 0.0;
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        return rig.drag(cx, cy, cx + static_cast<float>(dx), cy);
    }
    if (op == "select_label") {
        if (!click_id_box(rig, jstr(step, "id"), false, false)) {
            return false;
        }
        rig.observe();
        const std::vector<zui::frame::NodeCensus> open = zui::frame::Access::census(rig.app);
        const std::string label = jstr(step, "text");
        for (std::size_t i = 0; i < open.size(); ++i) {
            if (zui::test::inside_template(open, i) || open[i].tag != "option" || !wide(open[i])) {
                continue;
            }
            if (trim_ws(text_content(open, static_cast<int>(i))) != label && open[i].run_text != label) {
                continue;
            }
            return rig.at(open[i].x + open[i].w * 0.5f, open[i].y + open[i].h * 0.5f);
        }
        return false;
    }
    if (op == "click_cell") {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (zui::test::inside_template(rows, i) || !wide(rows[i])) {
                continue;
            }
            if (rows[i].tag == "button" && zui::test::class_token(rows[i].class_attr, "zcell") &&
                !zui::test::class_token(rows[i].class_attr, "zhead-btn")) {
                return rig.at(rows[i].x + rows[i].w * 0.5f, rows[i].y + rows[i].h * 0.5f);
            }
        }
        return false;
    }
    return false;
}

void compare_readback(const std::string& where, const nlohmann::json& measured, const nlohmann::json& expect) {
    for (auto it = expect.begin(); it != expect.end(); ++it) {
        if (!measured.contains(it.key())) {
            ADD_FAILURE() << where << " missing " << it.key();
            continue;
        }
        EXPECT_TRUE(measured.at(it.key()) == it.value())
            << where << " " << it.key() << "\n measured " << measured.at(it.key()).dump()
            << "\n browser  " << it.value().dump();
    }
}

nlohmann::json replay(WalkSession& session, const nlohmann::json& steps) {
    nlohmann::json recorded = nlohmann::json::array();
    int index = 0;
    for (const nlohmann::json& step : steps) {
        const std::string where = std::to_string(index) + " " + step.at("op").get<std::string>() + " " +
                                  jstr(step, "id") + jstr(step, "text");
        if (step.contains("until") && step.at("until").contains("before") &&
            step.at("until").at("before").get<bool>()) {
            EXPECT_TRUE(wait_gate(*session.rig, step.at("until"))) << where << " before";
        }
        EXPECT_TRUE(act(session, step)) << where << " act";
        if (step.contains("until") &&
            !(step.at("until").contains("before") && step.at("until").at("before").get<bool>())) {
            EXPECT_TRUE(wait_gate(*session.rig, step.at("until"))) << where << " until";
        }
        const nlohmann::json measured = measure_reads(*session.rig, step.at("read"));
        compare_readback(where, measured, step.at("expect"));
        recorded.push_back(nlohmann::json{{"op", step.at("op")}, {"measured", measured}});
        ++index;
    }
    return recorded;
}

void write_dump(const std::string& page, const nlohmann::json& passes) {
    const char* dir = std::getenv("ZUI_ENGINE_WALK");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(fs::path(dir) / (page + ".json"), std::ios::binary);
    out << nlohmann::json{{"passes", passes}}.dump(2) << "\n";
}

nlohmann::json walk_page(std::string& error, const char* page) {
    const nlohmann::json all = zui::test::load_browser_oracle(error);
    if (!error.empty()) {
        return {};
    }
    if (!all.contains("walk") || !all.at("walk").contains(page)) {
        error = std::string("browser oracle has no walk for ") + page;
        return {};
    }
    return all.at("walk").at(page);
}

bool attach(zui::test::HandlerRig& rig, int width, int height) {
    return rig.windowed(width, height) && rig.pump() && (rig.pump(), true);
}

void run_passes(const char* page, const nlohmann::json& spec,
                const std::function<bool(zui::test::HandlerRig&, WalkSession&, int, int)>& boot) {
    nlohmann::json recorded = nlohmann::json::array();
    for (const nlohmann::json& pass : spec.at("passes")) {
        const int width = pass.at("width").get<int>();
        const int height = pass.at("height").get<int>();
        zui::test::HandlerRig rig;
        WalkSession session;
        session.rig = &rig;
        ASSERT_TRUE(boot(rig, session, width, height)) << page << " boot " << width;
        nlohmann::json steps = replay(session, pass.at("steps"));
        recorded.push_back(nlohmann::json{{"width", width}, {"height", height}, {"steps", std::move(steps)}});
    }
    write_dump(page, recorded);
}

}  // namespace

TEST(AppInteract, AlbumPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "album");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("album", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<album::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("album-walk");
        fs::remove_all(session->dir);
        if (!rig.app.document().load(app_html("album").string())) {
            return false;
        }
        album::bind_actions(*session);
        album::show(*session);
        if (!attach(rig, width, height)) {
            return false;
        }
        rig.observe();
        for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.app)) {
            if (zui::test::class_token(row.class_attr, "album-btn") &&
                row.run_text.find("全部") != std::string::npos && wide(row)) {
                zui::frame::Access::click_xy(rig.app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
                break;
            }
        }
        if (rig.get("lightbox", "visible") == "true") {
            rig.click("light-close");
        }
        if (rig.get("confirm", "visible") == "true") {
            rig.click("confirm-cancel");
        }
        if (rig.get("album-dialog", "visible") == "true") {
            rig.click("album-form-cancel");
        }
        return true;
    });
}

TEST(AppInteract, CalendarPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "calendar");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("calendar", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<calendar::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("calendar-walk");
        fs::remove_all(session->dir);
        if (!rig.app.document().load(app_html("calendar").string())) {
            return false;
        }
        calendar::bind_actions(*session);
        calendar::show(*session);
        if (!attach(rig, width, height)) {
            return false;
        }
        return zui::frame::Access::click_id(rig.app, "view-month");
    });
}

TEST(AppInteract, ChatPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "chat");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("chat", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<chat::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("chat-walk");
        fs::remove_all(session->dir);
        if (!rig.app.document().load(app_html("chat").string())) {
            return false;
        }
        chat::bind_actions(*session);
        chat::show(*session);
        if (!attach(rig, width, height) || !rig.type("q", " ")) {
            return false;
        }
        return rig.click_text("conv", "林晚");
    });
}

// open_uri on the contacts links would ShellExecute tel: / mailto:. Noop keeps the
// handler's in-page result (the page stays, the href stays) without a host launch.
struct ScopedNoopPlatform {
    std::string previous;
    bool had = false;
    ScopedNoopPlatform() {
        if (const char* cur = std::getenv("ZUI_PLATFORM")) {
            had = true;
            previous = cur;
        }
#if defined(_WIN32)
        _putenv_s("ZUI_PLATFORM", "noop");
#else
        setenv("ZUI_PLATFORM", "noop", 1);
#endif
    }
    ~ScopedNoopPlatform() {
#if defined(_WIN32)
        _putenv_s("ZUI_PLATFORM", had ? previous.c_str() : "");
#else
        if (had) {
            setenv("ZUI_PLATFORM", previous.c_str(), 1);
        } else {
            unsetenv("ZUI_PLATFORM");
        }
#endif
    }
};

TEST(AppInteract, ContactsPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "contacts");
    ASSERT_TRUE(error.empty()) << error;
    ScopedNoopPlatform noop_platform;
    run_passes("contacts", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<contacts::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("contacts-walk");
        fs::remove_all(session->dir);
        if (!rig.app.document().load(app_html("contacts").string())) {
            return false;
        }
        contacts::bind_actions(*session);
        contacts::show(*session);
        if (!attach(rig, width, height) || !rig.type("q", " ")) {
            return false;
        }
        if (!zui::frame::Access::click_id(rig.app, "star-only") ||
            !zui::frame::Access::click_id(rig.app, "star-only")) {
            return false;
        }
        return rig.click_text("chip", "全部") && rig.click_text("person", "林晚");
    });
}

TEST(AppInteract, KanbanPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "kanban");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("kanban", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        const fs::path dir = zui::test::scratch_dir("kanban-walk");
        fs::remove_all(dir);
        if (!rig.app.document().load(app_html("kanban").string())) {
            return false;
        }
        auto session = std::make_shared<kanban::Session>(rig.app, dir);
        walk.keep = session;
        session->bind();
        // browser_oracle.py runs kanban_readback before the walk, and that submit persists one
        // extra todo card into localStorage. The recording therefore starts with 第六节图表 already
        // on the board. A fresh seed does not.
        {
            const fs::path data = dir / "native-data";
            fs::create_directories(data);
            std::ofstream out(data / "kanban.json", std::ios::binary);
            out << R"({
  "columns": [
    {"id": "todo", "name": "待办", "cardIds": ["c1", "c2", "c3", "c10"]},
    {"id": "doing", "name": "进行中", "cardIds": ["c4", "c5"]},
    {"id": "review", "name": "待校对", "cardIds": ["c6", "c7"]},
    {"id": "done", "name": "完成", "cardIds": ["c8"]},
    {"id": "later", "name": "搁置", "cardIds": ["c9"]}
  ],
  "cards": [
    {"id": "c1", "title": "校对第三章", "note": "第二节标题和那张被拆开的表。明天上午和林晚对目录。"},
    {"id": "c2", "title": "人名索引留白", "note": "等周宁补注，先不要上墨。"},
    {"id": "c3", "title": "还夜航表", "note": "灯塔印刷有人在等，周三下午之前。"},
    {"id": "c10", "title": "第六节图表", "note": ""},
    {"id": "c4", "title": "灯塔摘录封面", "note": "色号用上次的赭石，别换新样。"},
    {"id": "c5", "title": "竹纸样", "note": "毛边留下，不裁。"},
    {"id": "c6", "title": "渡口手记前言", "note": "改过一轮，等林晚用铅笔批。"},
    {"id": "c7", "title": "海岸植物续借", "note": "书局说这本可以续，夜航表不行。"},
    {"id": "c8", "title": "旧书店目录归档", "note": "何清已经对过册数。"},
    {"id": "c9", "title": "潮汐口诀再版", "note": "这季先不动。"}
  ]
})";
        }
        session->show();
        return attach(rig, width, height);
    });
}

TEST(AppInteract, ParticlePageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "particle");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("particle", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<particle::Session>();
        walk.keep = session;
        walk.particle = session.get();
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("particle-walk");
        std::error_code ec;
        fs::create_directories(session->dir, ec);
        if (!rig.app.document().load(app_html("particle").string())) {
            return false;
        }
        particle::bind_actions(*session);
        particle::show(*session);
        return attach(rig, width, height);
    });
}

TEST(AppInteract, WriterPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "writer");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("writer", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<writer::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("writer-walk");
        std::error_code ec;
        fs::remove_all(session->dir, ec);
        fs::create_directories(session->dir / "pieces", ec);
        fs::create_directories(session->dir / "history" / "d1", ec);
        // writer_readback clicks new-piece and saves twice before the walk, so the walk's next
        // piece is d2 and the older piece already has one snapshot of 旧稿.
        {
            std::ofstream library(session->dir / "library.json", std::ios::binary);
            library << R"({
  "seq": 1,
  "docs": [{
    "id": "d1",
    "parent": "",
    "name": "未命名文稿 d1",
    "folder": false,
    "created": "2026-10-10",
    "edited": "2026-10-10",
    "history": [{"id": "h1", "label": "h1"}]
  }]
})";
            std::ofstream piece(session->dir / "pieces" / "d1.txt", std::ios::binary);
            piece << "旧稿 新稿";
            std::ofstream snap(session->dir / "history" / "d1" / "h1.txt", std::ios::binary);
            snap << "旧稿";
        }
        if (!rig.app.document().load(app_html("writer").string())) {
            return false;
        }
        writer::bind_actions(*session);
        writer::show(*session);
        // The tree pane is `display: none` until hover or `show-tree`. A pointer left at the
        // origin sits on the 18px edge and opens that pane, so innerText then contains the names.
        if (!attach(rig, width, height)) {
            return false;
        }
        return rig.point(static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f);
    });
}

TEST(AppInteract, ReaderPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "reader");
    ASSERT_TRUE(error.empty()) << error;
    reader_test::force_noop_platform();
    run_passes("reader", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<reader::Session>();
        walk.keep = session;
        walk.reader = session.get();
        const fs::path root = zui::test::scratch_dir("reader-walk");
        const fs::path books = root / "books";
        std::error_code ec;
        fs::create_directories(books, ec);
        fs::copy_file(reader_test::fixture_dir() / "sample.txt", books / "sample.txt",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(reader_test::fixture_dir() / "catalog.json", books / "catalog.json",
                      fs::copy_options::overwrite_existing, ec);
        if (!rig.app.document().load(app_html("reader", "reader.html").string())) {
            return false;
        }
        session->app = &rig.app;
        session->books_dir = books;
        session->progress_path = root / "progress.txt";
        session->progress = reader::load_progress(session->progress_path);
        reader::bind_actions(*session);
        reader::show(*session);
        return attach(rig, width, height);
    });
}

TEST(AppInteract, ZdbPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "zdb");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("zdb", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<zdb::Session>();
        walk.keep = session;
        session->app = &rig.app;
        session->dir = zui::test::scratch_dir("zdb-walk");
        fs::remove_all(session->dir);
        if (!rig.app.document().load(app_html("zdb").string())) {
            return false;
        }
        zdb::bind_actions(*session);
        zdb::show_all(*session);
        return attach(rig, width, height);
    });
}

#ifndef ZUI_WITHOUT_ZMD
TEST(AppInteract, ZmdPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "zmd");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("zmd", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        auto session = std::make_shared<zmd::Session>(rig.app);
        walk.keep = session;
        // Load the page and hang the session on it (the order start() uses). apply_source
        // drags the preview fragment in, and this walk does not read the preview, so it
        // stays out: open_page() only binds and adopts what the page already shows.
        if (!session->open_page(app_html("zmd").string())) {
            return false;
        }
        return attach(rig, width, height);
    });
}
#endif

TEST(AppInteract, FileExplorerPageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "file-explorer");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("file-explorer", spec, [](zui::test::HandlerRig& rig, WalkSession&, int width, int height) {
        if (!rig.app.document().load(app_html("file-explorer").string())) {
            return false;
        }
        return attach(rig, width, height);
    });
}

TEST(AppInteract, SharePageMatchesTheBrowser) {
    std::string error;
    const nlohmann::json spec = walk_page(error, "share");
    ASSERT_TRUE(error.empty()) << error;
    run_passes("share", spec, [](zui::test::HandlerRig& rig, WalkSession& walk, int width, int height) {
        const fs::path home = zui::test::scratch_dir("share-walk");
        std::error_code ec;
        fs::remove_all(home, ec);
        fs::create_directories(home, ec);
        share::set_config_dir(home.string());
        if (!rig.app.document().load(app_html("share").string())) {
            return false;
        }
        auto session = std::make_shared<share::Session>(rig.app);
        walk.keep = session;
        walk.share = session.get();
        session->bind();
        session->show();
        return attach(rig, width, height);
    });
}
