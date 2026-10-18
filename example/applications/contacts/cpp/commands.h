#ifndef CONTACTS_COMMANDS_H
#define CONTACTS_COMMANDS_H

#include "store.h"

#include <memory>
#include <string>

namespace contacts {

struct ConfirmContext {
    PeopleStore& store;
    std::string& selected;
    bool& dirty;
};

class ConfirmCommand {
public:
    virtual ~ConfirmCommand() = default;
    virtual std::string body() const = 0;
    virtual const char* ok_label() const = 0;
    /// False when nothing changed, so the session does not write the file.
    virtual bool execute(ConfirmContext& ctx) = 0;
};

std::unique_ptr<ConfirmCommand> make_delete_person(std::string name);
std::unique_ptr<ConfirmCommand> make_restore_samples();

}  // namespace contacts

#endif
