// zmd as a daily editor: launch, editing, files, and chrome, each driven by the
// controls the page ships (clicks and keys), not by calling session methods.

#include "handler_rig.h"

#include "backends/platform.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../../example/applications/zmd/cpp/session.h"

namespace {

namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void set_env(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value == nullptr ? "" : value);
#else
    if (value == nullptr || value[0] == '\0') {
        unsetenv(key);
    } else {
        setenv(key, value, 1);
    }
#endif
}

/// Headless hosts report Unsupported for the save dialog, so export writes the
/// sibling file. A win32 host would open a real dialog and stall the test.
struct NoopPlatform {
    std::string previous;
    bool had = false;

    NoopPlatform() {
        const char* cur = std::getenv("ZUI_PLATFORM");
        if (cur != nullptr) {
            had = true;
            previous = cur;
        }
        set_env("ZUI_PLATFORM", "noop");
    }

    ~NoopPlatform() { set_env("ZUI_PLATFORM", had ? previous.c_str() : ""); }
};

/// Gives one case a private host profile. zmd pins its own app id, so the shared
/// `%TEMP%/zui-test-data/zmd` directory carried `settings.txt` - and the `notes/` workspace -
/// from one case into the next: the width, theme, focus, and autosave switch one case saved were
/// the next case's starting state, which showed up as order-dependent reds. The noop shell moves
/// both roots under `ZUI_TEST_DATA_DIR`, which is this case's own scratch directory.
struct SettingsGuard {
    fs::path dir;
    std::string previous_root;
    bool had_root = false;

    explicit SettingsGuard(zui::App& app) {
        (void)app;
        const char* current = std::getenv("ZUI_TEST_DATA_DIR");
        if (current != nullptr) {
            had_root = true;
            previous_root = current;
        }
        dir = zui::test::scratch_dir("zmd-daily");
        std::error_code ec;
        fs::create_directories(dir, ec);
        set_env("ZUI_TEST_DATA_DIR", dir.u8string().c_str());
    }

    ~SettingsGuard() {
        set_env("ZUI_TEST_DATA_DIR", had_root ? previous_root.c_str() : "");
        if (dir.empty()) {
            return;
        }
        // Only the scratch directory this case made is ours to delete.
        if (dir.filename().string().rfind("zui-zmd-daily-", 0) != 0) {
            return;
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

fs::path zmd_argv0() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zmd" / "zui_zmd.exe";
}

struct Daily {
    NoopPlatform platform;
    zui::test::HandlerRig rig;
    SettingsGuard guard;
    std::string arg0;
    std::string arg1;
    std::unique_ptr<zmd::Session> session;

    Daily() : guard(rig.app) {}

    /// Loads the page. `attach` refuses a window until a document exists, so the
    /// first call has to `start` before `windowed`.
    bool begin(const fs::path& file = {}) {
        rig.pump();
        session.reset();
        arg0 = zmd_argv0().string();
        arg1 = file.empty() ? std::string() : file.u8string();
        char* argv[2] = {arg0.data(), arg1.empty() ? nullptr : arg1.data()};
        const int argc = arg1.empty() ? 1 : 2;
        session = std::make_unique<zmd::Session>(rig.app);
        return session->start(argc, argv);
    }

    bool boot(int width, int height, const fs::path& file = {}) {
        if (!begin(file) || !rig.windowed(width, height)) {
            return false;
        }
        return rig.pump();
    }

    bool reopen(const fs::path& file = {}) {
        if (!begin(file)) {
            return false;
        }
        return rig.pump();
    }

    bool resize(int width, int height) {
        rig.app.window().set_window_size(width, height);
        rig.window().set_window_size(width, height);
        return rig.pump() && rig.pump();
    }

    std::string get(std::string_view id, std::string_view field) {
        return rig.app.document().get(std::string(id), std::string(field));
    }

    bool laid(std::string_view id) { return rig.laid(id); }

    std::optional<zui::ElementBox> box(std::string_view id) { return rig.app.document().measure(id); }
};

std::optional<zui::TextCaret> paper_bar(Daily& d) {
    return d.rig.app.document().text_caret_rect();
}

/// Replaces a field's whole value the way a user does: focus, select all, type.
bool replace_field(Daily& d, std::string_view id, std::string_view text) {
    if (!d.rig.app.document().focus(std::string(id)) || !d.rig.pump()) {
        return false;
    }
    const std::string current = d.get(id, "value");
    if (!current.empty()) {
        d.rig.app.document().set(std::string(id), "select", "0," + std::to_string(current.size()));
    }
    zui::backends::TextInputFrame frame;
    frame.text.assign(text.begin(), text.end());
    d.rig.window().inject(std::move(frame));
    return d.rig.pump() && d.rig.pump();
}

bool replace_source(Daily& d, std::string_view text) {
    // oninput defers preview restamp; a second pump finishes it before the
    // next click can focus the source in the same turn as load_fragment.
    return replace_field(d, "mdc-a-source", text);
}

bool click_edge(Daily& d, std::string_view id, bool right) {
    const std::optional<zui::ElementBox> box = d.box(id);
    if (!box || !box->laid || box->border.width < 4.f) {
        return false;
    }
    const float x = right ? box->border.x + box->border.width - 1.f : box->border.x + 1.f;
    return d.rig.at(x, box->border.y + box->border.height * 0.5f);
}

bool click_corner(Daily& d, std::string_view id) {
    const std::optional<zui::ElementBox> box = d.box(id);
    if (!box || !box->laid) {
        return false;
    }
    return d.rig.at(box->border.x + 8.f, box->border.y + 8.f);
}

/// The format toolbar is `pointer-events: none` until the title bar is hovered,
/// which is the prototype's reveal. Point at the document name, then click.
bool reveal_tools(Daily& d) {
    const std::optional<zui::ElementBox> name = d.box("mdc-a-name");
    if (!name || !name->laid) {
        return false;
    }
    return d.rig.point(name->border.x + name->border.width * 0.5f,
                       name->border.y + name->border.height * 0.5f);
}

/// True once two frames in a row measure the same laid box. Hover is flushed a frame late and the
/// panel the hover starts is a transition: a click aimed at a box measured while it still slides
/// lands where the button was a frame ago, which is how the folder button was missed before.
bool settled(Daily& d, std::string_view id) {
    std::optional<zui::ElementBox> last = d.box(id);
    if (!last || !last->laid) {
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!d.rig.pump()) {
            return false;
        }
        const std::optional<zui::ElementBox> now = d.box(id);
        if (!now || !now->laid) {
            return false;
        }
        if (now->border.x == last->border.x && now->border.y == last->border.y &&
            now->border.width == last->border.width && now->border.height == last->border.height) {
            return true;
        }
        last = now;
    }
    return false;
}
/// True when the hit chain names `id` as a whole token: `#zmd-link` matches a chain that carries
/// `button#zmd-link`, never `button#zmd-link-cancel`.
bool chain_has(const std::string& chain, std::string_view id) {
    const std::string needle = "#" + std::string(id);
    for (std::size_t at = chain.find(needle); at != std::string::npos;
         at = chain.find(needle, at + 1)) {
        const std::size_t end = at + needle.size();
        if (end == chain.size() || chain[end] == ' ') {
            return true;
        }
    }
    return false;
}
bool click_tool(Daily& d, std::string_view id) {
    if (!reveal_tools(d)) {
        return false;
    }
    // Hover is flushed a frame late, and the reveal slides `.mdw-tools` through a paint transform
    // the measure seam cannot see: the laid rect stands still while the box on screen moves. The
    // press and the release run one frame apart, so aim only once the hit test - the answer the
    // press consults - finds the button at its laid center on two frames in a row. A click aimed
    // before that lands while the panel is still coming to rest, the press and the release hit
    // different nodes, and the UI Events click is cancelled at their common ancestor.
    std::string previous;
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!d.rig.pump()) {
            return false;
        }
        const std::optional<zui::ElementBox> box = d.box(id);
        if (!box || !box->laid) {
            return false;
        }
        const std::string chain =
            zui::frame::Access::hit_chain(d.rig.app, box->border.x + box->border.width * 0.5f,
                                          box->border.y + box->border.height * 0.5f);
        if (chain_has(chain, id) && chain == previous) {
            return d.rig.click(id);
        }
        previous = chain;
    }
    return false;
}

/// Clicks `id` once the hit test answers with it on two frames in a row. Any panel still sliding
/// through a transition moves the box between the press and the release, and UI Events cancels
/// that pair at the two targets' common ancestor - the answer the rig waits for is the answer the
/// press will get.
bool click_settled(Daily& d, std::string_view id) {
    std::string previous;
    for (int i = 0; i < 40; ++i) {
        const std::optional<zui::ElementBox> box = d.box(id);
        if (!box || !box->laid) {
            return false;
        }
        const std::string chain =
            zui::frame::Access::hit_chain(d.rig.app, box->border.x + box->border.width * 0.5f,
                                          box->border.y + box->border.height * 0.5f);
        if (chain_has(chain, id) && chain == previous) {
            return d.rig.click(id);
        }
        previous = chain;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!d.rig.pump()) {
            return false;
        }
    }
    return false;
}

template <typename Pred>
bool until(Daily& d, Pred pred) {
    if (pred()) {
        return true;
    }
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!d.rig.pump()) {
            return false;
        }
        if (pred()) {
            return true;
        }
    }
    return false;
}


/// The file panel rests off the left edge until the pointer is on its strip.
bool reveal_files(Daily& d) {
    const std::optional<zui::ElementBox> hot = d.box("zmd-hot-l");
    if (!hot || !hot->laid) {
        return false;
    }
    if (!d.rig.point(hot->border.x + 4.f, hot->border.y + hot->border.height * 0.5f)) {
        return false;
    }
    const bool open = until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        return floated && floated->border.x > -1.f && floated->border.x < 300.f;
    });
    return open && settled(d, "zmd-float-l");
}

int find_row(Daily& d, const char* prefix, std::string_view name) {
    for (int i = 0; i < 40; ++i) {
        const std::string id = std::string(prefix) + std::to_string(i);
        if (d.get(id, "text") == name) {
            return i;
        }
        if (!d.laid(id) && d.get(id, "text").empty()) {
            break;
        }
    }
    return -1;
}

std::string width_token(std::string_view classes) {
    for (int w = 520; w <= 760; w += 10) {
        const std::string token = "w" + std::to_string(w);
        if (zui::test::class_token(classes, token)) {
            return token;
        }
    }
    return {};
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

void shift_right(Daily& d) {
    zui::backends::TextInputFrame step;
    step.right = 1;
    step.shift = true;
    zui::backends::KeyPress press;
    press.key = "ArrowRight";
    press.shift = true;
    step.key_events.push_back(std::move(press));
    d.rig.window().inject(std::move(step));
}

const char* kFixture =
    "# \xe6\x97\xa5\xe8\xaf\xbe\xe6\xa0\x87\xe9\xa2\x98\n"
    "\n"
    "\xe6\xad\xa3\xe6\x96\x87 **bold** \xe4\xb8\x8e *\xe6\x96\x9c* \xe5\x8f\x8a `code`\xe3\x80\x82\n"
    "\n"
    "- \xe4\xb8\x80\xe9\xa1\xb9\n"
    "\n"
    "[\xe7\xa4\xba\xe4\xbe\x8b](https://example.com)\n"
    "\n"
    "| \xe5\x88\x97 | \xe5\x80\xbc |\n"
    "| --- | --- |\n"
    "| \xe7\x94\xb2 | \xe4\xb9\x99 |\n"
    "\n"
    "alpha word alpha\n";

}  // namespace

TEST(AppInteract, ZmdLaunchShowsTheWritingSurface) {
    Daily d;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) {
            ASSERT_TRUE(d.boot(1200, 640)) << "pass " << pass;
        } else {
            ASSERT_TRUE(d.resize(1200, 900));
            ASSERT_TRUE(d.reopen()) << "pass " << pass;
        }
        EXPECT_NE(d.get("mdc-a-source", "value").find(
                      "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8\xe5\xb0\x8f\xe7\xab\xb9 Markdown"),
                  std::string::npos)
            << d.get("mdc-a-source", "value");
        EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "set-closed"));
        EXPECT_FALSE(d.laid("mdc-a-set"));
        EXPECT_TRUE(d.laid("zmd-bold"));
        EXPECT_EQ(d.get("zmd-h1", "text").find("H1"), std::string::npos)
            << d.get("zmd-h1", "text");
        EXPECT_FALSE(d.laid("zmd-view-source"));
        EXPECT_FALSE(d.laid("zmd-view-split"));

        ASSERT_TRUE(d.resize(1200, 900));

        const std::optional<zui::ElementBox> shell = d.box("mdc-a");
        const std::optional<zui::ElementBox> page = d.box("zmd-page");
        const std::optional<zui::ElementBox> status = d.box("mdc-a-status");
        ASSERT_TRUE(shell && shell->laid);
        ASSERT_TRUE(page && page->laid);
        ASSERT_TRUE(status && status->laid);
        EXPECT_GT(shell->border.height, 700.f) << "pass " << pass;
        EXPECT_TRUE(near(shell->border.height, page->content_height, 8.f))
            << "shell " << shell->border.height << " page " << page->content_height;
        EXPECT_TRUE(near(status->border.y + status->border.height,
                         shell->border.y + shell->border.height, 8.f));

        const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
        ASSERT_TRUE(paper && paper->laid) << "pass " << pass;
        EXPECT_LT(paper->border.width, shell->border.width - 80.f) << "pass " << pass;
        EXPECT_TRUE(near(paper->border.x + paper->border.width * 0.5f,
                         shell->border.x + shell->border.width * 0.5f, 40.f))
            << "paper x " << paper->border.x << " w " << paper->border.width
            << " shell w " << shell->border.width;

        EXPECT_TRUE(zui::test::any_text(d.rig.app, "li", "",
                                        "\xe5\x8f\x98\xe6\x88\x90\xe6\xa0\x87\xe9\xa2\x98"));
        EXPECT_TRUE(zui::test::any_text(d.rig.app, "code", "", "|"));
        EXPECT_TRUE(zui::test::any_text(d.rig.app, "td", "",
                                        "Ctrl+K \xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf"));

        ASSERT_TRUE(d.rig.click("zmd-view-preview"));
        EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-preview"));
        ASSERT_TRUE(d.rig.click("zmd-view-write"));
        EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-live"));
        EXPECT_TRUE(d.laid("mdc-a-preview"));
        ASSERT_TRUE(d.rig.click("mdc-a-preview"));
        ASSERT_TRUE(d.rig.pump());
        ASSERT_TRUE(d.rig.pump());
        EXPECT_TRUE(paper_bar(d).has_value());
        {
            const std::string before = d.get("mdc-a-source", "value");
            ASSERT_TRUE(d.rig.type("mdc-a-source", "CARET-TYPE"));
            EXPECT_NE(d.get("mdc-a-source", "value").find("CARET-TYPE"), std::string::npos)
                << d.get("mdc-a-source", "value");
            ASSERT_TRUE(replace_source(d, before));
        }

        ASSERT_TRUE(d.rig.click("zmd-set-status"));
        EXPECT_TRUE(d.laid("mdc-a-set"));
        EXPECT_EQ(d.get("zmd-tab-type", "text"), "\xe6\x8e\x92\xe7\x89\x88");

        ASSERT_TRUE(click_corner(d, "zmd-scrim"));
        EXPECT_FALSE(d.laid("mdc-a-set"));
        EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "set-closed"));

        ASSERT_TRUE(click_tool(d, "zmd-set-open"));
        EXPECT_TRUE(d.laid("mdc-a-set"));
        ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
        ASSERT_TRUE(d.rig.pump());
        EXPECT_FALSE(d.laid("mdc-a-set"));
        EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "set-closed"));
    }
}

TEST(AppInteract, ZmdEditingGoesThroughThePage) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    const std::string welcome_counts = d.get("mdc-a-stats", "text");

    EXPECT_TRUE(d.laid("mdc-a-preview"));
    ASSERT_TRUE(replace_source(d, kFixture));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), kFixture);
    EXPECT_NE(d.get("mdc-a-stats", "text"), welcome_counts);
    EXPECT_NE(d.get("mdc-a-stats", "text").find("\xe5\xad\x97"), std::string::npos);
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "typing");

    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("mdc-a-preview"));
    // The heading's text travels in the body span the render names (`…l`).
    EXPECT_EQ(d.get("mdh0l", "text"), "\xe6\x97\xa5\xe8\xaf\xbe\xe6\xa0\x87\xe9\xa2\x98");
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "strong", "", "bold"));
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "li", "", "\xe4\xb8\x80\xe9\xa1\xb9"));
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "code", "", "code"));
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "a", "", "\xe7\xa4\xba\xe4\xbe\x8b"));
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "td", "", "\xe7\x94\xb2"));

    ASSERT_TRUE(d.rig.click("zmd-view-preview"));
    EXPECT_TRUE(d.laid("mdc-a-preview"));
    EXPECT_FALSE(d.laid("mdc-a-source"));
    EXPECT_EQ(d.get("mdh0l", "text"), "\xe6\x97\xa5\xe8\xaf\xbe\xe6\xa0\x87\xe9\xa2\x98");

    ASSERT_TRUE(click_tool(d, "zmd-find-open"));
    EXPECT_EQ(d.get("zmd-find", "visible"), "true");
    ASSERT_TRUE(d.rig.type("zmd-find-q", "alpha"));
    EXPECT_EQ(d.get("zmd-find-count", "text"), "1 / 2");
    EXPECT_EQ(d.get("mdc-a-source", "selection"), "alpha");
    const std::string first_caret = d.get("mdc-a-source", "caret");
    ASSERT_TRUE(d.rig.click("zmd-find-next"));
    EXPECT_EQ(d.get("zmd-find-count", "text"), "2 / 2");
    EXPECT_EQ(d.get("mdc-a-source", "selection"), "alpha");
    EXPECT_NE(d.get("mdc-a-source", "caret"), first_caret);
    ASSERT_TRUE(d.rig.click("zmd-find-prev"));
    EXPECT_EQ(d.get("zmd-find-count", "text"), "1 / 2");
    EXPECT_EQ(d.get("mdc-a-source", "caret"), first_caret);
    ASSERT_TRUE(d.rig.click("zmd-find-close"));
    EXPECT_EQ(d.get("zmd-find", "visible"), "false");

    const std::string sized = d.get("mdc-a-source", "value");
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(sized.size()) + "," + std::to_string(sized.size()));
    const char* format_ids[] = {"zmd-h1", "zmd-h2", "zmd-quote", "zmd-ul", "zmd-ol",
                                "zmd-table", "zmd-hr", "zmd-code"};
    std::string previous = d.get("mdc-a-source", "value");
    for (const char* id : format_ids) {
        ASSERT_TRUE(click_tool(d, id)) << id;
        const std::string next = d.get("mdc-a-source", "value");
        EXPECT_NE(next, previous) << id;
        previous = next;
    }
    EXPECT_NE(previous.find("| \xe5\x85\x83\xe7\xb4\xa0"), std::string::npos) << previous;
    EXPECT_NE(previous.find("---"), std::string::npos) << previous;

    const int stars = static_cast<int>(previous.size());
    const auto count_token = [](std::string_view text, std::string_view token) {
        int n = 0;
        for (std::size_t at = 0; (at = text.find(token, at)) != std::string_view::npos;
             at += token.size()) {
            ++n;
        }
        return n;
    };
    const int bold_before = count_token(previous, "**");
    ASSERT_TRUE(click_tool(d, "zmd-bold"));
    previous = d.get("mdc-a-source", "value");
    EXPECT_GT(count_token(previous, "**"), bold_before);
    (void)stars;

    const std::size_t word = previous.find("word");
    ASSERT_NE(word, std::string::npos);
    // Keyboard formatting needs the editing surface. The preview view hides it (`display: none`)
    // and the prototype's shortcut guard is `document.activeElement`, so a user types in 写作.
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().focus("mdc-a-source");
    d.rig.pump();
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(word) + "," + std::to_string(word + 4));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "b", true));
    ASSERT_TRUE(d.rig.pump());
    previous = d.get("mdc-a-source", "value");
    EXPECT_NE(previous.find("**word**"), std::string::npos) << previous;

    const int italic_before = count_token(previous, "*");
    ASSERT_TRUE(click_tool(d, "zmd-italic"));
    previous = d.get("mdc-a-source", "value");
    EXPECT_GT(count_token(previous, "*"), italic_before);

    const std::size_t code = previous.find("code");
    ASSERT_NE(code, std::string::npos);
    d.rig.app.document().focus("mdc-a-source");  // the toolbar click above moved it
    d.rig.pump();
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(code) + "," + std::to_string(code + 4));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "i", true));
    ASSERT_TRUE(d.rig.pump());
    previous = d.get("mdc-a-source", "value");
    EXPECT_NE(previous.find("*code*"), std::string::npos) << previous;

    const std::string before_link = previous;
    ASSERT_TRUE(click_tool(d, "zmd-link"));
    EXPECT_EQ(d.get("zmd-dlg-link", "visible"), "true");
    EXPECT_TRUE(d.laid("mdc-a-name"));
    ASSERT_TRUE(d.rig.click("zmd-link-cancel"));
    EXPECT_FALSE(d.laid("zmd-dlg-link"));
    EXPECT_EQ(d.get("mdc-a-source", "value"), before_link);
    EXPECT_TRUE(d.laid("mdc-a-name"));
    EXPECT_TRUE(d.laid("zmd-link"));
    ASSERT_TRUE(click_tool(d, "zmd-link"));
    ASSERT_EQ(d.get("zmd-dlg-link", "visible"), "true") << "the toolbar click did not reopen it";
    ASSERT_TRUE(d.rig.type("zmd-link-text", "\xe6\x97\xa5\xe8\xaf\xbe\xe9\x93\xbe\xe6\x8e\xa5"));
    ASSERT_TRUE(d.rig.type("zmd-link-url", "https://daily.example/a"));
    ASSERT_TRUE(d.rig.click("zmd-link-apply"));
    EXPECT_FALSE(d.laid("zmd-dlg-link"));
    EXPECT_NE(d.get("mdc-a-source", "value").find("https://daily.example/a"), std::string::npos);

    ASSERT_TRUE(click_tool(d, "zmd-image"));
    EXPECT_EQ(d.get("zmd-dlg-image", "visible"), "true");
    ASSERT_TRUE(d.rig.click("zmd-image-pick"));
    EXPECT_EQ(d.get("zmd-dlg-image", "visible"), "true");
    ASSERT_TRUE(d.rig.click("zmd-image-cancel"));
    EXPECT_FALSE(d.laid("zmd-dlg-image"));
    ASSERT_TRUE(click_tool(d, "zmd-image"));
    ASSERT_TRUE(d.rig.type("zmd-image-url", "assets/daily.png"));
    ASSERT_TRUE(d.rig.click("zmd-image-apply"));
    EXPECT_FALSE(d.laid("zmd-dlg-image"));
    EXPECT_NE(d.get("mdc-a-source", "value").find("assets/daily.png"), std::string::npos);
}

TEST(AppInteract, ZmdPreviewResolvesImagesNextToTheDocument) {
    const fs::path dir = zui::test::scratch_dir("zmd-image");
    fs::create_directories(dir / "assets");
    write_text(dir / "pic.md", "# Pic\n\n![red](assets/red8.png)\n");
    fs::copy_file(fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "cases" / "content-url" /
                      "red8.png",
                  dir / "assets" / "red8.png");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "pic.md"));
    ASSERT_TRUE(d.rig.pump());

    int img = -1;
    const std::vector<zui::frame::NodeCensus> rows = zui::test::census(d.rig.app);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        // The toolbar's own icons are 15x15 by class; the preview's picture has no class.
        if (rows[i].tag == "img" && rows[i].class_attr.find("mdw-ico-img") == std::string::npos) {
            img = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(img, 0) << "no <img> in the census";
    const zui::frame::NodeCensus& box = rows[static_cast<std::size_t>(img)];
    EXPECT_TRUE(box.laid);
    // `red8.png` is 8x8: a box that kept the image's intrinsic size is the decode that a
    // relative `assets/red8.png` has to reach next to the document.
    EXPECT_EQ(box.w, 8.f) << "image did not resolve: w=" << box.w << " h=" << box.h;
    EXPECT_EQ(box.h, 8.f);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// The prototype's openPalette spells the input hint for the mode it opens (js/app.js): the
// command list asks for a command, the file list for a file name. The native kept the command
// hint in the file list, because `set` had no `placeholder`.
TEST(AppInteract, ZmdPaletteHintFollowsItsMode) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "k", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "input", "",
                                    "\xe8\xbe\x93\xe5\x85\xa5\xe5\x91\xbd\xe4\xbb\xa4\xe2\x80\xa6"))
        << "the command palette lost its hint";  // 输入命令…
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "p", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "input", "",
                                    "\xe8\xbe\x93\xe5\x85\xa5\xe6\x96\x87\xe4\xbb\xb6\xe5\x90\x8d"
                                    "\xe2\x80\xa6"))
        << "the file palette did not spell its hint";  // 输入文件名…
    EXPECT_FALSE(zui::test::any_text(d.rig.app, "input", "",
                                     "\xe8\xbe\x93\xe5\x85\xa5\xe5\x91\xbd\xe4\xbb\xa4\xe2\x80\xa6"))
        << "the command hint stayed in the file list";
}

// A restarted session on a warm page drops the typing veil's class; the veil's flag and timer
// outlived it, so the first keystroke after the reload showed no veil until the stale timer
// fired (chrome.cpp pulse skips the class while the flag says it is up).
TEST(AppInteract, ZmdRestartDoesNotStrandTheTypingVeil) {
    const fs::path dir = zui::test::scratch_dir("zmd-veil-restart");
    fs::create_directories(dir);
    write_text(dir / "note.md", "# Note\n\nbody\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-source", "x"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-typing"))
        << "the first keystroke showed no veil";

    ASSERT_TRUE(d.reopen(dir / "note.md"));
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "is-typing"));

    // Well inside the old pulse's 1.2s: the next keystroke must bring the veil back.
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-source", "y"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-typing"))
        << "the stale timer kept the veil down";
}

// 打开文档 (Ctrl+O) used to call open_file directly: a dirty buffer was replaced without the
// 未保存 dialog the file row and the drop path ask for, and a folder picked by mistake opened as
// an empty buffer named after it.
TEST(AppInteract, ZmdOpenPickerAsksFirstAndRefusesAFolder) {
    const fs::path dir = zui::test::scratch_dir("zmd-open-picker");
    fs::create_directories(dir / "folder");
    write_text(dir / "note.md", "# Note\n\nbody\n");
    write_text(dir / "other.md", "# Other\n\nother-body\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    // Autosave off, so an edit stays unsaved and the picker has something to ask about.
    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    ASSERT_TRUE(d.rig.click("zmd-autosave"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(replace_source(d, "# Note\n\nbody dirty\n"));

    // A dirty buffer: the pick is held behind the unsaved dialog, and 取消 keeps the text.
    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {(dir / "other.md").u8string()}});
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "o", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.laid("zmd-dlg-unsaved")) << "打开文档 dropped the unsaved buffer";
    ASSERT_TRUE(d.rig.click("zmd-unsaved-cancel"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), "# Note\n\nbody dirty\n");
    EXPECT_NE(d.get("mdc-a-name", "text").find("note.md"), std::string::npos);

    // Clean again (save), then a folder picked by mistake keeps the buffer as it is.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "s", true));
    ASSERT_TRUE(d.rig.pump());
    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {(dir / "folder").u8string()}});
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "o", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(d.laid("zmd-dlg-unsaved")) << "a clean buffer had nothing to ask about";
    EXPECT_EQ(d.get("mdc-a-source", "value"), "# Note\n\nbody dirty\n");
    EXPECT_NE(d.get("mdc-a-name", "text").find("note.md"), std::string::npos)
        << "a folder opened as a document";

    // A real document still opens through the picker.
    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {(dir / "other.md").u8string()}});
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "o", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("other-body"), std::string::npos);
    EXPECT_NE(d.get("mdc-a-name", "text").find("other.md"), std::string::npos);
}

// 另存为 and 导出 HTML with a real picker answer: the picked path is where the bytes land, and
// the document follows the new name. Both branches were unreachable in the headless suite (the
// noop platform used to answer Unsupported for every ask).
TEST(AppInteract, ZmdSaveAsAndExportWriteThePickedPath) {
    const fs::path dir = zui::test::scratch_dir("zmd-pick-save");
    fs::create_directories(dir);
    write_text(dir / "note.md", "# Note\n\nbody\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    ASSERT_TRUE(replace_source(d, "# Note\n\nbody more\n"));

    const fs::path copy = dir / "copy.md";
    zui::backends::noop_set_next_pick_save({zui::PickStatus::Ok, copy.u8string()});
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe5\x8f\xa6\xe5\xad\x98\xe4\xb8\xba"));  // 另存为
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("cm0", "text"), "\xe5\x8f\xa6\xe5\xad\x98\xe4\xb8\xba");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "enter"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(fs::is_regular_file(copy)) << "另存为 did not write the picked path";
    EXPECT_EQ(slurp(copy), d.get("mdc-a-source", "value"));
    EXPECT_NE(d.get("mdc-a-name", "text").find("copy.md"), std::string::npos)
        << d.get("mdc-a-name", "text");
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "saved");

    const fs::path out = dir / "out.html";
    zui::backends::noop_set_next_pick_save({zui::PickStatus::Ok, out.u8string()});
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe5\xaf\xbc\xe5\x87\xba"));  // 导出
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("cm0", "text"), "\xe5\xaf\xbc\xe5\x87\xba HTML");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "enter"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(fs::is_regular_file(out)) << "导出 HTML did not write the picked path";
    const std::string page = slurp(out);
    EXPECT_NE(page.find("<article class=\"mdw-doc\">"), std::string::npos);
    EXPECT_NE(page.find("body more"), std::string::npos);
}

// 插入图片's 选择文件 copies the picked file into the assets folder and fills the dialog. A copy
// that cannot land used to fill the fields anyway, so 插入 wrote a picture link to a file that is
// not there.
TEST(AppInteract, ZmdPickImageFillsTheFieldsOnlyWhenTheCopyLands) {
    const fs::path dir = zui::test::scratch_dir("zmd-pick-image");
    fs::create_directories(dir / "assets" / "red8.png");  // the copy target is a directory
    write_text(dir / "note.md", "# Note\n\nbody\n");
    const fs::path blocked = dir / "red8.png";
    const fs::path landed = dir / "blue8.png";
    for (const fs::path& target : {blocked, landed}) {
        fs::copy_file(fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "cases" / "content-url" /
                          "red8.png",
                      target);
    }

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    ASSERT_TRUE(click_tool(d, "zmd-image"));
    ASSERT_EQ(d.get("zmd-dlg-image", "visible"), "true");
    ASSERT_TRUE(d.rig.type("zmd-image-url", "typed-by-hand"));
    ASSERT_EQ(d.get("zmd-image-url", "value"), "typed-by-hand");

    // The copy cannot land: the fields keep what they had.
    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {blocked.u8string()}});
    ASSERT_TRUE(d.rig.click("zmd-image-pick"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-image-url", "value"), "typed-by-hand")
        << "the dialog named a file that was never copied";

    // A copy that lands names the asset, next to wherever 图片存放 points.
    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {landed.u8string()}});
    ASSERT_TRUE(d.rig.click("zmd-image-pick"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-image-url", "value"), "assets/blue8.png");
    EXPECT_EQ(d.get("zmd-image-alt", "value"), "blue8");
    EXPECT_TRUE(fs::is_regular_file(dir / "assets" / "blue8.png"));
}

// A dropped folder, and a copy that could not land, used to leave a link to a picture that is
// not there: the snippet was inserted whatever `copy_file` answered.
TEST(AppInteract, ZmdDropWithoutALandingCopyInsertsNothing) {
    const fs::path dir = zui::test::scratch_dir("zmd-drop-miss");
    fs::create_directories(dir / "pics");
    write_text(dir / "note.md", "# Note\n\nbody\n");
    const fs::path picture = dir / "red8.png";
    fs::copy_file(fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "cases" / "content-url" /
                      "red8.png",
                  picture);
    // `assets/red8.png` is a directory, so `copy_file` cannot overwrite it.
    fs::create_directories(dir / "assets" / "red8.png");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    const std::string before = d.get("mdc-a-source", "value");
    const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
    ASSERT_TRUE(paper && paper->laid);
    const float x = paper->border.x + paper->border.width * 0.5f;
    const float y = paper->border.y + paper->border.height * 0.5f;

    ASSERT_TRUE(zui::frame::Access::drop_paths(d.rig.app, {(dir / "pics").u8string()}, x, y));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), before) << "a dropped folder became a picture";

    ASSERT_TRUE(zui::frame::Access::drop_paths(d.rig.app, {picture.u8string()}, x, y));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), before) << "a copy that failed was still linked";
}

TEST(AppInteract, ZmdDropCopiesThePictureAndLinksIt) {
    const fs::path dir = zui::test::scratch_dir("zmd-drop");
    fs::create_directories(dir);
    write_text(dir / "note.md", "# Note\n\nbody\n");
    const fs::path picture = dir / "red8.png";
    fs::copy_file(fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "cases" / "content-url" /
                      "red8.png",
                  picture);

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "note.md"));
    const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
    ASSERT_TRUE(paper && paper->laid);
    ASSERT_TRUE(zui::frame::Access::drop_paths(d.rig.app, {picture.u8string()},
                                               paper->border.x + paper->border.width * 0.5f,
                                               paper->border.y + paper->border.height * 0.5f));
    ASSERT_TRUE(d.rig.pump());
    // The dropped picture left the drop point for the workspace's assets folder...
    EXPECT_TRUE(fs::is_regular_file(dir / "assets" / "red8.png"));
    // ...and the document links it the way the picture dialog does.
    EXPECT_NE(d.get("mdc-a-source", "value").find("![red8](assets/red8.png)"), std::string::npos)
        << d.get("mdc-a-source", "value");

    // 图片存放 says where the next picture goes, in the copy and in the link.
    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    ASSERT_TRUE(replace_field(d, "zmd-assets", "media/"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    const fs::path second = dir / "blue8.png";
    fs::copy_file(fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "cases" / "content-url" /
                      "red8.png",
                  second);
    ASSERT_TRUE(zui::frame::Access::drop_paths(d.rig.app, {second.u8string()},
                                               paper->border.x + paper->border.width * 0.5f,
                                               paper->border.y + paper->border.height * 0.5f));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_regular_file(dir / "media" / "blue8.png"));
    EXPECT_NE(d.get("mdc-a-source", "value").find("![blue8](media/blue8.png)"), std::string::npos)
        << d.get("mdc-a-source", "value");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AppInteract, ZmdDroppedDocumentAsksBeforeItReplacesTheBuffer) {
    const fs::path dir = zui::test::scratch_dir("zmd-drop-doc");
    fs::create_directories(dir);
    write_text(dir / "note.md", "# Note\n\nnote-body\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
    ASSERT_TRUE(paper && paper->laid);
    const float drop_x = paper->border.x + paper->border.width * 0.5f;
    const float drop_y = paper->border.y + paper->border.height * 0.5f;

    ASSERT_TRUE(d.rig.type("mdc-a-source", "UNSAVED-DROP\n"));
    ASSERT_TRUE(zui::frame::Access::drop_paths(d.rig.app, {(dir / "note.md").u8string()}, drop_x,
                                               drop_y));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("zmd-dlg-unsaved")) << "a dropped document replaced a dirty buffer";
    EXPECT_NE(d.get("mdc-a-source", "value").find("UNSAVED-DROP"), std::string::npos);

    // 不保存 takes the dropped document instead.
    ASSERT_TRUE(d.rig.click("zmd-unsaved-discard"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("note-body"), std::string::npos)
        << d.get("mdc-a-source", "value");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AppInteract, ZmdNewDocumentFollowsTheNewFileRow) {
    const fs::path dir = zui::test::scratch_dir("zmd-new-here");
    fs::create_directories(dir / "sub");
    write_text(dir / "sub" / "note.md", "# Note\n\nbody\n");
    const fs::path untitled = fs::u8path("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d.md");  // 未命名.md

    Daily d;
    // The workspace is the directory argument; 当前文件夹 is the folder the panel shows.
    ASSERT_TRUE(d.boot(1200, 900, dir));
    ASSERT_TRUE(reveal_files(d));
    const int sub = find_row(d, "fl", "sub");
    ASSERT_GE(sub, 0);
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(sub)));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_GE(find_row(d, "fl", "note.md"), 0);

    // 当前文件夹 puts the next document in the folder on screen.
    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    ASSERT_TRUE(d.rig.click("zmd-new-here"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-file-new"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_regular_file(dir / "sub" / untitled))
        << "a new document did not follow 当前文件夹";

    // 工作目录根 sends the next document to the workspace root while the panel
    // is still inside sub.
    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    ASSERT_TRUE(d.rig.click("zmd-new-root"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-file-new"));
    ASSERT_TRUE(d.rig.pump());
    // The first untitled file is in sub, so the root's own unique name is 未命名.md.
    const fs::path second = fs::u8path("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d 2.md");
    EXPECT_TRUE(fs::is_regular_file(dir / untitled))
        << "工作目录根 did not create the file in the workspace root";
    EXPECT_FALSE(fs::is_regular_file(dir / "sub" / second))
        << "工作目录根 still created the file inside the shown folder";

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AppInteract, ZmdFindMarksItsHitsInThePreview) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "# \xe6\xa0\x87\xe9\xa2\x98\n\nalpha beta alpha\n"));

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("zmd-find-q", "alpha"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-find-count", "text"), "1 / 2");
    // The bar's count and the preview agree: both hits carry the sheet's find mark.
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "mark", "mdw-f-mark", "alpha"));
    // Closing the bar takes the marks away again.
    ASSERT_TRUE(d.rig.click("zmd-find-close"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(zui::test::any_text(d.rig.app, "mark", "mdw-f-mark", "alpha"));
}

TEST(AppInteract, ZmdFindJumpBringsTheHitIntoView) {
    std::string text;
    for (int i = 0; i < 30; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    text += "needle one\n\n";
    for (int i = 30; i < 60; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    text += "needle two\n";
    const std::size_t first_at = text.find("needle one");
    const std::size_t second_at = text.find("needle two");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, text));
    ASSERT_FLOAT_EQ(std::stof(d.get("mdc-a-scroll", "scroll_top")), 0.f)
        << "the page already scrolled";

    // The owning block: the preview block that starts closest to the hit at or before it.
    const auto block_at = [&](std::size_t at) -> std::string {
        std::string owning;
        long owning_beg = -1;
        for (int i = 0; i < 120; ++i) {
            for (const char* prefix : {"mdh", "mdb"}) {
                const std::string id = std::string(prefix) + std::to_string(i);
                const std::string beg = d.get(id, "data-beg");
                if (beg.empty()) {
                    continue;
                }
                const long value = std::strtol(beg.c_str(), nullptr, 10);
                if (value >= 0 && static_cast<std::size_t>(value) <= at && value > owning_beg) {
                    owning = id;
                    owning_beg = value;
                }
            }
        }
        return owning;
    };
    const auto in_view = [&](std::string_view id) {
        const std::optional<zui::ElementBox> scroller = d.box("mdc-a-scroll");
        const std::optional<zui::ElementBox> block = d.box(id);
        if (!scroller || !block || !scroller->laid || !block->laid) {
            return false;
        }
        return block->border.y >= scroller->border.y - 1.f &&
               block->border.y + block->border.height <=
                   scroller->border.y + scroller->border.height + 1.f;
    };

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("zmd-find-q", "needle"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("zmd-find-count", "text"), "1 / 2");

    // Typing the query jumps to the first hit and scrolls its block into view.
    const std::string first = block_at(first_at);
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(until(d, [&] { return in_view(first); }))
        << "the first hit stayed out of view";

    // 下一个 scrolls to the hit below it; 上一个 comes back.
    ASSERT_TRUE(d.rig.click("zmd-find-next"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("zmd-find-count", "text"), "2 / 2");
    const std::string second = block_at(second_at);
    ASSERT_FALSE(second.empty());
    EXPECT_TRUE(until(d, [&] { return in_view(second); }))
        << "下一个 left the hit out of view";

    ASSERT_TRUE(d.rig.click("zmd-find-prev"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("zmd-find-count", "text"), "1 / 2");
    EXPECT_TRUE(until(d, [&] { return in_view(first); }))
        << "上一个 left the hit out of view";
}

TEST(AppInteract, ZmdDeletingTextRetiresItsBlocks) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "alpha\n\nbeta\n\ngamma\n"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "p", "", "gamma"));

    // The document shrinks: the writing view is exactly what the source renders now. The old
    // render's ids no longer cover `beta`/`gamma`, so an appending fragment load would keep them
    // painted; the preview load replaces the article's children.
    ASSERT_TRUE(replace_source(d, "alpha\n"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "p", "", "alpha"));
    EXPECT_FALSE(zui::test::any_text(d.rig.app, "p", "", "beta")) << "a retired block still paints";
    EXPECT_FALSE(zui::test::any_text(d.rig.app, "p", "", "gamma")) << "a retired block still paints";
}

// 聚焦模式 dims every block but the one the caret is in (the sheet's
// `.is-focusing .mdw-doc > :not(.mdw-cur)`), so the preview has to carry exactly one `mdw-cur`
// block and it has to be the caret's - what the prototype's `updateCurrentBlock` keeps lit.
TEST(AppInteract, ZmdFocusModeKeepsTheCaretBlockLit) {
    std::string text;
    for (int i = 0; i < 60; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, text));
    // 聚焦模式 from the command palette.
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe8\x81\x9a\xe7\x84\xa6"));  // 聚焦
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.click("cm0"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-focusing"));

    // Put the caret at the end of para-5 and type, so the preview re-stamps around it.
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    const std::size_t at = text.find("para-5") + std::string("para-5").size();
    d.rig.app.document().set("mdc-a-source", "select", std::to_string(at) + "," + std::to_string(at));
    ASSERT_TRUE(d.rig.type("mdc-a-source", "X"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    // Exactly one block inside the preview is the current one, and it is the caret's. (The
    // prototype's static welcome paragraph in `mdc-a-doc` keeps its own `mdw-cur` in the markup;
    // that article is hidden in 写作 and is not what this checks.)
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    std::string current;
    int current_count = 0;
    for (std::size_t i = 0; i < census.size(); ++i) {
        if (!zui::test::class_token(census[i].class_attr, "mdw-cur")) {
            continue;
        }
        bool in_preview = false;
        for (int p = census[i].parent; p >= 0; p = census[static_cast<std::size_t>(p)].parent) {
            if (census[static_cast<std::size_t>(p)].id == "mdc-a-preview") {
                in_preview = true;
                break;
            }
        }
        if (in_preview) {
            ++current_count;
            current = census[i].id;
        }
    }
    EXPECT_EQ(current_count, 1) << "the lit block is not unique";
    EXPECT_EQ(current, "mdb5") << "the lit block is not the caret's";
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "p", "mdw-cur", "para-5X"));
    // The sheet dims the rest; the caret's block keeps its opacity.
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdb0", "opacity"), "0.28");
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdb5", "opacity"), "");
}

// The prototype's palette input takes the arrow keys to move the highlight and Enter to run the
// highlighted row; native `Palette::move` / `pick` were only reachable from the mouse.
// A write that fails shows 保存失败 on the save dot (chrome.cpp). The typing pulse's 1.2s
// timeout used to flip the dot back to 已保存 whatever happened, so the failure signal lived
// only for that long and the user was told the work was safe.
TEST(AppInteract, ZmdSaveFailureStaysOnTheDot) {
    const fs::path dir = zui::test::scratch_dir("zmd-save-error");
    fs::create_directories(dir);
    const fs::path doc = dir / "doc.md";
    write_text(doc, "alpha\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, doc));
    ASSERT_TRUE(replace_source(d, "alpha beta\n"));
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "saved");

    // The write target becomes unwritable: the file is replaced by a directory of its name, so
    // the next autosave cannot open it.
    std::error_code ec;
    fs::remove(doc, ec);
    ASSERT_TRUE(fs::create_directory(doc, ec)) << ec.message();

    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-source", "gamma"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "error") << "the failed write stayed silent";

    // The typing veil settles after 1.2s; the error must outlive it.
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_TRUE(d.rig.pump());
    }
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "error")
        << "the settle timer told the user the work was saved";
    EXPECT_EQ(d.get("mdc-a-save", "aria-label"), "\xe4\xbf\x9d\xe5\xad\x98\xe5\xa4\xb1\xe8\xb4\xa5")
        << "保存失败";

    // A close request's 保存 must not leave either: the write failed, so the window stays and
    // the text is still there.
    ASSERT_TRUE(d.rig.request_close());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.laid("zmd-dlg-unsaved"));
    ASSERT_TRUE(d.rig.click("zmd-unsaved-save"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.rig.pump()) << "the session quit over a save that failed";
    EXPECT_EQ(d.get("mdc-a-save", "data-state"), "error");
    EXPECT_NE(d.get("mdc-a-source", "value").find("gamma"), std::string::npos);
}

// The prototype dims the sheet while the window is away (js/app.js `window blur` adds
// `is-blurred`); the host reports the same fact as `visibilitychange` + `Window::visible()`.
TEST(AppInteract, ZmdWindowBlurDimsTheSheet) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "is-blurred"));
    const std::string lit = zui::frame::Access::computed(d.rig.app, "mdc-a-doc", "color");

    d.rig.window().set_visibility_for_test(zui::backends::HostVisibility::Hidden);
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-blurred"))
        << "the sheet stayed lit while the window was away";
    // The dim is a paint change, not a class nobody reads: the doc's colour moved.
    EXPECT_NE(zui::frame::Access::computed(d.rig.app, "mdc-a-doc", "color"), lit);

    d.rig.window().set_visibility_for_test(zui::backends::HostVisibility::Visible);
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "is-blurred"));
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdc-a-doc", "color"), lit);
}

// The browser focuses the find input the moment the bar opens (js/zmd.js `find-open`), and the
// palette defers its `palInput.focus()` one tick. Native loads the flag through the style flush,
// so an immediate focus was refused and the keyboard stayed in the document.
TEST(AppInteract, ZmdPanelsTakeTheKeyboardWhenTheyOpen) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "alpha\n"));

    // Typing with no click anywhere goes into the find field.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    zui::backends::TextInputFrame frame;
    frame.text.assign("\xe6\x89\xbe");  // 找
    d.rig.window().inject(std::move(frame));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-find-q", "value"), "\xe6\x89\xbe")
        << "the find bar never took the keyboard";
    EXPECT_EQ(d.get("mdc-a-source", "value"), "alpha\n") << "the query landed in the document";
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());

    // The palette does the same: Ctrl+K, then type the query with no click.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "k", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    zui::backends::TextInputFrame pal;
    pal.text.assign("\xe4\xb8\xbb\xe9\xa2\x98");  // 主题
    d.rig.window().inject(std::move(pal));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-pal-input", "value"), "\xe4\xb8\xbb\xe9\xa2\x98")
        << "the palette never took the keyboard";
    EXPECT_EQ(d.get("cm0", "text"), "\xe4\xb8\xbb\xe9\xa2\x98\xef\xbc\x9a\xe6\xb5\x85\xe8\x89\xb2")
        << "主题：浅色";
    EXPECT_EQ(d.get("mdc-a-source", "value"), "alpha\n");
}

// The prototype's format and edit shortcuts act only while the writing surface has focus
// (js/app.js checks `document.activeElement` against the editing article). The native surface is
// the source textarea; while a panel owns the focus, the document must not change.
TEST(AppInteract, ZmdEditKeysWaitForTheEditorsFocus) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "alpha beta\n"));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().set("mdc-a-source", "select", "6,10");

    // The find bar takes the focus: the format key is ignored.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true));
    ASSERT_TRUE(d.rig.pump());
    const std::string before = d.get("mdc-a-source", "value");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "b", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), before) << "the find bar let Ctrl+B through";

    // Closing the bar leaves nobody focused (the browser blurs to the body): still ignored.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "b", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), before) << "a blurred editor let Ctrl+B through";
    // Even undo stays off: the history belongs to the focused control in the browser.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "z", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), before);

    // The editing surface takes the focus back: the shortcuts act again.
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "b", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("**beta**"), std::string::npos)
        << d.get("mdc-a-source", "value");
}

// Ctrl+P lists the workspace documents; the browser's rows run `selectTreeRow` on the named
// entry, so picking one opens it. The native row id was the file name, which matched no command
// and did nothing.
TEST(AppInteract, ZmdFilePaletteOpensThePickedFile) {
    const fs::path dir = zui::test::scratch_dir("zmd-file-palette");
    fs::create_directories(dir);
    write_text(dir / "alpha.md", "# Alpha\n\nalpha-body\n");
    write_text(dir / "beta.md", "# Beta\n\nbeta-body\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "alpha.md"));
    ASSERT_NE(d.get("mdc-a-source", "value").find("alpha-body"), std::string::npos);

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "p", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "beta"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("cm0", "text"), "beta.md");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "enter"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("beta-body"), std::string::npos)
        << "the picked file never opened";
    EXPECT_NE(d.get("mdc-a-name", "text").find("beta.md"), std::string::npos);
}

// The palette advertises 撤销 Ctrl+Z / 重做 Ctrl+Y; the browser build answers them from the
// textarea's own history, so the native build has to route them to the document's.
TEST(AppInteract, ZmdUndoRedoShortcutsReachTheDocument) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "alpha\n"));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());

    const std::string typed = d.get("mdc-a-source", "value");
    ASSERT_TRUE(d.rig.type("mdc-a-source", "beta"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_NE(d.get("mdc-a-source", "value"), typed);

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "z", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "value"), typed) << "Ctrl+Z did not undo the typing";

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "y", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value"), typed) << "Ctrl+Y did not redo the typing";
}

TEST(AppInteract, ZmdPaletteKeysDriveTheList) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "k", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe4\xb8\xbb\xe9\xa2\x98"));  // 主题
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("cm0", "text"), "\xe4\xb8\xbb\xe9\xa2\x98\xef\xbc\x9a\xe6\xb5\x85\xe8\x89\xb2")
        << "主题：浅色";
    EXPECT_TRUE(zui::test::class_token(d.get("cm0", "class"), "is-active"));

    // ArrowDown moves the highlight one row; ArrowUp walks back and wraps to the last row.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "arrowdown"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("cm1", "class"), "is-active"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "arrowup"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("cm0", "class"), "is-active"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "arrowup"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("cm2", "class"), "is-active"))
        << "the highlight did not wrap";

    // Enter runs the highlighted row: 主题：深色 (back up one row first).
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "arrowup"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("cm1", "class"), "is-active"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "enter"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pal-closed"))
        << "the palette stayed open";
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "theme-dark"));
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "theme-system"));
}

TEST(AppInteract, ZmdPaletteHighlightsTheSelection) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, "alpha beta\n"));
    d.rig.app.document().set("mdc-a-source", "select", "6,10");
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("mdc-a-source", "selection"), "beta");

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "k", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("mdc-a-source", "selection"), "beta") << "the palette dropped it";
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe9\xab\x98\xe4\xba\xae"));  // 高亮
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(d.get("mdc-a-pal-input", "value"), "\xe9\xab\x98\xe4\xba\xae");
    ASSERT_EQ(d.get("cm0", "text"), "\xe9\xab\x98\xe4\xba\xae");
    ASSERT_TRUE(d.rig.click("cm0"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("==beta=="), std::string::npos)
        << d.get("mdc-a-source", "value");
    // And the preview paints it as the sheet's mark token.
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "mark", "", "beta"));
}

TEST(AppInteract, ZmdAcceleratorsReachTheCommands) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));

    // Views: Ctrl+1..4, as the palette advertises them.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "2", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-source"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "1", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-live"));

    // Panels: Ctrl+Shift+1..2, including the number row's shifted spelling.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "2", true, true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pin-r"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "@", true, true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "pin-r"));

    // Ctrl+Shift+F is 聚焦模式 - it used to open the find bar, which the palette never promised.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true, true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-focusing"));
    EXPECT_EQ(d.get("zmd-find", "visible"), "false");

    // Ctrl+, opens the sheet, Ctrl+Shift+K the link dialog.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, ",", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("mdc-a-set"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "k", true, true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-dlg-link", "visible"), "true");
}

TEST(AppInteract, ZmdTypewriterKeepsTheCaretInTheMiddle) {
    const fs::path dir = zui::test::scratch_dir("zmd-typewriter");
    fs::create_directories(dir);
    std::string text;
    for (int i = 0; i < 60; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    write_text(dir / "long.md", text);
    const std::size_t middle = text.find("para-30");
    ASSERT_NE(middle, std::string::npos);

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir / "long.md"));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(middle) + "," + std::to_string(middle));

    // Off: the caret is deep in the document. The paper keeps the caret's block on screen, which
    // is what a writing view does at the fold - it does not centre it, so the block sits against
    // the edge it came in through.
    ASSERT_TRUE(d.rig.type("mdc-a-source", "X"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_GT(std::stof(d.get("mdc-a-scroll", "scroll_top")), 0.f);
    const std::optional<zui::ElementBox> loose_block = d.box("mdb30");
    const std::optional<zui::ElementBox> loose_scroller = d.box("mdc-a-scroll");
    ASSERT_TRUE(loose_block && loose_block->laid);
    ASSERT_TRUE(loose_scroller && loose_scroller->laid);
    EXPECT_GE(loose_block->border.y, loose_scroller->border.y - 1.f);
    EXPECT_LE(loose_block->border.y + loose_block->border.height,
              loose_scroller->border.y + loose_scroller->border.height + 1.f);
    EXPECT_GT(loose_block->border.y + loose_block->border.height * 0.5f,
              loose_scroller->border.y + loose_scroller->border.height * 0.5f + 30.f)
        << "the block was centred with 打字机模式 off";

    // 打字机模式 from the command palette.
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe6\x89\x93\xe5\xad\x97\xe6\x9c\xba"));  // 打字机
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.click("cm0"));
    ASSERT_TRUE(d.rig.pump());

    // On: the block being written moves to the middle of the viewport.
    ASSERT_TRUE(d.rig.type("mdc-a-source", "Y"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_GT(std::stof(d.get("mdc-a-scroll", "scroll_top")), 0.f);
    const std::optional<zui::ElementBox> block = d.box("mdb30");
    const std::optional<zui::ElementBox> scroller = d.box("mdc-a-scroll");
    ASSERT_TRUE(block && block->laid);
    ASSERT_TRUE(scroller && scroller->laid);
    EXPECT_NEAR(block->border.y + block->border.height * 0.5f,
                scroller->border.y + scroller->border.height * 0.5f, 12.f)
        << "block y=" << block->border.y << " scroller y=" << scroller->border.y
        << " h=" << scroller->border.height;

    // A programmatic scroll must not eat the next toolbar click.
    ASSERT_TRUE(click_tool(d, "zmd-bold"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("**"), std::string::npos)
        << d.get("mdc-a-source", "value").substr(0, 80);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// The outline panel is the reading position made visible: the prototype's `syncOutline` gives
// the last heading at or above `scrollTop + 64` the `is-active` row and fills `.mdw-outline-bar`
// with the scroll ratio. Native reads the scroller's own geometry from its `onscroll`.
TEST(AppInteract, ZmdOutlineFollowsTheReadingPosition) {
    std::string text = "# Top\n\n";
    for (int i = 0; i < 30; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    text += "## Middle\n\n";
    for (int i = 30; i < 60; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    text += "## Bottom\n\n";
    for (int i = 60; i < 90; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, text));
    ASSERT_EQ(d.get("hd0", "text"), "Top");
    ASSERT_EQ(d.get("hd1", "text"), "Middle");
    ASSERT_EQ(d.get("hd2", "text"), "Bottom");
    EXPECT_TRUE(zui::test::class_token(d.get("hd0", "class"), "is-active"));

    // The bar carries one `prog*` step class; -1 when none is set yet.
    const auto progress_step = [&]() -> int {
        const std::string classes = d.get("zmd-outline-bar", "class");
        for (std::size_t at = classes.find("prog"); at != std::string::npos;
             at = classes.find("prog", at + 1)) {
            const std::size_t end = classes.find(' ', at);
            const std::string token =
                classes.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (token.size() > 4 && token[4] >= '0' && token[4] <= '9') {
                return static_cast<int>(std::strtol(token.c_str() + 4, nullptr, 10));
            }
        }
        return -1;
    };
    const auto scroll_to = [&](std::string value) {
        d.rig.app.document().set("mdc-a-scroll", "scroll_top", value);
        return d.rig.pump() && d.rig.pump();
    };

    // Middle becomes the current row once its heading reaches the reading anchor.
    const std::optional<zui::ElementBox> scroller = d.box("mdc-a-scroll");
    const std::optional<zui::ElementBox> middle = d.box("mdh1");
    ASSERT_TRUE(scroller && scroller->laid);
    ASSERT_TRUE(middle && middle->laid);
    const float offset = middle->border.y - scroller->border.y;
    ASSERT_GT(offset, 64.f);
    ASSERT_TRUE(scroll_to(std::to_string(static_cast<int>(offset))));
    EXPECT_TRUE(zui::test::class_token(d.get("hd1", "class"), "is-active"));
    const int middle_step = progress_step();
    EXPECT_GT(middle_step, 0);
    EXPECT_LT(middle_step, 100);

    // The end of the document: the last heading is current and the bar is full.
    ASSERT_TRUE(scroll_to("100000"));
    EXPECT_TRUE(zui::test::class_token(d.get("hd2", "class"), "is-active"));
    EXPECT_EQ(progress_step(), 100);

    // Back to the top: the first heading and an empty bar.
    ASSERT_TRUE(scroll_to("0"));
    EXPECT_TRUE(zui::test::class_token(d.get("hd0", "class"), "is-active"));
    EXPECT_EQ(progress_step(), 0);
}

// After the preview is scrolled programmatically (打字机模式 here) the article's painted box lies
// under the opaque title bar. Moving the pointer to the bar must take `:hover` there, so that the
// `.mdw-bar:hover .mdw-tools` reveal runs before the click. The engine used to keep `:hover` on
// the covered preview while its untransformed layout border held the point - the rule that stops
// a `:hover { transform }` from flickering - so the bar never matched, the toolbar stayed
// `pointer-events: none`, the press hit the bar, and the release - by then the frame's own
// restyle had revealed the tools - hit the button. UI Events cancels a click whose press and
// release targets differ, so the dialog never opened. Hover now follows the node painted above a
// box that did not move itself; `click_tool` also waits for the hit test to answer with the
// button before it aims.
TEST(AppInteract, ZmdToolbarClickAfterAScrollAndADialog) {
    std::string text;
    for (int i = 0; i < 60; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, text));
    // 打字机模式, from the palette, then typing: the preview scrolls under the pointer.
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe6\x89\x93\xe5\xad\x97\xe6\x9c\xba"));  // 打字机
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.click("cm0"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("mdc-a-source", "X"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const float scrolled = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    ASSERT_GT(scrolled, 0.f);

    // A dialog opens and closes again; the toolbar is where it always was.
    ASSERT_TRUE(click_tool(d, "zmd-link"));
    ASSERT_EQ(d.get("zmd-dlg-link", "visible"), "true");
    ASSERT_TRUE(d.rig.click("zmd-link-cancel"));
    ASSERT_TRUE(d.rig.pump());

    const std::string before = d.get("mdc-a-source", "value");
    ASSERT_TRUE(click_tool(d, "zmd-bold"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value"), before)
        << "the toolbar stopped taking clicks after the dialog cycle";
}

TEST(AppInteract, ZmdFilePanelWalksFolders) {
    const fs::path dir = zui::test::scratch_dir("zmd-folders");
    fs::create_directories(dir / "notes");
    write_text(dir / "notes" / "inside.md", "# Inside\n\ninside-body\n");
    write_text(dir / "root.md", "# Root\n\nroot-body\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir));
    ASSERT_TRUE(reveal_files(d));
    // The panel opens on the workspace root: the folder is a row, its contents are not shown.
    const int notes = find_row(d, "fl", "notes");
    ASSERT_GE(notes, 0);
    EXPECT_EQ(find_row(d, "fl", "inside.md"), -1);
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "\xe6\x96\x87\xe4\xbb\xb6");  // 文件
    // A folder row enters it; the title follows and the root's own file leaves the list.
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(notes)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_GE(find_row(d, "fl", "inside.md"), 0);
    EXPECT_EQ(find_row(d, "fl", "root.md"), -1);
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "notes");
    // A row inside the folder still opens its document.
    const int inside = find_row(d, "fl", "inside.md");
    ASSERT_GE(inside, 0);
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(inside)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("inside-body"), std::string::npos);
    // 返回上一级 brings the root listing back.
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-folder-up"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_GE(find_row(d, "fl", "root.md"), 0);
    EXPECT_EQ(find_row(d, "fl", "inside.md"), -1);
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "\xe6\x96\x87\xe4\xbb\xb6");  // 文件

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// One document with every block the renderer knows, driven through the flows a reader uses:
// every view, typing, the find bar, the outline, and a save/reopen round trip. It is a smoke
// sweep - it asserts the states stay coherent, and the Debug build's invariants do the rest.
TEST(AppInteract, ZmdLongDocumentWalksEveryViewAndSaves) {
    const fs::path dir = std::filesystem::temp_directory_path() /
                         ("zmd-kitchen-sink-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const fs::path sink = dir / "sink.md";
    std::string doc = "# \xe6\xa0\x87\xe9\xa2\x98\n\n";                                   // # 标题
    for (int i = 0; i < 40; ++i) {
        doc += "\xe5\xa1\xab\xe5\x85\x85\xe6\xae\xb5\xe8\x90\xbd\xef\xbc\x8c\xe6\x8a\x8a\xe6\xa0\x87"
               "\xe9\xa2\x98\xe6\x8e\xa8\xe5\x88\xb0\xe8\xa7\x86\xe5\x8f\xa3\xe4\xb8\x8b\xe9\x9d\xa2"
               "\xe3\x80\x82\n\n";  // 填充段落，把第二个标题推到视口下面。
    }
    doc += "\xe6\xae\xb5\xe8\x90\xbd **\xe7\xb2\x97** *\xe6\x96\x9c* `\xe7\xa0\x81` "     // 段落 **粗** *斜* `码`
           "==\xe9\xab\x98== ~~\xe5\x88\xa0~~ [\xe9\x93\xbe](https://example.com)\xe3\x80\x82\n\n";
    doc += "## \xe5\x88\x97\xe8\xa1\xa8\n\n- \xe7\x94\xb2\n- \xe4\xb9\x99\n  - \xe4\xb9\x99\xe4\xb8\x80\n- \xe4\xb8\x99\n\n";
    doc += "1. \xe4\xb8\x80\n2. \xe4\xba\x8c\n\n";
    doc += "- [x] \xe5\xae\x8c\xe6\x88\x90\n- [ ] \xe6\x9c\xaa\xe5\xae\x8c\xe6\x88\x90\n\n";
    doc += "> \xe5\xbc\x95\xe7\x94\xa8\n\n";
    doc += "```cpp\nint main() { return 0; }\n```\n\n";
    doc += "| \xe5\x88\x97\xe7\x94\xb2 | \xe5\x88\x97\xe4\xb9\x99 |\n| --- | --- |\n| \xe5\x80\xbc\xe4\xb8\x80 | \xe5\x80\xbc\xe4\xba\x8c |\n\n";
    doc += "---\n\n\xe7\xbb\x93\xe5\xb0\xbe\xe6\xae\xb5\xe8\x90\xbd\xe3\x80\x82\n";
    for (int i = 0; i < 40; ++i) {
        doc += "\xe6\x94\xb6\xe5\xb0\xbe\xe4\xb9\x8b\xe5\x90\x8e\xe7\x9a\x84\xe5\xa1\xab\xe5\x85\x85"
               "\xe6\xae\xb5\xe8\x90\xbd\xef\xbc\x8c\xe7\xbb\x99\xe5\xa4\xa7\xe7\xba\xb2\xe8\xb7\xb3"
               "\xe8\xbd\xac\xe7\x95\x99\xe5\x87\xba\xe7\xa9\xba\xe9\x97\xb4\xe3\x80\x82\n\n";
    }
    write_text(sink, doc);

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, sink));
    ASSERT_EQ(d.get("hd0", "text"), "\xe6\xa0\x87\xe9\xa2\x98");  // 标题
    ASSERT_EQ(d.get("hd1", "text"), "\xe5\x88\x97\xe8\xa1\xa8");  // 列表
    EXPECT_TRUE(d.laid("mdb2"));
    EXPECT_TRUE(d.laid("mdh1"));

    // Every view keeps the source and the counts; the preview's blocks stay reachable.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "2", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-source"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "3", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-split"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "4", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-preview"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "1", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-live"));
    EXPECT_EQ(d.get("mdc-a-source", "value"), doc);

    // Type at the end; the preview's last block follows.
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(doc.size()) + "," + std::to_string(doc.size()));
    ASSERT_TRUE(d.rig.type("mdc-a-source", "\n\xe8\xa1\xa5\xe5\x86\x99\xe7\x9a\x84\xe6\xae\xb5\xe8\x90\xbd\xe3\x80\x82\n"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    // The typed paragraph is far below the fold, and the re-stamped paper is laid out a frame
    // later: scroll to it and let the frames the census needs arrive (the paragraph is only in the
    // layout once the window reaches it).
    d.rig.app.document().set("mdc-a-scroll", "scroll_top", "100000");
    EXPECT_TRUE(until(d, [&] {
        return zui::test::any_text(d.rig.app, "p", "",
                                   "\xe8\xa1\xa5\xe5\x86\x99\xe7\x9a\x84\xe6\xae\xb5\xe8\x90\xbd");  // 补写的段落
    })) << "the new paragraph never reached the preview";

    // The find bar counts the table cell and closes again.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "f", true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.type("zmd-find-q", "\xe5\x80\xbc\xe4\xba\x8c"));  // 值二
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-find-count", "text"), "1 / 1");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-find", "visible"), "false");

    // The outline takes the clicked row to its heading.
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "2", true, true));  // Ctrl+Shift+2 pins it
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pin-r"));
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> panel = d.box("zmd-float-r");
        return panel && panel->border.x < 1100.f;
    })) << "the outline panel never settled on screen";
    const float before = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    ASSERT_TRUE(click_settled(d, "hd1"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(std::stof(d.get("mdc-a-scroll", "scroll_top")), before)
        << "the outline row did not scroll";
    {
        const std::optional<zui::ElementBox> heading = d.box("mdh1");
        const std::optional<zui::ElementBox> scroller = d.box("mdc-a-scroll");
        ASSERT_TRUE(heading && heading->laid && scroller && scroller->laid);
        EXPECT_GE(heading->border.y, scroller->border.y - 1.f);
        EXPECT_LE(heading->border.y + heading->border.height,
                  scroller->border.y + scroller->border.height + 1.f)
            << "the heading stayed out of view";
    }

    // Ctrl+S writes the typed paragraph through to the file, and a reopen reads it back.
    const std::string typed = d.get("mdc-a-source", "value");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "s", true));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(slurp(sink), typed);
    ASSERT_TRUE(d.reopen(sink));
    EXPECT_EQ(d.get("mdc-a-source", "value"), typed);
    EXPECT_EQ(d.get("hd1", "text"), "\xe5\x88\x97\xe8\xa1\xa8");
}

// A drag in the writing view that parks the pointer below the paper keeps the paper moving. The
// browser's contenteditable scrolls while the button stays down and the selection follows the text
// that arrives; the app's own drag only extended on a pointer move, so a parked pointer sat there
// while nothing happened.
TEST(AppInteract, ZmdWritingViewDragPastTheFoldKeepsScrolling) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    std::string text;
    for (int i = 0; i < 120; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    ASSERT_TRUE(replace_source(d, text));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.app.document().set("mdc-a-scroll", "scroll_top", "0"));
    ASSERT_TRUE(d.rig.pump());
    const std::optional<zui::ElementBox> pane = d.box("mdc-a-scroll");
    const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
    ASSERT_TRUE(pane && pane->laid);
    ASSERT_TRUE(paper && paper->laid);
    const float x = paper->border.x + 40.f;
    d.rig.window().pointer_.x = x;
    d.rig.window().pointer_.y = paper->border.y + 16.f;
    d.rig.window().pointer_.left_down = true;
    d.rig.window().pointer_.left_pressed = true;
    d.rig.window().pointer_.buttons = 1;
    d.rig.window().inject(zui::backends::TextInputFrame{});  // keep poll() from clearing the edges
    ASSERT_TRUE(d.rig.pump());
    // The pointer leaves the pane and stays there: the interval the drag armed has to scroll.
    d.rig.window().pointer_.y = pane->border.y + pane->border.height + 40.f;
    d.rig.window().pointer_.left_pressed = false;
    d.rig.window().inject(zui::backends::TextInputFrame{});
    ASSERT_TRUE(d.rig.pump());
    float top = 0.f;
    for (int i = 0; i < 12; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        d.rig.window().inject(zui::backends::TextInputFrame{});
        ASSERT_TRUE(d.rig.pump());
        top = std::stof(d.get("mdc-a-scroll", "scroll_top"));
        if (top > 100.f) {
            break;
        }
    }
    EXPECT_GT(top, 100.f) << "the paper did not move under the parked pointer";
    EXPECT_FALSE(d.get("mdc-a-source", "selection").empty())
        << "the drag selected nothing while the paper scrolled";
    d.rig.window().pointer_.left_down = false;
    d.rig.window().pointer_.left_released = true;
    d.rig.window().inject(zui::backends::TextInputFrame{});
    ASSERT_TRUE(d.rig.pump());
    d.rig.window().pointer_.left_released = false;
    d.rig.window().inject(zui::backends::TextInputFrame{});
    ASSERT_TRUE(d.rig.pump());
    const float parked = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        d.rig.window().inject(zui::backends::TextInputFrame{});
        ASSERT_TRUE(d.rig.pump());
    }
    EXPECT_FLOAT_EQ(std::stof(d.get("mdc-a-scroll", "scroll_top")), parked)
        << "the paper kept moving after the release";
}

// PageUp / PageDown in 写作 move the paper. The writing surface is the 24px keyboard proxy at the
// paper's top, and the browser's writing surface is a contenteditable: the same key scrolls the
// page there. The app has to do it, because the proxy's own scroll is invisible.
TEST(AppInteract, ZmdWritingViewPageKeysScrollThePaper) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    std::string text;
    for (int i = 0; i < 120; ++i) {
        text += "para-" + std::to_string(i) + "\n\n";
    }
    ASSERT_TRUE(replace_source(d, text));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.app.document().set("mdc-a-scroll", "scroll_top", "0"));
    ASSERT_TRUE(d.rig.pump());
    const float before = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "PageDown"));
    ASSERT_TRUE(d.rig.pump());
    const float after = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    EXPECT_GT(after, before + 100.f) << before << " -> " << after;
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "PageUp"));
    ASSERT_TRUE(d.rig.pump());
    const float back = std::stof(d.get("mdc-a-scroll", "scroll_top"));
    EXPECT_LT(back, after - 100.f) << after << " -> " << back;
}

// Typing deep in a 源码 document keeps the caret in view. The pane is 837px tall and the field is
// 4500px: a browser scrolls the nearest scrollport to the caret on every keystroke, and an editor
// that does not leaves the writer typing blind below the fold. The whole-source replace leaves the
// caret at the end, so the follow has to happen for that edit too.
TEST(AppInteract, ZmdSourceTypingKeepsTheCaretInView) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe6\xba\x90\xe7\xa0\x81"));  // 源码
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.click("cm0"));
    ASSERT_TRUE(d.rig.pump());
    std::string text;
    for (int i = 0; i < 200; ++i) {
        text += "line-" + std::to_string(i) + "\n";
    }
    ASSERT_TRUE(replace_source(d, text));
    EXPECT_GT(std::stof(d.get("mdc-a-scroll", "scroll_top")), 100.f)
        << "the replace left the caret at the end of a 4500px field";
    ASSERT_TRUE(d.rig.type("mdc-a-source", "X"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::optional<zui::ElementBox> pane = d.box("mdc-a-scroll");
    const std::optional<zui::ElementBox> field = d.box("mdc-a-source");
    ASSERT_TRUE(pane && pane->laid);
    ASSERT_TRUE(field && field->laid);
    EXPECT_GT(std::stof(d.get("mdc-a-scroll", "scroll_top")), 100.f);
    // The frame paints the caret as the only 1px column inside the focused field: it has to sit
    // inside the pane, which is what the follow is for.
    bool found = false;
    float bar_y = 0.f;
    float bar_h = 0.f;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.rect.w > 1.01f ||
            item.rect.h < 8.f || item.color.a != 255) {
            continue;
        }
        if (item.rect.x < field->border.x ||
            item.rect.x > field->border.x + field->border.width) {
            continue;
        }
        found = true;
        bar_y = item.rect.y;
        bar_h = item.rect.h;
    }
    ASSERT_TRUE(found) << "no caret bar painted for the source field";
    EXPECT_GE(bar_y, pane->border.y - 1.f) << "the caret is above the fold: " << bar_y;
    EXPECT_LE(bar_y + bar_h, pane->border.y + pane->border.height + 1.f)
        << "the caret is below the fold: " << bar_y + bar_h;
}

TEST(AppInteract, ZmdFilesRoundTripOnDisk) {
    const fs::path dir = fs::temp_directory_path() /
                         ("zmd-daily-files-" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const fs::path alpha = dir / "alpha.md";
    const fs::path beta = dir / "beta.md";
    write_text(alpha, "# Alpha\n\nalpha-body\n");
    write_text(beta, "# Beta\n\nbeta-body\n");

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, alpha));
    EXPECT_NE(d.get("mdc-a-source", "value").find("alpha-body"), std::string::npos);
    EXPECT_NE(d.get("fl0", "text").find("alpha.md"), std::string::npos);

    const int beta_row = find_row(d, "fl", "beta.md");
    ASSERT_GE(beta_row, 0);
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(beta_row)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("beta-body"), std::string::npos);

    ASSERT_TRUE(d.rig.type("mdc-a-source", "AUTOSAVE-MARK\n"));
    EXPECT_NE(slurp(beta).find("AUTOSAVE-MARK"), std::string::npos);

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-file-new"));
    EXPECT_TRUE(d.get("mdc-a-source", "value").empty());
    const fs::path created = dir / fs::u8path("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d.md");
    ASSERT_TRUE(fs::exists(created));
    ASSERT_TRUE(d.rig.type("mdc-a-source", "ROUND-TRIP-BODY\n"));
    EXPECT_EQ(slurp(created), "ROUND-TRIP-BODY\n");

    ASSERT_TRUE(d.reopen(created));
    EXPECT_EQ(d.get("mdc-a-source", "value"), "ROUND-TRIP-BODY\n");

    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    EXPECT_TRUE(d.laid("zmd-pane-file"));
    ASSERT_TRUE(d.rig.click("zmd-autosave"));
    EXPECT_EQ(d.get("zmd-autosave", "checked"), "false");
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());

    ASSERT_TRUE(d.rig.type("mdc-a-source", "DIRTY-MARK\n"));
    EXPECT_EQ(slurp(created).find("DIRTY-MARK"), std::string::npos);
    EXPECT_NE(d.get("mdc-a-source", "value").find("DIRTY-MARK"), std::string::npos);

    const int alpha_row = find_row(d, "fl", "alpha.md");
    ASSERT_GE(alpha_row, 0);
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(alpha_row)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("zmd-dlg-unsaved"));
    ASSERT_TRUE(d.rig.click("zmd-unsaved-cancel"));
    EXPECT_FALSE(d.laid("zmd-dlg-unsaved"));
    EXPECT_NE(d.get("mdc-a-source", "value").find("DIRTY-MARK"), std::string::npos);

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(alpha_row)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("zmd-dlg-unsaved"));
    ASSERT_TRUE(d.rig.click("zmd-unsaved-discard"));
    EXPECT_FALSE(d.laid("zmd-dlg-unsaved"));
    EXPECT_NE(d.get("mdc-a-source", "value").find("alpha-body"), std::string::npos);
    EXPECT_EQ(d.get("mdc-a-source", "value").find("DIRTY-MARK"), std::string::npos);

    ASSERT_TRUE(d.rig.type("mdc-a-source", "SAVE-MARK\n"));
    const int beta_again = find_row(d, "fl", "beta.md");
    ASSERT_GE(beta_again, 0);
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(beta_again)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.laid("zmd-dlg-unsaved"));
    ASSERT_TRUE(d.rig.click("zmd-unsaved-save"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(slurp(alpha).find("SAVE-MARK"), std::string::npos);
    EXPECT_NE(d.get("mdc-a-source", "value").find("beta-body"), std::string::npos);

    ASSERT_TRUE(replace_source(d, "# \xe5\xaf\xbc\xe5\x87\xba\xe6\xa0\x87\xe9\xa2\x98\n\n\xe5\xaf\xbc\xe5\x87\xba\xe6\xad\xa3\xe6\x96\x87\n"));
    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-export"));
    EXPECT_TRUE(d.laid("zmd-pane-export"));
    ASSERT_TRUE(d.rig.click("zmd-export-html"));
    const std::string html = slurp(dir / "beta.html");
    EXPECT_NE(html.find("<h1"), std::string::npos) << html;
    EXPECT_NE(html.find("\xe5\xaf\xbc\xe5\x87\xba\xe6\xa0\x87\xe9\xa2\x98"), std::string::npos) << html;

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-folder-new"));
    EXPECT_GE(find_row(d, "fl", "\xe6\x96\xb0\xe5\xbb\xba\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9"), 0);

    // A directory argument opens the welcome sample with no path, so autosave
    // cannot store an edit. Closing that window has to ask.
    auto contains_mark = [&](std::string_view mark) {
        for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && slurp(entry.path()).find(mark) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    {
        Daily leave;
        ASSERT_TRUE(leave.boot(1200, 900, dir));
        EXPECT_NE(leave.get("mdc-a-source", "value").find(
                      "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8\xe5\xb0\x8f\xe7\xab\xb9 Markdown"),
                  std::string::npos);
        ASSERT_TRUE(leave.rig.type("mdc-a-source", "QUIT-MARK\n"));
        ASSERT_TRUE(leave.rig.request_close());
        EXPECT_TRUE(leave.laid("zmd-dlg-unsaved"));
        ASSERT_TRUE(leave.rig.click("zmd-unsaved-cancel"));
        EXPECT_FALSE(leave.laid("zmd-dlg-unsaved"));
        EXPECT_NE(leave.get("mdc-a-source", "value").find("QUIT-MARK"), std::string::npos);
        ASSERT_TRUE(leave.rig.pump());

        ASSERT_TRUE(leave.rig.request_close());
        EXPECT_TRUE(leave.laid("zmd-dlg-unsaved"));
        leave.rig.click("zmd-unsaved-discard");
        EXPECT_NE(leave.get("mdc-a-save", "data-state"), "saved");
        EXPECT_NE(leave.get("mdc-a-source", "value").find("QUIT-MARK"), std::string::npos);
        EXPECT_FALSE(leave.rig.pump());
        EXPECT_FALSE(contains_mark("QUIT-MARK"));
    }
    {
        Daily leave;
        ASSERT_TRUE(leave.boot(1200, 900, dir));
        ASSERT_TRUE(leave.rig.type("mdc-a-source", "SAVE-QUIT\n"));
        ASSERT_TRUE(leave.rig.request_close());
        EXPECT_TRUE(leave.laid("zmd-dlg-unsaved"));
        leave.rig.click("zmd-unsaved-save");
        EXPECT_FALSE(leave.rig.pump());
        EXPECT_TRUE(contains_mark("SAVE-QUIT"));
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(AppInteract, ZmdChromePaintsAndRestores) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_FALSE(d.guard.dir.empty());

    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-type"));
    EXPECT_TRUE(zui::test::class_token(d.get("zmd-pane-type", "class"), "is-active"));

    ASSERT_TRUE(click_edge(d, "zmd-width", false));
    const std::string narrow_token = width_token(d.get("mdc-a", "class"));
    const std::optional<zui::ElementBox> narrow = d.box("mdc-a-preview");
    ASSERT_TRUE(narrow && narrow->laid);
    ASSERT_TRUE(click_edge(d, "zmd-width", true));
    const std::string wide_token = width_token(d.get("mdc-a", "class"));
    const std::optional<zui::ElementBox> wide = d.box("mdc-a-preview");
    ASSERT_TRUE(wide && wide->laid);
    EXPECT_NE(narrow_token, wide_token);
    EXPECT_GT(std::fabs(wide->border.width - narrow->border.width), 100.f)
        << narrow->border.width << " -> " << wide->border.width;

    ASSERT_TRUE(click_edge(d, "zmd-size", true));
    EXPECT_NE(d.get("zmd-size-val", "text").find("18"), std::string::npos)
        << d.get("zmd-size-val", "text");
    ASSERT_TRUE(click_edge(d, "zmd-line", false));
    EXPECT_NE(d.get("zmd-line-val", "text").find("1.4"), std::string::npos)
        << d.get("zmd-line-val", "text");

    ASSERT_TRUE(d.rig.click("zmd-font-sans"));
    ASSERT_TRUE(d.rig.click("zmd-font-serif"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "mdw-serif"));

    // The status-bar focus button sits under the settings scrim. The switch in
    // the open type pane is the one that can turn focus on while the sheet is up.
    ASSERT_TRUE(d.rig.click("zmd-focus-sw"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-focusing"));

    ASSERT_TRUE(d.rig.click("zmd-tab-look"));
    EXPECT_TRUE(d.laid("zmd-pane-look"));
    EXPECT_FALSE(d.laid("zmd-pane-type"));
    EXPECT_TRUE(zui::test::class_token(d.get("zmd-pane-look", "class"), "is-active"));
    EXPECT_EQ(d.get("zmd-tab-look", "text"), "\xe5\xa4\x96\xe8\xa7\x82");
    EXPECT_TRUE(d.laid("zmd-theme-light"));
    ASSERT_TRUE(d.rig.click("zmd-theme-light"));
    ASSERT_TRUE(d.rig.click("zmd-theme-system"));
    ASSERT_TRUE(d.rig.click("zmd-theme-dark"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "theme-dark"));

    // 强调色 and 窗口圆角 are steps too, and each writes the snapped value back to its control.
    ASSERT_TRUE(click_edge(d, "zmd-accent", true));
    const std::string accent = d.get("zmd-accent", "value");
    ASSERT_EQ(accent.size(), 7u) << accent;
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "acc-" + accent.substr(1)))
        << accent << " left " << d.get("mdc-a", "class");
    ASSERT_TRUE(click_edge(d, "zmd-radius", false));
    EXPECT_EQ(d.get("zmd-radius-val", "text"), "0 px");
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "rad0"));
    ASSERT_TRUE(click_edge(d, "zmd-radius", true));
    EXPECT_EQ(d.get("zmd-radius-val", "text"), "24 px");
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "rad24"));
    // The class really feeds the custom property the sheet reads.
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdc-a", "--w-radius"), "24px");
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdc-a", "--w-accent"), accent);

    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    EXPECT_TRUE(d.laid("zmd-pane-file"));
    const fs::path alt = fs::temp_directory_path() /
                         ("zmd-daily-ws-" + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(alt);
    ASSERT_TRUE(d.rig.app.document().focus("zmd-ws"));
    ASSERT_TRUE(d.rig.pump());
    const std::string ws_now = d.get("zmd-ws", "value");
    if (!ws_now.empty()) {
        d.rig.app.document().set("zmd-ws", "select", "0," + std::to_string(ws_now.size()));
    }
    const std::string alt_text = alt.u8string();
    zui::backends::TextInputFrame ws_frame;
    ws_frame.text = alt_text;
    d.rig.window().inject(std::move(ws_frame));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-ws", "value"), alt_text);

    ASSERT_TRUE(d.rig.type("zmd-assets", "-extra"));
    const std::string assets = d.get("zmd-assets", "value");
    EXPECT_NE(assets.find("-extra"), std::string::npos);
    ASSERT_TRUE(d.rig.click("zmd-new-here"));
    EXPECT_EQ(d.get("zmd-new-here", "aria-pressed"), "true");
    ASSERT_TRUE(d.rig.click("zmd-new-root"));
    EXPECT_EQ(d.get("zmd-new-root", "aria-pressed"), "true");

    ASSERT_TRUE(d.rig.click("zmd-tab-keys"));
    EXPECT_TRUE(d.laid("zmd-pane-keys"));
    EXPECT_TRUE(zui::test::class_token(d.get("zmd-pane-keys", "class"), "is-active"));
    ASSERT_TRUE(click_corner(d, "zmd-scrim"));
    EXPECT_FALSE(d.laid("mdc-a-set"));

    // Drive the status button once the scrim is gone, and leave focus on.
    ASSERT_TRUE(d.rig.click("zmd-focus"));
    ASSERT_TRUE(d.rig.click("zmd-focus"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-focusing"));

    const float wide_width = wide->border.width;
    ASSERT_TRUE(d.reopen());
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "mdw-serif"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "is-focusing"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "theme-dark"));
    EXPECT_EQ(width_token(d.get("mdc-a", "class")), wide_token);
    EXPECT_EQ(d.get("zmd-assets", "value"), assets);
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "rad24"));
    EXPECT_EQ(d.get("zmd-accent", "value"), accent);
    const std::optional<zui::ElementBox> restored = d.box("mdc-a-preview");
    ASSERT_TRUE(restored && restored->laid);
    EXPECT_TRUE(near(restored->border.width, wide_width, 8.f))
        << restored->border.width << " vs " << wide_width;

    // Unpinned floats sit off the page. Hover brings the file panel in; pinning
    // keeps it there after the pointer leaves.
    d.rig.point(600.f, 450.f);
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        return floated && floated->border.x < -1.f;
    }));
    const std::optional<zui::ElementBox> hot = d.box("zmd-hot-l");
    const float hot_x = hot ? hot->border.x + 4.f : 8.f;
    const float hot_y = hot ? hot->border.y + hot->border.height * 0.5f : 280.f;
    d.rig.point(hot_x, hot_y);
    float hover_x = hot ? hot->border.x : -9999.f;
    float hover_y = hot ? hot->border.height : -9999.f;
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        if (floated) {
            hover_x = floated->border.x;
        }
        return floated && floated->border.x > -1.f && floated->border.x < 300.f;
    })) << "float " << hover_x << " hot " << hot_x << "," << hot_y << " h " << hover_y
        << " laid " << d.laid("zmd-hot-l");
    d.rig.point(600.f, 450.f);
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        return floated && floated->border.x < -1.f;
    }));
    // The pin sits on the panel, so it is only hittable once the strip has
    // slid back in. Leaving the pointer there keeps :hover through the click.
    d.rig.point(hot_x, hot_y);
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        return floated && floated->border.x > -1.f && floated->border.x < 300.f;
    }));
    ASSERT_TRUE(d.rig.click("zmd-pin-files"));
    d.rig.point(600.f, 450.f);
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-l");
        return floated && floated->border.x > -1.f && floated->border.x < 300.f;
    }));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pin-l"));

    std::string long_doc = "# \xe9\xa6\x96\xe8\x8a\x82\xe6\xa0\x87\xe9\xa2\x98\n\n";
    for (int i = 0; i < 40; ++i) {
        long_doc += "\xe5\xa1\xab\xe5\x85\x85\xe6\xae\xb5\xe8\x90\xbd\xe7\x94\xa8\xe6\x9d\xa5\xe6\x8a\x8a"
                    "\xe6\x9c\xab\xe8\x8a\x82\xe6\xa0\x87\xe9\xa2\x98\xe6\x8e\xa8\xe5\x88\xb0\xe8\xa7\x86"
                    "\xe5\x8f\xa3\xe4\xb8\x8b\xe9\x9d\xa2\xe3\x80\x82\n\n";
    }
    long_doc += "## \xe6\x9c\xab\xe8\x8a\x82\xe6\xa0\x87\xe9\xa2\x98\n\n\xe7\xbb\x93\xe5\xb0\xbe\n";
    // Room under 末节标题 for the outline jump: with a short tail the jump clamps at the
    // bottom, the heading never reaches the reading anchor, and the outline keeps the row
    // above current - the prototype's own `syncOutline` rule, not a bug to pin here.
    for (int i = 0; i < 40; ++i) {
        long_doc += "\xe5\xa1\xab\xe5\x85\x85\xe6\xae\xb5\xe8\x90\xbd\xe4\xb9\x8b\xe5\x90\x8e\xe3\x80\x82\n\n";
    }
    ASSERT_TRUE(replace_source(d, long_doc));
    ASSERT_TRUE(d.rig.click("zmd-view-preview"));
    EXPECT_EQ(d.get("hd0", "text"), "\xe9\xa6\x96\xe8\x8a\x82\xe6\xa0\x87\xe9\xa2\x98");
    EXPECT_EQ(d.get("hd1", "text"), "\xe6\x9c\xab\xe8\x8a\x82\xe6\xa0\x87\xe9\xa2\x98");

    const std::optional<zui::ElementBox> hot_r = d.box("zmd-hot-r");
    const float hot_rx = hot_r ? hot_r->border.x + hot_r->border.width * 0.5f : 1180.f;
    const float hot_ry = hot_r ? hot_r->border.y + hot_r->border.height * 0.5f : 450.f;
    d.rig.point(hot_rx, hot_ry);
    // x < 1100 is true while the pin is still past the right edge of a 1200px
    // window. Wait until the whole panel, pin included, is on screen.
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-r");
        return floated && floated->border.x > 900.f &&
               floated->border.x + floated->border.width < 1196.f;
    }));
    ASSERT_TRUE(d.rig.click("zmd-pin-outline"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pin-r"));
    d.rig.point(600.f, 450.f);
    ASSERT_TRUE(until(d, [&] {
        const std::optional<zui::ElementBox> floated = d.box("zmd-float-r");
        return floated && floated->border.x < 1100.f;
    }));

    // The row jumps the paper to its heading. Start from the top so the jump has one direction to
    // prove: a replace of the whole source leaves the caret at its end, and the paper follows the
    // caret, so the click would otherwise travel up to a heading above it.
    ASSERT_TRUE(d.rig.click("zmd-to-top"));
    ASSERT_TRUE(d.rig.pump());
    const float before_scroll = std::strtof(d.get("mdc-a-scroll", "scroll_top").c_str(), nullptr);
    ASSERT_TRUE(d.rig.click("hd1"));
    ASSERT_TRUE(d.rig.pump());
    const float after_scroll = std::strtof(d.get("mdc-a-scroll", "scroll_top").c_str(), nullptr);
    EXPECT_GT(after_scroll, before_scroll + 40.f) << before_scroll << " -> " << after_scroll;
    EXPECT_TRUE(zui::test::class_token(d.get("hd1", "class"), "is-active"));

    ASSERT_TRUE(d.rig.click("zmd-to-top"));
    ASSERT_TRUE(d.rig.pump());
    const float topped = std::strtof(d.get("mdc-a-scroll", "scroll_top").c_str(), nullptr);
    EXPECT_LT(topped, 8.f) << topped;
    EXPECT_TRUE(zui::test::class_token(d.get("hd0", "class"), "is-active"));

    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    EXPECT_FALSE(zui::test::class_token(d.get("mdc-a", "class"), "pal-closed"));
    ASSERT_TRUE(click_corner(d, "zmd-pal-scrim"));
    EXPECT_TRUE(zui::test::class_token(d.get("mdc-a", "class"), "pal-closed"));

    ASSERT_TRUE(d.rig.click("zmd-pal-open"));
    ASSERT_TRUE(d.rig.type("mdc-a-pal-input", "\xe6\xba\x90\xe7\xa0\x81"));
    EXPECT_NE(d.get("cm0", "text").find("\xe6\xba\x90\xe7\xa0\x81"), std::string::npos)
        << d.get("cm0", "text");
    EXPECT_FALSE(d.laid("cm1"));
    ASSERT_TRUE(d.rig.click("cm0"));
    EXPECT_TRUE(zui::test::class_token(d.get("md-editor-mock", "class"), "view-source"));

    std::error_code ec;
    fs::remove_all(alt, ec);
}

/// How many `tag` elements the paper holds. `load_fragment` keeps old nodes the new render no
/// longer names, so a click that appends left the render's wrappers (`<blockquote>`, `<ul>`,
/// `<hr>`) behind and the count grew with every click.
int preview_tags(Daily& d, std::string_view tag) {
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    int count = 0;
    for (std::size_t i = 0; i < census.size(); ++i) {
        if (census[i].tag != tag) {
            continue;
        }
        bool in_preview = census[i].id == "mdc-a-preview";
        for (int p = census[i].parent; p >= 0 && !in_preview;
             p = census[static_cast<std::size_t>(p)].parent) {
            in_preview = census[static_cast<std::size_t>(p)].id == "mdc-a-preview";
        }
        if (in_preview) {
            ++count;
        }
    }
    return count;
}

std::size_t field_size(Daily& d, std::string_view id, std::string_view field) {
    return static_cast<std::size_t>(std::strtoull(d.get(id, field).c_str(), nullptr, 10));
}

// A click in the writing view re-stamps the paper: the render plants the caret marker in the
// clicked block. `load_fragment` only takes the old nodes the new render still names, so every
// click used to leave the previous render's wrappers behind at the top of the paper - an empty
// `<blockquote>`, empty `<ul>`s, a stray `<hr>` - and another set stacked with the next click.
// The paper grew a blank strip above the heading, every block moved down under it, and the next
// click mapped a layout the user was no longer looking at.
TEST(AppInteract, ZmdWriteClickKeepsThePaperWhole) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_EQ(preview_tags(d, "blockquote"), 1);
    ASSERT_EQ(preview_tags(d, "hr"), 1);
    const std::optional<zui::ElementBox> heading = d.box("mdh0");
    ASSERT_TRUE(heading && heading->laid);

    // Three clicks, three different lines: the caret lands in the line that was clicked and the
    // paper keeps its shape - no block climbs in above the heading.
    const char* lines[] = {"mdb1", "mdb3", "mdb4"};
    for (const char* id : lines) {
        const std::optional<zui::ElementBox> box = d.box(id);
        ASSERT_TRUE(box && box->laid) << id;
        ASSERT_TRUE(d.rig.at(box->border.x + 60.f, box->border.y + box->border.height * 0.5f))
            << id;
        ASSERT_TRUE(d.rig.pump());
        ASSERT_TRUE(d.rig.pump());
        const std::size_t beg = field_size(d, id, "data-beg");
        const std::size_t end = field_size(d, id, "data-at");
        const std::size_t caret = field_size(d, "mdc-a-source", "caret");
        EXPECT_GE(caret, beg) << id << " caret " << caret << " outside [" << beg << "," << end
                              << ")";
        EXPECT_LE(caret, end) << id << " caret " << caret << " outside [" << beg << "," << end
                              << ")";
        EXPECT_EQ(preview_tags(d, "blockquote"), 1) << id;
        EXPECT_EQ(preview_tags(d, "hr"), 1) << id;
        const std::optional<zui::ElementBox> heading_now = d.box("mdh0");
        ASSERT_TRUE(heading_now && heading_now->laid) << id;
        EXPECT_FLOAT_EQ(heading_now->border.y, heading->border.y) << id;
    }
}

/// The default accent `app.css` hands the marker bar (`--w-accent`), as rgb.
bool accent_rgb(Daily& d, int& r, int& g, int& b) {
    const std::string value = zui::frame::Access::computed(d.rig.app, "mdc-a", "--w-accent");
    if (value.size() != 7 || value[0] != '#') {
        return false;
    }
    const auto hex = [&](std::size_t at) {
        return static_cast<int>(std::strtol(value.substr(at, 2).c_str(), nullptr, 16));
    };
    r = hex(1);
    g = hex(3);
    b = hex(5);
    return true;
}

// The writing view's source box is a 2%-opacity keyboard/IME proxy parked at the paper's corner:
// its own caret bar used to paint at full strength there - a second caret in the wrong place -
// while the caret marker the render plants in the clicked block sat where the user was writing.
// The proxy asks for no caret (`caret-color: transparent`); the paper's marker is the caret.
TEST(AppInteract, ZmdWritingViewPaintsOneCaret) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(zui::frame::Access::computed(d.rig.app, "mdc-a-source", "caret-color"),
              "transparent");

    const std::optional<zui::ElementBox> line = d.box("mdb1");
    ASSERT_TRUE(line && line->laid);
    ASSERT_TRUE(d.rig.at(line->border.x + 60.f, line->border.y + line->border.height * 0.5f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::optional<zui::TextCaret> marker = paper_bar(d);
    ASSERT_TRUE(marker);
    // The visible caret sits on the line that was clicked.
    EXPECT_GE(marker->y + marker->height, line->border.y);
    EXPECT_LE(marker->y, line->border.y + line->border.height);

    int r = 0;
    int g = 0;
    int b = 0;
    ASSERT_TRUE(accent_rgb(d, r, g, b));
    const std::optional<zui::ElementBox> source = d.box("mdc-a-source");
    ASSERT_TRUE(source && source->laid);
    int proxy_carets = 0;
    int marker_bars = 0;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.color.a != 255) {
            continue;
        }
        if (item.color.r != r || item.color.g != g || item.color.b != b) {
            continue;
        }
        if (item.rect.h < 8.f || item.rect.w > 3.f) {
            continue;
        }
        const float cx = item.rect.x + item.rect.w * 0.5f;
        const float cy = item.rect.y + item.rect.h * 0.5f;
        const bool in_proxy = cx >= source->border.x && cy >= source->border.y &&
                              cx <= source->border.x + source->border.width &&
                              cy <= source->border.y + source->border.height;
        if (in_proxy) {
            ++proxy_carets;
        } else {
            ++marker_bars;
        }
    }
    EXPECT_EQ(proxy_carets, 0) << "the hidden source box still paints a caret";
    EXPECT_GE(marker_bars, 1) << "the paper's caret marker is missing";
}

// The bar follows the caret. The render used to plant it at the block's edge - the first or the
// last character of the paragraph, whichever half the offset sat in - so clicking or typing in
// the middle of a line put the caret somewhere else on screen. The inline pass now emits it in
// front of the byte the offset names, and only offsets it never emits fall back to the edge.
TEST(AppInteract, ZmdWritingViewPutsTheCaretWhereTheOffsetIs) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    auto place_caret = [&](std::size_t at) {
        d.rig.app.document().set("mdc-a-source", "select",
                                 std::to_string(at) + "," + std::to_string(at));
        // An arrow key re-stamps the paper (`on_accel`), which is where the marker lives.
        zui::frame::Access::key(d.rig.app, "ArrowRight");
        return d.rig.pump() && d.rig.pump();
    };
    ASSERT_TRUE(place_caret(0));
    const std::optional<zui::TextCaret> at_start = paper_bar(d);
    ASSERT_TRUE(at_start);
    const std::optional<zui::ElementBox> heading = d.box("mdh0");
    ASSERT_TRUE(heading && heading->laid);
    // The caret at the line's start ("#" of "# 欢迎...") shows at the heading's first character.
    EXPECT_NEAR(at_start->x, heading->border.x, 8.f);
    EXPECT_GE(at_start->y + at_start->height, heading->border.y);
    EXPECT_LE(at_start->y, heading->border.y + heading->border.height);

    // Past "欢迎使用" (12 UTF-8 bytes, so offset 14 is the next character): the bar is on the
    // heading's own line, well to the right of the edge marker - not back at the line's start.
    ASSERT_TRUE(place_caret(14));
    const std::optional<zui::TextCaret> at_mid = paper_bar(d);
    ASSERT_TRUE(at_mid);
    EXPECT_GT(at_mid->x, at_start->x + 20.f);
    EXPECT_GE(at_mid->y + at_mid->height, heading->border.y);
    EXPECT_LE(at_mid->y, heading->border.y + heading->border.height);

    // And the line after it stays clear: the marker did not drift into the next block.
    const std::optional<zui::ElementBox> body = d.box("mdb0");
    ASSERT_TRUE(body && body->laid);
    EXPECT_LT(at_mid->y, body->border.y);
}

// The caret is painted on the line, not laid into it. Moving it onto the title must not
// shift that line: an in-flow bar grew h1 (line-height 1.35) and the whole heading jumped.
TEST(AppInteract, ZmdHeadingStaysPutWhenTheCaretArrives) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::string welcome = "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8";  // 欢迎使用
    auto welcome_pen = [&](float& x, float& y) {
        bool found = false;
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::DrawText) {
                continue;
            }
            if (item.text.find(welcome) == std::string::npos) {
                continue;
            }
            x = item.rect.x;
            y = item.rect.y;
            found = true;
        }
        return found;
    };
    auto place_caret = [&](std::size_t at) {
        d.rig.app.document().set("mdc-a-source", "select",
                                 std::to_string(at) + "," + std::to_string(at));
        zui::frame::Access::key(d.rig.app, "ArrowRight");
        return d.rig.pump() && d.rig.pump();
    };

    // Offset 40 sits in the paragraph under the title, so the heading has no bar.
    ASSERT_TRUE(place_caret(40));
    const std::optional<zui::ElementBox> heading0 = d.box("mdh0");
    ASSERT_TRUE(heading0 && heading0->laid);
    float text_x = 0.f;
    float text_y = 0.f;
    ASSERT_TRUE(welcome_pen(text_x, text_y));

    // Offset 2 is the first visible character of "# 欢迎使用小竹 Markdown".
    ASSERT_TRUE(place_caret(2));
    const std::optional<zui::ElementBox> heading1 = d.box("mdh0");
    ASSERT_TRUE(heading1 && heading1->laid);
    float text_x1 = 0.f;
    float text_y1 = 0.f;
    ASSERT_TRUE(welcome_pen(text_x1, text_y1));
    EXPECT_NEAR(heading1->border.y, heading0->border.y, 0.5f);
    EXPECT_NEAR(heading1->border.height, heading0->border.height, 0.5f);
    EXPECT_NEAR(text_x1, text_x, 0.5f) << "the title shifted sideways when the caret arrived";
    EXPECT_NEAR(text_y1, text_y, 0.5f) << "the title shifted vertically when the caret arrived";

    const std::optional<zui::TextCaret> caret = paper_bar(d);
    ASSERT_TRUE(caret);
    EXPECT_NEAR(caret->x, text_x1, 8.f);
    EXPECT_GE(caret->y + caret->height, heading1->border.y);
    EXPECT_LE(caret->y, heading1->border.y + heading1->border.height);
}

/// True when the preview's innerText holds a Document Selection. The prototype article keeps a
/// demo `mdw-sel` span; that class is not the writing-view highlight.
bool selection_painted(Daily& d) {
    return !d.get("mdc-a-preview", "selection").empty();
}

bool preview_has_selection_wrapper(Daily& d) {
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    for (std::size_t i = 0; i < census.size(); ++i) {
        const bool wrapped = zui::test::class_token(census[i].class_attr, "mdw-sel") ||
                             zui::test::class_token(census[i].class_attr, "mdw-selhost");
        if (!wrapped) {
            continue;
        }
        bool in_preview = census[i].id == "mdc-a-preview";
        for (int p = census[i].parent; p >= 0 && !in_preview;
             p = census[static_cast<std::size_t>(p)].parent) {
            in_preview = census[static_cast<std::size_t>(p)].id == "mdc-a-preview";
        }
        if (in_preview) {
            return true;
        }
    }
    return false;
}

// 写作 shows what is selected: the paper wraps the source selection in the sheet's selection
// span, so Ctrl+A, a find jump or a drag is visible on the page itself instead of only in the
// parked textarea. The caret bar gives way while a selection is up, the way a browser does.
TEST(AppInteract, ZmdWritingViewPaintsTheSelection) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(selection_painted(d)) << "nothing is selected yet";

    // Ctrl+A selects the whole source; the paper re-stamps with the selection on it.
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "a", /*ctrl=*/true));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "selection"), d.get("mdc-a-source", "value"));
    EXPECT_TRUE(selection_painted(d));
    EXPECT_FALSE(preview_has_selection_wrapper(d));
    const std::string paper = d.get("mdc-a-preview", "selection");
    EXPECT_NE(paper.find("\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8\xe5\xb0\x8f"
                          "\xe7\xab\xb9 Markdown"),
              std::string::npos)
        << paper;  // 欢迎使用小竹 Markdown
    EXPECT_NE(paper.find("samples/welcome.md"), std::string::npos) << paper;
    EXPECT_FALSE(paper_bar(d).has_value()) << "the caret bar stays hidden while a selection is up";

    // Clicking the paper collapses the selection, and the caret bar comes back.
    ASSERT_TRUE(d.rig.click("mdc-a-preview"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(selection_painted(d));
    EXPECT_TRUE(paper_bar(d).has_value());
}

// A drag in the writing view selects source text: the paper is the writing surface there, and a
// mouse selection is how a reader points at it. The press anchors the selection on the offset
// under the pointer, each move extends it to the offset under the pointer, and the click that
// ends the drag leaves the selection alone.
TEST(AppInteract, ZmdDragInTheWritingViewSelects) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> first = d.box("mdb0");
    const std::optional<zui::ElementBox> second = d.box("mdb1");
    ASSERT_TRUE(first && first->laid);
    ASSERT_TRUE(second && second->laid);
    const float x0 = first->border.x + 60.f;
    const float y0 = first->border.y + first->border.height * 0.5f;
    const float x1 = second->border.x + 60.f;
    const float y1 = second->border.y + second->border.height * 0.5f;

    ASSERT_TRUE(d.rig.drag(x0, y0, x1, y1));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    // The selection starts in the first line and reaches into the second: the drag extended it
    // while the preview was being re-stamped under it.
    const std::string selected = d.get("mdc-a-source", "selection");
    ASSERT_FALSE(selected.empty()) << "the drag selected nothing";
    const std::size_t from = field_size(d, "mdc-a-source", "selection_start");
    const std::size_t to = field_size(d, "mdc-a-source", "selection_end");
    EXPECT_LT(from, to);
    EXPECT_GE(from, field_size(d, "mdb0", "data-beg"));
    EXPECT_LE(from, field_size(d, "mdb0", "data-at"));
    EXPECT_GE(to, field_size(d, "mdb1", "data-beg"));
    EXPECT_LE(to, field_size(d, "mdb1", "data-at"));
    // The paper paints it, and the click that ended the drag did not collapse it.
    EXPECT_TRUE(selection_painted(d));
    EXPECT_FALSE(preview_has_selection_wrapper(d));
    EXPECT_FALSE(paper_bar(d).has_value()) << "the caret bar stays hidden while a selection is up";

    // A plain click (no movement) still lands a collapsed caret.
    ASSERT_TRUE(d.rig.click("mdc-a-preview"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.get("mdc-a-source", "selection").empty());
    EXPECT_FALSE(selection_painted(d));
    EXPECT_TRUE(paper_bar(d).has_value());
}

// A double click takes the word under the pointer, a triple click the whole line: the paper maps
// the point onto a source offset, so the word and line bounds come from the source text there.
TEST(AppInteract, ZmdMultiClickInTheWritingViewWidensTheSelection) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, kFixture));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> heading = d.box("mdh0");
    ASSERT_TRUE(heading && heading->laid);
    const float x = heading->border.x + heading->border.width * 0.3f;
    const float y = heading->border.y + heading->border.height * 0.5f;
    const std::string line = "# \xe6\x97\xa5\xe8\xaf\xbe\xe6\xa0\x87\xe9\xa2\x98";  // "# 日课标题"

    d.rig.window().pointer_.click_count = 2;
    ASSERT_TRUE(d.rig.at(x, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string word = d.get("mdc-a-source", "selection");
    EXPECT_FALSE(word.empty()) << "the double click selected nothing";
    EXPECT_EQ(word.find(' '), std::string::npos) << "more than a word: [" << word << "]";
    EXPECT_EQ(word.find('\n'), std::string::npos) << word;
    EXPECT_NE(line.find(word), std::string::npos) << "[" << word << "] is not part of the line";
    EXPECT_LT(word.size(), line.size());

    d.rig.window().pointer_.click_count = 3;
    ASSERT_TRUE(d.rig.at(x, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "selection"), line);

    // Past the end of the line the click maps onto the newline that closes it: the word there is
    // the last one on the line, not the break itself.
    d.rig.window().pointer_.click_count = 2;
    ASSERT_TRUE(d.rig.at(heading->border.x + heading->border.width - 4.f, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    // The CJK run is one word: a browser segments `日课标题` that way, and the old ASCII-only rule
    // took the single character under the pointer instead.
    EXPECT_EQ(d.get("mdc-a-source", "selection"),
              "\xe6\x97\xa5\xe8\xaf\xbe\xe6\xa0\x87\xe9\xa2\x98")  // 日课标题
        << "[" << d.get("mdc-a-source", "selection") << "]";

    d.rig.window().pointer_.click_count = 1;
    ASSERT_TRUE(d.rig.at(x, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.get("mdc-a-source", "selection").empty());
}

// Shift+arrow grows a selection in the source, and the paper paints it: the engine's caret keys
// collapsed whatever was selected, so there was no way to select by keyboard at all. Driving the
// frame (a key press plus the caret count) is the path the window takes.
TEST(AppInteract, ZmdShiftArrowGrowsTheSelectionOnThePaper) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().set("mdc-a-source", "select", "2,2");
    ASSERT_TRUE(d.rig.pump());

    zui::backends::TextInputFrame step;
    step.right = 2;
    zui::backends::KeyPress press;
    press.key = "ArrowRight";
    press.shift = true;
    step.key_events.push_back(std::move(press));
    d.rig.window().inject(std::move(step));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    // The welcome document opens on "# 欢迎使用小竹 Markdown": two codepoints right of offset 2.
    EXPECT_EQ(d.get("mdc-a-source", "selection"), "\xe6\xac\xa2\xe8\xbf\x8e")  // 欢迎
        << d.get("mdc-a-source", "selection");
    EXPECT_NE(d.get("mdc-a-preview", "selection").find("\xe6\xac\xa2\xe8\xbf\x8e"),
              std::string::npos)
        << d.get("mdc-a-preview", "selection");
    EXPECT_FALSE(preview_has_selection_wrapper(d));

    // The toolbar's 加粗 wraps exactly that range.
    ASSERT_TRUE(reveal_tools(d));
    ASSERT_TRUE(click_tool(d, "zmd-bold"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_NE(d.get("mdc-a-source", "value").find("**\xe6\xac\xa2\xe8\xbf\x8e**"),
              std::string::npos)
        << d.get("mdc-a-source", "value");
}

// A click's x converts through the text's own box, not the column's: the writing view wraps each
// line's body in the span it names (`…l`). The column is about five times the width of a short
// heading, so aiming near the end of the line used to land the caret a fifth of the way in.
TEST(AppInteract, ZmdWriteClickMapsXThroughTheText) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, kFixture));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> body = d.box("mdh0l");
    const std::optional<zui::ElementBox> block = d.box("mdh0");
    ASSERT_TRUE(body && body->laid);
    ASSERT_TRUE(block && block->laid);
    ASSERT_GT(body->border.width, 20.f);
    EXPECT_LT(body->border.width, block->border.width) << "the body box is the text's, not the column's";
    // Four fifths along the text: the caret lands in the line's last quarter.
    ASSERT_TRUE(d.rig.at(body->border.x + body->border.width * 0.8f,
                         body->border.y + body->border.height * 0.5f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t caret = field_size(d, "mdc-a-source", "caret");
    const std::size_t beg = field_size(d, "mdh0", "data-beg");
    const std::size_t end = field_size(d, "mdh0", "data-at");
    EXPECT_GE(caret, beg + (end - beg) * 3 / 4)
        << "caret " << caret << " outside the line's last quarter [" << beg << "," << end << ")";
    EXPECT_LE(caret, end);
}

// Right-to-left drag on the welcome quote: the highlight must follow the pointer (the right
// side of the line), not grow from 好的… as if the hit-test were mirrored. The paper must also
// keep `，` and `现` as whole characters — a mid-UTF-8 `<span class="mdw-sel">` painted .notdef.
TEST(AppInteract, ZmdWriteDragOnTheQuoteFollowsThePointer) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> body = d.box("mdb1l");
    const std::optional<zui::ElementBox> block = d.box("mdb1");
    ASSERT_TRUE(body && body->laid) << "welcome quote body";
    ASSERT_TRUE(block && block->laid);
    ASSERT_GT(body->border.width, 40.f);
    const float body_y = body->border.y;
    const float block_y = block->border.y;
    const float y = body->border.y + body->border.height * 0.5f;
    const float x_right = body->border.x + body->border.width * 0.88f;
    const float x_left = body->border.x + body->border.width * 0.12f;
    const auto vis_left = d.rig.app.document().text_offset_at("mdb1", x_left, y);
    const auto vis_right = d.rig.app.document().text_offset_at("mdb1", x_right, y);
    ASSERT_TRUE(vis_left && vis_right) << "hit-test missed the quote";
    EXPECT_LT(*vis_left, *vis_right)
        << "horizontal-tb CJK: a larger x must map to a larger innerText offset; visL="
        << *vis_left << " visR=" << *vis_right;

    ASSERT_TRUE(d.rig.drag(x_right, y, x_left, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const auto vis_left_after = d.rig.app.document().text_offset_at("mdb1", x_left, y);
    const auto vis_right_after = d.rig.app.document().text_offset_at("mdb1", x_right, y);
    ASSERT_TRUE(vis_left_after && vis_right_after);
    EXPECT_LT(*vis_left_after, *vis_right_after)
        << "re-stamping mdw-sel must not mirror the line; visL=" << *vis_left_after
        << " visR=" << *vis_right_after;

    const std::string selected = d.get("mdc-a-source", "selection");
    ASSERT_FALSE(selected.empty()) << "the drag selected nothing";
    const std::size_t from = field_size(d, "mdc-a-source", "selection_start");
    const std::size_t to = field_size(d, "mdc-a-source", "selection_end");
    EXPECT_LT(from, to);
    EXPECT_EQ(selected.find("\xef\xbf\xbd"), std::string::npos) << selected;
    // Right-to-left drag from 88% to 12% covers the middle of the quote, including 只在你
    // (right of centre). The mirrored mapping painted only 好的编辑器…消失.
    EXPECT_NE(selected.find("\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0"), std::string::npos)
        << selected;  // 只在你
    EXPECT_NE(selected.find("\xe6\xb6\x88\xe5\xa4\xb1"), std::string::npos) << selected;  // 消失
    EXPECT_FALSE(preview_has_selection_wrapper(d));
    const std::string paper = d.get("mdb1", "selection");
    EXPECT_NE(paper.find("\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0"), std::string::npos) << paper;
    EXPECT_EQ(paper.find("\xe5\xa5\xbd\xe7\x9a\x84"), std::string::npos) << paper;  // 好的
    const std::optional<zui::ElementBox> body_after = d.box("mdb1l");
    const std::optional<zui::ElementBox> block_after = d.box("mdb1");
    ASSERT_TRUE(body_after && body_after->laid);
    ASSERT_TRUE(block_after && block_after->laid);
    EXPECT_NEAR(body_after->border.y, body_y, 0.5f);
    EXPECT_NEAR(block_after->border.y, block_y, 0.5f);
    const std::optional<zui::ElementBox> strip = d.box("zmd-sel-strip");
    ASSERT_TRUE(strip && strip->laid) << "format strip for the highlight";
    const float strip_top = strip->border.y;
    const float strip_bottom = strip_top + strip->border.height;
    const float body_top = body_after->border.y;
    const float body_bottom = body_top + body_after->border.height;
    const bool above = strip_bottom <= body_top + 4.f;
    const bool below = strip_top >= body_bottom - 4.f;
    EXPECT_TRUE(above || below) << "strip overlaps the quote body; strip_y=" << strip_top
                                << " strip_h=" << strip->border.height << " body_y=" << body_top
                                << " body_h=" << body_after->border.height;
}

// A click on the strip wraps the parked source selection. pointerdown stays on the strip so
// the paper does not collapse the range the button is about to format.
TEST(AppInteract, ZmdSelectionToolbarBoldsTheRange) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> body = d.box("mdb1l");
    ASSERT_TRUE(body && body->laid);
    const float y = body->border.y + body->border.height * 0.5f;
    ASSERT_TRUE(d.rig.drag(body->border.x + body->border.width * 0.88f, y,
                           body->border.x + body->border.width * 0.12f, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string selected = d.get("mdc-a-source", "selection");
    ASSERT_FALSE(selected.empty());

    ASSERT_TRUE(d.rig.click("zmd-sel-bold"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string source = d.get("mdc-a-source", "value");
    EXPECT_NE(source.find("**" + selected + "**"), std::string::npos) << source;
}

// A source offset in the blank line between paragraphs, and an exclusive end past the last
// block, still project onto the glyphs those bytes surround. The paper must not collapse to a
// caret just because the offset itself emits nothing.
TEST(AppInteract, ZmdGapSelectionStillHighlightsTheParagraph) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    const std::string source = "hello\n\nworld\n";
    ASSERT_TRUE(replace_source(d, source));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());

    d.rig.app.document().set("mdc-a-source", "select", "0,5");
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "selection"), "hello\n");
    EXPECT_EQ(d.get("mdc-a-preview", "selection"), "hello");
    EXPECT_FALSE(paper_bar(d).has_value());

    d.rig.app.document().set("mdc-a-source", "select",
                             "1," + std::to_string(source.size() - 1));
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string paper = d.get("mdc-a-preview", "selection");
    EXPECT_NE(paper.find("ello"), std::string::npos) << paper;
    EXPECT_NE(paper.find("world"), std::string::npos) << paper;
    EXPECT_FALSE(paper_bar(d).has_value());
}

// A fenced code block is paper innerText. A range from the paragraph before it to the paragraph
// after it has to cover the code, not stop short by those bytes.
TEST(AppInteract, ZmdSelectionAcrossAFenceIncludesTheCode) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    const std::string source = "before\n\n```\ncode line\n```\n\nafter\n";
    ASSERT_TRUE(replace_source(d, source));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    // Start inside "before", end inside "after". Not the whole source: select-all takes a
    // different path that clamps to the full innerText.
    const std::size_t end = source.find("after") + 3;
    d.rig.app.document().set("mdc-a-source", "select", "1," + std::to_string(end));
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string paper = d.get("mdc-a-preview", "selection");
    EXPECT_NE(paper.find("efore"), std::string::npos) << paper;
    EXPECT_NE(paper.find("code line"), std::string::npos) << paper;
    EXPECT_NE(paper.find("aft"), std::string::npos) << paper;
}

// Welcome's task rows use the sheet's checkbox list, the same markup the prototype paints.
TEST(AppInteract, ZmdWelcomeTasksUseTheSheetCheckbox) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    bool task = false;
    bool box = false;
    bool old_skin = false;
    for (std::size_t i = 0; i < census.size(); ++i) {
        bool in_preview = census[i].id == "mdc-a-preview";
        for (int p = census[i].parent; p >= 0 && !in_preview;
             p = census[static_cast<std::size_t>(p)].parent) {
            in_preview = census[static_cast<std::size_t>(p)].id == "mdc-a-preview";
        }
        if (!in_preview) {
            continue;
        }
        if (zui::test::class_token(census[i].class_attr, "mdw-task")) {
            task = true;
        }
        if (census[i].tag == "input") {
            box = true;
        }
        if (zui::test::class_token(census[i].class_attr, "task-list") ||
            zui::test::class_token(census[i].class_attr, "md-check") ||
            census[i].class_attr.find("md-check") != std::string::npos) {
            old_skin = true;
        }
    }
    EXPECT_TRUE(task);
    EXPECT_TRUE(box);
    EXPECT_FALSE(old_skin);
    EXPECT_TRUE(zui::test::any_text(d.rig.app, "span", "zmd-body",
                                    "\xe5\xaf\xbc\xe5\x87\xba PDF"));  // 导出 PDF
}

// A table block's source span covers the body rows, and each cell's vis entry is that row's
// byte. Selecting only `c` highlights `c`. A range that stops on `c` does not take `d`.
TEST(AppInteract, ZmdTableCellSelectionStaysInTheCell) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    const std::string source = "| a | b |\n| --- | --- |\n| c | d |\n";
    ASSERT_TRUE(replace_source(d, source));
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    const std::size_t cell_c = source.find('c');
    const std::size_t cell_a = source.find('a');
    ASSERT_NE(cell_c, std::string::npos);
    ASSERT_NE(cell_a, std::string::npos);

    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(cell_c) + "," + std::to_string(cell_c));
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("mdc-a-source", "selection"), "c");
    EXPECT_EQ(d.get("mdc-a-preview", "selection"), "c");

    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(cell_a) + "," + std::to_string(cell_c));
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::string paper = d.get("mdc-a-preview", "selection");
    EXPECT_EQ(paper, "abc") << paper;
}

// The strip is absolute inside the scroller. Its top is stored in the scrollport's content
// coordinates, which paint then shifts by scrollTop. Placing it from the viewport caret while
// the paper is already scrolled must still sit just above the block.
TEST(AppInteract, ZmdSelectionStripStaysAboveTheBlockWhenScrolled) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 700));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    std::string source;
    for (int i = 0; i < 24; ++i) {
        source += "paragraph ";
        source += std::to_string(i);
        source += "\n\n";
    }
    ASSERT_TRUE(replace_source(d, source));
    ASSERT_TRUE(d.rig.app.document().set("mdc-a-scroll", "scroll_top", "200"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::optional<zui::ElementBox> port = d.box("mdc-a-scroll");
    ASSERT_TRUE(port && port->laid);
    EXPECT_GE(std::stof(d.get("mdc-a-scroll", "scroll_top")), 150.f);

    std::string body_id;
    for (int i = 0; i < 24; ++i) {
        const std::string id = "mdb" + std::to_string(i) + "l";
        const std::optional<zui::ElementBox> box = d.box(id);
        if (!box || !box->laid || box->border.height < 4.f) {
            continue;
        }
        const float mid = box->border.y + box->border.height * 0.5f;
        if (mid > port->border.y + 24.f && mid < port->border.y + port->client_height - 24.f) {
            body_id = id;
            break;
        }
    }
    ASSERT_FALSE(body_id.empty()) << "no paragraph in the scrolled port";
    const std::string block_id = body_id.substr(0, body_id.size() - 1);
    const std::size_t beg = field_size(d, block_id, "data-beg");
    const std::size_t at = field_size(d, block_id, "data-at");
    ASSERT_GT(at, beg);
    ASSERT_TRUE(d.rig.app.document().focus("mdc-a-source"));
    ASSERT_TRUE(d.rig.pump());
    d.rig.app.document().set("mdc-a-source", "select",
                             std::to_string(beg) + "," + std::to_string(at > beg ? at - 1 : at));
    ASSERT_TRUE(d.rig.pump());
    shift_right(d);
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::optional<zui::ElementBox> strip = d.box("zmd-sel-strip");
    const std::optional<zui::ElementBox> body_after = d.box(body_id);
    ASSERT_TRUE(strip && strip->laid);
    ASSERT_TRUE(body_after && body_after->laid);
    const float strip_top = strip->border.y;
    const float strip_bottom = strip_top + strip->border.height;
    const float body_top = body_after->border.y;
    const float body_bottom = body_top + body_after->border.height;
    const bool above = strip_bottom <= body_top + 4.f && strip_bottom >= body_top - 48.f;
    const bool below = strip_top >= body_bottom - 4.f && strip_top <= body_bottom + 48.f;
    EXPECT_TRUE(above || below) << "id=" << body_id << " strip_y=" << strip_top
                                << " strip_h=" << strip->border.height << " body_y=" << body_top
                                << " body_h=" << body_after->border.height
                                << " scroll=" << d.get("mdc-a-scroll", "scroll_top");
}

// Shift+click extends the selection from the caret instead of moving it, the way it does in a
// text control: the range runs from the old caret to the click, and the paper paints it.

// ArrowDown in д�� steps the paper's visual line (not the 24px IME proxy). From the heading,
// one Down lands in the following body block's source range.
TEST(AppInteract, ZmdWritingViewArrowDownStepsPaperLines) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, kFixture));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> heading = d.box("mdh0l");
    ASSERT_TRUE(heading && heading->laid);
    ASSERT_TRUE(d.rig.at(heading->border.x + 4.f, heading->border.y + heading->border.height * 0.5f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t head_beg = field_size(d, "mdh0", "data-beg");
    const std::size_t head_end = field_size(d, "mdh0", "data-at");
    const std::size_t caret0 = field_size(d, "mdc-a-source", "caret");
    EXPECT_GE(caret0, head_beg);
    EXPECT_LE(caret0, head_end);

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "ArrowDown"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t body_beg = field_size(d, "mdb0", "data-beg");
    const std::size_t body_end = field_size(d, "mdb0", "data-at");
    const std::size_t caret1 = field_size(d, "mdc-a-source", "caret");
    EXPECT_GE(caret1, body_beg) << "ArrowDown stayed in the heading; caret=" << caret1;
    EXPECT_LE(caret1, body_end) << "ArrowDown overshot the next block; caret=" << caret1;
}



// Soft-wrapped body: click near the end of the SECOND visual line must place the caret on that
// line (not stay on line 1 between early glyphs).
TEST(AppInteract, ZmdWriteClickSoftWrapSecondLine) {
    Daily d;
    // Narrow stage so the long CJK paragraph soft-wraps to 2+ visual lines.
    ASSERT_TRUE(d.boot(720, 900));
    const char* src =
        "# Title\n"
        "\n"
        "所见即所得：左边悬停滑出文件树，右边悬停滑出目录，用 Ctrl+K 打开命令面板。"
        "再写一句足够长的中文来保证一定会折行到第二行。\n"
        "\n"
        "next\n";
    ASSERT_TRUE(replace_source(d, src));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> body = d.box("mdb0l");
    const std::optional<zui::ElementBox> blk = d.box("mdb0");
    ASSERT_TRUE(body && body->laid);
    ASSERT_TRUE(blk && blk->laid);
    const zui::ElementBox::Rect band =
        body->border.height >= blk->border.height * 0.8f ? body->border : blk->border;
    ASSERT_GE(band.height, 36.f) << "paragraph did not soft-wrap tall enough; h=" << band.height;

    // Lower-right of the wrapped paragraph (second line, near the period).
    ASSERT_TRUE(d.rig.at(band.x + band.width * 0.92f, band.y + band.height * 0.82f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::TextCaret> caret = paper_bar(d);
    ASSERT_TRUE(caret);
    EXPECT_GE(caret->y, band.y + band.height * 0.35f)
        << "caret still on first visual line; caret_y=" << caret->y
        << " band_y=" << band.y << " band_h=" << band.height;
}

// The second visual line is short. A click just past its period must land after that period,
// not a fraction of the column width back on the first line.
TEST(AppInteract, ZmdWriteClickPastWrappedPeriod) {
    Daily d;
    // Narrow enough that "打开命令面板。" wraps onto a short second line.
    ASSERT_TRUE(d.boot(520, 900));
    const char* src =
        "# Title\n"
        "\n"
        "\xe6\x89\x80\xe8\xa7\x81\xe5\x8d\xb3\xe6\x89\x80\xe5\xbe\x97\xef\xbc\x9a"
        "\xe5\xb7\xa6\xe8\xbe\xb9\xe6\x82\xac\xe5\x81\x9c\xe6\xbb\x91\xe5\x87\xba"
        "\xe6\x96\x87\xe4\xbb\xb6\xe6\xa0\x91\xef\xbc\x8c"
        "\xe5\x8f\xb3\xe8\xbe\xb9\xe6\x82\xac\xe5\x81\x9c\xe6\xbb\x91\xe5\x87\xba"
        "\xe7\x9b\xae\xe5\xbd\x95\xef\xbc\x8c"
        "`Ctrl+K` "
        "\xe6\x89\x93\xe5\xbc\x80\xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf\xe3\x80\x82\n"
        "\n"
        "next\n";
    ASSERT_TRUE(replace_source(d, src));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::string marker = "\xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf";  // 命令面板
    const std::string earlier = "\xe5\xb7\xa6\xe8\xbe\xb9";  // 左边 — line 1, not this run
    float hit_x = 0.f;
    float hit_y = 0.f;
    bool found = false;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (item.text.find(marker) == std::string::npos ||
            item.text.find(earlier) != std::string::npos) {
            continue;
        }
        // DrawText stores the pen at the baseline and leaves rect.w at 0. The run's
        // right edge is the sum of the layout advances.
        float width = 0.f;
        for (float adv : item.glyph_advances) {
            width += adv;
        }
        if (width < 1.f) {
            width = std::max(item.rect.w, item.font_px * 7.f);
        }
        // Prefer the lower line when a shadow repeats the same run.
        if (found && item.rect.y < hit_y) {
            continue;
        }
        hit_x = item.rect.x + width + 18.f;
        hit_y = item.rect.y;
        found = true;
    }
    ASSERT_TRUE(found) << "painted run of the wrapped sentence was not found";
    const std::optional<zui::ElementBox> wrapped = d.box("mdb0");
    ASSERT_TRUE(wrapped && wrapped->laid);
    ASSERT_GE(wrapped->border.height, 36.f) << "paragraph did not wrap; h=" << wrapped->border.height;
    const std::string source = d.get("mdc-a-source", "value");
    const std::size_t period = source.rfind("\xe3\x80\x82");  // 。
    ASSERT_NE(period, std::string::npos);

    ASSERT_TRUE(d.rig.at(hit_x, hit_y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t caret = field_size(d, "mdc-a-source", "caret");
    EXPECT_GE(caret, period + 3) << "click past the period landed at " << caret
                                 << " period at " << period;
    const std::optional<zui::TextCaret> bar = paper_bar(d);
    ASSERT_TRUE(bar);
    EXPECT_NEAR(bar->y + bar->height * 0.5f, hit_y, 20.f)
        << "caret y " << bar->y << " click y " << hit_y;
}

TEST(AppInteract, ZmdArrowDownStepsOnePaperLine) {
    Daily d;
    ASSERT_TRUE(d.boot(520, 900));
    const char* src =
        "# Title\n"
        "\n"
        "\xe6\x89\x80\xe8\xa7\x81\xe5\x8d\xb3\xe6\x89\x80\xe5\xbe\x97\xef\xbc\x9a"
        "\xe5\xb7\xa6\xe8\xbe\xb9\xe6\x82\xac\xe5\x81\x9c\xe6\xbb\x91\xe5\x87\xba"
        "\xe6\x96\x87\xe4\xbb\xb6\xe6\xa0\x91\xef\xbc\x8c"
        "\xe5\x8f\xb3\xe8\xbe\xb9\xe6\x82\xac\xe5\x81\x9c\xe6\xbb\x91\xe5\x87\xba"
        "\xe7\x9b\xae\xe5\xbd\x95\xef\xbc\x8c"
        "`Ctrl+K` "
        "\xe6\x89\x93\xe5\xbc\x80\xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf\xe3\x80\x82\n"
        "\n"
        "next\n";
    ASSERT_TRUE(replace_source(d, src));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> body = d.box("mdb0");
    ASSERT_TRUE(body && body->laid);
    ASSERT_GE(body->border.height, 36.f);
    ASSERT_TRUE(d.rig.at(body->border.x + 24.f, body->border.y + 8.f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t before = field_size(d, "mdc-a-source", "caret");
    const std::optional<zui::TextCaret> bar0 = paper_bar(d);
    ASSERT_TRUE(bar0);

    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "ArrowDown"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t after = field_size(d, "mdc-a-source", "caret");
    const std::optional<zui::TextCaret> bar1 = paper_bar(d);
    ASSERT_TRUE(bar1);
    const std::size_t body_end = field_size(d, "mdb0", "data-at");
    EXPECT_GT(after, before);
    EXPECT_LE(after, body_end) << "ArrowDown left the paragraph; caret=" << after;
    EXPECT_GT(bar1->y, bar0->y + 8.f)
        << "ArrowDown stayed on the same paper line";
    EXPECT_LT(bar1->y, bar0->y + 48.f)
        << "ArrowDown jumped past the next visual line";
}

TEST(AppInteract, ZmdShiftClickExtendsTheSelection) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(replace_source(d, kFixture));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> line = d.box("mdb1l");
    ASSERT_TRUE(line && line->laid);
    const float y = line->border.y + line->border.height * 0.5f;
    ASSERT_TRUE(d.rig.at(line->border.x + line->border.width * 0.2f, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t from = field_size(d, "mdc-a-source", "caret");
    EXPECT_TRUE(d.get("mdc-a-source", "selection").empty());

    d.rig.window().pointer_.shift = true;
    ASSERT_TRUE(d.rig.at(line->border.x + line->border.width * 0.8f, y));
    d.rig.window().pointer_.shift = false;
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    const std::size_t to = field_size(d, "mdc-a-source", "caret");
    ASSERT_GT(to, from);
    const std::string source = d.get("mdc-a-source", "value");
    EXPECT_EQ(d.get("mdc-a-source", "selection"), source.substr(from, to - from));
    EXPECT_EQ(field_size(d, "mdc-a-source", "selection_start"), from);
    EXPECT_EQ(field_size(d, "mdc-a-source", "selection_end"), to);
    EXPECT_TRUE(selection_painted(d)) << "the paper does not paint the shift-click range";

    // A plain click collapses it again.
    ASSERT_TRUE(d.rig.at(line->border.x + line->border.width * 0.5f, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(d.get("mdc-a-source", "selection").empty());
}


TEST(AppInteract, DebugOverlapZmdPhase2InlineCodeGap) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.laid("mdc-a-preview"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const auto& items = d.rig.renderer().last_items;
    struct RunInfo {
        float x0 = 0;
        float y = 0;
        float x1 = 0;
        std::string text;
        bool mono = false;
    };
    std::vector<RunInfo> runs;
    auto is_cjk = [](unsigned char b) { return b >= 0x80; };
    for (const auto& it : items) {
        if (it.op != zui::paint::DisplayOp::DrawText || it.text.empty()) {
            continue;
        }
        bool has_cjk = false;
        for (unsigned char b : it.text) {
            if (is_cjk(b)) {
                has_cjk = true;
                break;
            }
        }
        const bool mono = it.font_family.find("monospace") != std::string::npos ||
                          it.font_family.find("Consolas") != std::string::npos ||
                          it.font_family.find("Mono") != std::string::npos;
        if (!has_cjk && !mono) {
            continue;
        }
        RunInfo r;
        r.x0 = it.rect.x;
        r.y = it.rect.y;
        r.text = it.text;
        r.mono = mono;
        float sum = 0.f;
        for (float a : it.glyph_advances) {
            sum += a;
        }
        r.x1 = r.x0 + (sum > 0.f ? sum : static_cast<float>(it.text.size()));
        runs.push_back(std::move(r));
    }

    int cross_overlap = 0;
    std::string sample;
    for (const auto& a : runs) {
        if (!a.mono) {
            continue;
        }
        for (const auto& b : runs) {
            if (b.mono) {
                continue;
            }
            if (std::fabs(a.y - b.y) > 14.f) {
                continue;
            }
            const float left = std::max(a.x0, b.x0);
            const float right = std::min(a.x1, b.x1);
            if (right - left > 2.f) {
                ++cross_overlap;
                if (sample.empty()) {
                    sample = "mono[" + std::to_string(a.x0) + "," + std::to_string(a.x1) +
                             "] y=" + std::to_string(a.y) + " vs cjk[" + std::to_string(b.x0) +
                             "," + std::to_string(b.x1) + "] text_bytes=" +
                             std::to_string(b.text.size());
                }
            }
        }
    }

    // Also require the welcome bullet's 标题 not to sit under a `#` chip.
    bool title_ok = true;
    for (const auto& b : runs) {
        // 标题
        if (b.text.find("\xe6\xa0\x87\xe9\xa2\x98") == std::string::npos) {
            continue;
        }
        for (const auto& a : runs) {
            if (!a.mono || std::fabs(a.y - b.y) > 14.f) {
                continue;
            }
            if (b.x0 + 0.5f < a.x1 && b.x0 + 8.f > a.x0) {
                title_ok = false;
                sample = "标题 under chip: title_x=" + std::to_string(b.x0) + " chip_x1=" +
                         std::to_string(a.x1);
            }
        }
    }

    std::ofstream dump(std::string(ZUI_SOURCE_DIR) +
                       "/docs/superpowers/plans/_zmd-overlap-phase2-dump.txt");
    dump << "runs=" << runs.size() << " cross_overlap=" << cross_overlap
         << " title_ok=" << (title_ok ? 1 : 0) << " sample=[" << sample << "]\n";
    for (const auto& r : runs) {
        if (r.y < 100.f || r.y > 420.f) {
            continue;
        }
        dump << (r.mono ? "MONO" : "CJK") << " y=" << r.y << " x=[" << r.x0 << "," << r.x1
             << "] bytes=" << r.text.size() << "\n";
    }
    dump.close();

    EXPECT_EQ(cross_overlap, 0) << sample;
    EXPECT_TRUE(title_ok) << sample;
}




TEST(AppInteract, DebugOverlapZmdPhase2bFirstBodyRun) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.laid("mdc-a-preview"));

    // Click the first body line on the paper so the writing view plants zmd-caret mid-paragraph.
    const auto paper = d.box("mdc-a-preview");
    ASSERT_TRUE(paper && paper->laid);
    // Body text starts ~y=153 in prior dumps; aim mid first run (after 滑出 ~x=500).
    ASSERT_TRUE(d.rig.at(500.f, 155.f));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(paper_bar(d).has_value());

    {
        std::ofstream predump(std::string(ZUI_SOURCE_DIR) +
                              "/docs/superpowers/plans/_zmd-overlap-phase2b-preview.html",
                              std::ios::binary);
        for (auto& p : fs::recursive_directory_iterator(d.guard.dir)) {
            if (p.path().filename() == "_preview.html") {
                predump << slurp(p.path());
                break;
            }
        }
    }

    const auto& items = d.rig.renderer().last_items;
    std::ofstream dump(std::string(ZUI_SOURCE_DIR) +
                       "/docs/superpowers/plans/_zmd-overlap-phase2b-dump.txt");
    if (const auto caret_box = paper_bar(d)) {
        dump << "CARET (" << caret_box->x << "," << caret_box->y << ","
             << caret_box->width << "," << caret_box->height
             << ") source_caret=" << d.get("mdc-a-source", "caret") << "\n";
    }

    int body_hits = 0;
    int empty_adv = 0;
    int same_pen = 0;
    int zero_step = 0;
    float body_y = -1.f;
    const char* suo = "\xe6\x89\x80";
    const char* huachu = "\xe6\xbb\x91\xe5\x87\xba";
    const char* wenjian = "\xe6\x96\x87\xe4\xbb\xb6";

    for (const auto& it : items) {
        if (it.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (it.text.find(suo) == std::string::npos &&
            it.text.find(huachu) == std::string::npos &&
            it.text.find(wenjian) == std::string::npos) {
            continue;
        }
        ++body_hits;
        if (body_y < 0.f) {
            body_y = it.rect.y;
        }
        float sum = 0.f;
        for (float a : it.glyph_advances) {
            sum += a;
            if (a < 0.5f) {
                ++zero_step;
            }
        }
        if (it.glyph_advances.empty()) {
            ++empty_adv;
        }
        dump << "BODY y=" << it.rect.y << " x=" << it.rect.x << " font=" << it.font_px
             << " bytes=" << it.text.size() << " adv_n=" << it.glyph_advances.size()
             << " sum=" << sum << " empty=" << (it.glyph_advances.empty() ? 1 : 0) << "\n  adv=";
        for (std::size_t i = 0; i < it.glyph_advances.size() && i < 40; ++i) {
            dump << it.glyph_advances[i] << " ";
        }
        dump << "\n  pens=";
        float pen = it.rect.x;
        float prev = pen - 100.f;
        for (std::size_t i = 0; i < it.glyph_advances.size() && i < 40; ++i) {
            dump << pen << " ";
            if (i > 0 && std::fabs(pen - prev) < 0.05f) {
                ++same_pen;
            }
            prev = pen;
            pen += it.glyph_advances[i];
        }
        dump << "\n";
    }

    dump << "BAND y~" << body_y << "\n";
    int band = 0;
    for (const auto& it : items) {
        if (it.op != zui::paint::DisplayOp::DrawText || body_y < 0.f) {
            continue;
        }
        if (std::fabs(it.rect.y - body_y) > 24.f) {
            continue;
        }
        ++band;
        float sum = 0.f;
        for (float a : it.glyph_advances) {
            sum += a;
        }
        dump << "  x=" << it.rect.x << " bytes=" << it.text.size()
             << " adv_n=" << it.glyph_advances.size() << " sum=" << sum
             << " empty=" << (it.glyph_advances.empty() ? 1 : 0) << " hex=";
        for (std::size_t i = 0; i < it.text.size() && i < 48; ++i) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned char>(it.text[i]));
            dump << buf;
        }
        dump << "\n";
    }
    int empty_adv_band = 0;
    for (const auto& it : items) {
        if (it.op != zui::paint::DisplayOp::DrawText || body_y < 0.f) {
            continue;
        }
        if (std::fabs(it.rect.y - body_y) > 24.f) {
            continue;
        }
        if (it.glyph_advances.empty() && it.text.size() >= 6) {
            ++empty_adv_band;
        }
    }
    dump << "empty_adv_band=" << empty_adv_band << "\n";
        dump << "SUMMARY body_hits=" << body_hits << " empty_adv=" << empty_adv
         << " same_pen=" << same_pen << " zero_step=" << zero_step << " band=" << band << "\n";
    dump.close();

    EXPECT_GE(body_hits, 1);
    EXPECT_EQ(empty_adv, 0);
    EXPECT_EQ(same_pen, 0);
    EXPECT_EQ(zero_step, 0);
    EXPECT_EQ(empty_adv_band, 0) << "fallback Text paint without advances in body band";
}

const zui::paint::DisplayItem* painted_text(Daily& d, std::string_view needle) {
    const zui::paint::DisplayItem* found = nullptr;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
            continue;
        }
        if (item.text.find(needle) == std::string::npos) {
            continue;
        }
        if (found == nullptr || item.font_px >= found->font_px) {
            found = &item;
        }
    }
    return found;
}

float advance_sum(const zui::paint::DisplayItem& item) {
    float sum = 0.f;
    for (float a : item.glyph_advances) {
        sum += a;
    }
    return sum;
}

// The highlight has to be on the paper while the button is still down. It used to
// appear only on release: each move wrote the textarea selection after the deferred
// paper projection, and paint read that textarea range.
TEST(AppInteract, ZmdDragHighlightFollowsThePointer) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* quote =
        painted_text(d, "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8");  // 好的编辑器
    ASSERT_NE(quote, nullptr);
    ASSERT_GT(advance_sum(*quote), 40.f);
    const float x0 = quote->rect.x + 4.f;
    const float x1 = quote->rect.x + advance_sum(*quote) - 4.f;
    const float y = quote->rect.y - 4.f;
    EXPECT_TRUE(d.get("mdc-a-preview", "selection").empty());

    auto hold = [&](float x, float py, bool pressed, bool released, bool down) {
        d.rig.window().inject(zui::backends::TextInputFrame{});
        auto& pointer = d.rig.window().pointer_;
        pointer.x = x;
        pointer.y = py;
        pointer.left_pressed = pressed;
        pointer.left_released = released;
        pointer.left_down = down;
        pointer.buttons = down ? 1 : 0;
        return d.rig.pump();
    };
    ASSERT_TRUE(hold(x0, y, true, false, true));
    EXPECT_TRUE(d.get("mdc-a-preview", "selection").empty());
    ASSERT_TRUE(hold(x1, y, false, false, true));

    const std::string selected = d.get("mdc-a-preview", "selection");
    EXPECT_FALSE(selected.empty()) << "the paper range is still empty while the button is down";
    bool fill = false;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.color.a < 16 || item.rect.w < 8.f ||
            item.rect.h < 8.f) {
            continue;
        }
        if (!(item.color.b > item.color.r + 20 && item.color.b > item.color.g + 10)) {
            continue;
        }
        const float bot = item.rect.y + item.rect.h;
        if (item.rect.y <= quote->rect.y && bot >= quote->rect.y && item.rect.x < x1 &&
            item.rect.x + item.rect.w > x0) {
            fill = true;
            break;
        }
    }
    EXPECT_TRUE(fill) << "no selection fill on the quote while the button is down";

    ASSERT_TRUE(hold(x1, y, false, true, false));
    d.rig.window().inject(zui::backends::TextInputFrame{});
    d.rig.window().pointer_.left_released = false;
    d.rig.window().pointer_.buttons = 0;
    ASSERT_TRUE(d.rig.pump());
    EXPECT_FALSE(d.get("mdc-a-preview", "selection").empty());
}

// Welcome opens with a caret on the title glyphs, not only after a later click.
TEST(AppInteract, ZmdWelcomeCaretSitsOnTheTitle) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::optional<zui::ElementBox> heading = d.box("mdh0");
    ASSERT_TRUE(heading && heading->laid);
    const zui::paint::DisplayItem* title =
        painted_text(d, "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8");  // 欢迎使用
    ASSERT_NE(title, nullptr);
    const std::optional<zui::TextCaret> caret = paper_bar(d);
    ASSERT_TRUE(caret) << "welcome paper has no caret";
    EXPECT_GT(caret->height, 4.f);
    EXPECT_GE(caret->y + caret->height, title->rect.y - title->font_px);
    EXPECT_LE(caret->y, title->rect.y);
    EXPECT_NEAR(caret->x, title->rect.x, 8.f);

    bool filled = false;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.rect.h < 4.f || item.rect.w > 4.f) {
            continue;
        }
        if (std::fabs(item.rect.x - caret->x) < 3.f && std::fabs(item.rect.y - caret->y) < 3.f &&
            item.color.a > 0) {
            filled = true;
            break;
        }
    }
    EXPECT_TRUE(filled) << "caret rect is not painted";
}

// An empty source still has a line the caret can sit on.
TEST(AppInteract, ZmdEmptyPaperHasACaretLine) {
    const fs::path dir = zui::test::scratch_dir("zmd-empty-caret");
    fs::create_directories(dir);
    const fs::path file = dir / "empty.md";
    write_text(file, "");
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, file));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    EXPECT_TRUE(d.get("mdc-a-source", "value").empty()) << d.get("mdc-a-source", "value");
    const std::string paper = d.get("mdc-a-preview", "text");
    EXPECT_EQ(paper.find("\xe6\xac\xa2\xe8\xbf\x8e"), std::string::npos) << paper;
    const std::optional<zui::ElementBox> line = d.box("mdb0");
    ASSERT_TRUE(line && line->laid) << "empty paper has no block";
    EXPECT_GT(line->border.height, 8.f);
    const std::optional<zui::TextCaret> caret = paper_bar(d);
    ASSERT_TRUE(caret) << "empty paper clears the caret; source caret="
                       << d.get("mdc-a-source", "caret")
                       << " sel=" << d.get("mdc-a-source", "selection_start") << ","
                       << d.get("mdc-a-source", "selection_end")
                       << " paper=[" << paper << "]";
    EXPECT_GT(caret->height, 4.f);
    EXPECT_GE(caret->y + caret->height, line->border.y);
    EXPECT_LE(caret->y, line->border.y + line->border.height);
    EXPECT_GE(caret->x, line->border.x - 1.f);
    EXPECT_LE(caret->x, line->border.x + line->border.width);
    bool filled = false;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.rect.h < 4.f || item.rect.w > 4.f) {
            continue;
        }
        if (std::fabs(item.rect.x - caret->x) < 3.f && std::fabs(item.rect.y - caret->y) < 3.f &&
            item.color.a > 0) {
            filled = true;
            break;
        }
    }
    EXPECT_TRUE(filled) << "empty-paper caret rect is not painted x=" << caret->x
                        << " y=" << caret->y << " h=" << caret->height;
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Backtick chips share the CJK baseline and use a whole-pixel face.
TEST(AppInteract, ZmdCodeChipSharesTheLineBaseline) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* cjk =
        painted_text(d, "\xe6\x89\x93\xe5\xbc\x80\xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf");
    ASSERT_NE(cjk, nullptr);
    const zui::paint::DisplayItem* chip = nullptr;
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    for (const zui::frame::NodeCensus& row : census) {
        if (row.tag != "code" || !row.laid || row.text.find("Ctrl+K") != 0) {
            continue;
        }
        if (std::fabs((row.y + row.h * 0.5f) - cjk->rect.y) > 24.f) {
            continue;
        }
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
                continue;
            }
            if (item.text.find("Ctrl+K") == std::string::npos) {
                continue;
            }
            if (item.rect.x < row.x - 2.f || item.rect.x > row.x + row.w + 2.f) {
                continue;
            }
            chip = &item;
            break;
        }
        break;
    }
    ASSERT_NE(chip, nullptr);
    EXPECT_NEAR(chip->rect.y, cjk->rect.y, 0.5f)
        << "chip baseline " << chip->rect.y << " cjk " << cjk->rect.y
        << " chip_px=" << chip->font_px << " cjk_px=" << cjk->font_px;
    EXPECT_NEAR(chip->font_px, std::round(chip->font_px), 0.01f)
        << "chip font " << chip->font_px << " is not a whole pixel";
}

// Selecting the quote does not invent U+FFFD and does not move the glyphs.
TEST(AppInteract, ZmdQuoteSelectionKeepsGlyphPositions) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* before =
        painted_text(d, "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8");  // 好的编辑器
    ASSERT_NE(before, nullptr);
    const float x = before->rect.x;
    const float y = before->rect.y;
    const std::string before_text = before->text;
    const std::vector<float> before_adv = before->glyph_advances;
    ASSERT_FALSE(before_adv.empty());
    ASSERT_FALSE(before_text.empty());

    const std::optional<zui::ElementBox> body = d.box("mdb1l");
    ASSERT_TRUE(body && body->laid);
    const float hit_y = y - 4.f;
    ASSERT_TRUE(d.rig.drag(body->border.x + body->border.width * 0.88f, hit_y,
                           body->border.x + 4.f, hit_y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::string selected = d.get("mdc-a-source", "selection");
    EXPECT_EQ(selected.find("\xef\xbf\xbd"), std::string::npos) << selected;
    EXPECT_NE(selected.find("\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8"), std::string::npos)
        << selected;  // 编辑器
    const zui::paint::DisplayItem* after =
        painted_text(d, "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->text.find("\xef\xbf\xbd"), std::string::npos) << after->text;
    EXPECT_NEAR(after->rect.x, x, 0.5f);
    EXPECT_NEAR(after->rect.y, y, 0.5f);
    // A selection splits one shaped run into slices. The slice that still holds 好的编辑器
    // keeps that run's pen and its advances; the rest of the run continues at the slice edge.
    ASSERT_LE(after->text.size(), before_text.size());
    EXPECT_EQ(before_text.compare(0, after->text.size(), after->text), 0) << after->text;
    ASSERT_LE(after->glyph_advances.size(), before_adv.size());
    ASSERT_GT(after->glyph_advances.size(), 0u);
    for (std::size_t i = 0; i < after->glyph_advances.size(); ++i) {
        EXPECT_NEAR(after->glyph_advances[i], before_adv[i], 0.05f) << "advance " << i;
    }
    // Walk every slice of the original run. Each one must start where the previous
    // slice's advances ended and must keep those advances.
    float cursor = x;
    std::size_t gi = 0;
    while (gi < before_adv.size()) {
        const zui::paint::DisplayItem* slice = nullptr;
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f ||
                item.glyph_advances.empty()) {
                continue;
            }
            if (std::fabs(item.rect.y - y) > 0.5f || std::fabs(item.rect.x - cursor) > 0.75f) {
                continue;
            }
            if (gi + item.glyph_advances.size() > before_adv.size()) {
                continue;
            }
            bool same = true;
            for (std::size_t j = 0; j < item.glyph_advances.size(); ++j) {
                if (std::fabs(item.glyph_advances[j] - before_adv[gi + j]) > 0.05f) {
                    same = false;
                    break;
                }
            }
            if (!same || item.text.find("\xef\xbf\xbd") != std::string::npos) {
                continue;
            }
            slice = &item;
            break;
        }
        if (slice == nullptr) {
            std::string near;
            for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
                if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
                    continue;
                }
                if (std::fabs(item.rect.y - y) > 3.f) {
                    continue;
                }
                near += " x=" + std::to_string(item.rect.x) + " bytes=" +
                        std::to_string(item.text.size()) + " adv=" +
                        std::to_string(item.glyph_advances.size()) + " [" + item.text + "]\n";
            }
            FAIL() << "quote slice left its place at x=" << cursor << " glyph=" << gi << "\n"
                   << near;
        }
        for (float adv : slice->glyph_advances) {
            cursor += adv;
        }
        gi += slice->glyph_advances.size();
    }
}

// A drag through `assets/` highlights the chip's glyph ink, including the last glyph.
TEST(AppInteract, ZmdCodeChipHighlightCoversTheGlyphs) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::frame::NodeCensus* code = nullptr;
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    for (const zui::frame::NodeCensus& row : census) {
        if (row.tag == "code" && row.laid && row.text == "assets/") {
            code = &row;
            break;
        }
    }
    ASSERT_NE(code, nullptr) << "welcome has no assets/ chip";
    const float y = code->y + code->h * 0.5f;
    // ::selection is the accent washed toward transparent. The chip's own background is an
    // opaque gray, so a blue-dominant fill is the highlight and not that background.
    auto selection_fill = [](const zui::paint::DisplayItem& item) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.color.a < 16 || item.rect.w < 8.f ||
            item.rect.h < 8.f) {
            return false;
        }
        const int r = item.color.r;
        const int g = item.color.g;
        const int b = item.color.b;
        return b > r + 20 && b > g + 10;
    };
    auto covers_last = [&](float left, float right, float baseline, float font_px) {
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (!selection_fill(item)) {
                continue;
            }
            const float bot = item.rect.y + item.rect.h;
            const float edge = item.rect.x + item.rect.w;
            if (item.rect.y <= baseline - font_px * 0.5f && bot >= baseline &&
                item.rect.x <= left + 0.5f && edge + 0.5f >= right) {
                return true;
            }
        }
        return false;
    };
    const zui::paint::DisplayItem* chip = nullptr;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op == zui::paint::DisplayOp::DrawText && item.blur <= 0.f &&
            item.text == "assets/") {
            chip = &item;
            break;
        }
    }
    ASSERT_NE(chip, nullptr);
    ASSERT_FALSE(chip->glyph_advances.empty());
    const float before_last = chip->glyph_advances.back();
    const float before_ink = advance_sum(*chip);
    EXPECT_FALSE(covers_last(chip->rect.x + before_ink - before_last, chip->rect.x + before_ink,
                             chip->rect.y, chip->font_px))
        << "a selection fill already covered the slash before the drag";
    const float x0 = code->x + 6.f;
    const float x1 = code->x + code->w - 2.f;
    // Across the laid-out chip, from inside the left padding to inside the right edge,
    // so the range includes the last glyph of assets/.
    ASSERT_TRUE(d.rig.drag(x0, y, x1, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* painted = nullptr;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op == zui::paint::DisplayOp::DrawText && item.blur <= 0.f &&
            item.text == "assets/") {
            painted = &item;
            break;
        }
    }
    ASSERT_NE(painted, nullptr) << "assets/ chip was not painted after the drag";
    EXPECT_EQ(painted->text.find("\xef\xbf\xbd"), std::string::npos);
    ASSERT_FALSE(painted->glyph_advances.empty());
    const float last = painted->glyph_advances.back();
    EXPECT_GT(last, 0.5f);
    const float ink_w = advance_sum(*painted);
    const float last_left = painted->rect.x + ink_w - last;
    const float last_right = painted->rect.x + ink_w;
    EXPECT_TRUE(covers_last(last_left, last_right, painted->rect.y, painted->font_px))
        << "selection fill misses the last glyph of assets/ left=" << last_left
        << " right=" << last_right << " last=" << last;
    const std::string selected = d.get("mdc-a-source", "selection");
    EXPECT_NE(selected.find("assets/"), std::string::npos) << selected;
}

// A drag that starts in the text after a chip and ends in the text before it has to
// highlight all three: the CJK before the chip, the chip glyphs, and the CJK after it.
// Dragging only inside the chip box (the case above) stays green when the neighbours
// are left unpainted.
TEST(AppInteract, ZmdDragAcrossACodeChipHighlightsTheNeighbours) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    // 变成列表 / 变成引用 — the welcome line `变成列表，`> ` 变成引用`.
    const zui::paint::DisplayItem* list =
        painted_text(d, "\xe5\x8f\x98\xe6\x88\x90\xe5\x88\x97\xe8\xa1\xa8");
    const zui::paint::DisplayItem* cite =
        painted_text(d, "\xe5\x8f\x98\xe6\x88\x90\xe5\xbc\x95\xe7\x94\xa8");
    ASSERT_NE(list, nullptr) << "welcome line has no 变成列表";
    ASSERT_NE(cite, nullptr) << "welcome line has no 变成引用";
    EXPECT_NEAR(list->rect.y, cite->rect.y, 1.f);

    const zui::paint::DisplayItem* chip = nullptr;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
            continue;
        }
        if (std::fabs(item.rect.y - cite->rect.y) > 2.f) {
            continue;
        }
        // The chip is `> ` with the space kept as U+00A0, so the shaped text is not "> ".
        if (!item.text.empty() && item.text[0] == '>' && item.text.size() <= 4) {
            chip = &item;
            break;
        }
    }
    ASSERT_NE(chip, nullptr) << "no > chip on the 变成引用 line";
    ASSERT_FALSE(chip->glyph_advances.empty());

    auto pen_at = [](const zui::paint::DisplayItem& item, std::size_t byte) {
        float x = item.rect.x;
        if (item.glyph_advances.size() == item.text.size()) {
            const std::size_t n = std::min(byte, item.glyph_advances.size());
            for (std::size_t i = 0; i < n; ++i) {
                x += item.glyph_advances[i];
            }
            return x;
        }
        std::size_t b = 0;
        std::size_t gi = 0;
        while (b < byte && b < item.text.size() && gi < item.glyph_advances.size()) {
            x += item.glyph_advances[gi++];
            ++b;
            while (b < item.text.size() &&
                   (static_cast<unsigned char>(item.text[b]) & 0xc0) == 0x80) {
                ++b;
            }
        }
        return x;
    };
    const std::string lie = "\xe5\x88\x97\xe8\xa1\xa8";  // 列表
    const std::size_t lie_at = list->text.find(lie);
    ASSERT_NE(lie_at, std::string::npos) << list->text;
    const float lie_left = pen_at(*list, lie_at);
    const float lie_right = pen_at(*list, lie_at + lie.size());
    const float cite_left = cite->rect.x;
    const float cite_right = cite->rect.x + advance_sum(*cite);
    const float chip_left = chip->rect.x;
    const float chip_mark = chip->rect.x + chip->glyph_advances.front();

    auto selection_fill = [](const zui::paint::DisplayItem& item) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.color.a < 16 || item.rect.w < 8.f ||
            item.rect.h < 8.f) {
            return false;
        }
        const int r = item.color.r;
        const int g = item.color.g;
        const int b = item.color.b;
        return b > r + 20 && b > g + 10;
    };
    auto covers = [&](float left, float right, float baseline, float font_px) {
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (!selection_fill(item)) {
                continue;
            }
            const float bot = item.rect.y + item.rect.h;
            const float edge = item.rect.x + item.rect.w;
            if (item.rect.y <= baseline - font_px * 0.5f && bot >= baseline &&
                item.rect.x <= left + 0.5f && edge + 0.5f >= right) {
                return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(covers(lie_left, lie_right, list->rect.y, list->font_px));
    EXPECT_FALSE(covers(chip_left, chip_mark, chip->rect.y, chip->font_px));
    EXPECT_FALSE(covers(cite_left, cite_right, cite->rect.y, cite->font_px));

    // Right side of 用, leftward to just inside 列 (the middle of 变成列表).
    const float y = cite->rect.y - 4.f;
    const float x_from = cite_right - 1.f;
    const float x_to = lie_left + 1.f;
    ASSERT_LT(x_to, x_from);
    ASSERT_TRUE(d.rig.drag(x_from, y, x_to, y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::string selected = d.get("mdc-a-source", "selection");
    EXPECT_NE(selected.find(lie), std::string::npos) << selected;
    EXPECT_NE(selected.find(">"), std::string::npos) << selected;
    EXPECT_NE(selected.find("\xe5\x8f\x98\xe6\x88\x90\xe5\xbc\x95\xe7\x94\xa8"), std::string::npos)
        << selected;

    EXPECT_TRUE(covers(lie_left, lie_right, list->rect.y, list->font_px))
        << "selection fill misses 列表";
    EXPECT_TRUE(covers(chip_left, chip_mark, chip->rect.y, chip->font_px))
        << "selection fill misses the > chip";
    EXPECT_TRUE(covers(cite_left, cite_right, cite->rect.y, cite->font_px))
        << "selection fill misses 变成引用";
}

// The format strip sits above the selected line and does not cover the paragraph above it.
TEST(AppInteract, ZmdFormatStripClearsThePreviousLine) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* prev =
        painted_text(d, "\xe6\x89\x80\xe8\xa7\x81\xe5\x8d\xb3\xe6\x89\x80\xe5\xbe\x97");  // 所见即所得
    const zui::paint::DisplayItem* quote =
        painted_text(d, "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8");
    ASSERT_NE(prev, nullptr);
    ASSERT_NE(quote, nullptr);
    const float prev_y = prev->rect.y;
    const float quote_y = quote->rect.y;
    // Font box of the paragraph above the quote. Its bottom is baseline + descent.
    d.rig.app.document().set_text_caret("mdb0", 0);
    const std::optional<zui::TextCaret> prev_bar = paper_bar(d);
    ASSERT_TRUE(prev_bar) << "previous paragraph has no font box";
    const float prev_glyph_bottom = prev_bar->y + prev_bar->height;

    const std::optional<zui::ElementBox> body = d.box("mdb1l");
    ASSERT_TRUE(body && body->laid);
    const float hit_y = quote_y - 4.f;
    ASSERT_TRUE(d.rig.drag(body->border.x + 8.f, hit_y,
                           body->border.x + body->border.width * 0.7f, hit_y));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const zui::paint::DisplayItem* prev_after =
        painted_text(d, "\xe6\x89\x80\xe8\xa7\x81\xe5\x8d\xb3\xe6\x89\x80\xe5\xbe\x97");
    const zui::paint::DisplayItem* quote_after =
        painted_text(d, "\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8");  // 编辑器
    ASSERT_NE(prev_after, nullptr);
    ASSERT_NE(quote_after, nullptr);
    EXPECT_NEAR(prev_after->rect.y, prev_y, 0.5f);
    EXPECT_NEAR(quote_after->rect.y, quote_y, 0.5f);
    const std::optional<zui::ElementBox> strip = d.box("zmd-sel-strip");
    ASSERT_TRUE(strip && strip->laid);
    const float strip_top = strip->border.y;
    const float strip_bottom = strip_top + strip->border.height;
    float line_top = 0.f;
    float line_h = 0.f;
    bool line_box = false;
    float best = 1.0e9f;
    const float font = quote_after->font_px;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect || item.rect.h < font * 1.15f ||
            item.rect.h > font * 2.5f) {
            continue;
        }
        const float bot = item.rect.y + item.rect.h;
        const float right = item.rect.x + item.rect.w;
        if (item.rect.y > quote_after->rect.y || bot < quote_after->rect.y ||
            item.rect.x > quote_after->rect.x + 2.f || right < quote_after->rect.x + 8.f) {
            continue;
        }
        const float delta = std::fabs(item.rect.h - font * 1.72f);
        if (delta < best) {
            best = delta;
            line_top = item.rect.y;
            line_h = item.rect.h;
            line_box = true;
        }
    }
    ASSERT_TRUE(line_box) << "selected quote has no line-box highlight";
    const float line_bottom = line_top + line_h;
    // The bubble has to sit below the previous glyph bottom. The gap above this quote is
    // shorter than the bubble, so it drops below the selected line box instead of covering
    // the paragraph above.
    EXPECT_GE(strip_top, prev_glyph_bottom - 0.5f)
        << "bubble top " << strip_top << " covers glyphs that end at " << prev_glyph_bottom;
    const float gap = line_top - prev_glyph_bottom;
    if (gap + 0.5f < strip->border.height) {
        EXPECT_GE(strip_top, line_bottom - 0.5f)
            << "bubble top " << strip_top << " line box bottom " << line_bottom;
    } else {
        EXPECT_LE(strip_bottom, line_top + 0.5f)
            << "bubble bottom " << strip_bottom << " line box top " << line_top;
    }
}

// The welcome table has a visible row rule, and the break plus the closing sentence are laid out.
TEST(AppInteract, ZmdWelcomeTableRuleAndClosingSentence) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    bool hr = false;
    bool tail = false;
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    for (const zui::frame::NodeCensus& row : census) {
        if (row.tag == "hr" && row.laid && row.h >= 1.f && row.w > 40.f) {
            hr = true;
        }
        if (row.text.find("samples/welcome.md") != std::string::npos && row.laid && row.h > 4.f &&
            row.w > 20.f) {
            tail = true;
        }
    }
    EXPECT_TRUE(hr) << "thematic break has no box";
    EXPECT_TRUE(tail) << "closing sentence has no box";

    const zui::paint::DisplayItem* cell = painted_text(d, "Ctrl+1");
    ASSERT_NE(cell, nullptr);
    const std::optional<zui::ElementBox> paper = d.box("mdc-a-preview");
    ASSERT_TRUE(paper && paper->laid);
    bool rule = false;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect &&
            item.op != zui::paint::DisplayOp::StrokeRect) {
            continue;
        }
        if (item.rect.h < 0.4f || item.rect.h > 4.f || item.rect.w < 40.f) {
            continue;
        }
        if (item.rect.x < paper->border.x || item.rect.x > paper->border.x + paper->border.width) {
            continue;
        }
        if (item.rect.y < cell->rect.y - 8.f || item.rect.y > cell->rect.y + 40.f) {
            continue;
        }
        const int dr = std::abs(static_cast<int>(item.color.r) - 255);
        const int dg = std::abs(static_cast<int>(item.color.g) - 255);
        const int db = std::abs(static_cast<int>(item.color.b) - 255);
        if (item.color.a > 200 && std::max(dr, std::max(dg, db)) >= 25) {
            rule = true;
            break;
        }
    }
    EXPECT_TRUE(rule) << "table row under Ctrl+1 has no visible rule";
}

namespace {

int png_side(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char buf[24] = {};
    in.read(reinterpret_cast<char*>(buf), 24);
    if (!in) {
        return 0;
    }
    return (static_cast<int>(buf[16]) << 24) | (static_cast<int>(buf[17]) << 16) |
           (static_cast<int>(buf[18]) << 8) | static_cast<int>(buf[19]);
}

const zui::frame::NodeCensus* child_tag(const std::vector<zui::frame::NodeCensus>& rows,
                                        int parent, std::string_view tag) {
    if (parent < 0) {
        return nullptr;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].parent == parent && rows[i].tag == tag) {
            return &rows[i];
        }
    }
    return nullptr;
}

const zui::paint::DisplayItem* glyph_in(Daily& d, const zui::frame::NodeCensus& box,
                                        std::string_view text) {
    const zui::paint::DisplayItem* found = nullptr;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
            continue;
        }
        if (item.text.find(text) == std::string::npos) {
            continue;
        }
        if (item.rect.x < box.x - 2.f || item.rect.x > box.x + box.w + 2.f) {
            continue;
        }
        if (item.rect.y < box.y - 4.f || item.rect.y > box.y + box.h + 8.f) {
            continue;
        }
        found = &item;
        break;
    }
    return found;
}

}  // namespace

// Factory 新建位置 follows the folder the panel is showing. 工作目录根 still
// sends both 新建文件 and 新建文件夹 to the workspace root.
TEST(AppInteract, ZmdNewFileAndFolderFollowTheShownFolder) {
    const fs::path dir = zui::test::scratch_dir("zmd-create-in");
    fs::create_directories(dir / "sub");
    write_text(dir / "sub" / "note.md", "# Note\n");
    const fs::path untitled = fs::u8path("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d.md");  // 未命名.md
    const std::string folder_name =
        "\xe6\x96\xb0\xe5\xbb\xba\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9";  // 新建文件夹

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir));
    ASSERT_TRUE(reveal_files(d));
    const int sub = find_row(d, "fl", "sub");
    ASSERT_GE(sub, 0);
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(sub)));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "sub");

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-file-new"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_regular_file(dir / "sub" / untitled))
        << "factory 新建文件 landed outside the shown folder";
    EXPECT_FALSE(fs::is_regular_file(dir / untitled));
    EXPECT_GE(find_row(d, "fl", untitled.u8string()), 0);
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "sub");

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-folder-new"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_directory(dir / "sub" / fs::u8path(folder_name)))
        << "factory 新建文件夹 landed outside the shown folder";
    EXPECT_FALSE(fs::is_directory(dir / fs::u8path(folder_name)));
    EXPECT_GE(find_row(d, "fl", folder_name), 0);
    EXPECT_EQ(d.get("zmd-folder-title", "text"), "sub");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// 工作目录根 sends both 新建文件 and 新建文件夹 to the workspace root.
TEST(AppInteract, ZmdNewFileAndFolderStayInTheShownFolder) {
    const fs::path dir = zui::test::scratch_dir("zmd-create-root");
    fs::create_directories(dir / "sub");
    write_text(dir / "sub" / "note.md", "# Note\n");
    const fs::path untitled = fs::u8path("\xe6\x9c\xaa\xe5\x91\xbd\xe5\x90\x8d.md");
    const std::string folder_name =
        "\xe6\x96\xb0\xe5\xbb\xba\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9";

    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, dir));
    ASSERT_TRUE(reveal_files(d));
    const int sub = find_row(d, "fl", "sub");
    ASSERT_GE(sub, 0);
    ASSERT_TRUE(d.rig.click("fl" + std::to_string(sub)));
    ASSERT_TRUE(d.rig.pump());

    ASSERT_TRUE(d.rig.click("zmd-set-status"));
    ASSERT_TRUE(d.rig.click("zmd-tab-file"));
    ASSERT_TRUE(d.rig.click("zmd-new-root"));
    ASSERT_TRUE(zui::frame::Access::key(d.rig.app, "Escape"));
    ASSERT_TRUE(d.rig.pump());

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-file-new"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_regular_file(dir / untitled))
        << "工作目录根 did not create the file in the workspace root";
    EXPECT_FALSE(fs::is_regular_file(dir / "sub" / untitled));

    ASSERT_TRUE(reveal_files(d));
    ASSERT_TRUE(d.rig.click("zmd-folder-new"));
    ASSERT_TRUE(d.rig.pump());
    EXPECT_TRUE(fs::is_directory(dir / fs::u8path(folder_name)))
        << "工作目录根 did not create the folder in the workspace root";
    EXPECT_FALSE(fs::is_directory(dir / "sub" / fs::u8path(folder_name)));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// A chip's glyphs share the neighboring baseline and sit inside the chip background.
TEST(AppInteract, ZmdCodeChipBackgroundHoldsTheGlyphs) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    auto check = [&](std::string_view chip, std::string_view neighbor) {
        const zui::paint::DisplayItem* beside = painted_text(d, neighbor);
        ASSERT_NE(beside, nullptr) << neighbor;
        const zui::frame::NodeCensus* box = nullptr;
        float best = 1.0e9f;
        for (const zui::frame::NodeCensus& row : census) {
            if (row.tag != "code" || !row.laid || row.text.find(chip) != 0) {
                continue;
            }
            const float dy = std::fabs((row.y + row.h * 0.5f) - beside->rect.y);
            if (dy < best) {
                best = dy;
                box = &row;
            }
        }
        ASSERT_NE(box, nullptr) << chip;
        ASSERT_LT(best, 24.f) << chip << " is not on the neighbor's line";
        const zui::paint::DisplayItem* glyph = glyph_in(d, *box, chip);
        ASSERT_NE(glyph, nullptr) << chip;
        EXPECT_NEAR(glyph->rect.y, beside->rect.y, 1.f)
            << chip << " baseline " << glyph->rect.y << " neighbor " << beside->rect.y;
        EXPECT_NEAR(glyph->font_px, std::round(glyph->font_px), 0.01f);
        const float em_top = glyph->rect.y - glyph->font_px;
        EXPECT_LE(box->y, em_top + 1.f) << chip << " background starts below the glyphs";
        EXPECT_GE(box->y + box->h, glyph->rect.y - 1.f)
            << chip << " background ends above the baseline";
    };
    check("assets/", "\xe4\xb8\x8b\xe7\x9a\x84\xe5\xbc\x95\xe7\x94\xa8");  // 下的引用
    check("samples/welcome.md",
          "\xe5\xb0\xb1\xe6\x98\xaf\xe8\xbf\x99\xe4\xbb\xbd\xe7\xa8\xbf\xe5\xad\x90");  // 就是这份稿子
    check("Ctrl+B", "\xe9\xa1\xb6\xe9\x83\xa8\xe6\xa0\xbc\xe5\xbc\x8f\xe6\x9d\xa1");  // 顶部格式条
    check("#", "\xe5\x8f\x98\xe6\x88\x90\xe6\xa0\x87\xe9\xa2\x98");                    // 变成标题
    check(">", "\xe5\x8f\x98\xe6\x88\x90\xe5\xbc\x95\xe7\x94\xa8");                    // 变成引用

    const zui::frame::NodeCensus* cell_chip = nullptr;
    for (const zui::frame::NodeCensus& row : census) {
        if (row.tag != "code" || !row.laid || row.text != "#") {
            continue;
        }
        if (cell_chip != nullptr && row.y <= cell_chip->y) {
            continue;
        }
        cell_chip = &row;
    }
    ASSERT_NE(cell_chip, nullptr);
    const zui::paint::DisplayItem* cell_glyph = glyph_in(d, *cell_chip, "#");
    ASSERT_NE(cell_glyph, nullptr);
    EXPECT_NEAR(cell_glyph->font_px, std::round(cell_glyph->font_px), 0.01f);
    EXPECT_LE(cell_chip->y, cell_glyph->rect.y - cell_glyph->font_px + 1.f);
    EXPECT_GE(cell_chip->y + cell_chip->h, cell_glyph->rect.y - 1.f);
    const zui::paint::DisplayItem* cell_text = painted_text(d, "Ctrl+1");
    ASSERT_NE(cell_text, nullptr);
    EXPECT_NEAR(cell_text->font_px, std::round(cell_text->font_px), 0.01f);
}

TEST(AppInteract, ZmdArchitecturePaperKeepsWordsAndFences) {
    const fs::path doc = fs::path(ZUI_SOURCE_DIR) / "docs" / "ARCHITECTURE.md";
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900, doc));
    ASSERT_TRUE(d.rig.click("zmd-view-write"));
    ASSERT_TRUE(d.rig.pump());
    ASSERT_TRUE(d.rig.pump());

    auto splits = [&](std::string_view fragment, std::string_view word) {
        for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
                continue;
            }
            if (item.text.find(fragment) != std::string::npos &&
                item.text.find(word) == std::string::npos) {
                return item.text;
            }
        }
        return std::string();
    };
    EXPECT_TRUE(splits("Tabl", "Table").empty()) << splits("Tabl", "Table");
    EXPECT_TRUE(splits("Structu", "Structure").empty()) << splits("Structu", "Structure");
    EXPECT_TRUE(splits("Behavio", "Behavior").empty()) << splits("Behavio", "Behavior");
    std::string code_break;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f) {
            continue;
        }
        if (item.text.size() >= 3 && item.text.compare(item.text.size() - 3, 3, "cod") == 0 &&
            item.text.find("code") == std::string::npos) {
            code_break = item.text;
            break;
        }
    }
    EXPECT_TRUE(code_break.empty()) << code_break;

    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    const zui::frame::NodeCensus* fence = nullptr;
    for (const zui::frame::NodeCensus& row : census) {
        if (row.tag != "code" || !row.laid || row.parent < 0 ||
            census[static_cast<std::size_t>(row.parent)].tag != "pre") {
            continue;
        }
        const bool directory = row.text.find("source/") != std::string::npos ||
                               row.run_text.find("source/") != std::string::npos;
        if (!directory) {
            continue;
        }
        if (fence == nullptr || row.h > fence->h) {
            fence = &row;
        }
    }
    ASSERT_NE(fence, nullptr) << "the directory fence has no box";
    EXPECT_GT(fence->h, 80.f) << "directory fence h=" << fence->h << " run=["
                              << fence->run_text.substr(0, 60) << "]";
    const std::string pre_id = census[static_cast<std::size_t>(fence->parent)].id;
    ASSERT_FALSE(pre_id.empty());
    ASSERT_TRUE(d.rig.app.document().scroll_into_view(pre_id));
    ASSERT_TRUE(d.rig.pump());
    const std::vector<zui::frame::NodeCensus> shown = zui::frame::Access::census(d.rig.app);
    fence = nullptr;
    for (const zui::frame::NodeCensus& row : shown) {
        if (row.tag != "code" || !row.laid || row.parent < 0 ||
            shown[static_cast<std::size_t>(row.parent)].tag != "pre") {
            continue;
        }
        if (row.run_text.find("source/") == std::string::npos &&
            row.text.find("source/") == std::string::npos) {
            continue;
        }
        if (fence == nullptr || row.h > fence->h) {
            fence = &row;
        }
    }
    ASSERT_NE(fence, nullptr);
    const zui::paint::DisplayItem* heading = painted_text(d, "Directories");
    ASSERT_NE(heading, nullptr);
    const bool heading_inside = heading->rect.y > fence->y + 4.f &&
                                heading->rect.y < fence->y + fence->h - 4.f;
    EXPECT_FALSE(heading_inside)
        << "heading y " << heading->rect.y << " sits inside the fence " << fence->y << ".."
        << fence->y + fence->h;

    std::vector<float> lines;
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::DrawText || item.blur > 0.f || item.text.empty()) {
            continue;
        }
        if (item.rect.y < fence->y - 2.f || item.rect.y > fence->y + fence->h + 2.f) {
            continue;
        }
        bool known = false;
        for (float y : lines) {
            if (std::fabs(y - item.rect.y) < 2.f) {
                known = true;
                break;
            }
        }
        if (!known) {
            lines.push_back(item.rect.y);
        }
    }
    ASSERT_GE(lines.size(), 3u) << "the fence did not stack its lines";
    std::sort(lines.begin(), lines.end());
    for (std::size_t i = 1; i < lines.size(); ++i) {
        EXPECT_GE(lines[i] - lines[i - 1], 8.f)
            << "fence lines share vertical space at " << lines[i - 1] << " and " << lines[i];
    }
}

TEST(AppInteract, ZmdChromeIconBitmapMatchesTheBox) {
    Daily d;
    ASSERT_TRUE(d.boot(1200, 900));
    ASSERT_TRUE(reveal_files(d));
    const std::vector<zui::frame::NodeCensus> census = zui::frame::Access::census(d.rig.app);
    int button = -1;
    for (std::size_t i = 0; i < census.size(); ++i) {
        if (census[i].id == "zmd-file-new") {
            button = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(button, 0);
    const zui::frame::NodeCensus* img = child_tag(census, button, "img");
    ASSERT_NE(img, nullptr);
    ASSERT_TRUE(img->laid);
    EXPECT_NEAR(img->w, 13.f, 0.6f);
    EXPECT_NEAR(img->h, 13.f, 0.6f);
    const fs::path png =
        fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zmd" / "icons" / "file-plus.png";
    EXPECT_GE(png_side(png), 13);

    const zui::frame::NodeCensus* save = nullptr;
    for (const zui::frame::NodeCensus& row : census) {
        if (row.id == "mdc-a-save" && row.laid) {
            save = &row;
            break;
        }
    }
    ASSERT_NE(save, nullptr);
    EXPECT_NEAR(save->w, 6.f, 0.6f);
    EXPECT_NEAR(save->h, 6.f, 0.6f);
    for (const zui::paint::DisplayItem& item : d.rig.renderer().last_items) {
        if (item.op != zui::paint::DisplayOp::FillRect && item.op != zui::paint::DisplayOp::ShadowRect) {
            continue;
        }
        const float cx = item.rect.x + item.rect.w * 0.5f;
        const float cy = item.rect.y + item.rect.h * 0.5f;
        if (cx < save->x - 4.f || cx > save->x + save->w + 4.f || cy < save->y - 4.f ||
            cy > save->y + save->h + 4.f) {
            continue;
        }
        EXPECT_LE(item.rect.w, save->w + 2.f) << "save dot halo width " << item.rect.w;
        EXPECT_LE(item.blur, 0.6f);
    }
}
