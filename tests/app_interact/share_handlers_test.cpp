// Every handler share/cpp/session.cpp registers, driven through the control that declares it.
// The browser reads a real scratch directory, and the config file lands in a scratch config dir,
// so `开始` exercises the same `load_config` + `net_start` path the app takes.
//
// The transfer itself needs a second endpoint; `zui_share bench recv|sendto` covers discovery,
// the 8-way chunked transfer, and resume over a real socket pair (see the example README).

#include "browser_oracle.h"
#include "handler_rig.h"

#include "../../example/applications/share/cpp/gesture.h"
#include "../../example/applications/share/cpp/session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path share_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "share" / "index.html";
}

void write_file(const fs::path& path, std::size_t bytes) {
    std::ofstream out(path, std::ios::binary);
    std::string block(bytes, 'x');
    out.write(block.data(), static_cast<std::streamsize>(block.size()));
}

struct ShareRig {
    zui::test::HandlerRig rig;
    std::unique_ptr<share::Session> session;
    fs::path home;     ///< scratch config dir (config.txt)
    fs::path listing;  ///< scratch directory the browser shows

    /// @p with_name writes a config first, which is how a second run starts on the browse screen.
    /// Width above 760px is the desktop sidebar; at or below that the phone chip strip is shown.
    bool up(bool with_name = false, int width = 1100, int height = 720) {
        home = zui::test::scratch_dir("share-handlers");
        std::error_code ec;
        fs::remove_all(home, ec);
        fs::create_directories(home, ec);
        share::set_config_dir(home.string());
        if (with_name) {
            std::ofstream out(home / "config.txt");
            out << "id=0000000000000001\nname=测试设备\nsave_dir=" << (home / "recv").string() << "\n";
        }
        if (!rig.app.document().load(share_html().string())) {
            return false;
        }
        session = std::make_unique<share::Session>(rig.app);
        session->bind();
        session->show();
        return rig.windowed(width, height) && rig.pump() && (rig.pump(), true);
    }

    /// Builds a small directory and points the browser at it.
    void stage_listing() {
        listing = home / "listing";
        std::error_code ec;
        fs::create_directories(listing / "folder", ec);
        write_file(listing / "a-cover.png", 1200);
        write_file(listing / "b-notes.txt", 12);
        write_file(listing / "c-manual.pdf", 2400);
        ASSERT_TRUE(session->browser().open(listing.string()));
        session->render_files();
        rig.pump();
    }

    /// Same setup as `up`, then the headless seam a work-unit gate measures through.
    bool up_headless(bool with_name = false) {
        home = zui::test::scratch_dir("share-handlers");
        std::error_code ec;
        fs::remove_all(home, ec);
        fs::create_directories(home, ec);
        share::set_config_dir(home.string());
        if (with_name) {
            std::ofstream out(home / "config.txt");
            out << "id=0000000000000001\nname=测试设备\nsave_dir=" << (home / "recv").string() << "\n";
        }
        if (!rig.app.document().load(share_html().string())) {
            return false;
        }
        session = std::make_unique<share::Session>(rig.app);
        session->bind();
        session->show();
        return rig.headless();
    }

    bool on(std::string_view screen) const { return rig.has_class("app", screen); }

    std::vector<std::string> texts(std::string_view token) { return rig.class_texts(token); }
};

struct ShareOracle {
    nlohmann::json share;
    std::string error;

    bool open() {
        const nlohmann::json all = zui::test::load_browser_oracle(error);
        if (!error.empty()) {
            return false;
        }
        if (!all.contains("share")) {
            error = "browser oracle has no share readback";
            return false;
        }
        share = all.at("share");
        return true;
    }

    std::string str(const char* key) const { return share.at(key).get<std::string>(); }
};

share::Peer staged_peer() {
    share::Peer peer;
    peer.id = 42;
    peer.name = "书房电脑";
    peer.ip = "10.0.0.8";
    peer.port = 0;
    return peer;
}

void stage_peer(ShareRig& r) {
    share::net_stage_peer(staged_peer());
    r.session->poll();
    r.rig.pump();
}

void stage_offer(ShareRig& r) {
    share::IncomingOffer offer;
    offer.offer_id = 7;
    offer.from_name = "客厅电脑";
    offer.from_ip = "10.0.0.9";
    offer.nfiles = 2;
    offer.nbytes = 1200;
    share::net_stage_incoming(offer);
    r.session->poll();
    r.rig.pump();
}

void stage_phase(ShareRig& r, share::XferPhase phase) {
    share::XferView view;
    view.phase = phase;
    view.sending = true;
    view.peer_name = "客厅电脑";
    view.peer_ip = "10.0.0.8";
    view.total = 1000;
    view.done = phase == share::XferPhase::Done ? 1000 : 400;
    view.error = "连不上对方";
    share::net_stage_xfer(view);
    r.session->poll();
    r.rig.pump();
}

bool click_scrim(ShareRig& r) {
    r.rig.observe();
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "scrim") || row.w < 1.f || row.h < 1.f) {
            continue;
        }
        return r.rig.at(row.x + 4.f, row.y + 4.f);
    }
    return false;
}

}  // namespace

TEST(AppInteract, ShareFirstNameMustNotBeEmpty) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up());
    // No config yet, so the name screen is up.
    EXPECT_TRUE(r.on(oracle.str("empty_name_screen")));
    EXPECT_EQ(r.rig.get("name-input", "value"), "");

    ASSERT_TRUE(r.rig.click("start"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("empty_name_screen")));
    EXPECT_TRUE(r.rig.visible("app-error"));
    EXPECT_EQ(r.rig.get("app-error", "text"), oracle.str("empty_name_error"));

    ASSERT_TRUE(r.rig.type("name-input", oracle.str("started_me")));
    ASSERT_TRUE(r.rig.click("start"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("started_screen")));
    EXPECT_EQ(r.rig.get("me-name", "text"), oracle.str("started_me"));
    EXPECT_TRUE(fs::exists(r.home / "config.txt"));
}

TEST(AppInteract, ShareBrowseListsTheRealDirectory) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();

    // Points at the same directory and renders one row per entry.
    EXPECT_EQ(r.rig.get("path", "value"), r.listing.string());
    EXPECT_EQ(zui::test::count_class(r.rig.app, "frow"), 4);
    const std::vector<std::string> names = r.texts("fname");
    ASSERT_EQ(names.size(), 4u);
    EXPECT_EQ(names[0], "folder");
    EXPECT_EQ(names[1], "a-cover.png");
    EXPECT_EQ(names[2], "b-notes.txt");
    EXPECT_EQ(names[3], "c-manual.pdf");
    // The type column reads the extension.
    const std::vector<std::string> kinds = r.texts("fkind");
    ASSERT_EQ(kinds.size(), 4u);
    EXPECT_EQ(kinds[0], "文件夹");
    EXPECT_EQ(kinds[1], "图片");
    EXPECT_EQ(kinds[2], "文本");
    EXPECT_EQ(kinds[3], "PDF");
}

TEST(AppInteract, ShareFolderRowEntersAndUpReturns) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();

    ASSERT_TRUE(r.rig.click("row-0"));  // the only folder
    r.rig.pump();
    EXPECT_EQ(r.rig.get("path", "value"), (r.listing / "folder").string());
    EXPECT_EQ(zui::test::count_class(r.rig.app, "frow"), 0);

    ASSERT_TRUE(r.rig.click("up"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("path", "value"), r.listing.string());
    EXPECT_EQ(zui::test::count_class(r.rig.app, "frow"), 4);
}

TEST(AppInteract, ShareSelectionDrivesTheBottomBar) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();

    EXPECT_EQ(r.rig.get("sel-label", "text"), "已选 0 项");
    EXPECT_EQ(r.rig.get("share", "disabled"), "true");
    EXPECT_FALSE(r.rig.visible("clear"));

    ASSERT_TRUE(r.rig.click("row-1"));  // a-cover.png
    r.rig.pump();
    EXPECT_EQ(r.rig.get("sel-label", "text"), "已选 1 项 · 1.17 KB");
    EXPECT_EQ(r.rig.get("share", "disabled"), "false");
    EXPECT_TRUE(r.rig.visible("clear"));

    ASSERT_TRUE(r.rig.click("clear"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("sel-label", "text"), "已选 0 项");
    EXPECT_EQ(r.rig.get("share", "disabled"), "true");
}

TEST(AppInteract, SharePickShowsTheEmptyPeerState) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();

    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    EXPECT_TRUE(r.on("screen-pick"));
    EXPECT_EQ(r.rig.get("pick-sum", "text"), "已选 1 项 · 1.17 KB");
    // No second instance is running in a unit test, so the picker shows its empty state.
    EXPECT_TRUE(r.rig.visible("pick-empty"));
    EXPECT_EQ(zui::test::count_class(r.rig.app, "peer-row"), 0);

    ASSERT_TRUE(r.rig.click("pick-back"));
    r.rig.pump();
    EXPECT_TRUE(r.on("screen-browse"));
}

TEST(AppInteract, ShareSettingsPersistIdentity) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();

    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("settings_screen")));

    ASSERT_TRUE(r.rig.app.document().set("set-name", "value", ""));
    ASSERT_TRUE(r.rig.click("save-name"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("app-error", "text"), oracle.str("empty_save_name_error"));

    ASSERT_TRUE(r.rig.type("set-name", oracle.str("saved_name")));
    ASSERT_TRUE(r.rig.click("save-name"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("me-name", "text"), oracle.str("saved_name"));

    std::ifstream in(r.home / "config.txt");
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(body.find("name=" + oracle.str("saved_name")), std::string::npos);

    ASSERT_TRUE(r.rig.click("use-cwd"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("set-dir", "value"), r.listing.string());

    // Escape is the page body's "on_accel" handler.
    zui::frame::Access::key(r.rig.app, "Escape");
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("browse_screen")));
}

/// A flat list of 1000 or more rows stamps a window of at most 32 `.frow` nodes. The first and
/// last visible names are the browser's, not a count of every file.
TEST(AppInteract, ShareLargeDirectoryShowsTheBrowserWindow) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    const std::string first = oracle.str("large_first");
    const std::string last = oracle.str("large_last");
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.listing = r.home / "big";
    std::error_code ec;
    fs::create_directories(r.listing, ec);
    for (int i = 0; i < 2000; ++i) {
        write_file(r.listing / ("f" + std::to_string(i) + ".txt"), 1);
    }
    ASSERT_TRUE(r.session->browser().open(r.listing.string()));
    r.session->render_files();
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(r.session->browser().entries().size(), 2000u);
    const int shown = zui::test::count_class(r.rig.app, "frow");
    EXPECT_GT(shown, 0);
    EXPECT_LE(shown, 32);
    const std::vector<std::string> head = r.texts("fname");
    ASSERT_FALSE(head.empty());
    EXPECT_EQ(head.front(), first);

    ASSERT_TRUE(r.rig.app.document().set("file-list", "scroll_top", "100000000"));
    ASSERT_TRUE(r.rig.pump());
    const std::vector<std::string> tail = r.texts("fname");
    ASSERT_FALSE(tail.empty());
    EXPECT_EQ(tail.back(), last);
    EXPECT_LE(zui::test::count_class(r.rig.app, "frow"), 32);
}

TEST(AppInteract, ShareEnterOnTheNameStarts) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.type("name-input", oracle.str("typed_me")));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("me-name", "text"), oracle.str("typed_me"));
    ASSERT_TRUE(r.rig.press("name-input", "Enter"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("started_screen")));
    EXPECT_EQ(r.rig.get("me-name", "text"), oracle.str("started_me"));
}

TEST(AppInteract, ShareRefreshPicksUpANewFile) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    const std::vector<std::string> expected =
        oracle.share.at("refresh_names").get<std::vector<std::string>>();
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.listing = r.home / "listing";
    std::error_code ec;
    fs::create_directories(r.listing, ec);
    for (const std::string& name : expected) {
        write_file(r.listing / name, 1);
    }
    ASSERT_TRUE(r.session->browser().open(r.listing.string()));
    r.session->render_files();
    ASSERT_TRUE(r.rig.pump());
    write_file(r.listing / "c-new.txt", 1);
    ASSERT_TRUE(r.rig.click("refresh"));
    r.rig.pump();
    const std::vector<std::string> names = r.texts("fname");
    ASSERT_GE(names.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(names[i], expected[i]);
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "c-new.txt"), names.end());
    EXPECT_TRUE(r.on(oracle.str("browse_screen")));
}

TEST(AppInteract, SharePathEditOpensThatDirectory) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    EXPECT_EQ(oracle.str("path_committed"), oracle.str("path_value"));
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    const fs::path other = r.home / "other";
    std::error_code ec;
    fs::create_directories(other, ec);
    write_file(other / "from-path.txt", 4);
    ASSERT_TRUE(r.rig.type("path", "q"));
    ASSERT_TRUE(r.rig.app.document().set("path", "value", other.string()));
    ASSERT_TRUE(r.rig.app.document().focus("refresh"));
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(r.rig.get("path", "value"), other.string());
    EXPECT_TRUE(r.on(oracle.str("path_screen")));
    const std::vector<std::string> names = r.texts("fname");
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "from-path.txt");
}

TEST(AppInteract, SharePeerSideDirectOfferWaits) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));  // width 1100 — sidebar card visible
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("peer-42"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("send_screen")));  // screen-wait
    EXPECT_NE(r.rig.get("wait-title", "text").find(staged_peer().name), std::string::npos);
}

TEST(AppInteract, SharePeerChipDirectOfferWaits) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true, 390, 720));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("chip-42"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("send_screen")));
}

TEST(AppInteract, SharePeerDirectOfferSetsRetryPeer) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("peer-42"));
    r.rig.pump();
    stage_phase(r, share::XferPhase::Rejected);
    EXPECT_TRUE(r.on("screen-fail"));
    EXPECT_TRUE(r.rig.visible("fail-retry"));
    ASSERT_TRUE(r.rig.click("fail-retry"));
    r.rig.pump();
    EXPECT_TRUE(r.on("screen-wait"));
}

TEST(AppInteract, SharePeerStripDoesNotOpenThePicker) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ASSERT_TRUE(oracle.share.at("peer_still").get<bool>());

    // Desktop, wider than the 760px breakpoint: the sidebar card is the on_peer control.
    // The chip strip is display:none, so a hit-test cannot press it.
    ShareRig wide;
    ASSERT_TRUE(wide.up(true));
    wide.stage_listing();
    stage_peer(wide);
    EXPECT_TRUE(wide.on(oracle.str("peer_screen")));
    EXPECT_TRUE(zui::test::any_text(wide.rig.app, "", "", staged_peer().name));
    ASSERT_TRUE(wide.rig.click("peer-42"));
    wide.rig.pump();
    EXPECT_TRUE(wide.on(oracle.str("peer_screen")));
    EXPECT_TRUE(zui::test::any_text(wide.rig.app, "", "", staged_peer().name));

    // Phone: the same handler lives on chip-42, and the sidebar card is display:none.
    ShareRig narrow;
    ASSERT_TRUE(narrow.up(true, 390, 720));
    narrow.stage_listing();
    stage_peer(narrow);
    EXPECT_TRUE(narrow.on(oracle.str("peer_screen")));
    EXPECT_TRUE(zui::test::any_text(narrow.rig.app, "", "", staged_peer().name));
    ASSERT_TRUE(narrow.rig.click("chip-42"));
    narrow.rig.pump();
    EXPECT_TRUE(narrow.on(oracle.str("peer_screen")));
    EXPECT_TRUE(zui::test::any_text(narrow.rig.app, "", "", staged_peer().name));
}

TEST(AppInteract, ShareSendToAPeerWaits) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("send_screen")));
    EXPECT_NE(r.rig.get("wait-title", "text").find(staged_peer().name), std::string::npos);
}

TEST(AppInteract, ShareWaitCancelReturnsToBrowse) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("wait-cancel"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("wait_cancel_screen")));
}

TEST(AppInteract, ShareIncomingAcceptStartsTheTransfer) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_offer(r);
    EXPECT_TRUE(r.rig.has_class("app", "incoming"));
    ASSERT_TRUE(r.rig.click("accept"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("accept_screen")));
    EXPECT_EQ(r.rig.has_class("app", "incoming"), oracle.share.at("accept_incoming").get<bool>());
}

TEST(AppInteract, ShareIncomingRejectHidesTheSheet) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_offer(r);
    EXPECT_TRUE(r.rig.has_class("app", "incoming"));
    ASSERT_TRUE(r.rig.click("reject"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("reject_screen")));
    EXPECT_EQ(r.rig.has_class("app", "incoming"), oracle.share.at("reject_incoming").get<bool>());
}

TEST(AppInteract, ShareIncomingScrimRejects) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_offer(r);
    EXPECT_TRUE(r.rig.has_class("app", "incoming"));
    // The sheet's scrim is the id-less control whose handler is "on_reject".
    ASSERT_TRUE(click_scrim(r));
    r.rig.pump();
    EXPECT_EQ(r.rig.has_class("app", "incoming"), oracle.share.at("scrim_incoming").get<bool>());
    EXPECT_TRUE(r.on(oracle.str("reject_screen")));
}

TEST(AppInteract, ShareTransferCancelReturnsToBrowse) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_phase(r, share::XferPhase::Transferring);
    EXPECT_TRUE(r.on("screen-transfer"));
    ASSERT_TRUE(r.rig.click("xfer-cancel"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("xfer_cancel_screen")));
}

TEST(AppInteract, ShareDoneReturnsToBrowse) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_phase(r, share::XferPhase::Done);
    EXPECT_TRUE(r.on("screen-done"));
    ASSERT_TRUE(r.rig.click("done-ok"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("done_screen")));
}

TEST(AppInteract, SharePathToFileUriWindowsStyle) {
#if defined(_WIN32)
    EXPECT_EQ(share::path_to_file_uri("C:\\Temp\\recv"), "file:///C:/Temp/recv");
#else
    EXPECT_EQ(share::path_to_file_uri("/tmp/recv"), "file:///tmp/recv");
#endif
}

TEST(AppInteract, ShareDoneOpenButtonExists) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_phase(r, share::XferPhase::Done);
    EXPECT_TRUE(r.on("screen-done"));
    EXPECT_NE(r.rig.get("done-open", "text").find("打开接收文件夹"), std::string::npos);
    ASSERT_TRUE(r.rig.click("done-open"));  // must not crash even if open_uri fails in test host
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("done-ok"));
    r.rig.pump();
}

TEST(AppInteract, ShareWaitShowsResumeHint) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    const std::string hint = r.rig.get("wait-hint", "text");
    EXPECT_NE(hint.find("断点续传"), std::string::npos);
}

TEST(AppInteract, ShareFailRetryReoffersSamePeer) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("send_screen")));  // wait
    stage_phase(r, share::XferPhase::Rejected);
    EXPECT_TRUE(r.on(oracle.str("fail_screen")));
    EXPECT_FALSE(r.rig.get("fail-retry", "text").empty());
    EXPECT_TRUE(r.rig.visible("fail-retry"));
    ASSERT_TRUE(r.rig.click("fail-retry"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("send_screen")));  // back to wait
}

TEST(AppInteract, ShareFailRetryHiddenWithoutSelection) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    // Stage fail without ever offering — no retry_peer_
    stage_phase(r, share::XferPhase::Error);
    EXPECT_TRUE(r.on("screen-fail"));
    EXPECT_FALSE(r.rig.visible("fail-retry"));
}

TEST(AppInteract, ShareFailRetryHiddenAfterDone) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    // Successful offer snapshots retry_peer_; Done must clear it on Browse return.
    stage_phase(r, share::XferPhase::Done);
    EXPECT_TRUE(r.on("screen-done"));
    ASSERT_TRUE(r.rig.click("done-ok"));
    r.rig.pump();
    EXPECT_TRUE(r.on("screen-browse"));
    // Later unrelated Fail (e.g. receive error) must not show retry.
    stage_phase(r, share::XferPhase::Error);
    EXPECT_TRUE(r.on("screen-fail"));
    EXPECT_FALSE(r.rig.visible("fail-retry"));
}

TEST(AppInteract, ShareFailRetryHiddenAfterXferCancelToBrowse) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    ASSERT_TRUE(r.rig.click("row-1"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.click("share"));
    r.rig.pump();
    stage_peer(r);
    ASSERT_TRUE(r.rig.click("send-42"));
    r.rig.pump();
    stage_phase(r, share::XferPhase::Transferring);
    EXPECT_TRUE(r.on("screen-transfer"));
    // Make cancel take the Idle Browse exit without poll auto-navigating first.
    {
        share::XferView view;
        view.phase = share::XferPhase::Idle;
        share::net_stage_xfer(view);
    }
    ASSERT_TRUE(r.rig.click("xfer-cancel"));
    r.rig.pump();
    EXPECT_TRUE(r.on("screen-browse"));
    stage_phase(r, share::XferPhase::Error);
    EXPECT_TRUE(r.on("screen-fail"));
    EXPECT_FALSE(r.rig.visible("fail-retry"));
}

TEST(AppInteract, ShareFailBackReturnsToBrowse) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_phase(r, share::XferPhase::Error);
    EXPECT_TRUE(r.on(oracle.str("fail_screen")));
    ASSERT_TRUE(r.rig.click("fail-back"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("fail_back_screen")));
}

TEST(AppInteract, ShareSaveDirectoryPersists) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    EXPECT_EQ(oracle.str("save_dir_input"), oracle.str("save_dir_field"));
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.app.document().set("set-dir", "value", ""));
    ASSERT_TRUE(r.rig.click("save-dir"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("app-error", "text"), oracle.str("empty_dir_error"));

    const fs::path saved = r.home / "saved";
    ASSERT_TRUE(r.rig.app.document().set("set-dir", "value", saved.string()));
    ASSERT_TRUE(r.rig.click("save-dir"));
    r.rig.pump();
    EXPECT_EQ(r.rig.get("set-dir", "value"), saved.string());
    EXPECT_EQ(r.rig.get("app-error", "text"), oracle.str("save_dir_error"));
    EXPECT_TRUE(fs::is_directory(saved));
    std::ifstream in(r.home / "config.txt");
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(body.find("save_dir=" + saved.string()), std::string::npos);
}

TEST(AppInteract, ShareApplySaveDirPersistsResolvedPath) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    const auto saved = r.home / "picked";
    ASSERT_TRUE(r.rig.app.document().set("set-dir", "value", saved.string()));
    ASSERT_TRUE(r.rig.click("save-dir"));  // goes through apply_save_dir after refactor
    r.rig.pump();
    EXPECT_EQ(r.rig.get("set-dir", "value"), saved.string());
    std::ifstream in(r.home / "config.txt");
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(body.find("save_dir=" + saved.string()), std::string::npos);
}

TEST(AppInteract, ShareApplySaveDirRejectsContentUriWithoutPersist) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    const std::string before = r.session->config().save_dir;
    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    ASSERT_TRUE(r.rig.app.document().set("set-dir", "value",
        "content://com.example.other/tree/abc"));
    ASSERT_TRUE(r.rig.click("save-dir"));
    r.rig.pump();
    EXPECT_EQ(r.session->config().save_dir, before);
    EXPECT_NE(r.rig.get("app-error", "text").find("无法使用"), std::string::npos);
}


TEST(AppInteract, ShareTryResolveRejectsContentUri) {
    // Primary document id may map to a real /storage/... path; never return the raw URI.
    const std::string primary =
        share::try_resolve_save_dir("content://com.android.externalstorage.documents/tree/primary%3ADownload");
    EXPECT_TRUE(primary.empty() || primary.rfind("/storage/", 0) == 0);
    EXPECT_NE(primary.rfind("content://", 0), 0u);
    // Unresolvable / foreign authority must be empty:
    EXPECT_TRUE(share::try_resolve_save_dir("content://com.example.other/tree/abc").empty());
}

TEST(AppInteract, ShareTryResolveAcceptsPrimaryAbsolute) {
#if defined(__ANDROID__)
    const std::string p = "/storage/emulated/0/Download/zui_share_test_recv";
    const std::string got = share::try_resolve_save_dir(p);
    EXPECT_FALSE(got.empty());
    EXPECT_NE(got.rfind("content://", 0), 0u);
#else
    // Desktop: any creatable temp path
    auto dir = zui::test::scratch_dir("share-resolve") / "recv";
    const std::string got = share::try_resolve_save_dir(dir.string());
    EXPECT_EQ(got, dir.string());
    EXPECT_TRUE(std::filesystem::is_directory(dir));
#endif
}

TEST(AppInteract, ShareLoadConfigKeepsSavedDir) {
    ShareRig r;
    r.home = zui::test::scratch_dir("share-handlers");
    std::error_code ec;
    std::filesystem::remove_all(r.home, ec);
    std::filesystem::create_directories(r.home / "kept", ec);
    share::set_config_dir(r.home.string());
    {
        std::ofstream out(r.home / "config.txt");
        out << "id=0000000000000001\nname=\u8bbe\u5907\nsave_dir=" << (r.home / "kept").string() << "\n";
    }
    // Non-empty saved dir must survive load on this desktop host (Android overwrite bug removed).
    auto c = share::load_config();
    EXPECT_EQ(c.save_dir, (r.home / "kept").string());
}

TEST(AppInteract, ShareLoadConfigDropsContentUri) {
    ShareRig r;
    r.home = zui::test::scratch_dir("share-handlers");
    std::error_code ec;
    std::filesystem::remove_all(r.home, ec);
    std::filesystem::create_directories(r.home, ec);
    share::set_config_dir(r.home.string());
    {
        std::ofstream out(r.home / "config.txt");
        out << "id=0000000000000001\nname=\u8bbe\u5907\n"
            << "save_dir=content://com.example.other/tree/abc\n";
    }
    auto c = share::load_config();
    EXPECT_NE(c.save_dir.rfind("content://", 0), 0u);
    EXPECT_FALSE(c.save_dir.empty());
}

TEST(AppInteract, ShareSettingsBackReturnsToBrowse) {
    ShareOracle oracle;
    ASSERT_TRUE(oracle.open()) << oracle.error;
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("settings_screen")));
    ASSERT_TRUE(r.rig.click("settings-back"));
    r.rig.pump();
    EXPECT_TRUE(r.on(oracle.str("settings_back_screen")));
}

TEST(AppInteract, ShareStoragePermissionStatusDesktop) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    ASSERT_TRUE(r.rig.click("settings"));
    r.rig.pump();
    // Desktop: status node exists and request button is hidden (Unsupported).
    EXPECT_FALSE(r.rig.get("storage-perm-status", "text").empty());
    EXPECT_EQ(r.rig.get("storage-perm-status", "text"), "当前平台无需额外存储权限");
    EXPECT_EQ(r.rig.get("request-storage", "visible"), "false");
}


TEST(AppInteract, ShareRefreshStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ShareRig r;
    ASSERT_TRUE(r.up_headless(true));
    r.stage_listing();
    // A new file on disk, so the measured click is a refresh that restamps the list.
    write_file(r.listing / "c-new.txt", 8);
    const zui::perf::FrameWork work = r.rig.measure([&] {
        ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "refresh"));
    });
    const std::vector<std::string> names = r.texts("fname");
    ASSERT_NE(std::find(names.begin(), names.end(), "c-new.txt"), names.end());
    // Measured 46 / 49 / 13 / 100, then boxes=56 on the next run: refresh re-reads the
    // directory and restamps the new row. Box reuse moves the create count. A frame that
    // cascades the whole shell, or stamps every screen, blows these up.
    EXPECT_LE(work.cascade_visited, 128u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 160u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.boxes_created, 128u) << "boxes=" << work.boxes_created;
    EXPECT_LE(work.flow_remeasures, 320u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, ShareIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    // show() polls the network every 200ms and that tick restamps the peer strip. Stop it so the
    // idle sample is a frame with no tick, which is the same quiet frame the other sessions measure.
    r.session->stop();
    ASSERT_TRUE(r.rig.pump());
    ASSERT_TRUE(r.rig.pump());
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.painted_walked, 0u);
    ASSERT_TRUE(r.rig.pump());
    const zui::perf::FrameWork third = zui::perf::last_frame_work();
    EXPECT_EQ(third.cascade_visited, 0u);
    EXPECT_EQ(third.laid_out, 0u);
    EXPECT_EQ(third.painted_walked, 0u);
}

TEST(AppInteract, ShareIdleResubmitsTheLastList) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    r.stage_listing();
    r.session->stop();
    ASSERT_FALSE(r.rig.renderer().submit_sizes.empty());
    const std::size_t first = r.rig.renderer().submit_sizes.back();
    const int present = r.rig.renderer().present_count;
    ASSERT_GT(first, 0u);
    ASSERT_TRUE(r.rig.pump());
    EXPECT_GT(r.rig.renderer().present_count, present);
    EXPECT_EQ(r.rig.renderer().submit_sizes.back(), first);
}

TEST(AppInteract, ShareGestureTouchPullDownRefresh) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Touch, 100.f, 100.f, /*incoming=*/false, /*files=*/true);
    EXPECT_EQ(g.up_for_test(110.f, 200.f), share::GestureAction::Refresh);
}

TEST(AppInteract, ShareGestureMouseDoesNotRefresh) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Mouse, 100.f, 100.f, /*incoming=*/false, /*files=*/true);
    EXPECT_EQ(g.up_for_test(110.f, 200.f), share::GestureAction::None);
}

TEST(AppInteract, ShareGesturePenDoesNotRefresh) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Pen, 100.f, 100.f, /*incoming=*/false, /*files=*/true);
    EXPECT_EQ(g.up_for_test(110.f, 200.f), share::GestureAction::None);
}

TEST(AppInteract, ShareGestureTouchSwipeRightBack) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Touch, 100.f, 100.f, /*incoming=*/false, /*files=*/false);
    EXPECT_EQ(g.up_for_test(220.f, 110.f), share::GestureAction::Back);
}

TEST(AppInteract, ShareGestureTouchPullDownRejectOnIncoming) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Touch, 100.f, 100.f, /*incoming=*/true, /*files=*/false);
    EXPECT_EQ(g.up_for_test(110.f, 200.f), share::GestureAction::Reject);
}

TEST(AppInteract, ShareGestureTouchPullDownNotRejectOffIncoming) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Touch, 100.f, 100.f, /*incoming=*/false, /*files=*/false);
    EXPECT_EQ(g.up_for_test(110.f, 200.f), share::GestureAction::None);
}

TEST(AppInteract, ShareGestureMouseSwipeRightDoesNotBack) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Mouse, 100.f, 100.f, /*incoming=*/false, /*files=*/false);
    EXPECT_EQ(g.up_for_test(220.f, 110.f), share::GestureAction::None);
}

TEST(AppInteract, ShareGestureShortPullDoesNotRefresh) {
    share::GestureTracker g;
    g.down_for_test(zui::PointerType::Touch, 100.f, 100.f, /*incoming=*/false, /*files=*/true);
    EXPECT_EQ(g.up_for_test(105.f, 140.f), share::GestureAction::None);
}

TEST(AppInteract, ShareNotifyIncomingBody) {
    share::IncomingOffer o;
    o.from_name = "客厅平板";
    o.nfiles = 2;
    o.nbytes = 1200;
    const std::string body = share::notify_incoming_body(o);
    EXPECT_NE(body.find("客厅平板"), std::string::npos);
    EXPECT_NE(body.find("2 个文件"), std::string::npos);
    EXPECT_NE(body.find(share::format_bytes(1200)), std::string::npos);
}

TEST(AppInteract, ShareNotifyIncomingBodySingleFile) {
    share::IncomingOffer o;
    o.from_name = "客厅平板";
    o.nfiles = 1;
    o.nbytes = 512;
    const std::string body = share::notify_incoming_body(o);
    EXPECT_NE(body.find("1 个文件"), std::string::npos);
}

TEST(AppInteract, ShareNotifyDoneBody) {
    share::XferView v;
    v.phase = share::XferPhase::Done;
    v.sending = true;
    v.peer_name = "书房电脑";
    v.total = 2048;
    EXPECT_EQ(share::notify_done_body(v),
              std::string("发送完成：与 书房电脑，共 ") + share::format_bytes(2048));
    v.sending = false;
    EXPECT_EQ(share::notify_done_body(v),
              std::string("接收完成：与 书房电脑，共 ") + share::format_bytes(2048));
}

TEST(AppInteract, ShareNotifyFailRejectedBody) {
    share::XferView v;
    v.phase = share::XferPhase::Rejected;
    v.peer_name = "书房电脑";
    EXPECT_EQ(share::notify_fail_body(v), "书房电脑 拒绝了此次发送");
}

TEST(AppInteract, ShareNotifyFailErrorBody) {
    share::XferView v;
    v.phase = share::XferPhase::Error;
    v.peer_name = "书房电脑";
    v.error.clear();
    EXPECT_EQ(share::notify_fail_body(v), "与 书房电脑 的传输失败");
    v.error = "连不上对方";
    EXPECT_EQ(share::notify_fail_body(v), "与 书房电脑 的传输失败：连不上对方");
    v.error = std::string(100, 'x');
    const std::string body = share::notify_fail_body(v);
    EXPECT_EQ(body.find("与 书房电脑 的传输失败："), 0u);
    EXPECT_LE(body.size() - std::string("与 书房电脑 的传输失败：").size(), 80u);
}

TEST(AppInteract, ShareIncomingOfferDoesNotCrashWithoutNotifyPlugin) {
    ShareRig r;
    ASSERT_TRUE(r.up(true));
    stage_offer(r);
    EXPECT_TRUE(r.rig.has_class("app", "incoming"));
    // Same offer_id must not re-notify on a second poll (dedupe); UI stays up.
    r.session->poll();
    r.rig.pump();
    EXPECT_TRUE(r.rig.has_class("app", "incoming"));
}

TEST(AppInteract, ShareDoneAndFailPhaseDoNotCrashWithoutNotifyPlugin) {
    {
        ShareRig r;
        ASSERT_TRUE(r.up(true));
        stage_phase(r, share::XferPhase::Done);
        EXPECT_TRUE(r.on("screen-done"));
    }
    {
        ShareRig r;
        ASSERT_TRUE(r.up(true));
        stage_phase(r, share::XferPhase::Error);
        EXPECT_TRUE(r.on("screen-fail"));
    }
    {
        ShareRig r;
        ASSERT_TRUE(r.up(true));
        stage_phase(r, share::XferPhase::Rejected);
        EXPECT_TRUE(r.on("screen-fail"));
    }
}
