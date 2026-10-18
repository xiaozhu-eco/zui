// Every handler chat/cpp/session.cpp registers, driven through the control that declares it. The
// assertions name the user-visible result of that one handler (docs/TEST_ARCHITECTURE.md 五个判决).

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "../../example/applications/chat/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path chat_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "chat" / "index.html";
}

fs::path scratch() {
    const fs::path dir = zui::test::scratch_dir("chat-handlers");
    fs::remove_all(dir);
    return dir;
}

struct ChatRig {
    zui::test::HandlerRig rig;
    chat::Session session;

    /// Load the real page, bind the real actions, show the real session, then clear the search box.
    /// The demo keeps its query in a file-scope global that `show()` does not reset, so a test that
    /// did not clear it would start from the previous test's filter.
    bool up() {
        session.app = &rig.app;
        session.dir = scratch();
        if (!rig.app.document().load(chat_html().string())) {
            return false;
        }
        chat::bind_actions(session);
        chat::show(session);
        if (!rig.windowed()) {
            return false;
        }
        if (!rig.pump() || !rig.pump()) {
            return false;
        }
        if (!rig.type("q", " ")) {  // trim(" ") -> g_query = ""
            return false;
        }
        // `g_active` leaks the same way: make the first conversation current so every test starts
        // from a known thread, whatever the previous test left selected.
        return rig.click_text("conv", "林晚");
    }
};

int convos(const ChatRig& r) { return zui::test::count_class(r.rig.app, "conv"); }

}  // namespace

TEST(AppInteract, ChatQueryWithoutAMatchShowsTheEmptyNote) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(convos(r), 4) << "the seed has four conversations";

    ASSERT_TRUE(r.rig.type("q", "zzzz"));

    EXPECT_EQ(r.rig.get("conv-empty", "visible"), "true");
    EXPECT_NE(r.rig.get("conv-empty", "text").find("没有符合"), std::string::npos)
        << r.rig.get("conv-empty", "text");
    EXPECT_EQ(convos(r), 0);
}

TEST(AppInteract, ChatQueryKeepsTheMatchingConversation) {
    ChatRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.type("q", "渡口"));

    EXPECT_EQ(convos(r), 1) << "only 渡口书局 matches";
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "conv", "渡口书局"));
}

TEST(AppInteract, ChatConversationClickSwitchesTheThread) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(r.rig.get("thread-name", "text"), "林晚");

    ASSERT_TRUE(r.rig.click_text("conv", "渡口书局"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("thread-name", "text"), "渡口书局");
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "conv", "渡口书局"));
}

TEST(AppInteract, ChatCreateOpensTheCreatorDialog) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(r.rig.get("creator", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("creator", "visible"), "true");
    EXPECT_EQ(r.rig.get("creator-name", "value"), "") << "the field starts empty";
}

TEST(AppInteract, ChatCreatorCancelHidesTheDialog) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("creator", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "creator-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("creator", "visible"), "false");
}

TEST(AppInteract, ChatCreatorSaveAddsAndSelectsTheConversation) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("creator-name", "value", "校本组"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("创建"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("creator", "visible"), "false");
    EXPECT_EQ(convos(r), 5) << "the new conversation joins the four seeded ones";
    EXPECT_EQ(r.rig.get("thread-name", "text"), "校本组") << "the new conversation becomes current";
}

TEST(AppInteract, ChatCreatorSaveRejectsAnEmptyName) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("创建"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("creator-error", "text"), "请填写名字");
    EXPECT_EQ(r.rig.get("creator", "visible"), "true") << "the dialog stays open";
    EXPECT_EQ(convos(r), 4);
}

TEST(AppInteract, ChatSendAppendsTheDraft) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.app.document().set("draft", "value", "校对稿已收到"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("发送"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("draft", "value"), "") << "send clears the composer";
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "bubble", "校对稿已收到"))
        << "the message must be on screen";
}

TEST(AppInteract, ChatDraftEnterSendsTheMessage) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.app.document().set("draft", "value", "回车发送"));
    ASSERT_TRUE(r.rig.app.document().focus("draft"));
    r.rig.observe();

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Enter"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("draft", "value"), "");
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "bubble", "回车发送"));
}

/// The composer is one long soft-wrapped line (rows=1, no `\n`), which is where the caret keys
/// and the click used to give up: the value had no newline, the run table was skipped, and both
/// the arrows and the click fell back to "the whole value is one line". ArrowDown did nothing,
/// and a click under the wrap jumped back to the first line.
TEST(AppInteract, ChatComposerCaretWalksTheWrappedLines) {
    ChatRig r;
    ASSERT_TRUE(r.up());

    // Long enough to soft-wrap in the composer two rows tall.
    std::string draft;
    for (int i = 0; i < 20; ++i) {
        draft += "word" + std::to_string(i) + " ";
    }
    ASSERT_TRUE(r.rig.app.document().set("draft", "value", draft));
    ASSERT_TRUE(r.rig.app.document().focus("draft"));
    ASSERT_TRUE(r.rig.pump());
    r.rig.app.document().set("draft", "select", "0,0");
    ASSERT_TRUE(r.rig.pump());
    const std::optional<zui::ElementBox> box = r.rig.app.document().measure("draft");
    ASSERT_TRUE(box && box->laid);

    // The caret bar the frame paints for the focused control: the only 1px column inside the
    // composer's box.
    const auto caret_bar = [&](float& out_x, float& out_y) {
        for (const zui::paint::DisplayItem& item : r.rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::FillRect || item.rect.w > 1.01f ||
                item.rect.h < 8.f || item.color.a != 255) {
                continue;
            }
            if (item.rect.x < box->border.x || item.rect.x > box->border.x + box->border.width ||
                item.rect.y < box->border.y || item.rect.y > box->border.y + box->border.height) {
                continue;
            }
            out_x = item.rect.x;
            out_y = item.rect.y;
            return true;
        }
        return false;
    };
    float bar_x = 0.f;
    float bar_y = 0.f;
    ASSERT_TRUE(caret_bar(bar_x, bar_y)) << "no caret bar painted for the composer";
    EXPECT_GE(bar_y, box->border.y);
    EXPECT_LE(bar_y, box->border.y + box->border.height - 8.f);

    // ArrowDown walks to the second visual line: its first byte, the value's soft wrap. The bar
    // follows the caret it belongs to - it drops a line and its x returns to that line's start.
    // Measuring the whole value as one line put it on the first line at the width of the line
    // above, past the right edge of the composer.
    zui::backends::TextInputFrame down;
    down.down = 1;
    r.rig.window().inject(std::move(down));
    ASSERT_TRUE(r.rig.pump());
    ASSERT_TRUE(r.rig.pump());
    const std::size_t wrap = std::stoul(r.rig.get("draft", "caret"));
    EXPECT_GT(wrap, 0u) << "ArrowDown did not move the caret";
    EXPECT_LT(wrap, draft.size()) << "ArrowDown jumped past the last line";
    float wrap_x = 0.f;
    float wrap_y = 0.f;
    ASSERT_TRUE(caret_bar(wrap_x, wrap_y));
    EXPECT_GT(wrap_y, bar_y + 8.f) << "the caret bar stayed on the first line";
    EXPECT_LT(wrap_x, bar_x + 100.f) << "the caret bar carries the line above's width";

    // A click inside the second visual line lands inside it (this used to read the whole value as
    // one line and answer the value's first line).
    ASSERT_TRUE(r.rig.at(box->border.x + 6.f, box->border.y + box->border.height * 0.7f));
    ASSERT_TRUE(r.rig.pump());
    const std::size_t clicked = std::stoul(r.rig.get("draft", "caret"));
    EXPECT_GE(clicked, wrap) << clicked << " vs wrap " << wrap;
    EXPECT_LE(clicked, wrap + 24u) << clicked << " vs wrap " << wrap;
    float click_bar_x = 0.f;
    float click_bar_y = 0.f;
    ASSERT_TRUE(caret_bar(click_bar_x, click_bar_y));
    EXPECT_GT(click_bar_y, bar_y + 8.f) << "the caret bar of the click stayed on the first line";
    EXPECT_LT(click_bar_x, bar_x + 40.f) << "the caret bar of the click carries stale width";

    // Home and End stay inside that line.
    zui::backends::TextInputFrame home;
    home.home = 1;
    r.rig.window().inject(std::move(home));
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(std::stoul(r.rig.get("draft", "caret")), wrap) << "Home left the line";
    zui::backends::TextInputFrame to_end;
    to_end.end = 1;
    r.rig.window().inject(std::move(to_end));
    ASSERT_TRUE(r.rig.pump());
    EXPECT_EQ(std::stoul(r.rig.get("draft", "caret")), draft.size()) << "End left the line";
}

TEST(AppInteract, ChatRemoveAsksThenDeletesTheConversation) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    const int before = convos(r);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "remove"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除和「"), std::string::npos)
        << r.rig.get("confirm-body", "text");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(convos(r), before - 1);
}

TEST(AppInteract, ChatRestoreAsksThenReseeds) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    // Get off the seed first, so "restore" has something to restore.
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "remove"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();
    ASSERT_EQ(convos(r), 3);

    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("confirm-body", "text").find("替换"), std::string::npos)
        << r.rig.get("confirm-body", "text");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(convos(r), 4) << "the sample conversations are back";
}

TEST(AppInteract, ChatConfirmCancelKeepsTheData) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    const int before = convos(r);
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(convos(r), before) << "cancel changes nothing";
}

TEST(AppInteract, ChatAccelEscapeClosesTheOpenDialog) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("creator", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("creator", "visible"), "false");
}

TEST(AppInteract, ChatScrollingAwayFromTheBottomShowsJump) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(r.rig.get("jump", "visible"), "false") << "the thread opens pinned to the bottom";

    // The first scroll records the bottom baseline; the second is what reads as "away from it".
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("jump", "visible"), "true") << "scrolling up shows 回到底部";
}

// view.js `query` input calls `renderList()` only. The log stays where the reader left it.
// README: 往上翻时停在原处，自己发送或点「回到底部」才贴回最新一条。
TEST(AppInteract, ChatQueryLeavesTheScrolledLogInPlace) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("jump", "visible"), "true");
    const std::string top = r.rig.get("log", "scroll_top");

    ASSERT_TRUE(r.rig.type("q", "林"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("jump", "visible"), "true");
    EXPECT_EQ(r.rig.get("log", "scroll_top"), top);
    EXPECT_EQ(r.rig.get("thread-name", "text"), "林晚");
}

TEST(AppInteract, ChatJumpReturnsToTheBottom) {
    ChatRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::wheel_id(r.rig.app, "log", -150.f));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("jump", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "jump"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("jump", "visible"), "false");
    EXPECT_GT(std::stof(r.rig.get("log", "scroll_top")), 0.f);
}

TEST(AppInteract, ChatSwitchThreadStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ChatRig r;
    ASSERT_TRUE(r.up());
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)r.rig.seam_click_text("conv", "渡口书局"); });
    // Measured 60 / 150 / 575 / 68 at the gate's introduction: switching thread rebuilds the log
    // and its bubbles, leaving the conversation list alone.
    EXPECT_LE(work.cascade_visited, 128u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 256u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 1000u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 144u) << "boxes=" << work.boxes_created;
}
