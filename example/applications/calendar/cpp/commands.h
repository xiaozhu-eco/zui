#ifndef CALENDAR_COMMANDS_H
#define CALENDAR_COMMANDS_H

#include "store.h"

#include <memory>
#include <string>

namespace calendar {

struct ConfirmContext {
    EventStore& store;
    int& year;
    int& month;
    std::string& selected;
    std::string& editing;
};

class ConfirmCommand {
public:
    virtual ~ConfirmCommand() = default;
    virtual std::string body() const = 0;
    virtual const char* ok_label() const = 0;
    /// Remove closes the editor. Restore leaves whatever dialog was already open.
    virtual bool hide_editor() const = 0;
    virtual void execute(ConfirmContext& ctx) = 0;
};

std::unique_ptr<ConfirmCommand> make_remove_event(std::string title);
std::unique_ptr<ConfirmCommand> make_restore_samples();

}  // namespace calendar

#endif
