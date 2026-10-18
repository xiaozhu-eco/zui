// Each app's start() is the path main() uses. An opened file stays the document;
// the HTML page beside the executable stays the page.

#include <gtest/gtest.h>

#include <zui/app.h>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../example/applications/album/cpp/session.h"
#ifndef ZUI_WITHOUT_ZMD
#include "../../example/applications/zmd/cpp/session.h"
#endif
#include "ui/view.h"

namespace fs = std::filesystem;

namespace {

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

fs::path copy_page(const fs::path& app_dir, const fs::path& source_html) {
    fs::create_directories(app_dir);
    const fs::path page = app_dir / "index.html";
    fs::copy_file(source_html, page, fs::copy_options::overwrite_existing);
    return page;
}

struct Argv {
    std::string exe;
    std::string content;
    std::vector<char*> pointers;

    Argv(const fs::path& exe_path, const fs::path& content_path) : exe(exe_path.string()), content(content_path.string()) {
        pointers.push_back(exe.data());
        pointers.push_back(content.data());
    }

    int argc() const { return static_cast<int>(pointers.size()); }
    char** argv() { return pointers.data(); }
};

}  // namespace

TEST(OpenedPath, AlbumImportsImageOnAlbumPage) {
    const fs::path root = fs::temp_directory_path() / "zui-opened-album";
    fs::remove_all(root);
    const fs::path app_dir = root / "app";
    const fs::path library = root / "library";
    fs::create_directories(library);
    copy_page(app_dir, fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "album" / "index.html");
    const fs::path image = root / "opener-shot.png";
    {
        std::ofstream out(image, std::ios::binary);
        out << "png-bytes";
    }

    zui::App app(0, nullptr);
    album::Session session;
    session.app = &app;
    session.dir = library;
    Argv args(app_dir / "zui_album.exe", image);
    ASSERT_TRUE(album::start(session, args.argc(), args.argv()));

    EXPECT_EQ(app.document().get("restore", "text"), "恢复示例");
    const album::Photo* imported = nullptr;
    const album::Photo* sample = nullptr;
    for (const album::Photo& photo : session.store().photos()) {
        if (photo.name == "opener-shot") {
            imported = &photo;
        }
        if (photo.name == "渡口") {
            sample = &photo;
        }
    }
    ASSERT_NE(imported, nullptr);
    EXPECT_NE(sample, nullptr);
    EXPECT_EQ(read_bytes(library / imported->file), read_bytes(image));
    fs::remove_all(root);
}

#ifndef ZUI_WITHOUT_ZMD
TEST(OpenedPath, MarkdownOpensDocumentOnEditorPage) {
    const fs::path root = fs::temp_directory_path() / "zui-opened-zmd";
    fs::remove_all(root);
    const fs::path app_dir = root / "app";
    copy_page(app_dir, fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zmd" / "index.html");
    const fs::path note = root / "opened-note.md";
    {
        std::ofstream out(note, std::ios::binary);
        out << "# opener note\n";
    }

    zui::App app(0, nullptr);
    zmd::Session session(app);
    Argv args(app_dir / "zui_zmd.exe", note);
    ASSERT_TRUE(session.start(args.argc(), args.argv()));

    // The page is the prototype's screen, so the name lives on `mdc-a-name`; the opened
    // document's path lives on Document.
    EXPECT_EQ(session.document().path(), note);
    EXPECT_EQ(session.document().source(), read_bytes(note));
    EXPECT_NE(session.document().path(), app_dir / "index.html");
    fs::remove_all(root);
}
#endif

TEST(OpenedPath, DatabaseOpensDocumentOnAppPage) {
    const fs::path root = fs::temp_directory_path() / "zui-opened-zdb";
    fs::remove_all(root);
    const fs::path app_dir = root / "app";
    copy_page(app_dir, fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zdb" / "index.html");
    const fs::path database = root / "opened.db";
    sqlite3* created = nullptr;
    ASSERT_EQ(sqlite3_open(database.string().c_str(), &created), SQLITE_OK);
    char* message = nullptr;
    ASSERT_EQ(sqlite3_exec(created, "CREATE TABLE notes(body TEXT); INSERT INTO notes(body) VALUES ('opened-by-opener');",
                           nullptr, nullptr, &message),
              SQLITE_OK)
        << (message == nullptr ? "" : message);
    sqlite3_free(message);
    sqlite3_close(created);

    zui::App app(0, nullptr);
    zdb::Session session;
    session.app = &app;
    Argv args(app_dir / "zui_zdb.exe", database);
    ASSERT_TRUE(zdb::start(session, args.argc(), args.argv())) << session.notice;

    EXPECT_EQ(app.document().get("open-sample", "text"), "示例库");
    EXPECT_EQ(app.document().get("file-name", "text"), database.filename().string());
    ASSERT_NE(session.db, nullptr);
    std::string body;
    ASSERT_EQ(sqlite3_exec(
                  session.db, "SELECT body FROM notes",
                  [](void* out, int count, char** values, char**) -> int {
                      if (count > 0 && values[0] != nullptr) {
                          static_cast<std::string*>(out)->assign(values[0]);
                      }
                      return 0;
                  },
                  &body, &message),
              SQLITE_OK)
        << (message == nullptr ? "" : message);
    sqlite3_free(message);
    EXPECT_EQ(body, "opened-by-opener");
    fs::remove_all(root);
}
