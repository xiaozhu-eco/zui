#ifndef KANBAN_EDITOR_H
#define KANBAN_EDITOR_H

#include <string>

namespace zui {
class Event;
}

namespace kanban {

class Session;

/// The card dialog and the confirm box that deletes a card, deletes a column, or restores
/// the sample board.
class Editor {
public:
    const std::string& picked() const { return picked_; }

    void open(Session& session, const std::string& card_id);
    void save(Session& session);
    void cancel(Session& session);
    void ask_remove_card(Session& session);
    void ask_remove_column(Session& session, const zui::Event& event);
    void ask_restore(Session& session);
    void confirm_ok(Session& session);
    void confirm_cancel(Session& session);

private:
    enum class Ask { None, RemoveCard, RemoveColumn, Restore };

    void ask(Session& session, Ask kind, const std::string& body, const std::string& ok);

    std::string editing_;
    std::string picked_;
    std::string pending_column_;
    Ask ask_ = Ask::None;
};

}  // namespace kanban

#endif
