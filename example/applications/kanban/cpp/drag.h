#ifndef KANBAN_DRAG_H
#define KANBAN_DRAG_H

#include <zui/app.h>

#include <string>
#include <vector>

namespace kanban {

class Session;

/// Cross-container sortable: cards move between columns, and the column grip reorders columns.
/// The board is rebuilt on the next frame so this callback does not free the row the engine is
/// still laying out.
class DragSession {
public:
    void on_start(Session& session, zui::Event& event);
    void on_move(Session& session, zui::Event& event);
    void on_over(Session& session, zui::Event& event);
    void on_drop(Session& session, zui::Event& event);
    void on_end(Session& session);

private:
    struct Intent {
        std::string kind;
        std::string id;
        std::string column_id;
        int index = 0;
        bool valid = false;
    };

    void tick(Session& session);
    void finish(Session& session);
    void note_scroll(Session& session, float x);
    void update(Session& session, const zui::Drag& drag);
    void hint(Session& session, const std::string& id, const std::string& cls);
    void clear_hint(Session& session);
    void show_ghost(Session& session, const std::string& title, const std::string& note);
    void hide_ghost(Session& session);
    std::string column_at(Session& session, float x, float y) const;
    int index_before(Session& session, const std::vector<std::string>& ids, const std::string& prefix,
                     float coord, bool horizontal, std::string& before_id) const;

    Intent intent_;
    std::string hint_id_;
    std::string hint_class_;
    int scroll_dir_ = 0;
    zui::TimerId timer_ = 0;
};

}  // namespace kanban

#endif
