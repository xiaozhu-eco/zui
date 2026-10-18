#ifndef KANBAN_SESSION_H
#define KANBAN_SESSION_H

#include "drag.h"
#include "editor.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>

namespace kanban {

/// One running 小竹看板. It owns the board, the editor, and the sortable drag, and it is the
/// only object `Document::bind` closes over. It is not movable: those callbacks keep its address.
class Session {
public:
    Session(zui::App& app, std::filesystem::path dir);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void bind();
    void show();

    zui::App& app() { return app_; }
    BoardStore& store() { return store_; }
    const BoardStore& store() const { return store_; }
    const Editor& editor() const { return editor_; }

    void render_board();
    void render_picks();
    void set_status(const std::string& text);
    bool persist();

private:
    void on_col_name(const zui::Event& event);
    void on_add_card(const zui::Event& event);
    void on_add_column();
    void on_card_up(const zui::Event& event);
    void on_card_down(const zui::Event& event);
    void on_accel(zui::Event& event);

    zui::App& app_;
    std::filesystem::path dir_;
    BoardStore store_;
    Editor editor_;
    DragSession drag_;
};

}  // namespace kanban

#endif
