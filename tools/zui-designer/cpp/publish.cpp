#include "publish.h"

#include <string>
#include <vector>

namespace designer {
namespace {

class Rows : public zui::ListModel {
public:
    struct Row {
        std::string id;
        std::string kind;
        std::string text;
        std::string sel;
        std::string geom;
        std::string look;
        std::string flags;
    };

    std::vector<Row> items;

    size_t row_count() const override { return items.size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (row >= items.size()) {
            return {};
        }
        const Row& item = items[row];
        if (key == "id") return item.id;
        if (key == "kind") return item.kind;
        if (key == "text") return item.text;
        if (key == "sel") return item.sel;
        if (key == "geom") return item.geom;
        if (key == "look") return item.look;
        if (key == "flags") return item.flags;
        return {};
    }
};

void fill_nodes(const Document& doc, const std::vector<std::unique_ptr<Node>>& nodes, Rows& rows) {
    for (const auto& node : nodes) {
        Rows::Row row;
        row.id = node->id;
        row.kind = "kind-" + node->kind;
        row.text = node->text;
        row.sel = node->id == doc.selected ? "is-selected" : "";
        row.geom = geom_class(*node);
        row.look = look_class(*node);
        row.flags = flags_class(*node);
        rows.items.push_back(std::move(row));
    }
}

void publish_level(zui::App& app, Document& doc, const std::string& host,
                   const std::vector<std::unique_ptr<Node>>& nodes, int depth) {
    Rows rows;
    fill_nodes(doc, nodes, rows);
    app.document().set_model(host, rows);
    if (depth >= 3) {
        return;
    }
    for (const auto& node : nodes) {
        publish_level(app, doc, "kids-" + node->id, node->children, depth + 1);
    }
}

std::string token_with_prefix(std::string_view cls, std::string_view prefix) {
    std::string current;
    for (size_t i = 0; i <= cls.size(); ++i) {
        if (i == cls.size() || cls[i] == ' ') {
            if (!current.empty() && current.compare(0, prefix.size(), prefix) == 0) {
                return current;
            }
            current.clear();
        } else {
            current.push_back(cls[i]);
        }
    }
    return {};
}

}  // namespace

void swap_prefix(zui::App& app, std::string_view id, std::string_view prefix, std::string_view next) {
    if (id.empty()) {
        return;
    }
    const std::string old = token_with_prefix(app.document().get(id, "class"), prefix);
    if (old == next) {
        return;
    }
    if (!old.empty()) {
        app.document().remove_class(id, old);
    }
    if (!next.empty()) {
        app.document().add_class(id, next);
    }
}

void set_flag(zui::App& app, std::string_view id, std::string_view token, bool on) {
    if (id.empty() || token.empty()) {
        return;
    }
    if (on) {
        app.document().add_class(id, token);
    } else {
        app.document().remove_class(id, token);
    }
}

void sync_geom(zui::App& app, const Node& node) {
    swap_prefix(app, node.id, "x-", "x-" + std::to_string(node.x));
    swap_prefix(app, node.id, "y-", "y-" + std::to_string(node.y));
    swap_prefix(app, node.id, "w-", "w-" + std::to_string(node.w));
    swap_prefix(app, node.id, "h-", "h-" + std::to_string(node.h));
}

void sync_look(zui::App& app, const Node& node) {
    swap_prefix(app, node.id, "bg-", node.bg);
    swap_prefix(app, node.id, "fg-", node.fg);
    swap_prefix(app, node.id, "fs-", node.fs);
    swap_prefix(app, node.id, "radius-", node.radius);
    swap_prefix(app, node.id, "align-", node.align);
}

void publish_tree(zui::App& app, Document& doc) {
    publish_level(app, doc, "board", doc.roots, 0);
    std::vector<Node*> nodes;
    collect_nodes(doc.roots, nodes);
    Rows layers;
    for (Node* node : nodes) {
        Rows::Row row;
        row.id = node->id;
        row.text = layer_text(doc, *node);
        row.sel = node->id == doc.selected ? "is-on" : "";
        layers.items.push_back(std::move(row));
    }
    app.document().set_model("layers", layers);
    app.document().set("layer-count", "text", std::to_string(nodes.size()));
    app.document().set("layers-empty", "visible", nodes.empty() ? "true" : "false");
    app.document().set("empty-hint", "visible", doc.roots.empty() ? "true" : "false");
    const DeviceSpec& dev = device_of(doc);
    set_flag(app, "device", "device-desktop", doc.device == "desktop");
    set_flag(app, "device", "device-phone", doc.device == "phone");
    set_flag(app, "board", "mode-free", dev.free);
    set_flag(app, "board", "mode-stack", !dev.free);
    app.document().set("device-title", "text", std::string(dev.label) + " " + std::to_string(dev.w) + " × " +
                                         std::to_string(dev.h));
    const std::string title = doc.title.empty() ? "小竹设计" : doc.title + " · 小竹设计";
    app.window().set_title(title);
}

}  // namespace designer
