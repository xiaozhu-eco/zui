#include "commands.h"

namespace chat {
namespace {

class RemoveConversation : public ConfirmCommand {
public:
    explicit RemoveConversation(std::string name) : name_(std::move(name)) {}

    std::string body() const override {
        return "删除和「" + name_ + "」的会话？消息只存在这台浏览器里。";
    }
    const char* ok_label() const override { return "删除"; }

    void execute(ChatContext& ctx) override {
        const std::string id = ctx.active;
        ctx.store.erase_convo(id);
        ctx.active = ctx.store.convos().empty() ? std::string{} : ctx.store.convos().front().id;
    }

private:
    std::string name_;
};

class RestoreSamples : public ConfirmCommand {
public:
    std::string body() const override { return "用示例会话替换当前内容？你写过的消息会消失。"; }
    const char* ok_label() const override { return "恢复"; }

    void execute(ChatContext& ctx) override {
        ctx.store.seed();
        ctx.active = ctx.store.convos().empty() ? std::string{} : ctx.store.convos().front().id;
    }
};

}  // namespace

std::unique_ptr<ConfirmCommand> make_remove_convo(std::string name) {
    return std::make_unique<RemoveConversation>(std::move(name));
}

std::unique_ptr<ConfirmCommand> make_restore_samples() {
    return std::make_unique<RestoreSamples>();
}

}  // namespace chat
