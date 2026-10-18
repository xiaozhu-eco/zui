#include "app/frame_session.h"
#include "fake_window.h"
#include "recording_renderer.h"

#include <zui/app.h>
#include "perf/perf_budget.h"

#include "log/log.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef ZUI_SOURCE_DIR
#error ZUI_SOURCE_DIR required
#endif

#include "../../example/applications/album/cpp/session.h"
#include "../../example/applications/calendar/cpp/session.h"
#include "../../example/applications/chat/cpp/session.h"
#include "../../example/applications/contacts/cpp/session.h"
#include "../../example/applications/kanban/cpp/session.h"
#include "../../example/applications/zdb/cpp/session.h"
#include "../../example/applications/zdb/cpp/ui/view.h"
#ifndef ZUI_WITHOUT_ZMD
#include "../../example/applications/zmd/cpp/session.h"
#endif

namespace fs = std::filesystem;

namespace {

fs::path app_html(const char* folder, const char* file = "index.html") {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / folder / file;
}

/// A private scratch path for one test. ctest runs the tests of this binary several at a time, so
/// a fixed name under the temp directory lets two processes write, remove, or lock the same file -
/// which showed up as order-dependent failures and `remove_all` lock errors.
fs::path unique_scratch(std::string_view name) {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string tag = info != nullptr ? std::string(info->name()) : std::string("no-test");
    return fs::temp_directory_path() / (std::string(name) + "-" + tag);
}

bool has_id(const std::vector<std::string>& ids, std::string_view want) {
    for (const auto& id : ids) {
        if (id == want) {
            return true;
        }
    }
    return false;
}

fs::path write_page(const char* name, const std::string& html) {
    const fs::path page = unique_scratch(name);
    std::ofstream out(page);
    out << html;
    return page;
}

bool class_token(std::string_view classes, std::string_view token) {
    std::size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && classes[i] == ' ') {
            ++i;
        }
        std::size_t j = i;
        while (j < classes.size() && classes[j] != ' ') {
            ++j;
        }
        if (classes.substr(i, j - i) == token) {
            return true;
        }
        i = j;
    }
    return false;
}

struct AttachedFrame {
    zui::App& app;
    zui::test::FakeWindow window;
    zui::test::RecordingRenderer renderer;

    explicit AttachedFrame(zui::App& app) : app(app) {}
    AttachedFrame(const AttachedFrame&) = delete;
    ~AttachedFrame() { zui::frame::Access::detach(app); }

    bool attach() {
        zui::frame::Session session;
        session.window = &window;
        session.renderer = &renderer;
        return zui::frame::Access::attach(app, session);
    }
};

void expect_next_frame_resubmits(AttachedFrame& host) {
    zui::App& app = host.app;
    ASSERT_TRUE(host.attach());
    ASSERT_TRUE(app.pump_frame());
    ASSERT_FALSE(host.renderer.submit_sizes.empty());
    const std::size_t first = host.renderer.submit_sizes.back();
    const int present1 = host.renderer.present_count;
    ASSERT_GT(first, 0u);
    ASSERT_TRUE(app.pump_frame());
    const std::size_t second = host.renderer.submit_sizes.back();
    EXPECT_GT(host.renderer.present_count, present1);
    EXPECT_EQ(second, first) << "pump 1 present=" << present1 << " submit=" << first
                             << " pump 2 present=" << host.renderer.present_count
                             << " submit=" << second;
}

void expect_clocked_frame_resubmits(AttachedFrame& host, double seconds) {
    zui::App& app = host.app;
    ASSERT_TRUE(host.attach());
    ASSERT_TRUE(app.pump_frame());
    // Hover is flushed a frame late, and that is the frame a transition starts.
    // Let it finish, then compare two frames that have nothing left to interpolate.
    zui::frame::Access::advance_clock(app, seconds);
    ASSERT_TRUE(app.pump_frame());
    zui::frame::Access::advance_clock(app, seconds);
    ASSERT_TRUE(app.pump_frame());
    ASSERT_FALSE(host.renderer.submit_sizes.empty());
    const std::size_t first = host.renderer.submit_sizes.back();
    ASSERT_GT(first, 0u);
    const int present1 = host.renderer.present_count;
    ASSERT_TRUE(app.pump_frame());
    const std::size_t second = host.renderer.submit_sizes.back();
    EXPECT_GT(host.renderer.present_count, present1);
    EXPECT_EQ(second, first) << "settled submit=" << first << " quiet submit=" << second;
}

void expect_next_frame_skips_paint(AttachedFrame& host) {
    ASSERT_TRUE(host.attach());
    ASSERT_TRUE(host.app.pump_frame());
    ASSERT_TRUE(host.app.pump_frame());
    EXPECT_EQ(zui::perf::last_frame_work().painted_walked, 0u);
}

void expect_clocked_frame_skips_paint(AttachedFrame& host, double seconds) {
    ASSERT_TRUE(host.attach());
    ASSERT_TRUE(host.app.pump_frame());
    // Same settle as the resubmit helper: the measured frame is the one after
    // the late hover transition has reached its end.
    zui::frame::Access::advance_clock(host.app, seconds);
    ASSERT_TRUE(host.app.pump_frame());
    zui::frame::Access::advance_clock(host.app, seconds);
    ASSERT_TRUE(host.app.pump_frame());
    EXPECT_EQ(zui::perf::last_frame_work().painted_walked, 0u);
}

std::vector<std::string> touched(const zui::perf::FrameWork& work) {
    return std::vector<std::string>(work.touched_ids, work.touched_ids + work.touched_id_count);
}

/// Open an overlay by clicking a chrome control, assert it lays out, then cancel it.
void expect_open_cancel(zui::App& app, const char* open_id, const char* overlay_id,
                        const char* cancel_id) {
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "false");
    ASSERT_TRUE(zui::frame::Access::click_id(app, open_id)) << open_id;
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "true") << overlay_id;
    float x = 0.f;
    float y = 0.f;
    EXPECT_TRUE(zui::frame::Access::box_center(app, overlay_id, x, y)) << overlay_id;
    EXPECT_GT(x, 0.f);
    EXPECT_GT(y, 0.f);
    ASSERT_TRUE(zui::frame::Access::click_id(app, cancel_id)) << cancel_id;
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "false") << overlay_id;
}

/// Overlays without a cancel control id: show via App::set, flush, hide.
void expect_set_overlay(zui::App& app, const char* overlay_id) {
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "false");
    ASSERT_TRUE(app.document().set(overlay_id, "visible", "true"));
    ASSERT_TRUE(zui::frame::Access::click_xy(app, 5.f, 5.f));
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "true");
    float x = 0.f;
    float y = 0.f;
    EXPECT_TRUE(zui::frame::Access::box_center(app, overlay_id, x, y));
    ASSERT_TRUE(app.document().set(overlay_id, "visible", "false"));
    ASSERT_TRUE(zui::frame::Access::click_xy(app, 5.f, 5.f));
    EXPECT_EQ(app.document().get(overlay_id, "visible"), "false");
}

int smoke_clicks(zui::App& app, int cap = 40) {
    const std::vector<std::string> ids = zui::frame::Access::interactive_ids(app);
    int ok = 0;
    const int n = static_cast<int>(ids.size()) < cap ? static_cast<int>(ids.size()) : cap;
    for (int i = 0; i < n; ++i) {
        if (zui::frame::Access::click_id(app, ids[static_cast<size_t>(i)])) {
            ++ok;
        }
    }
    return ok;
}

void expect_shell(zui::App& app, int min_ids, const char* wheel_id = nullptr) {
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    const auto ids = zui::frame::Access::interactive_ids(app);
    EXPECT_GE(static_cast<int>(ids.size()), min_ids);
    EXPECT_NO_FATAL_FAILURE({ (void)smoke_clicks(app); });
    if (wheel_id != nullptr) {
        (void)zui::frame::Access::wheel_id(app, wheel_id, 80.f);
    }
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

std::tm today_local() {
    const std::time_t now = std::time(nullptr);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &now);
#else
    localtime_r(&now, &out);
#endif
    return out;
}

/// Monday-based index of `day` in the month grid: the leading blanks of the first week plus the
/// days before it. The seed events all live on today, so this is where the test clicks.
int month_grid_index(int year, int month, int day) {
    std::tm first{};
    first.tm_year = year - 1900;
    first.tm_mon = month - 1;
    first.tm_mday = 1;
    first.tm_isdst = -1;
    if (std::mktime(&first) == static_cast<std::time_t>(-1)) {
        return -1;
    }
    return (first.tm_wday + 6) % 7 + (day - 1);
}

}  // namespace

TEST(AppInteract, Album) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("album").string()));
    album::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    album::bind_actions(session);
    album::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    const auto ids = zui::frame::Access::interactive_ids(app);
    EXPECT_GE(static_cast<int>(ids.size()), 8);
    float x = 0, y = 0;
    EXPECT_FALSE(zui::frame::Access::box_center(app, "restore", x, y));
    ASSERT_TRUE(zui::frame::Access::click_id(app, "sample-open"));
    EXPECT_TRUE(zui::frame::Access::box_center(app, "restore", x, y));
    EXPECT_TRUE(zui::frame::Access::box_center(app, "add-album", x, y));

    expect_open_cancel(app, "restore", "confirm", "confirm-cancel");
    expect_open_cancel(app, "add-album", "album-dialog", "album-form-cancel");

    EXPECT_GE(smoke_clicks(app), 1);
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

TEST(AppInteract, Calendar) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("calendar").string()));
    calendar::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    calendar::bind_actions(session);
    calendar::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    EXPECT_GE(static_cast<int>(zui::frame::Access::interactive_ids(app).size()), 10);

    expect_open_cancel(app, "create", "editor", "editor-cancel");
    ASSERT_TRUE(zui::frame::Access::click_id(app, "sample-open"));
    expect_open_cancel(app, "restore", "confirm", "confirm-cancel");

    EXPECT_TRUE(zui::frame::Access::click_xy(app, 5.f, 5.f));
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

/// Clicking an event chip in the month grid opens that event's editor. The browser gets there
/// because `view.js` builds a `<button class="chip">` per event with its own listener; the app
/// gets there because the template's chip declares `onclick="on_event"` and `data-id="{{iN}}"`,
/// and a click stops at the innermost element that declares a handler (EVENT.md section 5). The
/// day number and the empty cell around it still just select the day.
TEST(AppInteract, CalendarMonthChipOpensTheEvent) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("calendar").string()));
    calendar::Session session;
    session.app = &app;
    // A scratch directory: `show()` seeds the sample events because there is no data file, and
    // the real app's user data dir is left alone.
    session.dir = unique_scratch("zui-calendar-interact");
    fs::remove_all(session.dir);
    calendar::bind_actions(session);
    calendar::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));

    const std::tm today = today_local();
    const int index = month_grid_index(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
    ASSERT_GE(index, 0);
    ASSERT_LT(index, 42) << "today's cell has to be one of the six drawn rows";
    const float col = static_cast<float>(index % 7);
    const float row = static_cast<float>(index / 7);
    // Month grid metrics at 1100x720: the board is the 820px left of the 280px agenda, the header
    // row is 32px under the 57px top bar, and six date rows split the rest.
    const float cell_x = col * (820.0f / 7.0f);
    const float cell_y = 57.0f + 32.0f + row * ((720.0f - 57.0f - 32.0f) / 6.0f);
    // The first chip sits under the 22px day number plus its 2px margin.
    const float chip_x = cell_x + 30.0f;
    const float chip_y = cell_y + 4.0f + 1.0f + 22.0f + 2.0f + 9.0f;

    EXPECT_EQ(app.document().get("editor", "visible"), "false");
    ASSERT_TRUE(zui::frame::Access::click_xy(app, chip_x, chip_y)) << "chip at " << chip_x << "," << chip_y;
    EXPECT_EQ(app.document().get("editor", "visible"), "true");
    // The seed's earliest event on today, which is the chip the grid draws first.
    EXPECT_EQ(app.document().get("editor-title", "text"), "编辑日程");
    EXPECT_EQ(app.document().get("f-title", "value"), "校对第三章");
    EXPECT_EQ(app.document().get("f-start", "value"), "10:00");
    ASSERT_TRUE(zui::frame::Access::click_id(app, "editor-cancel"));
    EXPECT_EQ(app.document().get("editor", "visible"), "false");

    // The day number is not a chip: selecting the day still opens nothing.
    ASSERT_TRUE(zui::frame::Access::click_xy(app, cell_x + 12.0f, cell_y + 12.0f));
    EXPECT_EQ(app.document().get("editor", "visible"), "false");
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

struct TextRows : zui::ListModel {
    std::vector<std::string> rows;
    size_t row_count() const override { return rows.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "text" ? rows[row] : std::string{};
    }
};

/// The same list, but each row's identity is its text, so inserting a row in the middle leaves
/// every other row's identity intact (RFC_MODEL.md KD3: `row_key` or position).
struct KeyedRows : zui::ListModel {
    std::vector<std::string> rows;
    size_t row_count() const override { return rows.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "text" ? rows[row] : std::string{};
    }
    std::string row_key(size_t row) const override { return rows[row]; }
};

struct GridRows : zui::TableModel {
    std::vector<std::vector<std::string>> cells;
    std::vector<std::string> keys;
    size_t row_count() const override { return cells.size(); }
    size_t column_count() const override { return cells.empty() ? 0 : cells[0].size(); }
    std::string cell(size_t row, size_t col) const override { return cells[row][col]; }
    std::string column_key(size_t col) const override { return col == 0 ? "a" : "b"; }
    std::string row_key(size_t row) const override { return keys[row]; }
};

// A model re-stamp that drops rows takes those rows' boxes out of the tree. The live container
// recorded the rows, so its cached answers go with them (`forget_boxes`); the frame has to lay the
// container out again, because placement publishes a box without a result as 0x0 - the command
// palette's list stood 0x0 (and unhittable) one frame after a filter, while its kept row was fine.
TEST(AppInteract, ReStampedFlexListKeepsItsBox) {
    const fs::path page = unique_scratch("zui-restamp-list.html");
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><head><style>"
               "#host{display:flex;flex-direction:column;gap:1px;margin:0;padding:5px;"
               "list-style:none;}"
               ".row{display:flex;width:100%;padding:8px 10px;}"
               "</style></head><body><ul id=\"host\"><template>"
               "<li><button class=\"row\" id=\"r{{index}}\">{{text}}</button></li>"
               "</template></ul></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app, 800, 600));
    TextRows all;
    all.rows = {"a", "b", "c", "d", "e", "f", "g", "h"};
    ASSERT_TRUE(app.document().set_model("host", all));
    zui::frame::Access::sync_observation(app);
    zui::frame::Access::sync_observation(app);
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "host", x, y, w, h))
        << "the first stamp sized the list";
    const float first_height = h;

    TextRows one;
    one.rows = {"a"};
    ASSERT_TRUE(app.document().set_model("host", one));
    zui::frame::Access::sync_observation(app);
    zui::frame::Access::sync_observation(app);

    float rx = 0.f, ry = 0.f, rw = 0.f, rh = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "r0", rx, ry, rw, rh));
    EXPECT_FALSE(zui::frame::Access::box_rect(app, "r1", x, y, w, h))
        << "the retired rows are gone";
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "host", x, y, w, h))
        << "the container lost its box when the retired rows were freed";
    EXPECT_LT(h, first_height) << "seven rows left, so the list shrank";
    EXPECT_GE(h, rh) << "the container still holds its row";
    // The kept row answers the hit test where it stands: the walk enters the container's box.
    const std::string chain =
        zui::frame::Access::hit_chain(app, rx + rw * 0.5f, ry + rh * 0.5f);
    EXPECT_NE(chain.find("#r0"), std::string::npos) << chain;
}

TEST(AppInteract, ReplaceFragmentDropsTheOldChildren) {
    const fs::path page = write_page(
        "zui-replace-frag.html",
        "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
        "</style></head><body><div id=\"host\"><p id=\"keep\">old</p><p id=\"gone\">old</p>"
        "</div></body></html>\n");
    const fs::path first =
        write_page("zui-replace-frag-a.html",
                   "<!DOCTYPE html><html><body><p id=\"a\">a</p></body></html>\n");
    const fs::path second =
        write_page("zui-replace-frag-b.html",
                   "<!DOCTYPE html><html><body><p id=\"b\">b</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    // The append call keeps what the fragment does not name - the semantics dialogs rely on.
    ASSERT_TRUE(app.document().load_fragment(first.string(), "host"));
    zui::frame::Access::sync_observation(app);
    EXPECT_TRUE(zui::frame::Access::box_rect(app, "keep", x, y, w, h));
    EXPECT_TRUE(zui::frame::Access::box_rect(app, "gone", x, y, w, h));
    EXPECT_TRUE(zui::frame::Access::box_rect(app, "a", x, y, w, h));

    // The replace call is the host's whole content: everything it held leaves, not only the ids
    // the new fragment names.
    ASSERT_TRUE(app.document().replace_fragment(second.string(), "host"));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "b", x, y, w, h));
    EXPECT_FALSE(zui::frame::Access::box_rect(app, "a", x, y, w, h))
        << "the node the earlier render loaded stays";
    EXPECT_FALSE(zui::frame::Access::box_rect(app, "keep", x, y, w, h))
        << "a host child stays";
    EXPECT_FALSE(zui::frame::Access::box_rect(app, "gone", x, y, w, h))
        << "a host child stays";
    // Hit testing agrees: the host's content is the one new block.
    const std::string chain =
        zui::frame::Access::hit_chain(app, x + w * 0.5f, y + h * 0.5f);
    EXPECT_NE(chain.find("#b"), std::string::npos) << chain;
}

TEST(AppInteract, SetModelSameIsFree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = unique_scratch("zui-perf-p4.html");
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><body><ul id=\"host\"><template><li>{{text}}</li></template></ul>"
               "</body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    TextRows model;
    model.rows = {"a", "b"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    ASSERT_GT(zui::perf::last_frame_work().cascade_visited, 0u);

    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.style_interned, 0u);
    EXPECT_EQ(second.boxes_created, 0u);
    // I2: the same model re-applied frees nothing either (PERF.md section 2.2 `box.destroyed`).
    EXPECT_EQ(second.boxes_destroyed, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.relowers, 0u);
}

TEST(AppInteract, IdleObservationIsFree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = unique_scratch("zui-perf-p7.html");
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><body><p id=\"t\">idle</p></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);
    for (int i = 0; i < 3; ++i) {
        zui::frame::Access::sync_observation(app);
        const zui::perf::FrameWork frame = zui::perf::last_frame_work();
        EXPECT_EQ(frame.cascade_visited, 0u) << i;
        EXPECT_EQ(frame.style_interned, 0u) << i;
        EXPECT_EQ(frame.boxes_created, 0u) << i;
        EXPECT_EQ(frame.laid_out, 0u) << i;
        EXPECT_EQ(frame.painted_walked, 0u) << i;
        EXPECT_EQ(frame.hit_boxes, 0u) << i;
        EXPECT_EQ(frame.world_walked, 0u) << i;
    }
}

TEST(AppInteract, ScrollDoesNotCascadeOrLayout) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = unique_scratch("zui-perf-p8.html");
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><body><div id=\"s\" style=\"overflow:auto;height:40px;"
               "width:80px\"><div style=\"height:400px\">x</div></div></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().set("s", "scroll_top", "30"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork frame = zui::perf::last_frame_work();
    EXPECT_EQ(frame.cascade_visited, 0u);
    EXPECT_EQ(frame.laid_out, 0u);
}

TEST(AppInteract, LocalTextEditDoesNotGrowWithRowCount) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    auto measure = [](int n) {
        std::string html = "<!DOCTYPE html><html><head><style>"
                           "#board{position:relative;width:120px;height:20px}"
                           ".row{position:absolute;left:0;top:0;width:80px;height:10px}"
                           "</style></head><body><div id=\"board\">";
        for (int i = 0; i < n; ++i) {
            html += "<p id=\"r" + std::to_string(i) + "\" class=\"row\">a</p>";
        }
        html += "</div></body></html>\n";
        const fs::path page =
            fs::temp_directory_path() / ("zui-perf-p9-" + std::to_string(n) + ".html");
        {
            std::ofstream out(page);
            out << html;
        }
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);
        const auto t0 = std::chrono::steady_clock::now();
        EXPECT_TRUE(app.document().set("r0", "text", "b"));
        zui::frame::Access::sync_observation(app);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        EXPECT_LT(ms, 50.0) << n;
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork small = measure(10);
    const zui::perf::FrameWork mid = measure(100);
    const zui::perf::FrameWork large = measure(1000);
    EXPECT_EQ(small.cascade_visited, mid.cascade_visited);
    EXPECT_EQ(mid.cascade_visited, large.cascade_visited);
    EXPECT_EQ(small.style_interned, mid.style_interned);
    EXPECT_EQ(mid.style_interned, large.style_interned);
    EXPECT_EQ(small.laid_out, mid.laid_out);
    EXPECT_EQ(mid.laid_out, large.laid_out);
}

TEST(AppInteract, SameTextSetIsFree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path dir = unique_scratch("zui-perf-i2");
    fs::create_directories(dir);
    const fs::path page = dir / "i2.html";
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><body><p id=\"t\">hi</p></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    ASSERT_TRUE(app.document().set("t", "text", "next"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork first = zui::perf::last_frame_work();
    // The paragraph already had text, so :empty does not flip and the cascade stays put.
    // The line is still measured.
    EXPECT_GT(first.laid_out, 0u);
    EXPECT_EQ(first.cascade_visited, 0u);

    ASSERT_TRUE(app.document().set("t", "text", "next"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.cascade_recomputed, 0u);
    EXPECT_EQ(second.style_interned, 0u);
    EXPECT_EQ(second.boxes_created, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.cascade_rounds, 0u);
}

TEST(AppInteract, EmptyToFirstCharIsIdempotent) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page("zui-perf-p1.html",
                                     "<!DOCTYPE html><html><body><p id=\"t\"></p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    // ':empty' can flip, so the first write re-cascades; the line is measured too.
    ASSERT_TRUE(app.document().set("t", "text", "a"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork first = zui::perf::last_frame_work();
    EXPECT_GT(first.cascade_visited, 0u);
    EXPECT_GT(first.laid_out, 0u);

    ASSERT_TRUE(app.document().set("t", "text", "a"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.cascade_recomputed, 0u);
    EXPECT_EQ(second.style_interned, 0u);
    EXPECT_EQ(second.boxes_created, 0u);
    EXPECT_EQ(second.laid_out, 0u);
}

TEST(AppInteract, ClassToggleScope) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-p2.html",
        "<!DOCTYPE html><html><head><style>"
        "#box{width:50px;height:50px}"
        ".paint{background-color:rgb(1,2,3)}"
        "#box.geom{width:120px}"
        "</style></head><body><div id=\"box\"></div></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    // A class the painter reads from computed style: no box churn, no layout.
    ASSERT_TRUE(app.document().add_class("box", "paint"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork paint = zui::perf::last_frame_work();
    EXPECT_EQ(paint.boxes_created, 0u);
    EXPECT_EQ(paint.laid_out, 0u);

    // add_class already short-circuits a token the element has (docs/PERF.md I2).
    ASSERT_TRUE(app.document().add_class("box", "paint"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork repeat = zui::perf::last_frame_work();
    EXPECT_EQ(repeat.cascade_visited, 0u);
    EXPECT_EQ(repeat.style_interned, 0u);
    EXPECT_EQ(repeat.boxes_created, 0u);
    EXPECT_EQ(repeat.laid_out, 0u);

    // A class that changes a layout property: still no box churn, but the box is measured.
    ASSERT_TRUE(app.document().add_class("box", "geom"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork geom = zui::perf::last_frame_work();
    EXPECT_EQ(geom.boxes_created, 0u);
    EXPECT_GT(geom.laid_out, 0u);
    EXPECT_FALSE(geom.touched_ids_truncated);
    EXPECT_TRUE(has_id(touched(geom), "box"));
}

TEST(AppInteract, VisibilityToggleScope) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-p3.html",
        "<!DOCTYPE html><html><head><style>[hidden]{display:none}</style></head><body>"
        "<div id=\"host\"><p id=\"a\">a</p><p id=\"b\">b</p></div>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().set("a", "visible", "false"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork hidden = zui::perf::last_frame_work();
    EXPECT_FALSE(hidden.touched_ids_truncated);
    // The host's child list is what changed; nothing above it rebuilds (docs/PERF.md I4) and the
    // removed node's boxes are not recreated.
    EXPECT_EQ(hidden.relowers, 0u);
    EXPECT_FALSE(has_id(touched(hidden), "a"));
    EXPECT_LE(hidden.boxes_created, 2u);
    // The style closure is the hidden element's own subtree, not the parent's.
    EXPECT_EQ(hidden.cascade_visited, 1u);
}

TEST(AppInteract, SetModelKeepsEqualRows) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-p5.html",
        "<!DOCTYPE html><html><body><ul id=\"host\">"
        "<template><li id=\"row-{{text}}\">{{text}}</li></template></ul></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    TextRows model;
    model.rows = {"a", "b", "c"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);

    // A row keeps a rendered fingerprint only if its own text is unchanged, and each row's box
    // carries that row's `id`, so `note_laid_box` names the row a box belongs to.
    model.rows = {"a2", "b2", "c2"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork all = zui::perf::last_frame_work();
    ASSERT_GT(all.boxes_created, 0u);
    ASSERT_TRUE(has_id(touched(all), "row-a2"));
    ASSERT_TRUE(has_id(touched(all), "row-b2"));
    ASSERT_TRUE(has_id(touched(all), "row-c2"));

    auto row_runs = [&]() {
        std::vector<std::pair<std::string, std::string>> out;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (row.tag == "li" && row.laid) {
                out.emplace_back(row.id, row.run_text);
            }
        }
        return out;
    };
    const std::vector<std::pair<std::string, std::string>> runs_before = row_runs();
    std::string seen;
    for (const auto& row : runs_before) {
        seen += " [" + row.first + "]=" + row.second;
    }
    ASSERT_EQ(runs_before.size(), 3u) << seen;

    // Stage two of docs/PERF.md section 4's `set_model` contract: one changed row rebuilds that
    // row only, and the rows that kept their node are not laid out again. The template holds no
    // `{{index}}` / `{{row}}`, so no kept row's fingerprint moves with its position.
    model.rows[1] = "b3";
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_FALSE(work.touched_ids_truncated);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_TRUE(has_id(touched(work), "host"));
    EXPECT_TRUE(has_id(touched(work), "row-b3"));
    EXPECT_FALSE(has_id(touched(work), "row-a2"));
    EXPECT_FALSE(has_id(touched(work), "row-c2"));
    ASSERT_EQ(all.boxes_created % 3u, 0u);
    EXPECT_EQ(work.boxes_created, all.boxes_created / 3u);
    // A kept row is still painting the run it had: the placement walk cleared `geo.runs` and the
    // frame wrote the same single line back (docs/RFC_MODEL.md wave 1, `refill_runs`).
    const std::vector<std::pair<std::string, std::string>> runs_after = row_runs();
    ASSERT_EQ(runs_after.size(), 3u) << seen;
    EXPECT_EQ(runs_after[0], runs_before[0]);
    EXPECT_NE(runs_after[1].second, runs_before[1].second);
    EXPECT_EQ(runs_after[2], runs_before[2]);
}

TEST(AppInteract, SetModelInsertKeepsTheMovedRows) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `:nth-child(2n)` makes a row's own computed style depend on its position, so a row that
    // moves is allowed to lay out again - it must not be rebuilt (RFC_MODEL.md KD3).
    const fs::path page = write_page(
        "zui-perf-p5-insert.html",
        "<!DOCTYPE html><html><head><style>li:nth-child(2n){height:40px}</style></head>"
        "<body><ul id=\"host\">"
        "<template><li id=\"row-{{text}}\">{{text}}</li></template></ul></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    KeyedRows model;
    model.rows = {"a", "b", "c"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);

    // Baseline: every row changes, so `boxes_created` is the cost of three rows.
    model.rows = {"a2", "b2", "c2"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork all = zui::perf::last_frame_work();
    ASSERT_GT(all.boxes_created, 0u);
    ASSERT_EQ(all.boxes_created % 3u, 0u);

    // Insert between b2 and c2. Identity is the text, so c2 keeps its node and only moves.
    model.rows = {"a2", "b2", "x", "c2"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_FALSE(work.touched_ids_truncated);
    EXPECT_EQ(work.relowers, 0u);
    EXPECT_TRUE(has_id(touched(work), "host"));
    EXPECT_TRUE(has_id(touched(work), "row-x"));
    EXPECT_FALSE(has_id(touched(work), "row-a2"));
    EXPECT_FALSE(has_id(touched(work), "row-b2"));
    // `row-c2` is allowed to appear: it moved from index 3 to 4, so `:nth-child(2n)` changed its
    // height and it laid out again. What must not happen is a *created* box outside the new row.
    EXPECT_EQ(work.boxes_created, all.boxes_created / 3u);

    // Nothing was lost on the way: all four rows still paint their own text. A `li` carries its
    // UA list marker as a run of its own, so compare the leading text.
    std::vector<std::string> texts;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.tag == "li" && row.laid) {
            texts.push_back(row.run_text);
        }
    }
    ASSERT_EQ(texts.size(), 4u);
    const char* want[] = {"a2", "b2", "x", "c2"};
    for (std::size_t i = 0; i < texts.size(); ++i) {
        EXPECT_EQ(texts[i].rfind(want[i], 0), 0u) << texts[i];
    }
}

TEST(AppInteract, SetModelKeptRowsKeepAbsorbedInlineTextOnTheParentLine) {
    const fs::path page = write_page(
        "zui-perf-p5-inline.html",
        "<!DOCTYPE html><html><head><style>#host{width:120px}</style></head><body>"
        "<ul id=\"host\"><template><li><span class=\"sub\">{{text}} alpha beta gamma delta "
        "epsilon zeta eta theta</span></li></template></ul></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    TextRows model;
    model.rows = {"a", "b"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);

    // A span whose text wrapped is absorbed into the row's line: the row paints the glyphs, and the
    // span's own `geo.runs` stay empty. Refilling that box would paint every character twice
    // (docs/RFC_MODEL.md wave 1, `inline_text_owned_by_parent`).
    auto absorbed_runs = [&]() {
        std::vector<std::string> out;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_token(row.class_attr, "sub") && row.laid) {
                out.push_back(row.run_text);
            }
        }
        return out;
    };
    const std::vector<std::string> before = absorbed_runs();
    ASSERT_EQ(before.size(), 2u);
    for (const std::string& run : before) {
        EXPECT_TRUE(run.empty()) << "run_text=" << run;
    }

    // One row is kept; the other is rebuilt. Neither may hand its glyphs to the span.
    model.rows[1] = "B";
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const std::vector<std::string> after = absorbed_runs();
    ASSERT_EQ(after.size(), 2u);
    for (const std::string& run : after) {
        EXPECT_TRUE(run.empty()) << "run_text=" << run;
    }
}

TEST(AppInteract, ToggleLocalToTheControl) {
    const fs::path page = write_page(
        "zui-perf-p6.html",
        "<!DOCTYPE html><html><body>"
        "<label><input id=\"cb\" type=\"checkbox\"> on</label>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "cb"));
    zui::frame::Access::sync_observation(app);
    EXPECT_EQ(app.document().get("cb", "checked"), "true");
}

TEST(AppInteract, ToggleLocalToTheControlStaysLocal) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-p6.html",
        "<!DOCTYPE html><html><body>"
        "<label><input id=\"cb\" type=\"checkbox\"> on</label>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "cb"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_FALSE(work.touched_ids_truncated);
    EXPECT_EQ(work.boxes_created, 0u);
    EXPECT_LE(work.laid_out, 2u);
}

TEST(AppInteract, CollapsedTableVariesEveryCell) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-p10.html",
        "<!DOCTYPE html><html><head><style>"
        "table{border-collapse:collapse}"
        "td{width:30px;height:20px;border:1px solid black}"
        ".wide{border:4px solid black}"
        "</style></head><body><table><tr><td id=\"c1\">a</td><td>b</td></tr>"
        "<tr><td>c</td><td>d</td></tr></table></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().add_class("c1", "wide"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_FALSE(work.touched_ids_truncated);
    // Changing one cell re-interns the whole collapsing table, not just that cell (PERF.md
    // section 2.4 item 5).
    EXPECT_GE(work.style_interned, 5u);
    EXPECT_GT(work.laid_out, 0u);
}

TEST(AppInteract, LocalWriteCascadesOnlyTheClosure) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // The sheets never leave the changed subtree (no `:has()`, no `+` / `~`), so the closure of a
    // write on a leaf is that leaf: the cascade may not visit the rest of the document
    // (docs/PERF.md I3 and the reverse case of section 6.4).
    std::string html = "<!DOCTYPE html><html><head><style>#b{color:rgb(1,2,3)}"
                       ".hot{color:rgb(4,5,6)}"
                       "body{margin:0}p{margin:0;height:8px}"
                       "</style></head><body>";
    for (int i = 0; i < 20; ++i) {
        html += "<p id=\"p" + std::to_string(i) + "\">row</p>";
    }
    html += "<p id=\"b\">b</p></body></html>\n";
    const fs::path page = write_page("zui-perf-i3-local.html", html);
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().add_class("b", "hot"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_EQ(work.cascade_visited, 1u) << "the closure of a leaf write is that leaf";
    EXPECT_EQ(work.cascade_rounds, 1u);
    // I5 audit: a write confined to the closure does not trip a root retry or a wall second pass
    // (docs/PERF.md section 6.4's fixture note).
    EXPECT_EQ(work.bar_retries, 0u);
    EXPECT_EQ(work.dirty_retries, 0u);
    EXPECT_EQ(work.flow_remeasures, 0u);
    EXPECT_EQ(work.flex_bar_reruns, 0u);
    EXPECT_EQ(work.grid_bar_reruns, 0u);
    // The layout stays inside the closure too.
    EXPECT_LE(work.laid_out, 3u);
}

TEST(AppInteract, HoverCompoundWithAttributeStaysLocal) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `.btn[data-x]:hover` is a hover compound whose other half is an attribute. The attribute is
    // evaluated by the same matcher the cascade uses, so a pointer move onto that leaf re-cascades
    // the leaf, not every node (docs/PERF.md I3; PERF-NOTES A2).
    std::string html = "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
                       ".btn[data-x]:hover{color:rgb(1,2,3)}"
                       "</style></head><body><div id=\"wrap\">"
                       "<p id=\"leaf\" class=\"btn\" data-x=\"1\">hover</p></div>";
    for (int i = 0; i < 30; ++i) {
        html += "<p id=\"r" + std::to_string(i) + "\">row</p>";
    }
    html += "</body></html>\n";
    const fs::path page = write_page("zui-perf-hover-attr.html", html);
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "leaf"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    // The hover chain node that can restyle is the leaf; the wrap / body / html do not carry the
    // classes, so the closure stays at one node per cascade round (press + release).
    EXPECT_LE(work.cascade_visited, 4u) << "the hover compound's attribute half is evaluable";
}

TEST(AppInteract, HasClosureStaysAtTheAnchor) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `.wrap:has(.flag) .menu` can only flip at `.wrap` - the argument looks down, and the subject
    // is inside the anchor's subtree. A write outside `.wrap` therefore cascades only the written
    // node, and a write that flips the `:has()` cascades `.wrap`'s subtree - not the 30 rows
    // outside it (docs/PERF.md section 2.4 rule 3; a `:has()` page used to be whole-document).
    std::string html = "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
                       ".wrap:has(.flag) .menu{color:rgb(1,2,3)}"
                       ".hot{color:rgb(4,5,6)}"
                       "</style></head><body><div id=\"wrap\">"
                       "<p id=\"menu\" class=\"menu\">m</p><p id=\"flag\" class=\"flag\">f</p></div>";
    for (int i = 0; i < 30; ++i) {
        html += "<p id=\"r" + std::to_string(i) + "\">row</p>";
    }
    html += "</body></html>\n";
    const fs::path page = write_page("zui-perf-has-anchor.html", html);
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    // Outside the anchor: the `:has()` cannot change, so the closure is the written node.
    ASSERT_TRUE(app.document().add_class("r0", "hot"));
    zui::frame::Access::sync_observation(app);
    zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_EQ(work.cascade_visited, 1u) << "a write outside the anchor does not reach it";

    // Flipping the argument: the closure is the anchor's subtree (wrap + menu + flag).
    ASSERT_TRUE(app.document().remove_class("flag", "flag"));
    zui::frame::Access::sync_observation(app);
    work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_LE(work.cascade_visited, 4u) << "the anchor subtree is the whole closure";
}

TEST(AppInteract, IdHasStaysOffTheUnrelatedWrite) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `.app:has(#switch:checked)` names one element. A class change on a cell that is not that
    // element does not flip the function, at either document size (docs/PERF.md I3). Checking
    // the switch, or removing it, still restyles the anchor.
    const auto page_of = [](int rows) {
        std::string html = "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
                           ".app:has(#switch:checked) .cell{color:rgb(1,2,3)}"
                           ".hot{color:rgb(4,5,6)}"
                           "</style></head><body><div id=\"app\" class=\"app\">"
                           "<input id=\"switch\" type=\"checkbox\">";
        for (int i = 0; i < rows; ++i) {
            html += "<p id=\"c" + std::to_string(i) + "\" class=\"cell\">x</p>";
        }
        html += "</div></body></html>\n";
        return html;
    };
    const auto visited_after_class = [&](int rows) {
        const fs::path page = write_page("zui-perf-id-has.html", page_of(rows));
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);
        EXPECT_TRUE(app.document().add_class("c0", "hot"));
        zui::frame::Access::sync_observation(app);
        return zui::perf::last_frame_work().cascade_visited;
    };
    EXPECT_EQ(visited_after_class(20), 1u);
    EXPECT_EQ(visited_after_class(80), 1u);

    const fs::path page = write_page("zui-perf-id-has.html", page_of(20));
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().add_class("c0", "hot"));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().add_class("c0", "hot"));
    zui::frame::Access::sync_observation(app);
    EXPECT_EQ(zui::perf::last_frame_work().cascade_visited, 0u);

    ASSERT_TRUE(app.document().set("switch", "checked", "true"));
    zui::frame::Access::sync_observation(app);
    EXPECT_GT(zui::perf::last_frame_work().cascade_visited, 20u);

    ASSERT_TRUE(app.document().unload_fragment("switch"));
    zui::frame::Access::sync_observation(app);
    EXPECT_GT(zui::perf::last_frame_work().cascade_visited, 10u);
}

TEST(AppInteract, SiblingEdgeStaysOnTheLeftCompound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `#theme:checked ~ .app` must not pull every following sibling of an unrelated cell.
    // The same edit at two document sizes visits that cell alone.
    const auto page_of = [](int rows) {
        std::string html = "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
                           "#theme:checked ~ .app .cell{color:rgb(1,2,3)}"
                           ".hot{color:rgb(4,5,6)}"
                           "</style></head><body>"
                           "<input id=\"theme\" type=\"checkbox\">"
                           "<div id=\"app\" class=\"app\"><p id=\"cell\" class=\"cell\">day</p>";
        for (int i = 0; i < rows; ++i) {
            html += "<p id=\"r" + std::to_string(i) + "\">row</p>";
        }
        html += "</div></body></html>\n";
        return html;
    };
    const auto visited_after_class = [&](int rows) {
        const fs::path page = write_page("zui-perf-sibling-edge.html", page_of(rows));
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);
        EXPECT_TRUE(app.document().add_class("cell", "hot"));
        zui::frame::Access::sync_observation(app);
        return zui::perf::last_frame_work().cascade_visited;
    };
    EXPECT_EQ(visited_after_class(20), 1u);
    EXPECT_EQ(visited_after_class(80), 1u);

    const fs::path page = write_page("zui-perf-sibling-edge.html", page_of(20));
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().set("theme", "checked", "true"));
    zui::frame::Access::sync_observation(app);
    EXPECT_GT(zui::perf::last_frame_work().cascade_visited, 20u);
}

TEST(AppInteract, ShrinkToFitSkipsTheUnchangedSecondWalk) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // An inline-block whose max-content already fits is laid out by the first indefinite walk.
    // A block sibling that is narrower still stretches, and text that does not fit still wraps.
    const fs::path page = write_page(
        "zui-perf-shrink-walk.html",
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0}div,span{margin:0}"
        "#fit{display:inline-block}"
        "#wide{display:inline-block;width:240px}"
        "#short,#long{display:block;height:12px}"
        "#narrow{width:30px}"
        "#wrap,#line{display:inline-block}"
        "#line{white-space:nowrap}"
        "</style></head><body>"
        "<span id=\"fit\">hi</span>"
        "<div id=\"wide\"><div id=\"short\">s</div><div id=\"long\">longer label text</div></div>"
        "<div id=\"narrow\"><span id=\"wrap\">one two three four five</span></div>"
        "<div id=\"open\"><span id=\"line\">one two three four five</span></div>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    const zui::perf::FrameWork cold = zui::perf::last_frame_work();
    float fit_x = 0, fit_y = 0, fit_w = 0, fit_h = 0;
    float short_x = 0, short_y = 0, short_w = 0, short_h = 0;
    float long_x = 0, long_y = 0, long_w = 0, long_h = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "fit", fit_x, fit_y, fit_w, fit_h));
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "short", short_x, short_y, short_w, short_h));
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "long", long_x, long_y, long_w, long_h));
    EXPECT_GT(fit_w, 1.f);
    EXPECT_LT(fit_w, 80.f);
    EXPECT_NEAR(short_w, long_w, 1.f);
    const float wrap_h = std::stof(zui::frame::Access::measure(app, "wrap", "content_height"));
    const float line_h = std::stof(zui::frame::Access::measure(app, "line", "content_height"));
    EXPECT_GT(wrap_h, line_h + 4.f);
    // The fitting inline-block and the definite 240px box do not take a second walk. The narrow
    // inline-block does, because its used width is below max-content.
    EXPECT_GE(cold.flow_remeasures, 1u);
    EXPECT_LE(cold.flow_remeasures, 6u) << "flow=" << cold.flow_remeasures;
}

TEST(AppInteract, ShrinkToFitStretchDoesNotWalkEveryAncestor) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // Nested shrink-to-fit blocks used to second-walk at every level. The indefinite levels only
    // contribute a size; the definite inline-block stretches the short sibling once. A real wrap
    // still takes its second walk. Deeper nesting must not add walks.
    const auto run = [](int depth) {
        std::string html =
            "<!DOCTYPE html><html><head><style>body{margin:0}div{margin:0}"
            "#host{display:inline-block}</style></head><body><div id=\"host\">";
        for (int i = 0; i < depth; ++i) {
            html += "<div id=\"n" + std::to_string(i) + "\">";
        }
        html += "<div id=\"short\">s</div><div id=\"long\">longer label text</div>";
        html += "<div id=\"full\" style=\"width:100%\">x</div>";
        for (int i = 0; i < depth; ++i) {
            html += "</div>";
        }
        html += "</div><div id=\"wrap\" style=\"display:inline-block;width:30px\">"
                "one two three four five</div></body></html>\n";
        const std::string name = "zui-perf-shrink-depth-" + std::to_string(depth) + ".html";
        const fs::path page = write_page(name.c_str(), html);
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        float short_w = 0, long_w = 0, full_w = 0, wrap_h = 0;
        float x = 0, y = 0, h = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "short", x, y, short_w, h));
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "long", x, y, long_w, h));
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "full", x, y, full_w, h));
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "wrap", x, y, x, wrap_h));
        EXPECT_NEAR(short_w, long_w, 1.f);
        EXPECT_NEAR(full_w, long_w, 1.f);
        EXPECT_GT(wrap_h, h + 4.f);
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork shallow = run(2);
    const zui::perf::FrameWork deep = run(8);
    EXPECT_EQ(shallow.flow_remeasures, deep.flow_remeasures);
    EXPECT_LE(deep.flow_remeasures, 4u) << "flow=" << deep.flow_remeasures;
}

TEST(AppInteract, FlexBaseSkipsMinWhenTheInlineSizeMatches) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A nowrap flex item's min-content border is the flex-base border. The item pass keeps that
    // result. A wrapping item is narrower than its max-content line, so the min ask still runs
    // and the item still shrinks inside a fixed row.
    const auto row = [](bool nowrap) {
        std::string html = std::string("<!DOCTYPE html><html><head><style>body{margin:0}") +
                           "span{white-space:" + (nowrap ? "nowrap" : "normal") +
                           "}</style></head><body><div id=\"row\" style=\"display:flex\">";
        for (int i = 0; i < 16; ++i) {
            html += "<span id=\"s" + std::to_string(i) + "\">alpha beta gamma</span>";
        }
        html += "</div></body></html>\n";
        const std::string name =
            std::string("zui-perf-flex-min-") + (nowrap ? "nowrap" : "wrap") + ".html";
        const fs::path page = write_page(name.c_str(), html);
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        float x = 0, y = 0, w = 0, h = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "s0", x, y, w, h));
        if (nowrap) {
            EXPECT_GT(w, 50.f) << "nowrap item collapsed to one word";
        }
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork wrapping = row(false);
    const zui::perf::FrameWork plain = row(true);
    EXPECT_LE(plain.layout_stores + 8u, wrapping.layout_stores)
        << "nowrap=" << plain.layout_stores << " wrap=" << wrapping.layout_stores;

    const fs::path narrow = write_page(
        "zui-perf-flex-min-narrow.html",
        "<!DOCTYPE html><html><head><style>body{margin:0}</style></head><body>"
        "<div id=\"box\" style=\"display:flex;width:80px\">"
        "<span id=\"item\">alpha beta gamma delta</span></div></body></html>\n");
    zui::App narrow_app(0, nullptr);
    ASSERT_TRUE(narrow_app.document().load(narrow.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(narrow_app));
    float x = 0, y = 0, w = 0, h = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(narrow_app, "item", x, y, w, h));
    EXPECT_LE(w, 82.f) << "wrapping item kept its max-content width";
    EXPECT_GT(h, 28.f) << "wrapping item stayed on one line";
}

TEST(AppInteract, MinMaxContentAskCollapsesWhenTheInlineSizeMatches) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A nowrap cell's max-content inline size is the min-content size. The table and the grid
    // keep that one result. A wrapping grid item still contributes its full max-content width,
    // and a narrow block still wraps.
    const auto run = [](bool nowrap) {
        const char* cell_ws = nowrap ? "nowrap" : "normal";
        std::string html = std::string("<!DOCTYPE html><html><head><style>body{margin:0}") +
                           "td{padding:0;white-space:" + cell_ws + "}" +
                           "#grid{display:grid;grid-template-columns:max-content max-content}" +
                           "#col{display:flex;flex-direction:column;white-space:nowrap}" +
                           "#narrow{width:40px}" +
                           "</style></head><body><table id=\"t\"><tr>";
        for (int i = 0; i < 12; ++i) {
            html += "<td id=\"c" + std::to_string(i) + "\">alpha beta gamma</td>";
        }
        html += "</tr></table><div id=\"grid\"><div id=\"col\">";
        for (int i = 0; i < 8; ++i) {
            html += "<span>alpha beta gamma</span>";
        }
        html += "</div><div id=\"wrapcol\">one two three four five six</div></div>"
                "<div id=\"narrow\">one two three four five</div></body></html>\n";
        const std::string name =
            std::string("zui-perf-minmax-") + (nowrap ? "nowrap" : "wrap") + ".html";
        const fs::path page = write_page(name.c_str(), html);
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        float x = 0, y = 0, w = 0, h = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "c0", x, y, w, h));
        EXPECT_GT(w, 50.f) << "cell collapsed to one word";
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "wrapcol", x, y, w, h));
        EXPECT_GT(w, 80.f) << "wrapping grid item used min-content";
        const float narrow_h =
            std::stof(zui::frame::Access::measure(app, "narrow", "content_height"));
        EXPECT_GT(narrow_h, 20.f);
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork wrapped = run(false);
    const zui::perf::FrameWork plain = run(true);
    EXPECT_LE(plain.layout_stores + 12u, wrapped.layout_stores)
        << "nowrap=" << plain.layout_stores << " wrap=" << wrapped.layout_stores;
}

TEST(AppInteract, TableRowReusesTheCellFinal) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // Cells in one row sit side by side. A row whose border block already matches the cell's
    // Final does not store that Final again, so a uniform table lays out fewer boxes than the
    // same grid with one stretched cell. The short cell still grows to the tall sibling.
    const auto load = [](const std::string& html) {
        const fs::path page = write_page("zui-perf-table-reuse.html", html);
        auto app = std::make_unique<zui::App>(0, nullptr);
        EXPECT_TRUE(app->document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(*app));
        return app;
    };
    const char* kBase =
        "<!DOCTYPE html><html><head><style>body{margin:0}td{padding:0;white-space:nowrap}"
        "</style></head><body>";
    auto uniform = load(std::string(kBase) +
                        "<table><tr><td id=\"a\">alpha</td><td id=\"b\">beta beta</td></tr>"
                        "<tr><td id=\"a2\">alpha</td><td id=\"b2\">beta beta</td></tr>"
                        "<tr><td id=\"a3\">alpha</td><td id=\"b3\">beta beta</td></tr>"
                        "</table></body></html>\n");
    float ax = 0, ay = 0, aw = 0, ah = 0;
    float bx = 0, by = 0, bw = 0, bh = 0;
    float a2x = 0, a2y = 0, a2w = 0, a2h = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(*uniform, "a", ax, ay, aw, ah));
    ASSERT_TRUE(zui::frame::Access::box_rect(*uniform, "b", bx, by, bw, bh));
    ASSERT_TRUE(zui::frame::Access::box_rect(*uniform, "a2", a2x, a2y, a2w, a2h));
    EXPECT_GT(bx, ax + 20.f) << "cells in a row stacked";
    EXPECT_NEAR(ay, by, 1.5f);
    EXPECT_GT(a2y, ay + 4.f);
    EXPECT_NEAR(ah, bh, 1.5f);
    const zui::perf::FrameWork uniform_work = zui::perf::last_frame_work();

    auto stretched = load(std::string(kBase) +
                          "<table><tr><td id=\"short\">alpha</td>"
                          "<td id=\"tall\"><div style=\"height:80px\">t</div></td></tr>"
                          "<tr><td>alpha</td><td>beta beta</td></tr>"
                          "<tr><td>alpha</td><td>beta beta</td></tr>"
                          "</table></body></html>\n");
    float sx = 0, sy = 0, sw = 0, sh = 0;
    float tx = 0, ty = 0, tw = 0, th = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(*stretched, "short", sx, sy, sw, sh));
    ASSERT_TRUE(zui::frame::Access::box_rect(*stretched, "tall", tx, ty, tw, th));
    EXPECT_GT(tx, sx + 20.f);
    EXPECT_NEAR(sy, ty, 1.5f);
    EXPECT_GT(sh, 60.f) << "short cell did not stretch to the row";
    EXPECT_NEAR(sh, th, 2.f);
    const zui::perf::FrameWork stretch_work = zui::perf::last_frame_work();
    EXPECT_LT(uniform_work.layout_stores, stretch_work.layout_stores)
        << "uniform=" << uniform_work.layout_stores << " stretch=" << stretch_work.layout_stores;
}

TEST(AppInteract, BlockBasisStaysOutOfUnrelatedKeys) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // The block percentage basis is part of a key only when that box reads it. Changing the
    // parent's height re-lays the parent. The rows do not have a percentage height, so they stay
    // on the result they already stored. `#half` and `#math` do read the basis.
    std::string rows;
    for (int i = 0; i < 16; ++i) {
        rows += "<div class=\"row\" id=\"r" + std::to_string(i) + "\">alpha beta</div>";
    }
    const fs::path page = write_page(
        "zui-perf-block-basis.html",
        "<!DOCTYPE html><html><head><style>"
        "html,body{margin:0}"
        "#wrap{width:240px;height:480px}"
        "#wrap.short{height:200px}"
        "#col{display:flex;flex-direction:column;width:240px}"
        ".row{font-size:14px}"
        "</style></head><body>"
        "<div id=\"wrap\"><div id=\"col\">" +
            rows +
            "</div></div>"
            "<div id=\"box\" style=\"width:100px;height:200px\">"
            "<div id=\"half\" style=\"height:50%\">h</div></div>"
            "<div id=\"mathbox\" style=\"width:80px;height:200px\">"
            "<div id=\"math\" style=\"height:calc(50% + 10px)\">m</div></div>"
            "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    float x = 0, y = 0, w = 0, h = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "r0", x, y, w, h));
    EXPECT_GT(w, 100.f);
    EXPECT_GT(h, 8.f);
    const float row_h = h;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "half", x, y, w, h));
    EXPECT_NEAR(h, 100.f, 2.f);
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "math", x, y, w, h));
    EXPECT_NEAR(h, 110.f, 2.f);
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().add_class("wrap", "short"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "wrap", x, y, w, h));
    EXPECT_NEAR(h, 200.f, 2.f);
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "r0", x, y, w, h));
    EXPECT_NEAR(h, row_h, 1.5f);
    EXPECT_GT(w, 100.f);
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "half", x, y, w, h));
    EXPECT_NEAR(h, 100.f, 2.f);
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "math", x, y, w, h));
    EXPECT_NEAR(h, 110.f, 2.f);
    // The rows stay cached. Ancestors whose own height moved, and anonymous flex boxes around
    // them, may store again. A full-suite run has stored 13 of those boxes; the row ids are the
    // signal that the children were not among them. A store past the cap, or a truncated id list,
    // means the walk left the ancestors.
    EXPECT_EQ(second.boxes_created, 0u);
    EXPECT_FALSE(second.touched_ids_truncated);
    std::string ids;
    for (std::size_t i = 0; i < second.touched_id_count; ++i) {
        const std::string& id = second.touched_ids[i];
        if (!ids.empty()) {
            ids += ",";
        }
        ids += id;
        const bool row = id.size() >= 2 && id[0] == 'r' && id[1] >= '0' && id[1] <= '9';
        EXPECT_FALSE(row) << id;
    }
    EXPECT_LE(second.layout_stores, 24u) << "stores=" << second.layout_stores
                                          << " laid=" << second.laid_out << " ids=" << ids;
}

TEST(AppInteract, SubjectBucketSkipsUnrelatedRules) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A subject bucket is a necessary condition, not the match. Unrelated class rules must not be
    // evaluated, and every selector kind the bucket keeps (id, class, type, universal, combinator,
    // attribute, :not, @media, @supports, @layer) still applies.
    const fs::path correct = write_page(
        "zui-bucket.html",
        "<!DOCTYPE html><html><head><style>"
        "div { min-height: 8px; }"
        "div#only { width: 36px; }"
        ".x.y { width: 27px; }"
        "#pre + #nxt { width: 28px; }"
        "#pre ~ #later { width: 29px; }"
        "#wrap > #direct { width: 23px; }"
        "#wrap #nested { width: 22px; }"
        "[data-k=v] { width: 25px; }"
        "#scope :not(.skip) { width: 19px; }"
        "@media (max-width: 2px) { #tiny { width: 15px; } }"
        "@media (min-width: 2px) { #wideq { width: 40px; } }"
        "@supports (display: block) { #sup { width: 44px; } }"
        "@supports (not (display: block)) { #supno { width: 12px; } }"
        "@layer c, d;"
        "@layer d { #layered { width: 40px; } }"
        "@layer c { #layered { width: 80px; } }"
        "</style></head><body>"
        "<div id=\"only\">x</div>"
        "<div class=\"x\" id=\"onlyx\">x</div>"
        "<div class=\"x y\" id=\"both\">x</div>"
        "<div id=\"pre\">x</div>"
        "<div id=\"nxt\">x</div>"
        "<div id=\"later\">x</div>"
        "<div id=\"nxt2\">x</div>"
        "<div id=\"wrap\"><div id=\"direct\">x</div>"
        "<div><div id=\"nested\">x</div></div></div>"
        "<div id=\"marked\" data-k=\"v\">x</div>"
        "<div id=\"scope\"><div id=\"yesnot\">x</div><div class=\"skip\" id=\"notme\">x</div></div>"
        "<div id=\"tiny\">x</div>"
        "<div id=\"wideq\">x</div>"
        "<div id=\"sup\">x</div>"
        "<div id=\"supno\">x</div>"
        "<div id=\"layered\">x</div>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(correct.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    auto width_of = [&](const char* id) {
        float x = 0, y = 0, w = 0, h = 0;
        if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
            return -1.f;
        }
        return w;
    };
    EXPECT_NEAR(width_of("only"), 36.f, 1.5f);
    EXPECT_GT(width_of("onlyx"), 200.f);
    EXPECT_NEAR(width_of("both"), 27.f, 1.5f);
    EXPECT_NEAR(width_of("nxt"), 28.f, 1.5f);
    EXPECT_GT(width_of("nxt2"), 200.f);
    EXPECT_NEAR(width_of("later"), 29.f, 1.5f);
    EXPECT_NEAR(width_of("direct"), 23.f, 1.5f);
    EXPECT_NEAR(width_of("nested"), 22.f, 1.5f);
    EXPECT_NEAR(width_of("marked"), 25.f, 1.5f);
    EXPECT_NEAR(width_of("yesnot"), 19.f, 1.5f);
    EXPECT_GT(width_of("notme"), 200.f);
    EXPECT_GT(width_of("tiny"), 200.f);
    EXPECT_NEAR(width_of("wideq"), 40.f, 1.5f);
    EXPECT_NEAR(width_of("sup"), 44.f, 1.5f);
    EXPECT_GT(width_of("supno"), 200.f);
    EXPECT_NEAR(width_of("layered"), 40.f, 1.5f);

    auto cells = [](const std::string& extra) {
        std::string html =
            "<!DOCTYPE html><html><head><style>.cell{width:36px}";
        html += extra;
        html += "</style></head><body>";
        for (int i = 0; i < 12; ++i) {
            html += "<div class=\"cell\" id=\"c";
            html += std::to_string(i);
            html += "\">x</div>";
        }
        html += "</body></html>\n";
        return html;
    };
    std::string noise;
    for (int i = 0; i < 40; ++i) {
        noise += ".n" + std::to_string(i) + "{width:1px}";
    }
    auto count_tests = [&](const std::string& html) {
        const fs::path page = write_page("zui-bucket-noise.html", html);
        zui::App page_app(0, nullptr);
        EXPECT_TRUE(page_app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(page_app));
        float x = 0, y = 0, w = 0, h = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(page_app, "c0", x, y, w, h));
        EXPECT_NEAR(w, 36.f, 1.5f);
        return zui::perf::last_frame_work().cascade_selector_tests;
    };
    const std::uint64_t quiet = count_tests(cells(""));
    const std::uint64_t noisy = count_tests(cells(noise));
    EXPECT_LE(noisy, quiet + 8u) << "quiet=" << quiet << " noisy=" << noisy;

    // A comma group is one slot per selector. Twelve spans pay for `span`, not for the other
    // twenty-five types in the same rule.
    auto span_tests = [](const std::string& rule) {
        std::string html = "<!DOCTYPE html><html><head><style>";
        html += rule;
        html += "</style></head><body>";
        for (int i = 0; i < 12; ++i) {
            html += "<span id=\"s" + std::to_string(i) + "\">x</span>";
        }
        html += "</body></html>\n";
        const fs::path page = write_page("zui-bucket-group.html", html);
        zui::App page_app(0, nullptr);
        EXPECT_TRUE(page_app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(page_app));
        return zui::perf::last_frame_work().cascade_selector_tests;
    };
    const std::uint64_t alone = span_tests("span{color:rgb(1,2,3)}");
    const std::uint64_t grouped = span_tests(
        "div,p,h1,h2,h3,h4,h5,h6,article,section,nav,aside,header,footer,main,"
        "figure,figcaption,blockquote,address,form,details,summary,pre,hr,span{color:rgb(1,2,3)}");
    EXPECT_LE(grouped, alone + 24u) << "alone=" << alone << " grouped=" << grouped;
}

TEST(AppInteract, ShowingHiddenSiblingDoesNotRebuildThePage) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // The hidden panel and a control inside it are both queued. The panel has no box yet, so
    // syncing it on its own relowers the nearest boxed ancestor and recreates the page. The
    // ancestor is already queued, and it builds only the panel (docs/PERF.md I4).
    const auto show = [](int rows) {
        std::string html =
            "<!DOCTYPE html><html><head><style>"
            "[hidden]{display:none}body{margin:0}p{margin:0;height:8px}"
            ".app{display:flex;flex-direction:column}"
            ".back{position:fixed;display:grid}"
            "</style></head><body><div id=\"app\" class=\"app\">";
        for (int i = 0; i < rows; ++i) {
            html += "<p id=\"c" + std::to_string(i) + "\" class=\"cell\">day</p>";
        }
        html += "</div><div id=\"editor\" class=\"back\" hidden>"
                "<p id=\"title\">Title</p>"
                "<button id=\"editor-remove\" hidden>remove</button>"
                "</div></body></html>\n";
        const std::string name = "zui-perf-show-sibling-" + std::to_string(rows) + ".html";
        const fs::path page = write_page(name.c_str(), html);
        zui::App app(0, nullptr);
        EXPECT_TRUE(app.document().load(page.string()));
        EXPECT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);
        EXPECT_TRUE(app.document().set("editor-remove", "visible", "true"));
        EXPECT_TRUE(app.document().set("editor", "visible", "true"));
        zui::frame::Access::sync_observation(app);
        float x = 0, y = 0, w = 0, h = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "title", x, y, w, h));
        EXPECT_GT(w, 1.f);
        EXPECT_GT(h, 1.f);
        EXPECT_TRUE(zui::frame::Access::box_rect(app, "c0", x, y, w, h));
        EXPECT_GT(h, 1.f);
        return zui::perf::last_frame_work();
    };
    const zui::perf::FrameWork small = show(20);
    const zui::perf::FrameWork large = show(80);
    EXPECT_FALSE(small.touched_ids_truncated);
    EXPECT_FALSE(large.touched_ids_truncated);
    EXPECT_EQ(small.relowers, 0u);
    EXPECT_EQ(large.relowers, 0u);
    EXPECT_GT(small.boxes_created, 0u);
    EXPECT_LT(small.boxes_created, 20u) << "boxes=" << small.boxes_created;
    EXPECT_EQ(small.boxes_created, large.boxes_created);
    EXPECT_EQ(small.cascade_visited, large.cascade_visited);
    EXPECT_EQ(small.laid_out, large.laid_out);
}

TEST(AppInteract, UnloadDropsTheRetiredStyleRoot) {
    // A write on a node and then removing that node in the same handler: the node is freed before
    // `flush_style` computes the closure, so it must not stay a declared root - a tight closure
    // walks its subtree (and its parent chain) and would read freed memory.
    const fs::path page = write_page(
        "zui-perf-retired-root.html",
        "<!DOCTYPE html><html><head><style>body{margin:0}.hot{color:rgb(9,9,9)}"
        "</style></head><body><div id=\"parent\"><p id=\"a\">a</p><p id=\"b\">b</p></div>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().set("a", "text", "x"));
    ASSERT_TRUE(app.document().unload_fragment("a"));
    zui::frame::Access::sync_observation(app);
}

TEST(AppInteract, UnloadRetiredStyleRootStaysInClosure) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    const fs::path page = write_page(
        "zui-perf-retired-root.html",
        "<!DOCTYPE html><html><head><style>body{margin:0}.hot{color:rgb(9,9,9)}"
        "</style></head><body><div id=\"parent\"><p id=\"a\">a</p><p id=\"b\">b</p></div>"
        "</body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().set("a", "text", "x"));
    ASSERT_TRUE(app.document().unload_fragment("a"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    // The parent subtree is all that is left: parent + b.
    EXPECT_LE(work.cascade_visited, 2u);
}

TEST(AppInteract, GlobalSelectorKeepsTheWholeDocumentClosure) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `:has()` can observe a change on an ancestor, so the closure is honestly the whole document
    // (docs/PERF.md section 2.4). The frame stays within budget - the bound just is not tight.
    const fs::path page = write_page(
        "zui-perf-i3-global.html",
        "<!DOCTYPE html><html><head><style>"
        ":root:has(#b.hot) .row{color:rgb(1,2,3)}"
        "</style></head><body>"
        "<p id=\"b\" class=\"row\">b</p><p class=\"row\">c</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().add_class("b", "hot"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_GE(work.cascade_visited, 2u) << "a :has() page cascades the whole document";
}

TEST(AppInteract, UnloadFragmentStaysInTheParentSubtree) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A fragment's nodes are removed from their parent, so only that parent's subtree can
    // re-match. The rest of the document (30 sibling rows) is not part of the closure.
    std::string html = "<!DOCTYPE html><html><head><style>"
                       "body{margin:0}p{margin:0;height:8px}"
                       "</style></head><body><div id=\"host\"></div>";
    for (int i = 0; i < 30; ++i) {
        html += "<p id=\"r" + std::to_string(i) + "\">row</p>";
    }
    html += "</body></html>\n";
    const fs::path page = write_page("zui-perf-unload.html", html);
    const fs::path frag = write_page("zui-perf-unload-frag.html",
                                     "<!DOCTYPE html><html><body><p id=\"frag\">f</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().load_fragment(frag.string(), "host"));
    zui::frame::Access::sync_observation(app);
    ASSERT_TRUE(app.document().unload_fragment("frag"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_LE(work.cascade_visited, 2u) << "only the host subtree re-matches";
}

TEST(AppInteract, PaintOnlyWriteDoesNotWalkTheWorld) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A class that only changes a painted property moves no box, so the frame neither lays anything
    // out nor re-places the world (docs/PERF.md I3's world.walk, I6's no-change paint).
    const fs::path page = write_page(
        "zui-perf-paint-world.html",
        "<!DOCTYPE html><html><head><style>"
        "body{margin:0}#box{width:40px;height:20px}"
        ".hot{background-color:rgb(9,9,9)}"
        "</style></head><body><div id=\"box\"></div>"
        "<p id=\"other\">other</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().add_class("box", "hot"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_EQ(work.cascade_visited, 1u);
    EXPECT_EQ(work.laid_out, 0u);
    EXPECT_EQ(work.world_walked, 0u) << "no box moved, so nothing to place";
}

TEST(AppInteract, ClickReusesTheHitWalk) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A click hit-tests the same point for the press and the release. Between two placements the
    // box tree and its world coordinates are frozen, so the second query must not walk again
    // (docs/PERF.md I7's hit.boxes).
    const fs::path page = write_page("zui-perf-hit-cache.html",
                                     "<!DOCTYPE html><html><head><style>body{margin:0}"
                                     "</style></head><body><div id=\"btn\">x</div></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "btn"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    // One walk of this four-box tree; two independent walks would be roughly twice this.
    EXPECT_LE(work.hit_boxes, 8u);
    EXPECT_GE(work.hit_boxes, 1u);
}

TEST(AppInteract, NthOfClosureStaysInTheParent) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // `:nth-child(An+B of S)` counts siblings that match `S`, so toggling `S` on one row re-matches
    // the rows after it - but the dependency stays inside that parent, so the closure is the parent
    // subtree, not the document (docs/PERF.md section 2.4).
    std::string html = "<!DOCTYPE html><html><head><style>body{margin:0}p{margin:0;height:8px}"
                       "li:nth-child(2n of .x){color:rgb(1,2,3)}"
                       "</style></head><body><ul id=\"list\">"
                       "<li id=\"a\" class=\"x\">1</li><li class=\"x\">2</li><li class=\"x\">3</li>"
                       "</ul>";
    for (int i = 0; i < 30; ++i) {
        html += "<p id=\"r" + std::to_string(i) + "\">row</p>";
    }
    html += "</body></html>\n";
    const fs::path page = write_page("zui-perf-nth-of.html", html);
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(app.document().remove_class("a", "x"));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    // The list: ul + three li.
    EXPECT_LE(work.cascade_visited, 4u) << "the of S dependency is parent-local";
}

TEST(AppInteract, TableModelReconcilesByRowKey) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    // A TableModel binds through ListModel::value, so the row half of the reconcile (and row_key)
    // applies to it too: changing one row's cell keeps the other rows' nodes and boxes.
    const fs::path page = write_page(
        "zui-perf-table-rows.html",
        "<!DOCTYPE html><html><body><div id=\"host\">"
        "<template><div class=\"row\"><span>{{a}}</span><span>{{b}}</span></div></template>"
        "</div></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    GridRows model;
    model.cells = {{"x", "1"}, {"y", "2"}, {"z", "3"}};
    model.keys = {"k0", "k1", "k2"};
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork cold = zui::perf::last_frame_work();
    ASSERT_GT(cold.boxes_created, 0u);

    model.cells[1][0] = "Y";
    ASSERT_TRUE(app.document().set_model("host", model));
    zui::frame::Access::sync_observation(app);
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_TRUE(work.within_budget) << work.violated;
    EXPECT_LE(work.boxes_created, cold.boxes_created)
        << "the table rebuild stays inside the host's rows";
    EXPECT_TRUE(has_id(touched(work), "host"));
}

TEST(AppInteract, Chat) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("chat").string()));
    chat::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    chat::bind_actions(session);
    chat::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    const auto ids = zui::frame::Access::interactive_ids(app);
    EXPECT_GE(static_cast<int>(ids.size()), 8);
    EXPECT_TRUE(has_id(ids, "draft"));
    float dx = 0, dy = 0;
    EXPECT_TRUE(zui::frame::Access::box_center(app, "draft", dx, dy));
    (void)zui::frame::Access::wheel_id(app, "log", 80.f);

    expect_open_cancel(app, "create", "creator", "creator-cancel");
    ASSERT_TRUE(zui::frame::Access::click_id(app, "sample-open"));
    expect_open_cancel(app, "restore", "confirm", "confirm-cancel");

    EXPECT_GE(smoke_clicks(app), 1);
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

/// Switching threads by clicking a conversation row is the app's most common interaction and the
/// one no id-driven click can reach: the row carries `data-id` (`Event::data`) and no `id`, so
/// `click_id` cannot name it and `smoke_clicks` walks past it. The handler rebuilds the message log
/// under a scroller that is scrolled to the bottom, and the scroller remembers a scroll anchor
/// *inside* that content (`Node::scroll_anchor`, CSS Scroll Anchoring). Rebuilding the rows used to
/// leave that remembered node dangling, and the next box-tree walk dereferenced it: the process
/// died with 0xC0000005 in `interaction.cpp`. The crash landed on the next `sync_world`, not inside
/// the click, so the click has to be followed by more interaction.
TEST(AppInteract, ChatConversationRowClickKeepsTheAnchorValid) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("chat").string()));
    chat::Session session;
    session.app = &app;
    // A scratch directory: the seeded conversations are what the row positions below assume.
    session.dir = unique_scratch("zui-chat-interact");
    fs::remove_all(session.dir);
    chat::bind_actions(session);
    chat::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));

    // The list is `flex: 1`, so its own height says nothing about where the rows are; the rows are
    // content-sized and start at its top edge (its padding is horizontal plus a 12px bottom). Its
    // rows have no id, so the sweep is what makes this robust: it clicks down the band the rows
    // occupy and requires one of the hits to switch the thread.
    float lx = 0.f;
    float ly = 0.f;
    float lw = 0.f;
    float lh = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "conv-list", lx, ly, lw, lh)) << "conv-list";
    const std::string before = app.document().get("thread-name", "text");
    ASSERT_FALSE(before.empty()) << "a thread has to be open to switch away from";
    bool switched = false;
    for (const float dy : {20.f, 76.f, 132.f}) {
        (void)zui::frame::Access::click_xy(app, lx + 20.f, ly + dy);
        if (app.document().get("thread-name", "text") != before) {
            switched = true;
            break;
        }
    }
    EXPECT_TRUE(switched) << "no conversation row was hit between " << ly << " and "
                          << (ly + 132.f);

    // Every later use of the app walks the box tree again, which is where the stale anchor was
    // dereferenced.
    (void)zui::frame::Access::wheel_id(app, "log", 40.f);
    ASSERT_TRUE(zui::frame::Access::click_id(app, "draft"));
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
    EXPECT_GE(smoke_clicks(app), 1);
}

/// The same defect through a second user action, with no coordinate in it at all: sending a message
/// rebuilds the log (`render_log` -> `set_model("log", ...)`) under the scrolled scroller.
TEST(AppInteract, ChatSendRebuildsTheScrolledLog) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("chat").string()));
    chat::Session session;
    session.app = &app;
    session.dir = unique_scratch("zui-chat-send-interact");
    fs::remove_all(session.dir);
    chat::bind_actions(session);
    chat::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));

    (void)zui::frame::Access::wheel_id(app, "log", 120.f);
    ASSERT_TRUE(zui::frame::Access::click_id(app, "draft"));
    ASSERT_TRUE(app.document().set("draft", "value", "写一条试试"));
    EXPECT_TRUE(zui::frame::Access::key(app, "Enter")) << "on_draft_key has to cancel the default line break";
    EXPECT_EQ(app.document().get("draft", "value"), "") << "the message went out";

    (void)zui::frame::Access::wheel_id(app, "log", 40.f);
    zui::frame::Access::sync_observation(app);
    (void)zui::frame::Access::measure(app, "log", "client_height");
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

TEST(AppInteract, Contacts) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("contacts").string()));
    contacts::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    contacts::bind_actions(session);
    contacts::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    EXPECT_GE(static_cast<int>(zui::frame::Access::interactive_ids(app).size()), 8);

    expect_open_cancel(app, "create", "editor", "editor-cancel");
    ASSERT_TRUE(zui::frame::Access::click_id(app, "sample-open"));
    expect_open_cancel(app, "restore", "confirm", "confirm-cancel");

    EXPECT_GE(smoke_clicks(app), 1);
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

TEST(AppInteract, Kanban) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("kanban").string()));
    kanban::Session session(app, app.host().user_data_dir());
    session.bind();
    session.show();
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    EXPECT_GE(static_cast<int>(zui::frame::Access::interactive_ids(app).size()), 5);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "sample-open"));
    expect_open_cancel(app, "restore", "confirm", "confirm-cancel");

    EXPECT_GE(smoke_clicks(app), 1);
    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}

#ifndef ZUI_WITHOUT_ZMD
// zmd's page is the prototype's markup copied verbatim: interactions are declared with
// `data-act` and one delegated listener, so the page carries no `on*` handler attributes
// and `interactive_ids` (which keys on them) sees only the buttons that do have ids.
// What the smoke test can still hold: the page loads, the session wires onto it, and the
// window element takes the state classes the session writes.
TEST(AppInteract, Zmd) {
    zui::App app(0, nullptr);
    const fs::path html = app_html("zmd");
    ASSERT_TRUE(app.document().load(html.string()));
    zmd::Session session(app);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));

    // The view classes belong to the screen element, not the window: css/zmd.css selects
    // `#md-editor-mock.view-split …` and js/zmd.js toggles them on the same node.
    session.settings().set_view(zmd::Settings::View::Split);
    EXPECT_TRUE(zui::frame::Access::has_class(app, "md-editor-mock", "view-split"));
    session.settings().set_view(zmd::Settings::View::Live);
    EXPECT_TRUE(zui::frame::Access::has_class(app, "md-editor-mock", "view-live"));

    // Every shipped control that carries an id and an on* handler is in this set.
    // The census test locks the exact list; this smoke only checks the page still wires up.
    EXPECT_GT(static_cast<int>(zui::frame::Access::interactive_ids(app).size()), 2);

    // zmd's own layer: the three dialogs reuse the settings-sheet skin, so open each one
    // once and check the layout path does not fall over.
    expect_set_overlay(app, "zmd-dlg-link");
    expect_set_overlay(app, "zmd-dlg-image");
    expect_set_overlay(app, "zmd-dlg-unsaved");

    EXPECT_TRUE(zui::frame::Access::key(app, "Escape"));
}
#endif

TEST(AppInteract, Zdb) {
    zui::App app(0, nullptr);
    const fs::path html = app_html("zdb");
    ASSERT_TRUE(app.document().load(html.string()));
    zdb::Session session;
    session.app = &app;
    session.dir = html.parent_path();
    zdb::bind_actions(session);
    zdb::show_all(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    EXPECT_GE(static_cast<int>(zui::frame::Access::interactive_ids(app).size()), 10);
    // File pick / run paths stay disarmed; shell clicks still exercise hit-testing.
    app.document().unbind("on_open");
    app.document().unbind("on_export");
    app.document().unbind("on_sample");
    app.document().unbind("on_drop");
    app.document().unbind("on_run");
    EXPECT_GE(smoke_clicks(app), 1);
}

TEST(AppInteract, ReaderShell) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("reader", "reader.html").string()));
    expect_shell(app, 10, "flow_wrap");
}

/// P0 gate: page-mode flow_wrap content box is 656×720 at 1100×720 with size_m.
TEST(AppInteract, ReaderPageMeasureGate) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("reader", "reader.html").string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app, 1100, 720));

    EXPECT_FALSE(zui::frame::Access::has_class(app, "reader", "open"));
    ASSERT_TRUE(app.document().add_class("reader", "open"));
    EXPECT_TRUE(zui::frame::Access::has_class(app, "reader", "open"));
    EXPECT_NE(app.document().get("reader", "class").find("open"), std::string::npos);

    ASSERT_TRUE(app.document().set("mode_page", "checked", "true"));
    EXPECT_EQ(app.document().get("mode_page", "checked"), "true");
    EXPECT_EQ(app.document().get("size_m", "checked"), "true");

    zui::frame::Access::sync_observation(app);

    const float content_w =
        std::stof(zui::frame::Access::measure(app, "flow_wrap", "content_width"));
    const float client_h =
        std::stof(zui::frame::Access::measure(app, "flow_wrap", "client_height"));
    EXPECT_NEAR(content_w, 656.f, 1.5f) << "page-mode size_m content width";
    EXPECT_NEAR(client_h, 720.f, 1.5f) << "open reader client height";

    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "flow_wrap", x, y, w, h));
    EXPECT_GT(w, 0.f);
    EXPECT_GT(h, 0.f);

    bool deferred_ran = false;
    app.document().defer([&] { deferred_ran = true; });
    zui::frame::Access::flush_deferred(app);
    EXPECT_TRUE(deferred_ran);
}

/// label.display:flex with width/height:36px must form a 36×36 border box (reader shelf plus).
TEST(AppInteract, LabelFlexFixedSizeHit) {
#if defined(_WIN32)
    _putenv_s("ZUI_PLATFORM", "noop");
#else
    setenv("ZUI_PLATFORM", "noop", 1);
#endif
    zui::App app(0, nullptr);
    const fs::path html = fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "label_flex_size.html";
    ASSERT_TRUE(app.document().load(html.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app, 400, 200));

    float x = 0, y = 0, w = 0, h = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "plus_btn", x, y, w, h));
    EXPECT_NEAR(w, 36.f, 1.f) << "label.plus width:36px must win over content 14px";
    EXPECT_NEAR(h, 36.f, 1.f) << "label.plus height:36px must win over content 14px";
    ZUI_FILE_INFO("plus_btn rect=({:.2f},{:.2f} {:.2f}x{:.2f})", x, y, w, h);

    EXPECT_EQ(app.document().get("shelf_menu", "checked"), "false");
    // border-radius:999px → circular hit; probe mid-edges (inside the circle) and center.
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float probes[5][2] = {
        {cx, cy}, {x + 6.f, cy}, {x + w - 6.f, cy}, {cx, y + 6.f}, {cx, y + h - 6.f},
    };
    for (const auto& c : probes) {
        ASSERT_TRUE(app.document().set("shelf_menu", "checked", "false"));
        zui::frame::Access::sync_observation(app);
        ASSERT_TRUE(zui::frame::Access::click_xy(app, c[0], c[1])) << "click " << c[0] << "," << c[1];
        zui::frame::Access::sync_observation(app);
        EXPECT_EQ(app.document().get("shelf_menu", "checked"), "true")
            << "label[for] hit at (" << c[0] << "," << c[1] << ") rect=(" << x << "," << y << " "
            << w << "x" << h << ")";
    }
}

TEST(AppInteract, CalendarMonthKeepsDayNumbers) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("calendar").string()));
    calendar::Session session;
    session.app = &app;
    session.dir = unique_scratch("zui-calendar-day-numbers");
    fs::remove_all(session.dir);
    calendar::bind_actions(session);
    calendar::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    const std::vector<zui::frame::NodeCensus> before = zui::frame::Access::census(app);
    auto button_of = [&](const std::vector<zui::frame::NodeCensus>& tree, int index) {
        for (int parent = tree[static_cast<std::size_t>(index)].parent; parent >= 0;
             parent = tree[static_cast<std::size_t>(parent)].parent) {
            if (tree[static_cast<std::size_t>(parent)].tag == "button") {
                return parent;
            }
        }
        return -1;
    };

    std::map<std::string, int> headers;
    int day_cells = 0;
    std::map<std::string, std::string> day_runs;
    std::map<std::string, float> day_ascents;
    int click_num = -1;
    std::string click_date;
    std::string selected_date;
    for (int i = 0; i < static_cast<int>(before.size()); ++i) {
        if (!class_token(before[static_cast<std::size_t>(i)].class_attr, "num")) {
            continue;
        }
        const int button = button_of(before, i);
        if (button < 0) {
            continue;
        }
        const zui::frame::NodeCensus& cell = before[static_cast<std::size_t>(button)];
        if (class_token(cell.class_attr, "mh")) {
            headers[before[static_cast<std::size_t>(i)].run_text] += 1;
            continue;
        }
        if (!class_token(cell.class_attr, "cell")) {
            continue;
        }
        ++day_cells;
        day_runs[cell.data_date] = before[static_cast<std::size_t>(i)].run_text;
        day_ascents[cell.data_date] = before[static_cast<std::size_t>(i)].run_ascent;
        if (class_token(cell.class_attr, "is-on") && !class_token(cell.class_attr, "is-out")) {
            selected_date = cell.data_date;
        }
        if (click_num < 0 && before[static_cast<std::size_t>(i)].laid &&
            !class_token(cell.class_attr, "is-out") && !class_token(cell.class_attr, "is-on")) {
            click_num = i;
            click_date = cell.data_date;
        }
    }
    std::string seen;
    for (const auto& kv : headers) {
        seen += " [" + kv.first + "]x" + std::to_string(kv.second);
    }
    EXPECT_EQ(headers.size(), 7u) << seen;
    for (const char* name : {"一", "二", "三", "四", "五", "六", "日"}) {
        EXPECT_EQ(headers[name], 1) << name << seen;
    }
    EXPECT_EQ(day_cells, 42);
    ASSERT_FALSE(selected_date.empty()) << "the month must already have a selected in-month day";
    ASSERT_GE(click_num, 0) << "no unselected in-month cell was laid out";
    ASSERT_NE(click_date, selected_date);

    const zui::frame::NodeCensus& num = before[static_cast<std::size_t>(click_num)];
    ASSERT_TRUE(zui::frame::Access::click_xy(app, num.x + num.w * 0.5f, num.y + num.h * 0.5f)) << click_date;
    zui::frame::Access::sync_observation(app);

    const std::vector<zui::frame::NodeCensus> after = zui::frame::Access::census(app);
    bool clicked_on = false;
    bool previous_on = false;
    for (int i = 0; i < static_cast<int>(after.size()); ++i) {
        if (!class_token(after[static_cast<std::size_t>(i)].class_attr, "num")) {
            continue;
        }
        const int button = button_of(after, i);
        if (button < 0 || !class_token(after[static_cast<std::size_t>(button)].class_attr, "cell")) {
            continue;
        }
        const zui::frame::NodeCensus& cell = after[static_cast<std::size_t>(button)];
        if (class_token(cell.class_attr, "is-on") && !class_token(cell.class_attr, "is-out")) {
            if (cell.data_date == click_date) {
                clicked_on = true;
            }
            if (cell.data_date == selected_date) {
                previous_on = true;
            }
        }
        const auto previous = day_runs.find(cell.data_date);
        ASSERT_TRUE(previous != day_runs.end()) << cell.data_date;
        EXPECT_EQ(after[static_cast<std::size_t>(i)].run_text, previous->second)
            << cell.data_date << " text=" << after[static_cast<std::size_t>(i)].text;
        // The kept row's line must come back at the same metric: `y + ascent` is the baseline, and
        // a refilled run with the wrong line box would move every date number (RFC_MODEL.md wave 1).
        EXPECT_FLOAT_EQ(after[static_cast<std::size_t>(i)].run_ascent,
                        day_ascents[cell.data_date])
            << cell.data_date;
    }
    EXPECT_TRUE(clicked_on) << click_date;
    EXPECT_FALSE(previous_on) << selected_date;
}

TEST(AppInteract, KanbanColumnsStayOnTheRowAxis) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("kanban").string()));
    const fs::path dir = unique_scratch("zui-kanban-axis");
    fs::remove_all(dir);
    kanban::Session session(app, dir);
    session.bind();
    session.show();
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    EXPECT_FALSE(zui::frame::Access::box_rect(app, "board-live", x, y, w, h));
    float todo_x = 0.f;
    float todo_y = 0.f;
    float doing_x = 0.f;
    float doing_y = 0.f;
    float tw = 0.f;
    float th = 0.f;
    float dw = 0.f;
    float dh = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "name-todo", todo_x, todo_y, tw, th));
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "name-doing", doing_x, doing_y, dw, dh));
    EXPECT_GE(doing_x, todo_x + 280.f) << "todo_x=" << todo_x << " doing_x=" << doing_x;
    EXPECT_LT(std::abs(doing_y - todo_y), 2.f) << "todo_y=" << todo_y << " doing_y=" << doing_y;
}

void show_album(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("album").string()));
    // The session outlives this helper: bind stores its address, and the pumps run after return.
    auto* session = new album::Session;
    session->app = &app;
    session->dir = unique_scratch("zui-album-idle");
    fs::remove_all(session->dir);
    album::bind_actions(*session);
    album::show(*session);
}

void show_calendar(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("calendar").string()));
    auto* session = new calendar::Session;
    session->app = &app;
    session->dir = unique_scratch("zui-calendar-idle");
    fs::remove_all(session->dir);
    calendar::bind_actions(*session);
    calendar::show(*session);
}

void show_chat(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("chat").string()));
    auto* session = new chat::Session;
    session->app = &app;
    session->dir = unique_scratch("zui-chat-idle");
    fs::remove_all(session->dir);
    chat::bind_actions(*session);
    chat::show(*session);
}

void show_contacts(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("contacts").string()));
    auto* session = new contacts::Session;
    session->app = &app;
    session->dir = unique_scratch("zui-contacts-idle");
    fs::remove_all(session->dir);
    contacts::bind_actions(*session);
    contacts::show(*session);
}

void show_kanban(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("kanban").string()));
    const fs::path dir = unique_scratch("zui-kanban-idle");
    fs::remove_all(dir);
    // The session outlives this function: bind() closes over it for the rest of the process.
    auto* session = new kanban::Session(app, dir);
    session->bind();
    session->show();
}

void show_zdb(zui::App& app) {
    ASSERT_TRUE(app.document().load(app_html("zdb").string()));
    auto* session = new zdb::Session;
    session->app = &app;
    session->dir = unique_scratch("zui-zdb-idle");
    fs::remove_all(session->dir);
    zdb::bind_actions(*session);
    zdb::show_all(*session);
}

#ifndef ZUI_WITHOUT_ZMD
void show_zmd(zui::App& app) {
    const fs::path html = app_html("zmd");
    ASSERT_TRUE(app.document().load(html.string()));
    // The page is loaded first, then the session wires onto it (open_page does both
    // when it owns the load; here the harness owns the app, so construct after).
    auto* session = new zmd::Session(app);
    session->settings().set_view(zmd::Settings::View::Live);
    // The native host hides the prototype caret (it blinks forever). Without this
    // class the idle frame keeps repainting that sample article.
    app.document().add_class("md-editor-mock", "zmd-host");
    session->apply_source("# \xe6\xa0\x87\xe9\xa2\x98\n\n\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n", false, true);
    session->refresh_preview();
}
#endif

TEST(AppInteract, AlbumIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_album(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, AlbumIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_album(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

TEST(AppInteract, CalendarIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_calendar(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, CalendarIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_calendar(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

TEST(AppInteract, ChatIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_chat(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, ChatIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_chat(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

TEST(AppInteract, ContactsIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_contacts(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, ContactsIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_contacts(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

TEST(AppInteract, KanbanIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_kanban(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, KanbanIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_kanban(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

TEST(AppInteract, ZdbIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_zdb(app);
    AttachedFrame host(app);
    expect_next_frame_resubmits(host);
}

TEST(AppInteract, ZdbIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_zdb(app);
    AttachedFrame host(app);
    expect_next_frame_skips_paint(host);
}

#ifndef ZUI_WITHOUT_ZMD
TEST(AppInteract, ZmdIdleResubmitsTheLastList) {
    zui::App app(0, nullptr);
    show_zmd(app);
    AttachedFrame host(app);
    // The staged pointer starts at (0, 0), inside the 24px edge strip, which
    // would slide the file panel in. Idle is the pointer over the page.
    host.window.pointer_.x = 400.f;
    host.window.pointer_.y = 300.f;
    expect_clocked_frame_resubmits(host, 0.5);
}

TEST(AppInteract, ZmdIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::App app(0, nullptr);
    show_zmd(app);
    AttachedFrame host(app);
    host.window.pointer_.x = 400.f;
    host.window.pointer_.y = 300.f;
    expect_clocked_frame_skips_paint(host, 0.5);
}
#endif

