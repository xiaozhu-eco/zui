#include "commands.h"

#include <algorithm>

namespace contacts {
namespace {

class DeletePerson : public ConfirmCommand {
public:
    explicit DeletePerson(std::string name) : name_(std::move(name)) {}

    std::string body() const override {
        return "删除「" + name_ + "」？此操作只影响这台浏览器里的名单。";
    }
    const char* ok_label() const override { return "删除"; }

    bool execute(ConfirmContext& ctx) override {
        auto& people = ctx.store.people();
        const auto it = std::find_if(people.begin(), people.end(),
                                     [&](const Person& person) { return person.id == ctx.selected; });
        if (it == people.end()) {
            return false;
        }
        const auto next = people.erase(it);
        if (next != people.end()) {
            ctx.selected = next->id;
        } else if (!people.empty()) {
            ctx.selected = people.back().id;
        } else {
            ctx.selected.clear();
        }
        ctx.dirty = true;
        return true;
    }

private:
    std::string name_;
};

class RestoreSamples : public ConfirmCommand {
public:
    std::string body() const override { return "用示例名单替换当前通讯录？你新增和改过的人会消失。"; }
    const char* ok_label() const override { return "恢复"; }

    bool execute(ConfirmContext& ctx) override {
        ctx.store.seed();
        ctx.selected = ctx.store.people().empty() ? std::string{} : ctx.store.people().front().id;
        ctx.dirty = true;
        return true;
    }
};

}  // namespace

std::unique_ptr<ConfirmCommand> make_delete_person(std::string name) {
    return std::make_unique<DeletePerson>(std::move(name));
}

std::unique_ptr<ConfirmCommand> make_restore_samples() { return std::make_unique<RestoreSamples>(); }

}  // namespace contacts
