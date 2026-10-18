// Row stamp: keep a row by identity and fingerprint, else clone the template proto
// (RFC_MODEL.md KD3). Nested `with_children` is `apply_nested`.

#include "html/document.h"
#include "html/document_internal.h"
#include "html/model_internal.h"

#include <zui/model.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace html {

std::string ModelBinder::subst_placeholders(std::string_view in,
                                            const Lookup& lookup) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (i + 1 < in.size() && in[i] == '{' && in[i + 1] == '{') {
            const size_t end = in.find("}}", i + 2);
            if (end != std::string_view::npos) {
                std::string key(in.substr(i + 2, end - (i + 2)));
                size_t a = 0;
                while (a < key.size() && std::isspace(static_cast<unsigned char>(key[a]))) {
                    ++a;
                }
                size_t b = key.size();
                while (b > a && std::isspace(static_cast<unsigned char>(key[b - 1]))) {
                    --b;
                }
                key = key.substr(a, b - a);
                out += lookup(key);
                i = end + 2;
                continue;
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

std::unique_ptr<Node> ModelBinder::clone_node(const Node* src, Node* parent) {
    auto n = std::make_unique<Node>();
    n->parent = parent;
    n->tag = src->tag;
    n->id = src->id;
    n->class_name = src->class_name;
    n->text = src->text;
    n->value = src->value;
    // `attrs` carries the `on*` handler attributes too, so a clone keeps them for free.
    n->attrs = src->attrs;
    n->visible = src->visible;
    n->disabled = src->disabled;
    n->checked = src->checked;
    n->default_selected = src->default_selected;
    for (const auto& ch : src->children) {
        n->children.push_back(clone_node(ch.get(), n.get()));
    }
    return n;
}

void ModelBinder::interpolate_node(Node* n, const Lookup& lookup) {
    n->text = subst_placeholders(n->text, lookup);
    n->id = subst_placeholders(n->id, lookup);
    n->class_name = subst_placeholders(n->class_name, lookup);
    n->value = subst_placeholders(n->value, lookup);
    // The `on*` handler attributes live in `attrs`, and the loop below substitutes them with
    // every other attribute, which is what lets a <template> say onclick="{{handler}}".
    for (auto& kv : n->attrs) {
        kv.second = subst_placeholders(kv.second, lookup);
    }
    if (n->attrs.count("class")) {
        n->class_name = n->attrs["class"];
    }
    if (n->attrs.count("id")) {
        n->id = n->attrs["id"];
    }
    for (auto& ch : n->children) {
        if (ch->tag == "template") {
            continue;
        }
        interpolate_node(ch.get(), lookup);
    }
}
int ModelBinder::row_stride(const Node* tmpl) {
    int n = 0;
    if (tmpl == nullptr) {
        return 0;
    }
    for (const auto& proto : tmpl->children) {
        if (proto != nullptr && proto->tag == "#text" && whitespace_only(proto->text)) {
            continue;
        }
        ++n;
    }
    return n;
}

std::string ModelBinder::node_fingerprint(const Node* n, const Node* stop) {
    if (n == nullptr) {
        return {};
    }
    std::string s;
    s.append(n->tag);
    s.push_back('\n');
    s.append(n->id);
    s.push_back('\n');
    s.append(n->class_name);
    s.push_back('\n');
    s.append(n->text);
    s.push_back('\n');
    s.append(n->value);
    s.push_back('\n');
    std::vector<std::pair<std::string, std::string>> attrs(n->attrs.begin(), n->attrs.end());
    std::sort(attrs.begin(), attrs.end());
    for (const auto& kv : attrs) {
        s.append(kv.first);
        s.push_back('=');
        s.append(kv.second);
        s.push_back('\n');
    }
    s.push_back('{');
    if (n != stop) {
        for (const auto& ch : n->children) {
            if (ch != nullptr && ch->tag != "template") {
                s.append(node_fingerprint(ch.get(), stop));
            }
        }
    }
    s.push_back('}');
    return s;
}

/// The fingerprint of the node `clone_node` + `interpolate_node` would make for @p n under
/// @p lookup. Same bytes as `node_fingerprint` on that clone, so a kept row can be compared
/// without being cloned (RFC_MODEL.md KD3). The two substitution rules this has to mirror:
/// `interpolate_node` writes the placeholder-substituted `attrs` value last, so an `id` / `class`
/// attribute wins over the field of the same name.
std::string ModelBinder::node_fingerprint_with(const Node* n, const Lookup& lookup,
                                               const Node* stop) {
    if (n == nullptr) {
        return {};
    }
    std::string s;
    s.append(n->tag);
    s.push_back('\n');
    std::string id = subst_placeholders(n->id, lookup);
    std::string class_name = subst_placeholders(n->class_name, lookup);
    const auto id_attr = n->attrs.find("id");
    if (id_attr != n->attrs.end()) {
        id = subst_placeholders(id_attr->second, lookup);
    }
    const auto class_attr = n->attrs.find("class");
    if (class_attr != n->attrs.end()) {
        class_name = subst_placeholders(class_attr->second, lookup);
    }
    s.append(id);
    s.push_back('\n');
    s.append(class_name);
    s.push_back('\n');
    s.append(subst_placeholders(n->text, lookup));
    s.push_back('\n');
    s.append(subst_placeholders(n->value, lookup));
    s.push_back('\n');
    std::vector<std::pair<std::string, std::string>> attrs(n->attrs.begin(), n->attrs.end());
    std::sort(attrs.begin(), attrs.end());
    for (const auto& kv : attrs) {
        s.append(kv.first);
        s.push_back('=');
        s.append(subst_placeholders(kv.second, lookup));
        s.push_back('\n');
    }
    s.push_back('{');
    if (n != stop) {
        for (const auto& ch : n->children) {
            if (ch != nullptr && ch->tag != "template") {
                s.append(node_fingerprint_with(ch.get(), lookup, stop));
            }
        }
    }
    s.push_back('}');
    return s;
}

std::string ModelBinder::group_fingerprint(const std::vector<Node*>& nodes, const Node* stop) {
    std::string s;
    for (const Node* n : nodes) {
        s.append(node_fingerprint(n, stop));
    }
    return s;
}
bool ModelBinder::apply_on_node(Node* host, size_t row_count, const FieldFn& field,
                                std::string& error, const RowKeyFn& row_key,
                                const std::function<void(Node*)>& retiring, bool* unchanged,
                                const ProtoFn& proto_of_row, const ListModel* nested,
                                size_t depth) {
    Document& doc = doc_;
    if (host == nullptr) {
        error = "model host missing";
        return false;
    }
    Node* tmpl = find_template(host);
    if (tmpl == nullptr) {
        error = "model host has no <template>";
        return false;
    }
    const std::vector<Node*> protos = model_protos(tmpl);
    if (proto_of_row && protos.empty()) {
        error = "column template has no cell";
        return false;
    }
    // A row template clones every proto per row (a row is a group of `stride` nodes). A column
    // template clones one proto per row, because a column is one cell (RFC_MODEL.md KD6).
    const int stride = proto_of_row ? 1 : row_stride(tmpl);
    auto row_protos = [&](size_t row) {
        std::vector<Node*> out;
        if (proto_of_row) {
            out.push_back(protos[proto_of_row(row)]);
            return out;
        }
        out.assign(protos.begin(), protos.end());
        return out;
    };
    std::vector<std::unique_ptr<Node>> live;
    std::unique_ptr<Node> kept_template;
    for (auto& ch : host->children) {
        if (ch.get() == tmpl) {
            kept_template = std::move(ch);
        } else if (ch != nullptr && ch->attrs.find("data-zui-spacer") == ch->attrs.end()) {
            live.push_back(std::move(ch));
        }
    }
    host->children.clear();
    const bool aligned = stride > 0 && live.size() % static_cast<size_t>(stride) == 0;
    const size_t old_rows = aligned ? live.size() / static_cast<size_t>(stride) : 0;
    std::vector<std::unique_ptr<Node>> next;
    if (kept_template) {
        tmpl = kept_template.get();
        next.push_back(std::move(kept_template));
    }
    bool changed = false;
    auto lookup_for = [&](size_t row) {
        return [&, row](std::string_view key) {
            std::string v = field(row, key);
            if (v.empty() && key == "index") {
                return std::to_string(row);
            }
            return v;
        };
    };
    // Per-row reconcile (RFC_MODEL.md KD3). A row is kept by identity (`row_key`, else position)
    // and by its rendered fingerprint; every other row is rebuilt on its own. The host's child
    // list then follows the new row order, so a kept row is moved instead of remade.
    std::vector<std::string> row_keys(row_count);
    for (size_t row = 0; row < row_count; ++row) {
        row_keys[row] = row_key ? row_key(row) : std::string{};
    }
    // The fingerprint each row *would* have. Computed from the template and the row's values, so
    // a row that is kept is never cloned - only the rows that changed are built, below.
    std::vector<std::string> wanted(row_count);
    for (size_t row = 0; row < row_count; ++row) {
        const Lookup lookup = lookup_for(row);
        const std::vector<Node*> row_nodes = row_protos(row);
        const Node* stop = nested_host_of(row_nodes);
        std::string s;
        for (const Node* proto : row_nodes) {
            s.append(node_fingerprint_with(proto, lookup, stop));
        }
        wanted[row] = std::move(s);
    }
    auto old_row_fingerprint = [&](size_t old) {
        std::vector<Node*> raw;
        raw.reserve(static_cast<size_t>(stride));
        for (int s = 0; s < stride; ++s) {
            raw.push_back(live[old * static_cast<size_t>(stride) + static_cast<size_t>(s)].get());
        }
        return group_fingerprint(raw, nested_host_of(raw));
    };
    // `consumed` is about matching - an old row answers at most one new row. `kept` is about the
    // node's fate. The two differ exactly when a row kept its identity but not its content: that
    // old node still has to retire, or the box tree keeps naming a node the model dropped.
    std::vector<bool> consumed(old_rows, false);
    std::vector<bool> kept(old_rows, false);
    std::vector<int> keep_of(row_count, -1);
    // Identity first: a keyed row takes the first unmatched old row with the same `model_key`.
    for (size_t row = 0; row < row_count; ++row) {
        if (row_keys[row].empty()) {
            continue;
        }
        for (size_t old = 0; old < old_rows; ++old) {
            if (consumed[old]) {
                continue;
            }
            Node* head = live[old * static_cast<size_t>(stride)].get();
            if (head == nullptr || head->model_key != row_keys[row]) {
                continue;
            }
            consumed[old] = true;
            if (old_row_fingerprint(old) == wanted[row]) {
                keep_of[row] = static_cast<int>(old);
                kept[old] = true;
            }
            break;
        }
    }
    // Then position: a row without a key matches the old row at its own index. A keyed row never
    // loses its node to a positional one (`空键的行按位置匹配，不与非空键抢同一行`).
    for (size_t row = 0; row < row_count; ++row) {
        if (keep_of[row] >= 0 || !row_keys[row].empty()) {
            continue;
        }
        if (row >= old_rows || consumed[row]) {
            continue;
        }
        consumed[row] = true;
        if (old_row_fingerprint(row) == wanted[row]) {
            keep_of[row] = static_cast<int>(row);
            kept[row] = true;
        }
    }
    // Build the replacement rows first, while the old nodes are still alive: `retiring` hands the
    // old boxes to the layout's detached list and a box keeps naming its node, so freeing a node
    // first would let the very next allocation land on its address (perf: `box.created` counts)
    // and leave that stale name pointing at a different node.
    std::vector<std::vector<std::unique_ptr<Node>>> built(row_count);
    for (size_t row = 0; row < row_count; ++row) {
        if (keep_of[row] >= 0) {
            continue;
        }
        changed = true;
        const Lookup lookup = lookup_for(row);
        for (Node* proto : row_protos(row)) {
            auto clone = clone_node(proto, host);
            interpolate_node(clone.get(), lookup);
            built[row].push_back(std::move(clone));
        }
        if (!built[row].empty()) {
            built[row][0]->model_key = row_keys[row];
        }
    }
    // Retire every old row that is not kept, before any new row registers an id: `unregister_ids`
    // erases by name, and a rebuilt row's fresh node can carry the same `id` as the old one (a
    // literal id, or `{{}}` inside one), so adopting first would erase the new node's entry. The
    // retired nodes stay owned by `live` until this function returns, the way the all-or-nothing
    // path always did.
    if (!aligned) {
        // The host's children are not a whole number of rows: a hand-edited host, or a template
        // whose stride changed. Nothing can be matched, so every old node retires.
        for (auto& owned : live) {
            if (owned == nullptr) {
                continue;
            }
            unregister_ids(doc, owned.get());
            if (retiring) {
                retiring(owned.get());
            }
            changed = true;
        }
    } else {
        for (size_t old = 0; old < old_rows; ++old) {
            if (kept[old]) {
                continue;
            }
            for (int s = 0; s < stride; ++s) {
                Node* owned =
                    live[old * static_cast<size_t>(stride) + static_cast<size_t>(s)].get();
                if (owned == nullptr) {
                    continue;
                }
                unregister_ids(doc, owned);
                if (retiring) {
                    retiring(owned);
                }
                changed = true;
            }
        }
    }
    // Now write the host's children in the new row order: a matched row is moved, an unmatched one
    // is cloned from the template.
    std::vector<size_t> row_head(row_count, 0);
    for (size_t row = 0; row < row_count; ++row) {
        row_head[row] = next.size();
        if (keep_of[row] < 0) {
            for (auto& n : built[row]) {
                adopt_ids(doc, n.get());
                next.push_back(std::move(n));
            }
            continue;
        }
        const size_t base = static_cast<size_t>(keep_of[row]) * static_cast<size_t>(stride);
        if (live[base] != nullptr) {
            // Assign, do not keep: a row whose `row_key` went back to empty must not answer a
            // later lookup with the key it used to have.
            live[base]->model_key = row_keys[row];
        }
        for (int s = 0; s < stride; ++s) {
            next.push_back(std::move(live[base + static_cast<size_t>(s)]));
        }
    }
    // The host's children are written in the new row order, so a matched row that moved is a
    // change even though nothing was created or retired.
    for (size_t row = 0; row < row_count && !changed; ++row) {
        if (keep_of[row] >= 0 && keep_of[row] != static_cast<int>(row)) {
            changed = true;
        }
    }
    if (changed) {
        note_dom_change(host);
    }
    host->children = std::move(next);
    bool nested_same = true;
    if (!apply_nested(host, row_count, stride, row_head, nested, depth, retiring, error,
                      &nested_same)) {
        return false;
    }
    if (unchanged != nullptr) {
        *unchanged = !changed && nested_same;
    }
    error.clear();
    return true;
}

}  // namespace html
}  // namespace zui
