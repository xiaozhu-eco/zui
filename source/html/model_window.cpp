// Windowed list stamp: a sliding slot of rows plus spacer nodes (RFC_MODEL.md).

#include "html/document.h"
#include "html/document_internal.h"
#include "html/model_internal.h"

#include <zui/model.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace html {

namespace {

constexpr std::size_t kListWindowSlots = 32;
constexpr std::size_t kListWindowOverscan = 4;

bool is_spacer(const Node* node) {
    return node != nullptr && node->attrs.find("data-zui-spacer") != node->attrs.end();
}

void collect_placeholder_keys(const Node* node, std::vector<std::string>& keys) {
    if (node == nullptr) {
        return;
    }
    auto scan = [&](std::string_view in) {
        size_t i = 0;
        while (i < in.size()) {
            if (i + 1 < in.size() && in[i] == '{' && in[i + 1] == '{') {
                const size_t end = in.find("}}", i + 2);
                if (end == std::string_view::npos) {
                    break;
                }
                std::string key(in.substr(i + 2, end - (i + 2)));
                size_t a = 0;
                while (a < key.size() && std::isspace(static_cast<unsigned char>(key[a])) != 0) {
                    ++a;
                }
                size_t b = key.size();
                while (b > a && std::isspace(static_cast<unsigned char>(key[b - 1])) != 0) {
                    --b;
                }
                key = key.substr(a, b - a);
                if (!key.empty() &&
                    std::find(keys.begin(), keys.end(), key) == keys.end()) {
                    keys.push_back(std::move(key));
                }
                i = end + 2;
                continue;
            }
            ++i;
        }
    };
    scan(node->text);
    scan(node->id);
    scan(node->class_name);
    scan(node->value);
    for (const auto& kv : node->attrs) {
        scan(kv.second);
    }
    for (const auto& child : node->children) {
        if (child != nullptr && child->tag != "template") {
            collect_placeholder_keys(child.get(), keys);
        }
    }
}

std::string spacer_style(float px) {
    const int height = static_cast<int>(std::lround(std::max(0.f, px)));
    // `flex: 0 0 Npx` keeps the spacer from shrinking inside a column flex list
    // (share's `.file-list`). Block layout ignores `flex` and uses `height`.
    const std::string px_text = std::to_string(height) + "px";
    return "height: " + px_text + "; flex: 0 0 " + px_text +
           "; margin: 0; padding: 0; border: none; pointer-events: none";
}

void install_spacers(Node* host, float before_px, float after_px) {
    auto make = [&](const char* which, float px) {
        auto node = std::make_unique<Node>();
        node->tag = "div";
        node->parent = host;
        node->attrs["data-zui-spacer"] = which;
        node->attrs["style"] = spacer_style(px);
        return node;
    };
    std::vector<std::unique_ptr<Node>> next;
    next.reserve(host->children.size() + 2);
    bool placed = false;
    for (auto& child : host->children) {
        if (!placed && child != nullptr && child->tag != "template") {
            next.push_back(make("before", before_px));
            placed = true;
        }
        next.push_back(std::move(child));
    }
    if (!placed) {
        next.push_back(make("before", before_px));
    }
    next.push_back(make("after", after_px));
    for (auto& child : next) {
        if (child != nullptr) {
            child->parent = host;
        }
    }
    host->children = std::move(next);
}

float spacer_before_px(const ListWindow& window) {
    return static_cast<float>(window.first) * window.row_px;
}

float spacer_after_px(const ListWindow& window) {
    const std::size_t shown =
        std::min(window.slots, window.row_count - std::min(window.first, window.row_count));
    const std::size_t tail = window.row_count - window.first - shown;
    return static_cast<float>(tail) * window.row_px;
}

}  // namespace

bool ModelBinder::rebind_window(ListWindow& window, Node* host,
                                const std::function<void(Node*)>& retiring, bool* unchanged) {
    if (retiring) {
        for (const auto& child : host->children) {
            if (is_spacer(child.get())) {
                retiring(child.get());
            }
        }
    }
    const std::size_t shown = std::min(
        window.slots, window.row_count - std::min(window.first, window.row_count));
    const std::size_t key_count = window.keys.size();
    const FieldFn field = [&window, key_count](size_t row, std::string_view key) {
        const size_t abs = window.first + row;
        for (size_t i = 0; i < key_count; ++i) {
            if (window.keys[i] == key) {
                const size_t slot = abs * key_count + i;
                if (slot < window.values.size()) {
                    const std::string& stored = window.values[slot];
                    // `{{index}}` is the absolute row when the model left it empty.
                    if (!stored.empty() || key != "index") {
                        return stored;
                    }
                }
            }
        }
        if (key == "index") {
            return std::to_string(abs);
        }
        return std::string{};
    };
    const RowKeyFn row_key = [&window](size_t row) {
        const size_t abs = window.first + row;
        if (abs < window.row_key.size()) {
            return window.row_key[abs];
        }
        return std::to_string(abs);
    };
    std::string error;
    bool same = false;
    if (!apply_on_node(host, shown, field, error, row_key, retiring, &same)) {
        return false;
    }
    install_spacers(host, spacer_before_px(window), spacer_after_px(window));
    if (unchanged != nullptr) {
        *unchanged = false;
    }
    return true;
}

bool ModelBinder::apply_windowed_list(Node* host, const ListModel& model, std::string& error,
                                      const std::function<void(Node*)>& retiring,
                                      bool* unchanged) {
    Node* tmpl = find_template(host);
    if (tmpl == nullptr) {
        error = "model host has no <template>";
        return false;
    }
    ListWindow window;
    window.host_id = host->id;
    window.row_count = model.row_count();
    window.slots = kListWindowSlots;
    const std::vector<Node*> protos = model_protos(tmpl);
    for (const Node* proto : protos) {
        collect_placeholder_keys(proto, window.keys);
    }
    const size_t key_count = window.keys.size();
    window.row_key.resize(window.row_count);
    window.values.resize(window.row_count * key_count);
    for (size_t row = 0; row < window.row_count; ++row) {
        std::string key = model.row_key(row);
        window.row_key[row] = key.empty() ? std::to_string(row) : std::move(key);
        for (size_t i = 0; i < key_count; ++i) {
            window.values[row * key_count + i] = model.value(row, window.keys[i]);
        }
    }
    erase_list_window(doc_, host->id);
    doc_.list_windows.push_back(std::move(window));
    if (!rebind_window(doc_.list_windows.back(), host, retiring, unchanged)) {
        error = "model host has no <template>";
        return false;
    }
    error.clear();
    return true;
}

bool sync_list_window(Document& doc, std::string_view host_id, float scroll_y, float row_px,
                      const std::function<void(Node*)>& retiring) {
    ListWindow* window = nullptr;
    for (ListWindow& candidate : doc.list_windows) {
        if (candidate.host_id == host_id) {
            window = &candidate;
            break;
        }
    }
    if (window == nullptr) {
        return false;
    }
    Node* host = find(doc, host_id);
    if (host == nullptr) {
        return false;
    }
    const bool have_row = row_px > 0.5f;
    const bool row_changed = have_row && std::fabs(window->row_px - row_px) > 0.5f;
    if (row_changed) {
        window->row_px = row_px;
    }
    std::size_t next_first = window->first;
    if (have_row) {
        const int raw = static_cast<int>(std::floor(scroll_y / window->row_px)) -
                        static_cast<int>(kListWindowOverscan);
        const std::size_t max_first =
            window->row_count > window->slots ? window->row_count - window->slots : 0;
        if (raw < 0) {
            next_first = 0;
        } else if (static_cast<std::size_t>(raw) > max_first) {
            next_first = max_first;
        } else {
            next_first = static_cast<std::size_t>(raw);
        }
    }
    if (next_first != window->first) {
        window->first = next_first;
        bool unchanged = false;
        if (!ModelBinder(doc).rebind_window(*window, host, retiring, &unchanged)) {
            return false;
        }
        return true;
    }
    if (!row_changed) {
        return false;
    }
    bool wrote = false;
    for (auto& child : host->children) {
        if (!is_spacer(child.get())) {
            continue;
        }
        const auto which = child->attrs.find("data-zui-spacer");
        if (which == child->attrs.end()) {
            continue;
        }
        const float px =
            which->second == "before" ? spacer_before_px(*window) : spacer_after_px(*window);
        const std::string style = spacer_style(px);
        if (child->attrs["style"] != style) {
            child->attrs["style"] = style;
            wrote = true;
        }
    }
    return wrote;
}

}  // namespace html
}  // namespace zui
