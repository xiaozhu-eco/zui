#ifndef WRITER_DESK_H
#define WRITER_DESK_H

#include <zui/document.h>

#include <memory>
#include <string>

namespace writer {

/// Tree, calendar, and history show different panes. Switching views is the only
/// place that decides which pane is visible.
class DeskView {
public:
    virtual ~DeskView() = default;
    virtual const char* id() const = 0;
    virtual void apply_chrome(zui::Document& doc) const = 0;
    virtual bool paints_calendar() const = 0;
    virtual bool paints_history() const = 0;
};

std::unique_ptr<DeskView> make_desk_view(const std::string& id);

}  // namespace writer

#endif
