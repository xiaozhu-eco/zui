#include "session.h"

#include "textutil.h"

namespace writer {
namespace fs = std::filesystem;

bool Session::page_missing(const char* id) const {
    return app == nullptr || app->document().get(id, "visible").empty();
}

// Each screen is its own HTML file. show() mounts them under the shell and
// then only one page is visible.
void Session::mount_pages() {
    if (app == nullptr) {
        return;
    }
    if (page_missing("workspace")) {
        app->document().load_fragment("pages/desk.html", "stage");
    }
    if (page_missing("calendar-view")) {
        app->document().load_fragment("pages/calendar.html", "stage");
    }
    if (page_missing("history-view")) {
        app->document().load_fragment("pages/history.html", "stage");
    }
    if (page_missing("bar-main")) {
        app->document().load_fragment("pages/bar.html", "dock");
    }
    if (page_missing("storage-bar")) {
        app->document().load_fragment("pages/storage.html", "settings-slot");
    }
}

void Session::present_loaded() {
    mount_pages();
    if (active_.empty()) {
        for (const Doc& doc : store_.docs()) {
            if (!doc.folder) {
                active_ = doc.id;
                break;
            }
        }
    }
    publish_tree();
    set_view(view_ == nullptr || std::string(view_->id()).empty() ? "tree" : view_->id());
    apply_mode();
    publish_calendar();
    publish_history();
    publish_today();
    publish_outline();
}

// The chosen folder is recorded under the default data directory. The next
// process starts there and follows this file, the same way the page keeps
// `xiaozhu-writer-root` and then loads that root.
void Session::remember_dir(const fs::path& next) {
    if (home.empty()) {
        home = dir;
    }
    std::error_code ec;
    fs::create_directories(home, ec);
    write_bytes(home / "storage-root.txt", next.string());
}

void Session::follow_remembered_dir() {
    if (home.empty()) {
        home = dir;
    }
    const std::string remembered = trim(read_bytes(home / "storage-root.txt"));
    if (remembered.empty()) {
        return;
    }
    const fs::path next(remembered);
    std::error_code ec;
    if (fs::is_directory(next, ec)) {
        dir = next;
    }
}

void Session::on_storage() {
    if (app == nullptr) {
        return;
    }
    const std::string text = trim(app->document().get("storage-path", "value"));
    if (text.empty()) {
        set_storage_status("请填写存储位置");
        return;
    }
    const fs::path next(text);
    std::error_code ec;
    fs::create_directories(next, ec);
    if (ec || !fs::is_directory(next)) {
        set_storage_status("无法使用该位置");
        return;
    }
    // Keep the library that is already on screen in the directory it came
    // from, then open whatever is stored at the new path. Writing the
    // in-memory set into that path would replace an existing library.json
    // and leave its piece files behind.
    store_.save(dir);
    remember_dir(next);
    dir = next;
    store_.load(dir);
    active_.clear();
    present_loaded();
    app->document().set("storage-path", "value", next.string());
    set_storage_status("已设为存储位置");
    set_status(store_.docs().empty() ? "这个位置还没有文稿。" : "已载入文稿");
}

}  // namespace writer
