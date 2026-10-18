#include "drag.h"

#include "session.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace kanban {

void DragSession::clear_hint(Session& session) {
    if (hint_id_.empty()) {
        return;
    }
    session.app().document().remove_class(hint_id_, hint_class_);
    hint_id_.clear();
    hint_class_.clear();
}

void DragSession::hint(Session& session, const std::string& id, const std::string& cls) {
    if (hint_id_ == id && hint_class_ == cls) {
        return;
    }
    clear_hint(session);
    if (id.empty() || !session.app().document().add_class(id, cls)) {
        return;
    }
    hint_id_ = id;
    hint_class_ = cls;
}

void DragSession::show_ghost(Session& session, const std::string& title, const std::string& note) {
    session.app().document().set("drag-ghost-title", "text", title);
    if (note.empty()) {
        session.app().document().set("drag-ghost-note", "text", "");
        session.app().document().set("drag-ghost-note", "visible", "false");
    } else {
        session.app().document().set("drag-ghost-note", "text", note);
        session.app().document().set("drag-ghost-note", "visible", "true");
    }
    session.app().document().set("drag-ghost", "visible", "true");
    session.app().document().add_class("drag-ghost", "is-held");
}

void DragSession::hide_ghost(Session& session) {
    session.app().document().remove_class("drag-ghost", "is-held");
    session.app().document().set("drag-ghost", "visible", "false");
}

void DragSession::finish(Session& session) {
    clear_hint(session);
    hide_ghost(session);
    session.app().document().remove_class("board", "is-dragging");
    if (intent_.kind == "card") {
        session.app().document().remove_class("card-" + intent_.id, "is-source");
    } else if (intent_.kind == "column") {
        session.app().document().remove_class("col-" + intent_.id, "is-source");
    }
    scroll_dir_ = 0;
    intent_ = {};
    if (timer_ != 0) {
        session.app().document().clear_timer(timer_);
        timer_ = 0;
    }
}

void DragSession::note_scroll(Session& session, float x) {
    scroll_dir_ = 0;
    const auto board = session.app().document().measure("board");
    if (!board || !board->laid) {
        return;
    }
    if (x < board->border.x + 48.f) {
        scroll_dir_ = -28;
    } else if (x > board->border.x + board->border.width - 48.f) {
        scroll_dir_ = 28;
    }
}

void DragSession::tick(Session& session) {
    if (scroll_dir_ == 0) {
        return;
    }
    const std::string raw = session.app().document().get("board", "scroll_left");
    const float current = raw.empty() ? 0.f : std::strtof(raw.c_str(), nullptr);
    session.app().document().set("board", "scroll_left", std::to_string(current + static_cast<float>(scroll_dir_)));
}

std::string DragSession::column_at(Session& session, float x, float y) const {
    std::string best;
    float best_dist = 1.0e9f;
    for (const Column& column : session.store().columns()) {
        const auto box = session.app().document().measure("col-" + column.id);
        if (!box || !box->laid || box->border.width <= 0.f) {
            continue;
        }
        const float right = box->border.x + box->border.width;
        const float bottom = box->border.y + box->border.height;
        if (x >= box->border.x && x <= right && y >= box->border.y && y <= bottom) {
            return column.id;
        }
        float dist = 0.f;
        if (x < box->border.x) {
            dist = box->border.x - x;
        } else if (x > right) {
            dist = x - right;
        }
        if (dist < best_dist) {
            best_dist = dist;
            best = column.id;
        }
    }
    return best_dist <= 20.f ? best : std::string{};
}

int DragSession::index_before(Session& session, const std::vector<std::string>& ids,
                              const std::string& prefix, float coord, bool horizontal,
                              std::string& before_id) const {
    before_id.clear();
    int index = 0;
    for (const std::string& id : ids) {
        const auto box = session.app().document().measure(prefix + id);
        if (!box || !box->laid || box->border.height <= 0.f) {
            ++index;
            continue;
        }
        const float mid = horizontal ? box->border.x + box->border.width * 0.5f
                                     : box->border.y + box->border.height * 0.5f;
        if (coord < mid) {
            before_id = id;
            return index;
        }
        ++index;
    }
    return index;
}

void DragSession::on_start(Session& session, zui::Event& event) {
    const zui::Drag* drag = event.is_drag();
    if (drag == nullptr) {
        return;
    }
    const std::string kind(event.drag_data("kind"));
    const std::string id(event.drag_data("id"));
    if ((kind != "card" && kind != "column") || id.empty()) {
        return;
    }
    intent_.kind = kind;
    intent_.id = id;
    intent_.valid = false;
    session.app().document().add_class("board", "is-dragging");
    // `.card.is-dragging` is `display: none` for the browser script. Hiding the source from C++
    // drops its box while this frame is still using the tree, so the native source only fades.
    if (kind == "card") {
        session.app().document().add_class("card-" + id, "is-source");
        const Card* card = session.store().find_card(id);
        show_ghost(session, card != nullptr ? card->title : std::string{},
                   card != nullptr ? card->note : std::string{});
    } else {
        session.app().document().add_class("col-" + id, "is-source");
        const Column* column = session.store().find_column(id);
        show_ghost(session, column != nullptr ? column->name : std::string{}, {});
    }
    if (timer_ == 0) {
        timer_ = session.app().document().set_interval([this, &session] { tick(session); }, 32);
    }
}

void DragSession::on_move(Session& session, zui::Event& event) {
    const zui::Drag* drag = event.is_drag();
    if (drag == nullptr) {
        return;
    }
    note_scroll(session, drag->x);
}

void DragSession::update(Session& session, const zui::Drag& drag) {
    intent_.valid = false;
    intent_.column_id.clear();
    if (intent_.kind == "card") {
        if (session.store().find_card(intent_.id) == nullptr) {
            return;
        }
        const std::string column_id = column_at(session, drag.x, drag.y);
        const Column* column = session.store().find_column(column_id);
        if (column == nullptr) {
            clear_hint(session);
            return;
        }
        std::vector<std::string> rest;
        rest.reserve(column->card_ids.size());
        for (const std::string& id : column->card_ids) {
            if (id != intent_.id) {
                rest.push_back(id);
            }
        }
        std::string before;
        intent_.index = index_before(session, rest, "card-", drag.y, false, before);
        intent_.column_id = column_id;
        intent_.valid = true;
        if (before.empty()) {
            hint(session, "cards-" + column_id, "drop-end");
        } else {
            hint(session, "card-" + before, "drop-before");
        }
        return;
    }
    if (intent_.kind != "column" || session.store().find_column(intent_.id) == nullptr) {
        clear_hint(session);
        return;
    }
    std::vector<std::string> rest;
    for (const Column& column : session.store().columns()) {
        if (column.id != intent_.id) {
            rest.push_back(column.id);
        }
    }
    std::string before;
    intent_.index = index_before(session, rest, "col-", drag.x, true, before);
    intent_.valid = true;
    if (before.empty()) {
        hint(session, "board", "drop-end");
    } else {
        hint(session, "col-" + before, "drop-before");
    }
}

void DragSession::on_over(Session& session, zui::Event& event) {
    const zui::Drag* drag = event.is_drag();
    if (drag == nullptr) {
        return;
    }
    note_scroll(session, drag->x);
    update(session, *drag);
    if (intent_.valid) {
        event.prevent_default();
    }
}

void DragSession::on_drop(Session& session, zui::Event& event) {
    const zui::Drag* drag = event.is_drag();
    if (drag == nullptr || !intent_.valid) {
        return;
    }
    event.prevent_default();
    const Intent intent = intent_;
    finish(session);
    if (intent.kind == "card") {
        session.store().move_card(intent.id, intent.column_id, intent.index);
    } else {
        session.store().move_column(intent.id, intent.index);
    }
    session.persist();
    // Rebuilding #board inside this callback frees rows this frame is still laying out.
    // The moved board paints on the next frame.
    session.app().document().defer([&session] { session.render_board(); });
}

void DragSession::on_end(Session& session) { finish(session); }

}  // namespace kanban
