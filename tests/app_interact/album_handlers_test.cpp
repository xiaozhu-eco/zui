// Every handler album/cpp/session.cpp registers, driven through the control that declares it.
//
// NOTE (engine bug worked around here): a click whose handler rebuilds the list that contains the
// clicked node leaves `Interaction::hover` pointing at the freed node. `Access::click_xy` survives
// it (no frame pump), but the borrowed-window pump calls `sync_hover` and dereferences the stale
// pointer. So a test that clicks an album button runs headless, and the input-driven tests keep the
// pointer off the album list (they normalize through 恢复示例 instead of clicking a row).

#include "browser_oracle.h"
#include "handler_rig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "../../example/applications/album/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path album_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "album" / "index.html";
}

struct AlbumRig {
    zui::test::HandlerRig rig;
    album::Session session;

    /// Click through the headless seam. A windowed pump calls `sync_hover`, which dereferences a
    /// `hovered` node a list rebuild freed; the seam does not pump, so it survives the rebuild.
    static bool seam_click(zui::test::HandlerRig& rig, std::string_view text) {
        zui::frame::Access::sync_observation(rig.app);
        for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.app)) {
            if (zui::test::class_token(row.class_attr, "album-btn") &&
                row.run_text.find(text) != std::string::npos && row.w > 0.f && row.h > 0.f) {
                return zui::frame::Access::click_xy(rig.app, row.x + row.w * 0.5f,
                                                    row.y + row.h * 0.5f);
            }
        }
        return false;
    }

    /// Leave `g_current = 全部` for the tests that run after this one (it is a file-scope global).
    ~AlbumRig() { (void)seam_click(rig, "全部"); }

    bool up(bool windowed) {
        session.app = &rig.app;
        session.dir = zui::test::scratch_dir("album-handlers");
        fs::remove_all(session.dir);
        if (!rig.app.document().load(album_html().string())) {
            return false;
        }
        album::bind_actions(session);
        album::show(session);
        if (windowed) {
            if (!rig.windowed() || !rig.pump() || !(rig.pump(), true)) {
                return false;
            }
        } else if (!rig.headless()) {
            return false;
        }
        // `g_current` / `g_light` / `g_zoom` leak across tests (show() never resets them).
        if (!seam_click(rig, "全部")) {
            return false;
        }
        if (rig.get("lightbox", "visible") == "true") {
            // The viewer opens with its tools closed, so there is no 关闭 button on screen: the
            // leftover viewer from an earlier test is left with Escape.
            if (!zui::frame::Access::key(rig.app, "Escape")) {
                return false;
            }
        }
        if (rig.get("confirm", "visible") == "true" && !rig.click("confirm-cancel")) {
            return false;
        }
        if (rig.get("album-dialog", "visible") == "true" && !rig.click("album-form-cancel")) {
            return false;
        }
        return true;
    }

    bool up_headless() { return up(false); }
    bool up_windowed() { return up(true); }

    bool open(std::string_view tile_text) {
        return rig.click_text("tile", tile_text) && (rig.observe(), true);
    }

    /// 信息 (`i`): opens the tools over the photo - the bar, the filmstrip and the form that
    /// edits the caption and the album. A test that drives one of those opens this first.
    bool tools() {
        if (!zui::frame::Access::key(rig.app, "i")) {
            return false;
        }
        rig.observe();
        return rig.laid("light-close");
    }
};

int photos(const AlbumRig& r) { return zui::test::count_class(r.rig.app, "tile"); }
int albums(const AlbumRig& r) { return zui::test::count_class(r.rig.app, "album-btn"); }

std::vector<std::string> open_option_labels(AlbumRig& r) {
    r.rig.observe();
    std::vector<std::string> labels;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "option" && row.w > 0.f && row.h > 0.f && !row.run_text.empty()) {
            labels.push_back(row.run_text);
        }
    }
    std::sort(labels.begin(), labels.end());
    return labels;
}

bool click_option(AlbumRig& r, const std::string& label) {
    r.rig.observe();
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "option" && row.run_text == label && row.w > 0.f && row.h > 0.f) {
            return r.rig.at(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, AlbumClickSelectsTheAlbum) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    EXPECT_EQ(r.rig.get("album-title", "text"), "全部");
    EXPECT_EQ(r.rig.get("album-count", "text"), "6 张");

    ASSERT_TRUE(r.rig.click_text("album-btn", "渡口"));
    r.rig.observe();

    EXPECT_EQ(photos(r), 2) << "渡口 holds 渡口 and 海岸";
    EXPECT_EQ(r.rig.get("album-count", "text"), "2 张");
    EXPECT_EQ(r.rig.get("album-name", "value"), "渡口") << "the editable album shows its name";
}

TEST(AppInteract, AlbumNameEditRenamesTheButton) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    // Create an editable album (both clicks land outside the rebuilt list) and rename it.
    ASSERT_TRUE(r.rig.click("add-album"));
    ASSERT_TRUE(r.rig.app.document().set("album-form-name", "value", "海雾"));
    ASSERT_TRUE(r.rig.click_labelled("创建"));
    ASSERT_EQ(r.rig.get("album-name", "value"), "海雾");

    ASSERT_TRUE(r.rig.app.document().set("album-name", "value", ""));
    ASSERT_TRUE(r.rig.type("album-name", "海雾集"));
    r.rig.observe();

    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "album-btn", "海雾集"))
        << "the sidebar button follows the rename";
}

// view.js `sortAlbums` ranks 未分类 last. A newly created album sits with the other named
// albums, ahead of that bin. The browser script is the source of the order.
TEST(AppInteract, AlbumNewAlbumSortsBeforeTheLooseBin) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    ASSERT_TRUE(r.rig.click("add-album"));
    ASSERT_TRUE(r.rig.app.document().set("album-form-name", "value", "海雾"));
    ASSERT_TRUE(r.rig.click_labelled("创建"));
    r.rig.observe();

    int loose = -1;
    int created = -1;
    int index = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "album-btn") || row.w <= 0.f) {
            continue;
        }
        if (row.run_text.find("未分类") != std::string::npos) {
            loose = index;
        }
        if (row.run_text.find("海雾") != std::string::npos) {
            created = index;
        }
        ++index;
    }
    ASSERT_GE(loose, 0);
    ASSERT_GE(created, 0);
    EXPECT_LT(created, loose);
}

TEST(AppInteract, AlbumPhotoClickOpensTheLightbox) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");

    ASSERT_TRUE(r.open("海岸"));

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "true");
    EXPECT_EQ(r.rig.get("light-name", "text"), "海岸");
    EXPECT_FALSE(r.rig.get("light-pos", "text").empty());
}

// The photo is the view: the bar, the filmstrip and the caption/album form are one disclosure
// (信息), closed when a photo opens.
TEST(AppInteract, AlbumLightOpensWithTheToolsClosed) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("渡口"));

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "true");
    EXPECT_EQ(r.rig.get("light-inspector", "checked"), "false");
    EXPECT_FALSE(r.rig.laid("light-close")) << "the bar is not in the photo's way";
    EXPECT_FALSE(r.rig.laid("light-note")) << "nor is the caption and album form";
    float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(r.rig.app, "stage", cx, cy, cw, ch));
    const float closed = ch;

    ASSERT_TRUE(r.tools());

    EXPECT_EQ(r.rig.get("light-inspector", "checked"), "true");
    EXPECT_TRUE(r.rig.laid("light-close"));
    EXPECT_TRUE(r.rig.laid("light-note"));
    ASSERT_TRUE(zui::frame::Access::box_rect(r.rig.app, "stage", cx, cy, cw, ch));
    EXPECT_GT(closed, ch) << "the photo takes the space the tools were using";
}

// With the tools closed the photo fills the page, so a click on it (or on the dark beside it)
// leaves the viewer: `#light-img` declares no handler, so the click reaches the lightbox.
TEST(AppInteract, AlbumLightClickLeavesTheViewer) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_EQ(r.rig.get("lightbox", "visible"), "true");

    ASSERT_TRUE(r.rig.click("lightbox"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");
}

// While the tools are up their clicks belong to them: a click on the photo is not "leave".
TEST(AppInteract, AlbumLightClickKeepsTheToolsOpen) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.click("lightbox"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "true");
}

TEST(AppInteract, AlbumLightCloseHidesTheLightbox) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_EQ(r.rig.get("lightbox", "visible"), "true");
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.click("light-close"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");
}

TEST(AppInteract, AlbumPrevStepsBack) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("灯塔"));
    ASSERT_EQ(r.rig.get("light-name", "text"), "灯塔");

    ASSERT_TRUE(r.rig.click("light-prev"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("light-name", "text"), "渡口") << "渡口 is the first photo in 全部";
}

TEST(AppInteract, AlbumNextStepsForward) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_EQ(r.rig.get("light-name", "text"), "渡口");

    ASSERT_TRUE(r.rig.click("light-next"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("light-name", "text"), "灯塔");
}

TEST(AppInteract, AlbumZoomInRaisesTheLabel) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_TRUE(r.tools());
    ASSERT_EQ(r.rig.get("zoom-label", "text"), "100%");

    ASSERT_TRUE(r.rig.click("zoom-in"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("zoom-label", "text"), "150%");
    EXPECT_TRUE(r.rig.has_class("stage", "is-zoomed"));
}

TEST(AppInteract, AlbumZoomOutClampsAtOneHundred) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.click("zoom-out"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("zoom-label", "text"), "100%") << "zoom never goes below 100%";
    EXPECT_FALSE(r.rig.has_class("stage", "is-zoomed"));
}

TEST(AppInteract, AlbumControlWheelZoomsTheStage) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_EQ(r.rig.get("zoom-label", "text"), "100%");

    ASSERT_TRUE(r.rig.wheel_window("stage", 1.f, /*ctrl=*/true));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("zoom-label", "text"), "150%") << "Ctrl+wheel zooms in";
}

TEST(AppInteract, AlbumNoteEditPersistsOnReopen) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.app.document().set("light-note", "value", ""));
    ASSERT_TRUE(r.rig.type("light-note", "潮位记在这张背面"));
    ASSERT_TRUE(r.rig.click("light-close"));
    ASSERT_TRUE(r.open("渡口"));

    EXPECT_EQ(r.rig.get("light-note", "value"), "潮位记在这张背面");
}

TEST(AppInteract, AlbumNoteEscapeClosesTheLightbox) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_EQ(r.rig.get("lightbox", "visible"), "true");
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.press("light-note", "Escape"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");
}

TEST(AppInteract, AlbumAccelEscapeClosesAndArrowsStep) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("渡口"));
    ASSERT_EQ(r.rig.get("light-name", "text"), "渡口");

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "ArrowRight"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("light-name", "text"), "灯塔") << "the body accelerator steps forward";

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");
}

TEST(AppInteract, AlbumPickAlbumMovesThePhoto) {
    std::string error;
    const nlohmann::json oracle = zui::test::load_browser_oracle(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(oracle.contains("album")) << "browser oracle has no album readback";
    const nlohmann::json& album = oracle.at("album");

    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.rig.click_text("album-btn", "渡口"));
    r.rig.observe();
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_EQ(r.rig.get("lightbox", "visible"), "true");
    ASSERT_TRUE(r.tools());
    EXPECT_EQ(r.rig.get("album-picks", "visible"), "false");
    EXPECT_EQ(r.rig.get("light-album", "value"), album.at("light_value").get<std::string>());

    ASSERT_TRUE(r.rig.click("light-album"));
    const std::vector<std::string> labels = open_option_labels(r);
    ASSERT_EQ(labels, album.at("option_labels").get<std::vector<std::string>>())
        << "the select lists the model albums";

    ASSERT_TRUE(click_option(r, album.at("move_to_label").get<std::string>()));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("lightbox", "visible"), album.at("after_lightbox").get<std::string>())
        << "the photo left the current album, so the lightbox closes";
    EXPECT_EQ(r.rig.get("album-count", "text"), album.at("after_count").get<std::string>())
        << "渡口 keeps only 渡口";
}

TEST(AppInteract, AlbumStageDragScrollsByTheDelta) {
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    ASSERT_TRUE(r.open("渡口"));
    // The zoom buttons live in the 信息 bar; Ctrl+wheel zooms without opening it.
    ASSERT_TRUE(r.rig.wheel_window("stage", 1.f, /*ctrl=*/true));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("zoom-label", "text"), "150%");
    ASSERT_TRUE(r.rig.has_class("stage", "is-zoomed"));

    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(r.rig.app, "stage", x, y, w, h));
    const float x0 = x + w * 0.5f;
    const float y0 = y + h * 0.5f;
    const float before = std::stof(r.rig.get("stage", "scroll_left"));
    constexpr float kDelta = 80.f;
    ASSERT_TRUE(r.rig.drag(x0, y0, x0 - kDelta, y0));
    r.rig.observe();

    const float after = std::stof(r.rig.get("stage", "scroll_left"));
    EXPECT_NEAR(after - before, kDelta, 1.5f)
        << "one drag applies the pointer delta once (before=" << before << " after=" << after << ")";
    EXPECT_EQ(r.rig.get("lightbox", "visible"), "true")
        << "a pan is not a click on the photo";

    ASSERT_TRUE(r.rig.wheel_window("stage", 1.f, /*ctrl=*/true));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("zoom-label", "text"), "200%");
}

TEST(AppInteract, AlbumDeletePhotoAsksThenDeletes) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    const int before = photos(r);
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_TRUE(r.tools());

    ASSERT_TRUE(r.rig.click("light-delete"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除「海岸」"), std::string::npos);

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(r.rig.get("lightbox", "visible"), "false");
    EXPECT_EQ(photos(r), before - 1);
}

TEST(AppInteract, AlbumDeleteAlbumAsksThenMovesItsPhotos) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    const int albums_before = albums(r);
    ASSERT_TRUE(r.rig.click_text("album-btn", "渡口"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click("delete-album"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除相册「渡口」"), std::string::npos);

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();
    EXPECT_EQ(albums(r), albums_before - 1);
    ASSERT_FALSE(zui::test::any_text(r.rig.app, "", "album-btn", "渡口"));

    // The photos moved to 未分类 rather than being deleted.
    ASSERT_TRUE(r.rig.click_text("album-btn", "全部"));
    r.rig.observe();
    EXPECT_EQ(photos(r), 6);
}

TEST(AppInteract, AlbumRestoreAsksThenReseeds) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.open("海岸"));
    ASSERT_TRUE(r.tools());
    ASSERT_TRUE(r.rig.click("light-delete"));
    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();
    ASSERT_EQ(photos(r), 5);

    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("restore"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("confirm-body", "text").find("恢复示例"), std::string::npos);

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(photos(r), 6) << "the six sample photos are back";
}

TEST(AppInteract, AlbumAddAlbumOpensTheDialog) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    EXPECT_EQ(r.rig.get("album-dialog", "visible"), "false");

    ASSERT_TRUE(r.rig.click("add-album"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("album-dialog", "visible"), "true");
    EXPECT_EQ(r.rig.get("album-form-name", "value"), "");
}

TEST(AppInteract, AlbumCancelDialogHidesIt) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.rig.click("add-album"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("album-dialog", "visible"), "true");

    ASSERT_TRUE(r.rig.click("album-form-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("album-dialog", "visible"), "false");
}

TEST(AppInteract, AlbumSaveDialogRejectsAnEmptyName) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_TRUE(r.rig.click("add-album"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("创建"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("album-form-error", "text"), "请填写名称");
    EXPECT_EQ(r.rig.get("album-dialog", "visible"), "true");
}

TEST(AppInteract, AlbumSaveDialogAddsAndSelectsTheAlbum) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    const int before = albums(r);
    ASSERT_TRUE(r.rig.click("add-album"));
    ASSERT_TRUE(r.rig.app.document().set("album-form-name", "value", "海雾"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("创建"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("album-dialog", "visible"), "false");
    EXPECT_EQ(albums(r), before + 1);
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "album-btn", "海雾"));
    EXPECT_EQ(r.rig.get("album-name", "value"), "海雾") << "the new album becomes current";
}

TEST(AppInteract, AlbumConfirmCancelKeepsThePhotos) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(r.rig.click("confirm-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(photos(r), 6);
}

TEST(AppInteract, AlbumImportLabelIsWiredToTheFileInput) {
    AlbumRig r;
    ASSERT_TRUE(r.up_headless());
    // The picker itself is the host's (it opens a real file dialog), so this pins the wiring the
    // handler reads: the label has the click handler and a real <input type=file> behind it.
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "label", "btn", "导入照片"));
    bool file_input = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "input" && row.id == "file") {
            file_input = true;
        }
    }
    EXPECT_TRUE(file_input) << "<input id=file> is what on_import feeds";
}

TEST(AppInteract, AlbumSelectStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    AlbumRig r;
    ASSERT_TRUE(r.up_windowed());
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)r.rig.seam_click_text("album-btn", "渡口"); });
    // Measured 34 / 71 / 528 / 38 when the gate was introduced, then 134 cascade on five
    // consecutive Debug runs once the author sheet (album-btn transition included) actually
    // applied. Selecting an album still rebuilds the column and its grid, not the whole shell.
    // A regression that re-cascades the page or re-measures the sidebar per tile blows these up.
    EXPECT_LE(work.cascade_visited, 192u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 128u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 900u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 80u) << "boxes=" << work.boxes_created;
}
