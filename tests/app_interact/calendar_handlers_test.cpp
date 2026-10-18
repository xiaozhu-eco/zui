// Every handler calendar/cpp/session.cpp registers, driven through the control that declares it.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#include "../../example/applications/calendar/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path calendar_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "calendar" / "index.html";
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

/// Monday-based weekday index of today, matching the app's `weekday_monday`.
int today_monday_index() {
    const std::tm t = today_local();
    return (t.tm_wday + 6) % 7;
}

std::string month_title(int offset) {
    std::tm t = today_local();
    int year = t.tm_year + 1900;
    int month = t.tm_mon + offset;
    while (month < 0) {
        month += 12;
        --year;
    }
    while (month > 11) {
        month -= 12;
        ++year;
    }
    return std::to_string(year) + "年" + std::to_string(month + 1) + "月";
}

struct CalendarRig {
    zui::test::HandlerRig rig;
    calendar::Session session;

    bool up() {
        session.app = &rig.app;
        session.dir = zui::test::scratch_dir("calendar-handlers");
        fs::remove_all(session.dir);
        if (!rig.app.document().load(calendar_html().string())) {
            return false;
        }
        calendar::bind_actions(session);
        calendar::show(session);
        if (!rig.windowed() || !rig.pump() || !rig.pump()) {
            return false;
        }
        // `g_week` leaks across tests; every test starts in the month grid the app opens in.
        return zui::frame::Access::click_id(rig.app, "view-month");
    }

    bool week_view() {
        return zui::frame::Access::click_id(rig.app, "view-week");
    }
};

int agenda_items(const CalendarRig& r) {
    return zui::test::count_class(r.rig.app, "agenda-item");
}

/// Cells that are laid out, not selected, and live (not the template prototype).
std::vector<zui::frame::NodeCensus> selectable_cells(const CalendarRig& r) {
    std::vector<zui::frame::NodeCensus> out;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (zui::test::class_token(row.class_attr, "cell") &&
            !zui::test::class_token(row.class_attr, "is-on") && row.w > 0.f && row.h > 0.f) {
            out.push_back(row);
        }
    }
    return out;
}

}  // namespace

TEST(AppInteract, CalendarTodayRestoresTheCurrentMonth) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "next"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("title", "text"), month_title(1));

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "today"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("title", "text"), month_title(0));
}

TEST(AppInteract, CalendarPrevMovesBackAMonth) {
    CalendarRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "prev"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("title", "text"), month_title(-1));
}

TEST(AppInteract, CalendarNextMovesForwardAMonth) {
    CalendarRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "next"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("title", "text"), month_title(1));
}

TEST(AppInteract, CalendarMonthButtonShowsTheGrid) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.week_view());
    r.rig.observe();
    ASSERT_EQ(r.rig.get("week-view", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "view-month"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("month-view", "visible"), "true");
    EXPECT_EQ(r.rig.get("week-view", "visible"), "false");
    EXPECT_TRUE(r.rig.has_class("view-month", "is-on"));
    EXPECT_FALSE(r.rig.has_class("view-week", "is-on"));
}

TEST(AppInteract, CalendarWeekButtonShowsTheWeek) {
    CalendarRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.week_view());
    r.rig.observe();

    EXPECT_EQ(r.rig.get("week-view", "visible"), "true");
    EXPECT_EQ(r.rig.get("month-view", "visible"), "false");
    EXPECT_TRUE(r.rig.has_class("view-week", "is-on"));
    EXPECT_NE(r.rig.get("title", "text").find(" – "), std::string::npos)
        << r.rig.get("title", "text");
}

TEST(AppInteract, CalendarDayCellSelectsTheDate) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const std::vector<zui::frame::NodeCensus> cells = selectable_cells(r);
    ASSERT_GE(cells.size(), 5u);
    const zui::frame::NodeCensus target = cells[cells.size() / 2];
    const std::string want = target.run_text;

    ASSERT_TRUE(zui::frame::Access::click_xy(r.rig.app, target.x + target.w * 0.5f,
                                             target.y + target.h * 0.5f));
    r.rig.observe();

    int selected = 0;
    std::string selected_text;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (zui::test::class_token(row.class_attr, "cell") &&
            zui::test::class_token(row.class_attr, "is-on")) {
            ++selected;
            selected_text = row.run_text;
        }
    }
    EXPECT_EQ(selected, 1) << "exactly the clicked day is selected";
    EXPECT_EQ(selected_text, want);
}

TEST(AppInteract, CalendarWeekDaySelectsTheDate) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.week_view());
    r.rig.observe();
    const int pick = (today_monday_index() + 1) % 7;  // a column that is not today

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "dow-" + std::to_string(pick)));
    r.rig.observe();

    EXPECT_TRUE(r.rig.has_class("dow-" + std::to_string(pick), "is-on"));
    EXPECT_TRUE(r.rig.has_class("day-" + std::to_string(pick), "is-on"));
    EXPECT_FALSE(r.rig.get("dow-" + std::to_string(pick), "value").empty());
}

TEST(AppInteract, CalendarWeekDayNewOpensTheEditor) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.week_view());
    r.rig.observe();
    const int pick = (today_monday_index() + 2) % 7;
    const std::string iso = r.rig.get("day-" + std::to_string(pick), "value");
    ASSERT_FALSE(iso.empty());

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "day-" + std::to_string(pick)));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("editor-title", "text"), "新建日程");
    EXPECT_EQ(r.rig.get("f-date", "value"), iso);
}

TEST(AppInteract, CalendarEventChipOpensTheEditor) {
    CalendarRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_text("chip", "校对第三章"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("editor-title", "text"), "编辑日程");
    EXPECT_EQ(r.rig.get("f-title", "value"), "校对第三章");
    EXPECT_EQ(r.rig.get("editor-remove", "visible"), "true");
}

TEST(AppInteract, CalendarAgendaItemOpensTheEditor) {
    CalendarRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_text("agenda-item", "和林晚对目录"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("f-title", "value"), "和林晚对目录");
}

TEST(AppInteract, CalendarColorRadioPaintsTheSavedEvent) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("f-title", "value", "配色校验"));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "color-clay"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    // The saved event carries the color the radio left in `g_color`, not just the radio's own state.
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "chip", "配色校验"));
    bool tone = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (zui::test::class_token(row.class_attr, "chip") &&
            row.run_text == "配色校验") {
            tone = zui::test::class_token(row.class_attr, "tone-clay");
        }
    }
    EXPECT_TRUE(tone) << "the chip must paint tone-clay";
}

TEST(AppInteract, CalendarCreateOpensTheEditor) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("editor-title", "text"), "新建日程");
    EXPECT_EQ(r.rig.get("editor-remove", "visible"), "false") << "a new event has nothing to remove";
}

namespace {

std::string selected_iso(const CalendarRig& r) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "button" && zui::test::class_token(row.class_attr, "cell") &&
            zui::test::class_token(row.class_attr, "is-on")) {
            return row.data_date;
        }
    }
    return {};
}

int zero_day_cells(const CalendarRig& r) {
    int count = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "button" && zui::test::class_token(row.class_attr, "cell") &&
            (row.w <= 0.f || row.h <= 0.f)) {
            ++count;
        }
    }
    return count;
}

bool click_date(CalendarRig& r, const std::string& iso) {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    bool found = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "button" && row.data_date == iso &&
            zui::test::class_token(row.class_attr, "cell") && row.w > 0.f && row.h > 0.f) {
            x = row.x;
            y = row.y;
            w = row.w;
            h = row.h;
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    // Chips sit under the day number and are their own hit target: they open the editor, whose
    // backdrop then covers the grid. The lower-left of the cell is the button's `on_day`.
    return zui::frame::Access::click_xy(r.rig.app, x + 8.f, y + h - 8.f);
}

}  // namespace

TEST(AppInteract, CalendarDayClickKeepsEveryCellHittable) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const std::tm today = today_local();
    const int year = today.tm_year + 1900;
    const int month = today.tm_mon + 1;
    auto iso = [&](int day) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
        return std::string(buf);
    };
    const std::string start = selected_iso(r);
    ASSERT_FALSE(start.empty());
    EXPECT_EQ(zero_day_cells(r), 0);

    // Moving `is-on` rebuilds two rows. Every other day cell must keep a border box, or the next
    // click misses it (`overflow: hidden` on a 0-size button clips its children).
    const int days[] = {5, 7, 6, 10, 11};
    for (const int day : days) {
        ASSERT_TRUE(click_date(r, iso(day))) << iso(day);
        r.rig.observe();
        EXPECT_EQ(selected_iso(r), iso(day)) << iso(day);
        EXPECT_EQ(zero_day_cells(r), 0) << iso(day);
        EXPECT_EQ(r.rig.get("editor", "visible"), "false") << iso(day);
    }

    ASSERT_TRUE(click_date(r, start)) << start;
    r.rig.observe();
    EXPECT_EQ(selected_iso(r), start);
    EXPECT_EQ(zero_day_cells(r), 0);
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, CalendarEditorCancelHidesTheEditor) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "editor-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, CalendarSaveRejectsAMissingTitle) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor-error", "text"), "请填写标题");
    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
}

TEST(AppInteract, CalendarSaveAddsTheEventToTheAgenda) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const int before = agenda_items(r);
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("f-title", "value", "复盘会"));
    ASSERT_TRUE(r.rig.app.document().set("f-start", "value", "13:00"));
    ASSERT_TRUE(r.rig.app.document().set("f-end", "value", "14:00"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
    EXPECT_EQ(agenda_items(r), before + 1);
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "agenda-item", "复盘会"));
}

TEST(AppInteract, CalendarRemoveAsksThenDeletesTheEvent) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const int before = agenda_items(r);
    ASSERT_TRUE(r.rig.click_text("agenda-item", "校对第三章"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "editor-remove"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除「校对第三章」"), std::string::npos);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
    EXPECT_EQ(agenda_items(r), before - 1);
}

TEST(AppInteract, CalendarRestoreAsksThenReseeds) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    // Remove one so the restore has a visible effect.
    ASSERT_TRUE(r.rig.click_text("agenda-item", "校对第三章"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "editor-remove"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();
    ASSERT_EQ(agenda_items(r), 1);

    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("confirm-body", "text").find("替换"), std::string::npos);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(agenda_items(r), 2) << "today has its two sample events back";
}

TEST(AppInteract, CalendarConfirmCancelKeepsTheEvents) {
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const int before = agenda_items(r);
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(agenda_items(r), before);
}

TEST(AppInteract, CalendarAccelEscapeClosesTheOpenDialog) {
    CalendarRig r;
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
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, CalendarPrevMonthStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    CalendarRig r;
    ASSERT_TRUE(r.up());
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)zui::frame::Access::click_id(r.rig.app, "prev"); });
    // Measured 354 / 419 / 1525 / 410 at the gate's introduction: stepping the month rebuilds the
    // 42-cell `month-view` model, not the agenda or the sidebar.
    EXPECT_LE(work.cascade_visited, 640u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 800u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 2600u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 800u) << "boxes=" << work.boxes_created;
}
