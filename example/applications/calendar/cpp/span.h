#ifndef CALENDAR_SPAN_H
#define CALENDAR_SPAN_H

#include <zui/document.h>

#include <memory>
#include <string>

namespace calendar {

/// Month and week are different ways to move the cursor and to label the chrome.
/// Both grids are still painted; this only owns the difference between the two modes.
class SpanMode {
public:
    virtual ~SpanMode() = default;
    virtual void apply_chrome(zui::Document& doc) const = 0;
    virtual std::string title(int year, int month, const std::string& selected) const = 0;
    virtual void shift(int step, int& year, int& month, std::string& selected) const = 0;
    virtual bool is_week() const = 0;
};

std::unique_ptr<SpanMode> make_month_span();
std::unique_ptr<SpanMode> make_week_span();

}  // namespace calendar

#endif
