#include "span.h"

#include "dates.h"

namespace calendar {
namespace {

class MonthSpan : public SpanMode {
public:
    void apply_chrome(zui::Document& doc) const override {
        doc.add_class("view-month", "is-on");
        doc.remove_class("view-week", "is-on");
        doc.set("month-view", "visible", "true");
        doc.set("week-view", "visible", "false");
    }

    std::string title(int year, int month, const std::string&) const override {
        return std::to_string(year) + "年" + std::to_string(month + 1) + "月";
    }

    void shift(int step, int& year, int& month, std::string&) const override {
        month += step;
        while (month < 0) {
            month += 12;
            --year;
        }
        while (month > 11) {
            month -= 12;
            ++year;
        }
    }

    bool is_week() const override { return false; }
};

class WeekSpan : public SpanMode {
public:
    void apply_chrome(zui::Document& doc) const override {
        doc.add_class("view-week", "is-on");
        doc.remove_class("view-month", "is-on");
        doc.set("month-view", "visible", "false");
        doc.set("week-view", "visible", "true");
    }

    std::string title(int, int, const std::string& selected) const override {
        const std::string week_start = start_of_week(selected);
        const std::string end = add_days(week_start, 6);
        int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
        parse_iso(week_start, y1, m1, d1);
        parse_iso(end, y2, m2, d2);
        return std::to_string(m1) + "月" + std::to_string(d1) + "日 – " + std::to_string(m2) + "月" +
               std::to_string(d2) + "日";
    }

    void shift(int step, int& year, int& month, std::string& selected) const override {
        selected = add_days(selected, step * 7);
        int y = 0, m = 0, d = 0;
        parse_iso(selected, y, m, d);
        year = y;
        month = m - 1;
    }

    bool is_week() const override { return true; }
};

}  // namespace

std::unique_ptr<SpanMode> make_month_span() { return std::make_unique<MonthSpan>(); }

std::unique_ptr<SpanMode> make_week_span() { return std::make_unique<WeekSpan>(); }

}  // namespace calendar
