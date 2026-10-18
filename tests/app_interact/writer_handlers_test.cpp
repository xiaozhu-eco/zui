// Writer handlers, driven through the controls declared on the shipped page.

#include "browser_oracle.h"
#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../../example/applications/writer/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path writer_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "writer" / "index.html";
}

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool replace_value(zui::test::HandlerRig& rig, std::string_view id, const std::string& text) {
    // Focus-then-type inserts at the caret, so clear the control first. The following
    // type() is what fires input on the shipped field.
    if (!rig.app.document().set(id, "value", "")) {
        return false;
    }
    return rig.type(id, text);
}

struct WriterRig {
    zui::test::HandlerRig rig;
    writer::Session session;
    fs::path dir;

    bool up(int width = 1100, int height = 720, bool wipe = true) {
        session.app = &rig.app;
        if (dir.empty()) {
            dir = zui::test::scratch_dir("writer");
        }
        session.dir = dir;
        std::error_code ec;
        if (wipe) {
            fs::remove_all(session.dir, ec);
        }
        fs::create_directories(session.dir, ec);
        if (!rig.app.document().load(writer_html().string())) {
            return false;
        }
        if (!rig.app.window().set_window_size(width, height)) {
            return false;
        }
        writer::bind_actions(session);
        writer::show(session);
        if (!rig.windowed(width, height) || !rig.pump() || !rig.pump()) {
            return false;
        }
        return true;
    }

    bool show_tree() {
        if (!rig.point(12.f, 160.f)) {
            return false;
        }
        return rig.pump();
    }
};

bool box(const WriterRig& rig, std::string_view id, float& x, float& y, float& w, float& h) {
    return zui::frame::Access::box_rect(rig.rig.app, id, x, y, w, h);
}

}  // namespace

TEST(AppInteract, WriterNewPieceShowsInTheTree) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());

    ASSERT_TRUE(rig.rig.click("new-piece"));
    rig.rig.pump();

    EXPECT_TRUE(zui::test::any_text(rig.rig.app, "", "", "未命名文稿 d1"));
    EXPECT_EQ(rig.rig.get("doc-title", "text"), "未命名文稿 d1");
}

TEST(AppInteract, WriterNewFolderHoldsAPiece) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-folder"));
    ASSERT_TRUE(rig.rig.click("node-d1"));
    ASSERT_TRUE(rig.rig.click("new-piece"));
    rig.rig.pump();

    EXPECT_TRUE(rig.rig.has_class("node-d2", "d1")) << "the piece is nested under the folder";
    EXPECT_TRUE(zui::test::any_text(rig.rig.app, "", "", "新建文件夹 d1"));
    ASSERT_TRUE(rig.rig.click("node-d2"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "文件夹里的句子"));
    EXPECT_EQ(rig.rig.get("editor", "value"), "文件夹里的句子");
}

TEST(AppInteract, WriterTreeSelectOpensThePiece) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "甲稿"));
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "乙稿"));

    ASSERT_TRUE(rig.rig.click("node-d1"));
    EXPECT_EQ(rig.rig.get("editor", "value"), "甲稿");
    ASSERT_TRUE(rig.rig.click("node-d2"));
    EXPECT_EQ(rig.rig.get("editor", "value"), "乙稿");
}

TEST(AppInteract, WriterRenameChangesTheTree) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "rename-name", "灯塔手记"));
    ASSERT_TRUE(rig.rig.click("rename-apply"));
    rig.rig.pump();

    EXPECT_EQ(rig.rig.get("doc-title", "text"), "灯塔手记");
    EXPECT_TRUE(zui::test::any_text(rig.rig.app, "", "", "灯塔手记"));
}

TEST(AppInteract, WriterEditModeAcceptsText) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(rig.rig.click("mode-edit"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "正在写的句子"));

    EXPECT_EQ(rig.rig.get("editor", "value"), "正在写的句子");
    EXPECT_EQ(rig.rig.get("editor", "visible"), "true");
    EXPECT_TRUE(rig.rig.has_class("mode-edit", "is-on"));

    // The frame path negates the window wheel delta. A positive dy arrives as
    // a negative Wheel::delta_y, which grows the text.
    ASSERT_TRUE(rig.rig.wheel_window("editor", 40.f, true));
    EXPECT_TRUE(rig.rig.has_class("app", "tx4"));
}

TEST(AppInteract, WriterReadModeShowsTextAndRejectsEdits) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "可读的句子"));
    ASSERT_TRUE(rig.rig.click("mode-read"));
    rig.rig.pump();

    EXPECT_EQ(rig.rig.get("reader", "text"), "可读的句子");
    EXPECT_EQ(rig.rig.get("reader", "visible"), "true");
    EXPECT_EQ(rig.rig.get("editor", "visible"), "false");
    EXPECT_TRUE(rig.rig.has_class("mode-read", "is-on"));
    EXPECT_FALSE(rig.rig.type("editor", "X"));
    EXPECT_EQ(rig.rig.get("editor", "value"), "可读的句子");

    ASSERT_TRUE(rig.rig.click("mode-edit"));
    EXPECT_EQ(rig.rig.get("editor", "value"), "可读的句子");
    EXPECT_EQ(rig.rig.get("editor", "visible"), "true");
}

TEST(AppInteract, WriterDeleteRemovesThePiece) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(rig.rig.click("delete-doc"));
    ASSERT_TRUE(rig.rig.pump());

    float x = 0, y = 0, w = 0, h = 0;
    EXPECT_FALSE(box(rig, "node-d2", x, y, w, h));
    EXPECT_TRUE(box(rig, "node-d1", x, y, w, h));
    EXPECT_GT(w, 0.f);
}

TEST(AppInteract, WriterStorageRoundTrip) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    const fs::path home = rig.dir;
    const fs::path store = home / "shelf";
    ASSERT_TRUE(rig.rig.click("settings"));
    ASSERT_TRUE(replace_value(rig.rig, "storage-path", store.string()));
    ASSERT_TRUE(rig.rig.click("storage-apply"));
    EXPECT_NE(rig.rig.get("storage-status", "text").find("已设为存储位置"), std::string::npos);

    ASSERT_TRUE(rig.show_tree());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "留在磁盘上的句子"));
    ASSERT_TRUE(rig.rig.click("save"));

    const fs::path piece = store / "pieces" / "d1.txt";
    const fs::path library = store / "library.json";
    ASSERT_TRUE(fs::is_regular_file(piece));
    EXPECT_EQ(read_bytes(piece), "留在磁盘上的句子");
    const std::string library_bytes = read_bytes(library);
    EXPECT_NE(library_bytes.find("d1"), std::string::npos);

    // A new empty session must open the shelf through the storage control.
    // Pointing Session.dir at the shelf would skip that path, and saving the
    // empty set there would replace library.json.
    WriterRig opened;
    ASSERT_TRUE(opened.up());
    ASSERT_TRUE(opened.rig.click("settings"));
    ASSERT_TRUE(replace_value(opened.rig, "storage-path", store.string()));
    ASSERT_TRUE(opened.rig.click("storage-apply"));
    EXPECT_EQ(opened.rig.get("editor", "value"), "留在磁盘上的句子");
    ASSERT_TRUE(opened.show_tree());
    EXPECT_TRUE(zui::test::any_text(opened.rig.app, "", "", "未命名文稿 d1"));
    EXPECT_EQ(read_bytes(piece), "留在磁盘上的句子");
    EXPECT_EQ(read_bytes(library), library_bytes);

    // The next process starts at the default data directory, as main does,
    // and follows the path the control recorded.
    WriterRig again;
    again.dir = home;
    ASSERT_TRUE(again.up(1100, 720, false));
    EXPECT_EQ(again.rig.get("editor", "value"), "留在磁盘上的句子");
    EXPECT_EQ(again.rig.get("storage-path", "value"), store.string());
    EXPECT_EQ(read_bytes(library), library_bytes);
}

TEST(AppInteract, WriterCalendarShowsActivity) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "rename-name", "灯塔手记"));
    ASSERT_TRUE(rig.rig.click("rename-apply"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "今天写的"));
    ASSERT_TRUE(rig.rig.click("save"));
    ASSERT_TRUE(rig.rig.click("view-calendar"));
    ASSERT_TRUE(rig.rig.pump());

    EXPECT_NE(rig.rig.get("cal-title", "text").find("年"), std::string::npos);
    EXPECT_TRUE(zui::test::any_text(rig.rig.app, "", "", "灯塔手记"));
    EXPECT_EQ(rig.rig.get("calendar-view", "visible"), "true");

    ASSERT_TRUE(rig.rig.click("view-tree"));
    EXPECT_EQ(rig.rig.get("workspace", "visible"), "true");
    EXPECT_EQ(rig.rig.get("calendar-view", "visible"), "false");
}

TEST(AppInteract, WriterHistoryShowsTheOlderText) {
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", "旧稿"));
    ASSERT_TRUE(rig.rig.click("save"));
    ASSERT_TRUE(rig.rig.type("editor", " 新稿"));
    ASSERT_TRUE(rig.rig.click("save"));
    ASSERT_TRUE(rig.rig.click("view-history"));
    ASSERT_TRUE(rig.rig.pump());

    std::string snap;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.rig.app)) {
        if (zui::test::class_token(row.class_attr, "snap") && row.w > 0.f && !row.id.empty()) {
            snap = row.id;
        }
    }
    ASSERT_FALSE(snap.empty());
    EXPECT_EQ(snap.compare(0, 5, "snap-"), 0);
    ASSERT_TRUE(rig.rig.click(snap));

    EXPECT_EQ(rig.rig.get("history-body", "text"), "旧稿");
    EXPECT_EQ(rig.rig.get("editor", "value"), "旧稿 新稿");
    EXPECT_NE(rig.rig.get("history-body", "text"), rig.rig.get("editor", "value"));
}

TEST(AppInteract, WriterDesktopTreeSitsBesideThePiece) {
    WriterRig rig;
    ASSERT_TRUE(rig.up(1100, 720));
    ASSERT_TRUE(rig.show_tree());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(rig.show_tree());
    ASSERT_TRUE(rig.rig.pump());

    float tx = 0, ty = 0, tw = 0, th = 0;
    float dx = 0, dy = 0, dw = 0, dh = 0;
    float mx = 0, my = 0, mw = 0, mh = 0;
    float rx = 0, ry = 0, rw = 0, rh = 0;
    ASSERT_TRUE(box(rig, "tree-pane", tx, ty, tw, th));
    ASSERT_TRUE(box(rig, "doc-pane", dx, dy, dw, dh));
    ASSERT_TRUE(box(rig, "mode-edit", mx, my, mw, mh));
    ASSERT_TRUE(box(rig, "mode-read", rx, ry, rw, rh));
    EXPECT_GT(tw, 80.f);
    EXPECT_LT(tw, 360.f);
    EXPECT_GT(th, 48.f);
    EXPECT_LT(th, 520.f) << "tree card is a full-height drawer";
    EXPECT_GT(ty, 40.f);
    float lx = 0, ly = 0, lw = 0, lh = 0;
    ASSERT_TRUE(box(rig, "tree", lx, ly, lw, lh));
    EXPECT_GT(lh, 8.f);
    EXPECT_LT(dx, 4.f);
    EXPECT_GT(dw, 1000.f);
    EXPECT_GT(tx + tw, dx + 30.f) << "tree card should float over the page";
    EXPECT_GT(mw, 0.f);
    EXPECT_GT(mh, 0.f);
    EXPECT_GT(rw, 0.f);
    EXPECT_GT(rh, 0.f);

    ASSERT_TRUE(rig.rig.point(600.f, 300.f));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_FALSE(box(rig, "tree-pane", tx, ty, tw, th));

    ASSERT_TRUE(rig.show_tree());
    ASSERT_TRUE(box(rig, "tree-pane", tx, ty, tw, th));
    EXPECT_GT(tw, 80.f);
    EXPECT_LT(th, 520.f);
}

TEST(AppInteract, WriterPhonePieceFitsAndTreeIsReachable) {
    WriterRig rig;
    ASSERT_TRUE(rig.up(390, 800));
    ASSERT_TRUE(rig.rig.click("tree-toggle"));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    ASSERT_TRUE(rig.rig.click("tree-toggle"));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(replace_value(rig.rig, "editor", "窄屏上的一段话"));
    ASSERT_TRUE(rig.rig.pump());

    float dx = 0, dy = 0, dw = 0, dh = 0;
    ASSERT_TRUE(box(rig, "doc-pane", dx, dy, dw, dh));
    EXPECT_LE(dx + dw, 392.f);
    EXPECT_GT(dw, 100.f);
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.rig.app)) {
        if (row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        EXPECT_LE(row.x + row.w, 392.f) << row.id << " " << row.tag << " " << row.class_attr;
    }
    float mx = 0, my = 0, mw = 0, mh = 0;
    float rx = 0, ry = 0, rw = 0, rh = 0;
    ASSERT_TRUE(box(rig, "mode-edit", mx, my, mw, mh));
    ASSERT_TRUE(box(rig, "mode-read", rx, ry, rw, rh));
    EXPECT_GT(mw, 0.f);
    EXPECT_GT(rw, 0.f);

    ASSERT_TRUE(rig.rig.click("tree-toggle"));
    ASSERT_TRUE(rig.rig.pump());
    float tx = 0, ty = 0, tw = 0, th = 0;
    ASSERT_TRUE(box(rig, "tree-pane", tx, ty, tw, th));
    EXPECT_GT(tw, 40.f);
    EXPECT_LE(tx + tw, 392.f);
    EXPECT_TRUE(box(rig, "mode-read", rx, ry, rw, rh));
    EXPECT_GT(rw, 0.f);
}

TEST(AppInteract, WriterNewPieceMatchesTheBrowser) {
    std::string error;
    const nlohmann::json oracle = zui::test::load_browser_oracle(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(oracle.contains("writer")) << "browser oracle has no writer readback";
    const nlohmann::json& spec = oracle.at("writer");

    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("new-piece"));
    EXPECT_EQ(rig.rig.get("doc-title", "text"), spec.at("new_title").get<std::string>());
    EXPECT_NE(rig.rig.get("status", "text").find(spec.at("new_status").get<std::string>()),
              std::string::npos);

    ASSERT_TRUE(replace_value(rig.rig, "editor", spec.at("first_body").get<std::string>()));
    ASSERT_TRUE(rig.rig.click("save"));
    ASSERT_TRUE(replace_value(rig.rig, "editor", spec.at("second_body").get<std::string>()));
    ASSERT_TRUE(rig.rig.click("save"));
    ASSERT_TRUE(rig.rig.click("view-history"));
    ASSERT_TRUE(rig.rig.pump());

    std::string snap;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.rig.app)) {
        if (zui::test::class_token(row.class_attr, "snap") && row.w > 0.f && !row.id.empty()) {
            snap = row.id;
        }
    }
    ASSERT_FALSE(snap.empty());
    EXPECT_EQ(snap.compare(0, 5, "snap-"), 0);
    ASSERT_TRUE(rig.rig.click(snap));
    EXPECT_EQ(rig.rig.get("history-body", "text"), spec.at("history_body").get<std::string>());
    EXPECT_EQ(rig.rig.get("editor", "value"), spec.at("second_body").get<std::string>());
}

TEST(AppInteract, WriterNewPieceStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    const zui::perf::FrameWork work = rig.rig.measure([&] {
        ASSERT_TRUE(zui::frame::Access::click_id(rig.rig.app, "new-piece"));
    });
    EXPECT_EQ(rig.rig.get("doc-title", "text"), "未命名文稿 d1");
    EXPECT_LE(work.cascade_visited, 256u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 320u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.boxes_created, 256u) << "boxes=" << work.boxes_created;
    EXPECT_LE(work.flow_remeasures, 512u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, WriterIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    WriterRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(rig.rig.pump());
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.painted_walked, 0u);
    ASSERT_TRUE(rig.rig.pump());
    const zui::perf::FrameWork third = zui::perf::last_frame_work();
    EXPECT_EQ(third.cascade_visited, 0u);
    EXPECT_EQ(third.laid_out, 0u);
    EXPECT_EQ(third.painted_walked, 0u);
}
