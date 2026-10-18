#ifndef CHAT_COMMANDS_H
#define CHAT_COMMANDS_H

#include "store.h"

#include <memory>
#include <string>

namespace chat {

/// What a confirm command may change. The session persists and repaints afterwards.
struct ChatContext {
    ChatStore& store;
    std::string& active;
};

/// Remove-conversation and restore-samples are different operations, so each is its own command.
class ConfirmCommand {
public:
    virtual ~ConfirmCommand() = default;
    virtual std::string body() const = 0;
    virtual const char* ok_label() const = 0;
    virtual void execute(ChatContext& ctx) = 0;
};

std::unique_ptr<ConfirmCommand> make_remove_convo(std::string name);
std::unique_ptr<ConfirmCommand> make_restore_samples();

}  // namespace chat

#endif
