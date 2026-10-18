#include "editor.h"

#include "session.h"

#include <string>

namespace kanban {

void Editor::ask(Session& session, Ask kind, const std::string& body, const std::string& ok) {
    ask_ = kind;
    session.app().document().set("confirm-body", "text", body);
    session.app().document().set("confirm-ok", "text", ok);
    session.app().document().set("confirm", "visible", "true");
}

void Editor::open(Session& session, const std::string& card_id) {
    const Card* card = session.store().find_card(card_id);
    if (card == nullptr) {
        return;
    }
    editing_ = card_id;
    const Column* column = session.store().column_of(card_id);
    picked_ = column != nullptr ? column->id : "";
    session.app().document().set("editor-error", "text", "");
    session.app().document().set("f-title", "value", card->title);
    session.app().document().set("f-note", "value", card->note);
    session.render_picks();
    session.app().document().set("editor", "visible", "true");
}

void Editor::save(Session& session) {
    Card* card = session.store().find_card(editing_);
    if (card == nullptr) {
        return;
    }
    const std::string title = trim(session.app().document().get("f-title", "value"));
    if (title.empty()) {
        session.app().document().set("editor-error", "text", "请填写标题");
        return;
    }
    card->title = title;
    card->note = trim(session.app().document().get("f-note", "value"));
    const std::string next = session.app().document().get("f-column", "value");
    const Column* current = session.store().column_of(card->id);
    if (current != nullptr && !next.empty() && current->id != next) {
        session.store().move_card(card->id, next, 9999);
    }
    session.app().document().set("editor", "visible", "false");
    session.persist();
    session.render_board();
}

void Editor::cancel(Session& session) { session.app().document().set("editor", "visible", "false"); }

void Editor::ask_remove_card(Session& session) {
    const Card* card = session.store().find_card(editing_);
    if (card == nullptr) {
        return;
    }
    ask(session, Ask::RemoveCard, "删除「" + card->title + "」？", "删除");
}

void Editor::ask_remove_column(Session& session, const zui::Event& event) {
    if (session.store().columns().size() <= 1) {
        session.set_status("至少留一列");
        return;
    }
    pending_column_ = std::string(event.data("id"));
    const Column* column = session.store().find_column(pending_column_);
    if (column == nullptr) {
        return;
    }
    const std::string extra = column->card_ids.empty()
                                  ? "这一列是空的。"
                                  : "其中 " + std::to_string(column->card_ids.size()) + " 张卡片会挪到旁边的列。";
    ask(session, Ask::RemoveColumn, "删除「" + column->name + "」？" + extra, "删除");
}

void Editor::ask_restore(Session& session) {
    ask(session, Ask::Restore, "用示例看板替换当前内容？你加过的卡片会消失。", "恢复");
}

void Editor::confirm_cancel(Session& session) {
    ask_ = Ask::None;
    session.app().document().set("confirm", "visible", "false");
}

void Editor::confirm_ok(Session& session) {
    const Ask kind = ask_;
    ask_ = Ask::None;
    session.app().document().set("confirm", "visible", "false");
    if (kind == Ask::RemoveCard) {
        session.store().erase_card(editing_);
        session.app().document().set("editor", "visible", "false");
        session.persist();
        session.render_board();
    } else if (kind == Ask::RemoveColumn) {
        if (!session.store().erase_column(pending_column_)) {
            return;
        }
        session.persist();
        session.render_board();
    } else if (kind == Ask::Restore) {
        session.store().seed();
        session.persist();
        session.render_board();
    }
}

}  // namespace kanban
