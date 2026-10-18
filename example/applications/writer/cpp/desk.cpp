#include "desk.h"

namespace writer {
namespace {

class TreeView : public DeskView {
public:
    const char* id() const override { return "tree"; }
    bool paints_calendar() const override { return false; }
    bool paints_history() const override { return false; }

    void apply_chrome(zui::Document& doc) const override {
        doc.set("workspace", "visible", "true");
        doc.set("calendar-view", "visible", "false");
        doc.set("history-view", "visible", "false");
        doc.add_class("view-tree", "is-on");
        doc.remove_class("view-calendar", "is-on");
        doc.remove_class("view-history", "is-on");
    }
};

class CalendarView : public DeskView {
public:
    const char* id() const override { return "calendar"; }
    bool paints_calendar() const override { return true; }
    bool paints_history() const override { return false; }

    void apply_chrome(zui::Document& doc) const override {
        doc.set("workspace", "visible", "false");
        doc.set("calendar-view", "visible", "true");
        doc.set("history-view", "visible", "false");
        doc.add_class("view-calendar", "is-on");
        doc.remove_class("view-tree", "is-on");
        doc.remove_class("view-history", "is-on");
    }
};

class HistoryView : public DeskView {
public:
    const char* id() const override { return "history"; }
    bool paints_calendar() const override { return false; }
    bool paints_history() const override { return true; }

    void apply_chrome(zui::Document& doc) const override {
        doc.set("workspace", "visible", "false");
        doc.set("calendar-view", "visible", "false");
        doc.set("history-view", "visible", "true");
        doc.add_class("view-history", "is-on");
        doc.remove_class("view-tree", "is-on");
        doc.remove_class("view-calendar", "is-on");
    }
};

}  // namespace

std::unique_ptr<DeskView> make_desk_view(const std::string& id) {
    if (id == "calendar") {
        return std::make_unique<CalendarView>();
    }
    if (id == "history") {
        return std::make_unique<HistoryView>();
    }
    return std::make_unique<TreeView>();
}

}  // namespace writer
