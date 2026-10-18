#include "store.h"

#include "textutil.h"

#include <nlohmann/json.hpp>

namespace writer {
namespace fs = std::filesystem;

void DocStore::erase_files(const fs::path& root, const Doc& doc) const {
    if (root.empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(root / "pieces" / (doc.id + ".txt"), ec);
    fs::remove_all(root / "history" / doc.id, ec);
}

std::string DocStore::fresh_id() {
    ++seq_;
    return "d" + std::to_string(seq_);
}

std::string DocStore::fresh_snap_id() {
    ++seq_;
    return "h" + std::to_string(seq_);
}

Doc* DocStore::find(const std::string& id) {
    for (Doc& doc : docs_) {
        if (doc.id == id) {
            return &doc;
        }
    }
    return nullptr;
}

const Doc* DocStore::find(const std::string& id) const {
    for (const Doc& doc : docs_) {
        if (doc.id == id) {
            return &doc;
        }
    }
    return nullptr;
}

void DocStore::save(const fs::path& root) const {
    if (root.empty()) {
        return;
    }
    std::error_code ec;
    fs::create_directories(root / "pieces", ec);
    fs::create_directories(root / "history", ec);
    nlohmann::json library;
    library["seq"] = seq_;
    library["docs"] = nlohmann::json::array();
    for (const Doc& doc : docs_) {
        nlohmann::json item;
        item["id"] = doc.id;
        item["parent"] = doc.parent;
        item["name"] = doc.name;
        item["folder"] = doc.folder;
        item["created"] = doc.created;
        item["edited"] = doc.edited;
        item["history"] = nlohmann::json::array();
        if (!doc.folder) {
            write_bytes(root / "pieces" / (doc.id + ".txt"), doc.body);
        }
        for (const Snap& snap : doc.history) {
            nlohmann::json entry;
            entry["id"] = snap.id;
            entry["label"] = snap.label;
            item["history"].push_back(std::move(entry));
            write_bytes(root / "history" / doc.id / (snap.id + ".txt"), snap.text);
        }
        library["docs"].push_back(std::move(item));
    }
    write_bytes(root / "library.json", library.dump(2));
}

void DocStore::load(const fs::path& root) {
    docs_.clear();
    seq_ = 0;
    if (root.empty()) {
        return;
    }
    const std::string text = read_bytes(root / "library.json");
    if (text.empty()) {
        return;
    }
    nlohmann::json library;
    try {
        library = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return;
    }
    seq_ = library.value("seq", 0);
    if (!library.contains("docs") || !library["docs"].is_array()) {
        return;
    }
    for (const nlohmann::json& item : library["docs"]) {
        Doc doc;
        doc.id = item.value("id", "");
        doc.parent = item.value("parent", "");
        doc.name = item.value("name", "");
        doc.folder = item.value("folder", false);
        doc.created = item.value("created", "");
        doc.edited = item.value("edited", "");
        if (doc.id.empty()) {
            continue;
        }
        if (!doc.folder) {
            doc.body = read_bytes(root / "pieces" / (doc.id + ".txt"));
            doc.saved_body = doc.body;
        }
        if (item.contains("history") && item["history"].is_array()) {
            for (const nlohmann::json& entry : item["history"]) {
                Snap snap;
                snap.id = entry.value("id", "");
                snap.label = entry.value("label", snap.id);
                if (snap.id.empty()) {
                    continue;
                }
                snap.text = read_bytes(root / "history" / doc.id / (snap.id + ".txt"));
                doc.history.push_back(std::move(snap));
            }
        }
        docs_.push_back(std::move(doc));
    }
}

}  // namespace writer
