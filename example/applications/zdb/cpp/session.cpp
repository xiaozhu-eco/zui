#include "session.h"

#include "sqlite3.h"

namespace zdb {

Session::Session() = default;

Session::~Session() {
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
}

const DbObject* selected_object(const Session& session) {
    if (session.selected < 0 || static_cast<std::size_t>(session.selected) >= session.objects.size()) {
        return nullptr;
    }
    return &session.objects[static_cast<std::size_t>(session.selected)];
}

}  // namespace zdb
