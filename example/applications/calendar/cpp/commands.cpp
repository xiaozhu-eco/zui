#include "commands.h"

#include "dates.h"

namespace calendar {
namespace {

class RemoveEvent : public ConfirmCommand {
public:
    explicit RemoveEvent(std::string title) : title_(std::move(title)) {}

    std::string body() const override { return "删除「" + title_ + "」？"; }
    const char* ok_label() const override { return "删除"; }
    bool hide_editor() const override { return true; }

    void execute(ConfirmContext& ctx) override {
        ctx.store.erase(ctx.editing);
        ctx.editing.clear();
    }

private:
    std::string title_;
};

class RestoreSamples : public ConfirmCommand {
public:
    std::string body() const override { return "用示例日程替换当前日历？你加过的日程会消失。"; }
    const char* ok_label() const override { return "恢复"; }
    bool hide_editor() const override { return false; }

    void execute(ConfirmContext& ctx) override {
        const std::string day = ctx.store.seed();
        int y = 0, m = 0, d = 0;
        if (parse_iso(day, y, m, d)) {
            ctx.year = y;
            ctx.month = m - 1;
            ctx.selected = day;
        }
    }
};

}  // namespace

std::unique_ptr<ConfirmCommand> make_remove_event(std::string title) {
    return std::make_unique<RemoveEvent>(std::move(title));
}

std::unique_ptr<ConfirmCommand> make_restore_samples() { return std::make_unique<RestoreSamples>(); }

}  // namespace calendar
