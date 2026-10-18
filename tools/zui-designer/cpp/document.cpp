#include "document.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace designer {
namespace {

using json = nlohmann::json;

const DeviceSpec kDevices[] = {
    {"desktop", "桌面", 704, 480, 88, 60, true},
    {"phone", "手机", 392, 640, 49, 80, false},
};

const KindSpec kKinds[] = {
    {"heading", "标题", "标题", 28, 6, "", "fg-ink", "fs-28", "", ""},
    {"text", "文本", "正文", 40, 8, "", "fg-soft", "fs-14", "", ""},
    {"button", "按钮", "按钮", 14, 5, "bg-accent", "fg-paper", "fs-14", "radius-8", ""},
    {"input", "输入框", "请输入", 32, 5, "bg-paper", "", "fs-14", "radius-8", ""},
    {"area", "多行文本", "多行文本", 36, 12, "bg-paper", "", "fs-14", "radius-8", ""},
    {"check", "勾选", "选项", 16, 4, "", "fg-ink", "fs-14", "", ""},
    {"image", "图片", "图片", 22, 16, "bg-tint", "fg-soft", "", "radius-12", ""},
    {"rule", "分割线", "", 40, 2, "", "", "", "", ""},
    {"row", "横向", "", 40, 12, "", "", "", "radius-8", ""},
    {"col", "纵向", "", 24, 28, "", "", "", "radius-8", ""},
};

template <typename Fn>
bool walk(std::vector<std::unique_ptr<Node>>& list, Node* parent, Fn&& fn) {
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        Node* node = list[i].get();
        if (fn(node, parent, list, i)) {
            return true;
        }
        if (walk(node->children, node, fn)) {
            return true;
        }
    }
    return false;
}

int subtree_height(const Node& node) {
    int height = 1;
    for (const auto& child : node.children) {
        height = std::max(height, 1 + subtree_height(*child));
    }
    return height;
}

void min_size(const std::string& kind, int& w, int& h) {
    if (kind == "rule") {
        w = 4;
        h = 1;
        return;
    }
    if (kind == "check") {
        w = 8;
        h = 3;
        return;
    }
    w = 4;
    h = 3;
}

std::string json_string(const json& value) {
    return value.is_string() ? value.get<std::string>() : std::string{};
}

int json_int(const json& value) {
    return value.is_number() ? value.get<int>() : 0;
}

void read_tree(Document& doc, const json& rows) {
    std::vector<Node*> by_id;
    auto find_built = [&](const std::string& id) -> Node* {
        for (Node* node : by_id) {
            if (node->id == id) {
                return node;
            }
        }
        return nullptr;
    };
    for (const json& row : rows) {
        if (!row.is_object()) {
            continue;
        }
        const std::string kind = json_string(row.value("kind", json()));
        if (kind_of(kind) == nullptr || !row.contains("id")) {
            continue;
        }
        auto node = std::make_unique<Node>();
        node->id = json_string(row["id"]);
        node->kind = kind;
        node->text = json_string(row.value("text", json()));
        node->x = json_int(row.value("x", json()));
        node->y = json_int(row.value("y", json()));
        node->w = json_int(row.value("w", json()));
        node->h = json_int(row.value("h", json()));
        node->bg = json_string(row.value("bg", json()));
        node->fg = json_string(row.value("fg", json()));
        node->fs = json_string(row.value("fs", json()));
        node->radius = json_string(row.value("radius", json()));
        node->align = json_string(row.value("align", json()));
        clamp_box(doc, *node);
        Node* raw = node.get();
        const std::string parent_id = json_string(row.value("parent", json()));
        Node* parent = parent_id.empty() ? nullptr : find_built(parent_id);
        if (parent != nullptr) {
            parent->children.push_back(std::move(node));
        } else {
            doc.roots.push_back(std::move(node));
        }
        by_id.push_back(raw);
    }
    int max_id = 0;
    for (Node* node : by_id) {
        if (node->id.size() > 1 && node->id[0] == 'n') {
            try {
                max_id = std::max(max_id, std::stoi(node->id.substr(1)));
            } catch (const std::exception&) {
            }
        }
    }
    if (doc.next_id <= max_id) {
        doc.next_id = max_id + 1;
    }
    if (!doc.selected.empty() && find_built(doc.selected) == nullptr) {
        doc.selected.clear();
    }
}

void write_tree(json& rows, const std::vector<std::unique_ptr<Node>>& list, const std::string& parent) {
    for (const auto& node : list) {
        rows.push_back({
            {"id", node->id},
            {"kind", node->kind},
            {"text", node->text},
            {"parent", parent},
            {"x", node->x},
            {"y", node->y},
            {"w", node->w},
            {"h", node->h},
            {"bg", node->bg},
            {"fg", node->fg},
            {"fs", node->fs},
            {"radius", node->radius},
            {"align", node->align},
        });
        write_tree(rows, node->children, node->id);
    }
}

void restore_snapshot(Document& doc, const std::string& text) {
    auto undo = std::move(doc.undo);
    auto redo = std::move(doc.redo);
    from_json(doc, text);
    doc.undo = std::move(undo);
    doc.redo = std::move(redo);
}

}  // namespace

const DeviceSpec& device_of(const Document& doc) {
    for (const DeviceSpec& spec : kDevices) {
        if (doc.device == spec.key) {
            return spec;
        }
    }
    return kDevices[0];
}

const KindSpec* kind_of(const std::string& kind) {
    for (const KindSpec& spec : kKinds) {
        if (kind == spec.kind) {
            return &spec;
        }
    }
    return nullptr;
}

bool is_container_kind(const std::string& kind) {
    return kind == "row" || kind == "col";
}

bool is_container(const Node* node) {
    return node != nullptr && is_container_kind(node->kind);
}

Located locate(Document& doc, const std::string& id) {
    Located found;
    if (id.empty()) {
        return found;
    }
    walk(doc.roots, nullptr, [&](Node* node, Node* parent, std::vector<std::unique_ptr<Node>>& list, int index) {
        if (node->id != id) {
            return false;
        }
        found.node = node;
        found.parent = parent;
        found.list = &list;
        found.index = index;
        return true;
    });
    return found;
}

int depth_of(Document& doc, const Node* node) {
    int depth = 0;
    const Node* cursor = node;
    while (cursor != nullptr) {
        ++depth;
        Located at = locate(doc, cursor->id);
        cursor = at.parent;
    }
    return depth;
}

bool is_under(Document& doc, const Node* node, const std::string& ancestor_id) {
    const Node* cursor = node;
    while (cursor != nullptr) {
        if (cursor->id == ancestor_id) {
            return true;
        }
        Located at = locate(doc, cursor->id);
        cursor = at.parent;
    }
    return false;
}

void clamp_box(const Document& doc, Node& node) {
    const DeviceSpec& dev = device_of(doc);
    int min_w = 0;
    int min_h = 0;
    min_size(node.kind, min_w, min_h);
    node.w = std::max(min_w, node.w);
    node.h = std::max(min_h, node.h);
    node.w = std::min(node.w, dev.cols);
    node.h = std::min(node.h, dev.rows);
    node.x = std::max(0, node.x);
    node.y = std::max(0, node.y);
    if (node.x + node.w > dev.cols) {
        node.x = std::max(0, dev.cols - node.w);
    }
    if (node.y + node.h > dev.rows) {
        node.y = std::max(0, dev.rows - node.h);
    }
}

std::unique_ptr<Node> make_node(Document& doc, const std::string& kind) {
    const KindSpec* spec = kind_of(kind);
    if (spec == nullptr) {
        return nullptr;
    }
    auto node = std::make_unique<Node>();
    node->id = "n" + std::to_string(doc.next_id++);
    node->kind = kind;
    node->text = spec->text;
    node->w = spec->w;
    node->h = spec->h;
    node->bg = spec->bg;
    node->fg = spec->fg;
    node->fs = spec->fs;
    node->radius = spec->radius;
    node->align = spec->align;
    return node;
}

std::string look_class(const Node& node) {
    std::string out;
    for (const std::string& token : {node.bg, node.fg, node.fs, node.radius, node.align}) {
        if (token.empty()) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += token;
    }
    return out;
}

std::string geom_class(const Node& node) {
    return "x-" + std::to_string(node.x) + " y-" + std::to_string(node.y) + " w-" +
           std::to_string(node.w) + " h-" + std::to_string(node.h);
}

std::string flags_class(const Node& node) {
    return is_container(&node) && node.children.empty() ? "is-empty" : "";
}

std::string layer_text(Document& doc, const Node& node) {
    const int depth = depth_of(doc, &node);
    std::string out;
    for (int i = 0; i < depth; ++i) {
        out += "· ";
    }
    const KindSpec* spec = kind_of(node.kind);
    out += spec != nullptr ? spec->label : node.kind;
    if (!node.text.empty()) {
        out.push_back(' ');
        out += node.text;
    }
    return out;
}

std::string look_field(const Node& node, const std::string& group) {
    if (group == "bg") return node.bg;
    if (group == "fg") return node.fg;
    if (group == "fs") return node.fs;
    if (group == "radius") return node.radius;
    if (group == "align") return node.align;
    return {};
}

void set_look_field(Node& node, const std::string& group, const std::string& token) {
    if (group == "bg") node.bg = token;
    else if (group == "fg") node.fg = token;
    else if (group == "fs") node.fs = token;
    else if (group == "radius") node.radius = token;
    else if (group == "align") node.align = token;
}

std::string to_json(const Document& doc) {
    json rows = json::array();
    write_tree(rows, doc.roots, "");
    json out = {
        {"version", 1},
        {"device", doc.device},
        {"title", doc.title},
        {"nextId", doc.next_id},
        {"selected", doc.selected},
        {"nodes", std::move(rows)},
    };
    return out.dump();
}

bool from_json(Document& doc, const std::string& text) {
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || parsed.value("version", 0) != 1 ||
        !parsed.contains("nodes") || !parsed["nodes"].is_array()) {
        return false;
    }
    doc.device = json_string(parsed.value("device", json())) == "phone" ? "phone" : "desktop";
    const std::string title = json_string(parsed.value("title", json()));
    doc.title = title.empty() ? "未命名界面" : title;
    doc.next_id = std::max(1, json_int(parsed.value("nextId", json())));
    doc.selected = json_string(parsed.value("selected", json()));
    doc.roots.clear();
    read_tree(doc, parsed["nodes"]);
    return true;
}

void push_undo(Document& doc) {
    doc.undo.push_back(to_json(doc));
    if (doc.undo.size() > 40) {
        doc.undo.erase(doc.undo.begin());
    }
    doc.redo.clear();
}

bool undo_doc(Document& doc) {
    if (doc.undo.empty()) {
        return false;
    }
    doc.redo.push_back(to_json(doc));
    const std::string snapshot = std::move(doc.undo.back());
    doc.undo.pop_back();
    restore_snapshot(doc, snapshot);
    return true;
}

bool redo_doc(Document& doc) {
    if (doc.redo.empty()) {
        return false;
    }
    doc.undo.push_back(to_json(doc));
    const std::string snapshot = std::move(doc.redo.back());
    doc.redo.pop_back();
    restore_snapshot(doc, snapshot);
    return true;
}

bool can_place(Document& doc, const std::string& kind, const Node* parent) {
    if (kind_of(kind) == nullptr) {
        return false;
    }
    const int parent_depth = parent != nullptr ? depth_of(doc, parent) : 0;
    const int next_depth = parent_depth + 1;
    if (next_depth > 4) {
        return false;
    }
    if (is_container_kind(kind) && next_depth > 3) {
        return false;
    }
    return true;
}

bool can_reparent(Document& doc, const Node* node, const Node* parent) {
    if (node == nullptr) {
        return false;
    }
    if (parent != nullptr && is_under(doc, parent, node->id)) {
        return false;
    }
    const int parent_depth = parent != nullptr ? depth_of(doc, parent) : 0;
    if (parent_depth + subtree_height(*node) > 4) {
        return false;
    }
    if (is_container(node) && parent_depth + 1 > 3) {
        return false;
    }
    return true;
}

void insert_node(Document& doc, std::unique_ptr<Node> node, Node* parent, int index) {
    if (node == nullptr) {
        return;
    }
    auto& list = parent != nullptr ? parent->children : doc.roots;
    if (index < 0 || index > static_cast<int>(list.size())) {
        index = static_cast<int>(list.size());
    }
    list.insert(list.begin() + index, std::move(node));
}

bool move_to(Document& doc, const std::string& id, Node* parent, int index) {
    Located at = locate(doc, id);
    if (at.node == nullptr || at.list == nullptr) {
        return false;
    }
    auto& dest_list = parent != nullptr ? parent->children : doc.roots;
    int dest = index < 0 ? static_cast<int>(dest_list.size()) : index;
    if (at.list == &dest_list && at.index < dest) {
        --dest;
    }
    std::unique_ptr<Node> held = std::move((*at.list)[static_cast<size_t>(at.index)]);
    at.list->erase(at.list->begin() + at.index);
    if (dest > static_cast<int>(dest_list.size())) {
        dest = static_cast<int>(dest_list.size());
    }
    if (dest < 0) {
        dest = 0;
    }
    dest_list.insert(dest_list.begin() + dest, std::move(held));
    return true;
}

bool remove_node(Document& doc, const std::string& id) {
    Located at = locate(doc, id);
    if (at.node == nullptr || at.list == nullptr) {
        return false;
    }
    at.list->erase(at.list->begin() + at.index);
    if (locate(doc, doc.selected).node == nullptr) {
        doc.selected.clear();
    }
    return true;
}

void load_sample(Document& doc) {
    doc.roots.clear();
    doc.selected.clear();
    doc.next_id = 1;
    auto heading = make_node(doc, "heading");
    heading->text = "小竹问候";
    heading->x = 4;
    heading->y = 3;
    auto text = make_node(doc, "text");
    text->text = "把组件拖进画布，选中后在右侧改属性。";
    text->x = 4;
    text->y = 10;
    text->w = 48;
    auto input = make_node(doc, "input");
    input->text = "你的名字";
    input->x = 4;
    input->y = 20;
    auto row = make_node(doc, "row");
    row->x = 4;
    row->y = 28;
    row->w = 36;
    row->h = 8;
    auto ok = make_node(doc, "button");
    ok->text = "确定";
    auto cancel = make_node(doc, "button");
    cancel->text = "取消";
    cancel->bg = "bg-paper";
    cancel->fg = "fg-ink";
    row->children.push_back(std::move(ok));
    row->children.push_back(std::move(cancel));
    const std::string selected = heading->id;
    doc.roots.push_back(std::move(heading));
    doc.roots.push_back(std::move(text));
    doc.roots.push_back(std::move(input));
    doc.roots.push_back(std::move(row));
    doc.selected = selected;
}

void collect_nodes(const std::vector<std::unique_ptr<Node>>& list, std::vector<Node*>& out) {
    for (const auto& node : list) {
        out.push_back(node.get());
        collect_nodes(node->children, out);
    }
}

}  // namespace designer
