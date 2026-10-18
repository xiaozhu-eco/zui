#include "session.h"

#include "publish.h"

#include <zui/host_types.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace designer {
namespace {

struct Target {
    bool ok = false;
    bool keep = false;
    bool free = false;
    Node* parent = nullptr;
    int index = 0;
};

struct Swatch {
    const char* id;
    const char* group;
    const char* token;
};

const Swatch kSwatches[] = {
    {"sw-bg-default", "bg", ""},
    {"sw-bg-paper", "bg", "bg-paper"},
    {"sw-bg-tint", "bg", "bg-tint"},
    {"sw-bg-accent", "bg", "bg-accent"},
    {"sw-bg-ink", "bg", "bg-ink"},
    {"sw-bg-danger", "bg", "bg-danger"},
    {"sw-fg-ink", "fg", "fg-ink"},
    {"sw-fg-soft", "fg", "fg-soft"},
    {"sw-fg-paper", "fg", "fg-paper"},
    {"sw-fg-accent", "fg", "fg-accent"},
    {"sw-fg-danger", "fg", "fg-danger"},
    {"sw-fs-12", "fs", "fs-12"},
    {"sw-fs-14", "fs", "fs-14"},
    {"sw-fs-16", "fs", "fs-16"},
    {"sw-fs-20", "fs", "fs-20"},
    {"sw-fs-28", "fs", "fs-28"},
    {"sw-radius-0", "radius", "radius-0"},
    {"sw-radius-8", "radius", "radius-8"},
    {"sw-radius-12", "radius", "radius-12"},
    {"sw-radius-999", "radius", "radius-999"},
    {"sw-align-left", "align", "align-left"},
    {"sw-align-center", "align", "align-center"},
    {"sw-align-right", "align", "align-right"},
};

void status(Session& session, const std::string& text) {
    session.app->document().set("status", "text", text);
}

void save_doc(Session& session) {
    if (session.store.empty()) {
        return;
    }
    std::ofstream out(session.store / "board.json", std::ios::binary);
    if (out) {
        out << to_json(session.doc);
    }
}

void load_doc(Session& session) {
    std::ifstream in(session.store / "board.json", std::ios::binary);
    if (!in) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    from_json(session.doc, buffer.str());
}

bool board_box(Session& session, float& x, float& y, float& w, float& h) {
    const auto box = session.app->document().measure("board");
    if (!box) {
        return false;
    }
    x = box->border.x;
    y = box->border.y;
    w = box->border.width;
    h = box->border.height;
    return true;
}

bool inside_board(Session& session, float px, float py) {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    if (!board_box(session, x, y, w, h)) {
        return false;
    }
    return px >= x && py >= y && px < x + w && py < y + h;
}

Node* node_at(Session& session, float px, float py, const std::string& skip) {
    std::vector<Node*> nodes;
    collect_nodes(session.doc.roots, nodes);
    Node* best = nullptr;
    float best_area = 0;
    for (Node* node : nodes) {
        if (!skip.empty() && is_under(session.doc, node, skip)) {
            continue;
        }
        const auto box = session.app->document().measure(node->id);
        if (!box) {
            continue;
        }
        const float x = box->border.x;
        const float y = box->border.y;
        const float w = box->border.width;
        const float h = box->border.height;
        if (px < x || py < y || px >= x + w || py >= y + h) {
            continue;
        }
        const float area = std::max(1.f, w * h);
        if (best == nullptr || area < best_area) {
            best = node;
            best_area = area;
        }
    }
    return best;
}

Target drop_target(Session& session, float px, float py, const std::string& moving_id,
                   const std::string& create_kind) {
    Target target;
    if (!inside_board(session, px, py)) {
        return target;
    }
    Node* moving = moving_id.empty() ? nullptr : locate(session.doc, moving_id).node;
    Node* hit = node_at(session, px, py, moving_id);
    const bool free = device_of(session.doc).free;
    auto accept = [&](Node* parent) {
        if (moving != nullptr) {
            return can_reparent(session.doc, moving, parent);
        }
        return can_place(session.doc, create_kind, parent);
    };
    auto beside = [&](Node* node) {
        Located at = locate(session.doc, node->id);
        const auto box = session.app->document().measure(node->id);
        const float x = box ? box->border.x : 0.f;
        const float y = box ? box->border.y : 0.f;
        const float w = box ? box->border.width : 0.f;
        const float h = box ? box->border.height : 0.f;
        const bool before = at.parent != nullptr && at.parent->kind == "row" ? px < x + w * 0.5f
                                                                              : py < y + h * 0.5f;
        return before ? at.index : at.index + 1;
    };
    if (hit != nullptr && is_container(hit) && accept(hit)) {
        target.ok = true;
        target.parent = hit;
        target.index = static_cast<int>(hit->children.size());
        return target;
    }
    if (hit != nullptr) {
        Located at = locate(session.doc, hit->id);
        if (at.parent != nullptr && accept(at.parent)) {
            target.ok = true;
            target.parent = at.parent;
            target.index = beside(hit);
            return target;
        }
        if (at.parent == nullptr && !free && accept(nullptr)) {
            target.ok = true;
            target.index = beside(hit);
            return target;
        }
    }
    if (moving != nullptr && free) {
        Located at = locate(session.doc, moving->id);
        if (at.parent == nullptr) {
            target.ok = true;
            target.keep = true;
            return target;
        }
        if (accept(nullptr)) {
            target.ok = true;
            target.free = true;
            target.index = static_cast<int>(session.doc.roots.size());
            return target;
        }
        target.ok = true;
        target.keep = true;
        return target;
    }
    if (accept(nullptr)) {
        target.ok = true;
        target.free = free;
        target.index = static_cast<int>(session.doc.roots.size());
    }
    return target;
}

void highlight(Session& session, const std::string& id) {
    if (session.drop_id == id) {
        return;
    }
    set_flag(*session.app, session.drop_id, "is-drop", false);
    session.drop_id = id;
    set_flag(*session.app, session.drop_id, "is-drop", true);
}

void hide_ghost(Session& session) {
    session.app->document().set("ghost", "visible", "false");
}

void show_ghost(Session& session, const std::string& kind, float px, float py) {
    const KindSpec* spec = kind_of(kind);
    if (spec == nullptr) {
        return;
    }
    float bx = 0;
    float by = 0;
    float bw = 0;
    float bh = 0;
    if (!board_box(session, bx, by, bw, bh) || px < bx || py < by || px >= bx + bw || py >= by + bh) {
        hide_ghost(session);
        return;
    }
    const DeviceSpec& dev = device_of(session.doc);
    int gx = static_cast<int>(std::floor((px - bx) / kCell)) - spec->w / 2;
    int gy = static_cast<int>(std::floor((py - by) / kCell)) - spec->h / 2;
    gx = std::max(0, std::min(gx, dev.cols - spec->w));
    gy = std::max(0, std::min(gy, dev.rows - spec->h));
    swap_prefix(*session.app, "ghost", "x-", "x-" + std::to_string(gx));
    swap_prefix(*session.app, "ghost", "y-", "y-" + std::to_string(gy));
    swap_prefix(*session.app, "ghost", "w-", "w-" + std::to_string(spec->w));
    swap_prefix(*session.app, "ghost", "h-", "h-" + std::to_string(spec->h));
    session.app->document().set("ghost-cap", "text", spec->label);
    session.app->document().set("ghost", "visible", "true");
}

void place_free(Session& session, Node& node, float px, float py) {
    float bx = 0;
    float by = 0;
    float bw = 0;
    float bh = 0;
    if (!board_box(session, bx, by, bw, bh)) {
        return;
    }
    node.x = static_cast<int>(std::floor((px - bx) / kCell)) - node.w / 2;
    node.y = static_cast<int>(std::floor((py - by) / kCell)) - node.h / 2;
    clamp_box(session.doc, node);
}

bool position_enabled(Session& session, const Node* node) {
    if (node == nullptr || !device_of(session.doc).free) {
        return false;
    }
    return locate(session.doc, node->id).parent == nullptr;
}

void mark_swatches(Session& session, const Node* node) {
    for (const Swatch& swatch : kSwatches) {
        const bool on = node != nullptr && look_field(*node, swatch.group) == swatch.token;
        set_flag(*session.app, swatch.id, "is-on", on);
    }
}

void close_menus(Session& session) {
    session.app->document().set("menu_file", "checked", "false");
    session.app->document().set("menu_edit", "checked", "false");
    session.app->document().set("menu_view", "checked", "false");
    session.app->document().set("menu_help", "checked", "false");
}

void refresh(Session& session) {
    zui::App& app = *session.app;
    const Node* node = locate(session.doc, session.doc.selected).node;
    app.document().set("comp-block", "visible", node != nullptr ? "true" : "false");
    app.document().set("page-hint", "visible", node == nullptr ? "true" : "false");
    app.document().set("prop-title", "value", session.doc.title);
    app.document().set("doc-name", "text", session.doc.title.empty() ? "未命名界面" : session.doc.title);
    app.document().set("btn-undo", "disabled", session.doc.undo.empty() ? "true" : "false");
    app.document().set("btn-redo", "disabled", session.doc.redo.empty() ? "true" : "false");
    app.document().set("menu-undo", "disabled", session.doc.undo.empty() ? "true" : "false");
    app.document().set("menu-redo", "disabled", session.doc.redo.empty() ? "true" : "false");
    app.document().set("status-sel", "text", "未选中");
    const bool xy = position_enabled(session, node);
    for (const char* id : {"nx-dec", "nx-inc", "ny-dec", "ny-inc"}) {
        app.document().set(id, "disabled", xy ? "false" : "true");
    }
    set_flag(app, "btn-desktop", "is-on", session.doc.device == "desktop");
    set_flag(app, "btn-phone", "is-on", session.doc.device == "phone");
    set_flag(app, "menu-desktop", "is-on", session.doc.device == "desktop");
    set_flag(app, "menu-phone", "is-on", session.doc.device == "phone");
    if (node != nullptr) {
        const KindSpec* spec = kind_of(node->kind);
        const std::string kind = spec != nullptr ? spec->label : node->kind;
        app.document().set("prop-kind", "text", kind + "  ·  " + node->id);
        app.document().set("status-sel", "text", kind + " · " + node->id);
        app.document().set("prop-text", "value", node->text);
        app.document().set("lab-x", "text", std::to_string(node->x * kCell));
        app.document().set("lab-y", "text", std::to_string(node->y * kCell));
        app.document().set("lab-w", "text", std::to_string(node->w * kCell));
        app.document().set("lab-h", "text", std::to_string(node->h * kCell));
    }
    mark_swatches(session, node);
    app.document().set("code-view", "value", generate_html(session.doc));
}

void select_node(Session& session, const std::string& id) {
    if (session.doc.selected != id) {
        set_flag(*session.app, session.doc.selected, "is-selected", false);
        set_flag(*session.app, "layer-" + session.doc.selected, "is-on", false);
        session.doc.selected = id;
        set_flag(*session.app, session.doc.selected, "is-selected", true);
        set_flag(*session.app, "layer-" + session.doc.selected, "is-on", true);
        session.text_undo = false;
    }
    refresh(session);
}

void publish_all(Session& session) {
    publish_tree(*session.app, session.doc);
    refresh(session);
}

void show_tab(Session& session, bool props) {
    close_menus(session);
    session.app->document().set("panel-props", "visible", props ? "true" : "false");
    session.app->document().set("panel-code", "visible", props ? "false" : "true");
    set_flag(*session.app, "tab-props", "is-on", props);
    set_flag(*session.app, "tab-code", "is-on", !props);
    set_flag(*session.app, "menu-props", "is-on", props);
    set_flag(*session.app, "menu-code", "is-on", !props);
    if (!props) {
        session.app->document().set("code-view", "value", generate_html(session.doc));
    }
}

std::string safe_name(std::string name) {
    if (name.empty()) {
        name = "界面";
    }
    for (char& c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            c = '_';
        }
    }
    return name + ".html";
}

const zui::Pointer* pointer_of(const zui::Event& event) {
    return event.is_pointer();
}

void on_down(Session& session, const zui::Event& event) {
    close_menus(session);
    const zui::Pointer* pointer = pointer_of(event);
    if (pointer == nullptr || (!pointer->button.empty() && pointer->button != "left")) {
        return;
    }
    const std::string role(event.data("role"));
    if (role == "pal") {
        session.drag = {};
        session.drag.mode = Drag::Mode::Create;
        session.drag.kind = std::string(event.data("kind"));
        session.drag.start_x = pointer->x;
        session.drag.start_y = pointer->y;
        return;
    }
    if (role == "resize" || role == "node") {
        const std::string id(event.data("id"));
        Node* node = locate(session.doc, id).node;
        if (node == nullptr) {
            return;
        }
        select_node(session, id);
        session.drag = {};
        session.drag.mode = role == "resize" ? Drag::Mode::Resize : Drag::Mode::Move;
        session.drag.id = id;
        session.drag.start_x = pointer->x;
        session.drag.start_y = pointer->y;
        session.drag.ox = node->x;
        session.drag.oy = node->y;
        session.drag.ow = node->w;
        session.drag.oh = node->h;
        set_flag(*session.app, id, "is-dragging", true);
        return;
    }
    if (role == "board") {
        session.drag = {};
        session.drag.mode = Drag::Mode::Board;
    }
}

void on_move(Session& session, const zui::Event& event) {
    const zui::Pointer* pointer = pointer_of(event);
    if (pointer == nullptr || session.drag.mode == Drag::Mode::None ||
        session.drag.mode == Drag::Mode::Board) {
        return;
    }
    const int dx = static_cast<int>(std::round((pointer->x - session.drag.start_x) / kCell));
    const int dy = static_cast<int>(std::round((pointer->y - session.drag.start_y) / kCell));
    if (dx != 0 || dy != 0) {
        session.drag.moved = true;
    }
    if (session.drag.mode == Drag::Mode::Create) {
        show_ghost(session, session.drag.kind, pointer->x, pointer->y);
        Target target = drop_target(session, pointer->x, pointer->y, "", session.drag.kind);
        highlight(session, target.parent != nullptr ? target.parent->id : "");
        status(session, target.ok ? "松手放下" : "拖到画布上");
        return;
    }
    Node* node = locate(session.doc, session.drag.id).node;
    if (node == nullptr) {
        return;
    }
    if (!session.drag.undo && session.drag.moved) {
        push_undo(session.doc);
        session.drag.undo = true;
    }
    if (session.drag.mode == Drag::Mode::Resize) {
        node->w = session.drag.ow + dx;
        node->h = session.drag.oh + dy;
        clamp_box(session.doc, *node);
        sync_geom(*session.app, *node);
        refresh(session);
        return;
    }
    Located at = locate(session.doc, node->id);
    if (device_of(session.doc).free && at.parent == nullptr) {
        node->x = session.drag.ox + dx;
        node->y = session.drag.oy + dy;
        clamp_box(session.doc, *node);
        sync_geom(*session.app, *node);
        refresh(session);
    }
    Target target = drop_target(session, pointer->x, pointer->y, node->id, node->kind);
    highlight(session, target.parent != nullptr ? target.parent->id : "");
}

void on_up(Session& session, const zui::Event& event) {
    const zui::Pointer* pointer = pointer_of(event);
    const Drag drag = session.drag;
    session.drag = {};
    hide_ghost(session);
    highlight(session, "");
    if (pointer == nullptr || drag.mode == Drag::Mode::None) {
        return;
    }
    if (!drag.id.empty()) {
        set_flag(*session.app, drag.id, "is-dragging", false);
    }
    if (drag.mode == Drag::Mode::Board) {
        select_node(session, "");
        status(session, "未选中");
        return;
    }
    if (drag.mode == Drag::Mode::Create) {
        Target target = drop_target(session, pointer->x, pointer->y, "", drag.kind);
        if (!target.ok) {
            status(session, "没有放到画布上");
            return;
        }
        push_undo(session.doc);
        auto node = make_node(session.doc, drag.kind);
        if (node == nullptr) {
            return;
        }
        if (target.free) {
            place_free(session, *node, pointer->x, pointer->y);
        }
        const std::string id = node->id;
        const KindSpec* spec = kind_of(drag.kind);
        insert_node(session.doc, std::move(node), target.parent, target.index);
        session.doc.selected = id;
        publish_all(session);
        save_doc(session);
        status(session, std::string("已添加「") + (spec != nullptr ? spec->label : drag.kind) + "」");
        return;
    }
    if (drag.mode == Drag::Mode::Resize) {
        refresh(session);
        save_doc(session);
        status(session, drag.moved ? "已调整大小" : "已选中");
        return;
    }
    Target target = drop_target(session, pointer->x, pointer->y, drag.id, "");
    if (target.keep) {
        refresh(session);
        save_doc(session);
        status(session, drag.moved ? "已移动" : "已选中");
        return;
    }
    if (target.ok) {
        Located at = locate(session.doc, drag.id);
        const bool same = at.node != nullptr && at.parent == target.parent && at.index == target.index;
        if (!same) {
            if (!drag.undo) {
                push_undo(session.doc);
            }
            if (target.free) {
                if (Node* node = locate(session.doc, drag.id).node) {
                    place_free(session, *node, pointer->x, pointer->y);
                }
            }
            move_to(session.doc, drag.id, target.parent, target.index);
            publish_all(session);
            save_doc(session);
            status(session, target.parent != nullptr ? "已放入容器" : "已移动");
            return;
        }
    }
    refresh(session);
    save_doc(session);
    status(session, drag.moved ? "已移动" : "已选中");
}

void on_key(Session& session, const zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing) {
        return;
    }
    const std::string name(key->key);
    if (name == "Delete" || name == "Backspace") {
        if (session.doc.selected.empty()) {
            return;
        }
        push_undo(session.doc);
        remove_node(session.doc, session.doc.selected);
        publish_all(session);
        save_doc(session);
        status(session, "已删除");
        return;
    }
    if ((key->ctrl || key->meta) && (name == "z" || name == "Z")) {
        if (key->shift) {
            if (redo_doc(session.doc)) {
                session.text_undo = false;
                session.title_undo = false;
                publish_all(session);
                save_doc(session);
                status(session, "已重做");
            }
        } else if (undo_doc(session.doc)) {
            session.text_undo = false;
            session.title_undo = false;
            publish_all(session);
            save_doc(session);
            status(session, "已撤销");
        }
        return;
    }
    if ((key->ctrl || key->meta) && (name == "y" || name == "Y")) {
        if (redo_doc(session.doc)) {
            session.text_undo = false;
            session.title_undo = false;
            publish_all(session);
            save_doc(session);
            status(session, "已重做");
        }
        return;
    }
    if (name == "Escape") {
        select_node(session, "");
        status(session, "未选中");
        return;
    }
    int axis = 0;
    int step = 0;
    if (name == "ArrowLeft") {
        axis = 1;
        step = -1;
    } else if (name == "ArrowRight") {
        axis = 1;
        step = 1;
    } else if (name == "ArrowUp") {
        axis = 2;
        step = -1;
    } else if (name == "ArrowDown") {
        axis = 2;
        step = 1;
    }
    if (axis == 0 || session.doc.selected.empty()) {
        return;
    }
    Node* node = locate(session.doc, session.doc.selected).node;
    if (node == nullptr || !position_enabled(session, node)) {
        return;
    }
    push_undo(session.doc);
    if (axis == 1) {
        node->x += step;
    } else {
        node->y += step;
    }
    clamp_box(session.doc, *node);
    sync_geom(*session.app, *node);
    refresh(session);
    save_doc(session);
}

void on_text(Session& session, const zui::Event& event) {
    Node* node = locate(session.doc, session.doc.selected).node;
    if (node == nullptr) {
        return;
    }
    if (!session.text_undo) {
        push_undo(session.doc);
        session.text_undo = true;
    }
    node->text = std::string(event.value());
    session.app->document().set("cap-" + node->id, "text", node->text);
    session.app->document().set("layer-" + node->id, "text", layer_text(session.doc, *node));
    session.app->document().set("code-view", "value", generate_html(session.doc));
    save_doc(session);
}

void on_title(Session& session, const zui::Event& event) {
    if (!session.title_undo) {
        push_undo(session.doc);
        session.title_undo = true;
    }
    session.doc.title = std::string(event.value());
    const DeviceSpec& dev = device_of(session.doc);
    session.app->document().set("device-title", "text", std::string(dev.label) + " " + std::to_string(dev.w) +
                                                 " × " + std::to_string(dev.h));
    const std::string window = session.doc.title.empty() ? "小竹设计" : session.doc.title + " · 小竹设计";
    session.app->window().set_title(window);
    session.app->document().set("doc-name", "text", session.doc.title.empty() ? "未命名界面" : session.doc.title);
    session.app->document().set("code-view", "value", generate_html(session.doc));
    save_doc(session);
}

void on_nudge(Session& session, const zui::Event& event) {
    Node* node = locate(session.doc, session.doc.selected).node;
    if (node == nullptr) {
        return;
    }
    const std::string key(event.data("key"));
    int step = 0;
    try {
        step = std::stoi(std::string(event.data("step")));
    } catch (const std::exception&) {
        return;
    }
    if ((key == "x" || key == "y") && !position_enabled(session, node)) {
        return;
    }
    push_undo(session.doc);
    if (key == "x") node->x += step;
    else if (key == "y") node->y += step;
    else if (key == "w") node->w += step;
    else if (key == "h") node->h += step;
    else return;
    clamp_box(session.doc, *node);
    sync_geom(*session.app, *node);
    refresh(session);
    save_doc(session);
    status(session, "已调整");
}

void on_swatch(Session& session, const zui::Event& event) {
    Node* node = locate(session.doc, session.doc.selected).node;
    if (node == nullptr) {
        return;
    }
    const std::string group(event.data("group"));
    const std::string token(event.data("token"));
    if (look_field(*node, group) == token) {
        return;
    }
    push_undo(session.doc);
    set_look_field(*node, group, token);
    sync_look(*session.app, *node);
    refresh(session);
    save_doc(session);
}

void on_device(Session& session, const zui::Event& event) {
    close_menus(session);
    const std::string name(event.data("device"));
    if (name != "desktop" && name != "phone") {
        return;
    }
    if (session.doc.device == name) {
        return;
    }
    push_undo(session.doc);
    session.doc.device = name;
    publish_all(session);
    save_doc(session);
    status(session, name == "phone" ? "手机画布：纵向排列" : "桌面画布：自由摆放");
}

void on_undo(Session& session) {
    close_menus(session);
    if (!undo_doc(session.doc)) {
        return;
    }
    session.text_undo = false;
    session.title_undo = false;
    publish_all(session);
    save_doc(session);
    status(session, "已撤销");
}

void on_redo(Session& session) {
    close_menus(session);
    if (!redo_doc(session.doc)) {
        return;
    }
    session.text_undo = false;
    session.title_undo = false;
    publish_all(session);
    save_doc(session);
    status(session, "已重做");
}

void on_sample(Session& session) {
    close_menus(session);
    push_undo(session.doc);
    load_sample(session.doc);
    publish_all(session);
    save_doc(session);
    status(session, "已放入示例");
}

void on_clear(Session& session) {
    close_menus(session);
    if (session.doc.roots.empty()) {
        return;
    }
    push_undo(session.doc);
    session.doc.roots.clear();
    session.doc.selected.clear();
    publish_all(session);
    save_doc(session);
    status(session, "已清空画布");
}

void on_export(Session& session) {
    close_menus(session);
    zui::FileDialogOptions options;
    options.title = "导出 HTML";
    options.default_name = safe_name(session.doc.title);
    options.filters.push_back({"HTML", "*.html"});
    const zui::PickPath picked = session.app->host().pick_save(options);
    if (picked.status != zui::PickStatus::Ok || picked.path.empty()) {
        status(session, "已取消导出");
        return;
    }
    std::ofstream out(picked.path, std::ios::binary);
    if (!out) {
        status(session, "导出失败");
        return;
    }
    out << generate_html(session.doc);
    status(session, "已导出 HTML");
}

void on_copy(Session& session) {
    if (session.app->host().set_clipboard_text(generate_html(session.doc))) {
        status(session, "已复制 HTML");
    } else {
        status(session, "复制失败");
    }
}

void on_layer(Session& session, const zui::Event& event) {
    select_node(session, std::string(event.data("id")));
    status(session, "已选中");
}

}  // namespace

void bind_actions(Session& session) {
    session.app->document().bind("on_down", [&session](zui::Event& event) { on_down(session, event); });
    session.app->document().bind("on_move", [&session](zui::Event& event) { on_move(session, event); });
    session.app->document().bind("on_up", [&session](zui::Event& event) { on_up(session, event); });
    session.app->document().bind("on_key", [&session](zui::Event& event) { on_key(session, event); });
    session.app->document().bind("on_field_key", [](zui::Event&) {});
    session.app->document().bind("on_field_blur", [&session](zui::Event&) {
        session.text_undo = false;
        session.title_undo = false;
    });
    session.app->document().bind("on_text", [&session](zui::Event& event) { on_text(session, event); });
    session.app->document().bind("on_title", [&session](zui::Event& event) { on_title(session, event); });
    session.app->document().bind("on_nudge", [&session](zui::Event& event) { on_nudge(session, event); });
    session.app->document().bind("on_swatch", [&session](zui::Event& event) { on_swatch(session, event); });
    session.app->document().bind("on_device", [&session](zui::Event& event) { on_device(session, event); });
    session.app->document().bind("on_undo", [&session](zui::Event&) { on_undo(session); });
    session.app->document().bind("on_redo", [&session](zui::Event&) { on_redo(session); });
    session.app->document().bind("on_sample", [&session](zui::Event&) { on_sample(session); });
    session.app->document().bind("on_clear", [&session](zui::Event&) { on_clear(session); });
    session.app->document().bind("on_export", [&session](zui::Event&) { on_export(session); });
    session.app->document().bind("on_copy", [&session](zui::Event&) { on_copy(session); });
    session.app->document().bind("on_tab", [&session](zui::Event& event) {
        show_tab(session, std::string(event.data("tab")) != "code");
    });
    session.app->document().bind("on_layer", [&session](zui::Event& event) { on_layer(session, event); });
}

void boot(Session& session) {
    session.store = session.app->host().user_data_dir();
    load_doc(session);
    publish_all(session);
    status(session, "就绪");
}

}  // namespace designer
