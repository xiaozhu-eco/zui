// Generated content: the declaration maps of ::before / ::after / ::marker / ::placeholder, the
// implicit `list-item` counter and author counters, and the marker-string synthesis. The cascade
// walk calls commit_pseudos() to attach or drop the generated nodes a style change produced.

#include "css/engine/cascade_internal.h"
#include "css/values.h"

#include <cctype>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace css {
namespace detail {
const char* kPseudoTag[] = {"::before", "::after", "::placeholder", "::marker"};
bool is_list_container_tag(std::string_view tag) {
    return tag == "ul" || tag == "ol" || tag == "menu" || tag == "dir";
}

int parse_html_integer(std::string_view raw, int fallback) {
    if (raw.empty()) {
        return fallback;
    }
    size_t i = 0;
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i])) != 0) {
        ++i;
    }
    if (i >= raw.size()) {
        return fallback;
    }
    const bool neg = raw[i] == '-';
    if (raw[i] == '+' || raw[i] == '-') {
        ++i;
    }
    if (i >= raw.size() || std::isdigit(static_cast<unsigned char>(raw[i])) == 0) {
        return fallback;
    }
    long value = 0;
    while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i])) != 0) {
        value = value * 10 + (raw[i] - '0');
        if (value > 1000000) {
            break;
        }
        ++i;
    }
    if (neg) {
        value = -value;
    }
    return static_cast<int>(value);
}
/// Marker string for `content: normal` + `list-style-type` (CSS Lists §3.2 / Counter Styles).
/// Symbolic styles and decimal include the style's suffix (a trailing space, or `". "`).
std::string marker_text_from_type(std::string_view type, int ordinal) {
    if (type == "none") {
        return {};
    }
    if (type == "circle") {
        return "\u25E6 ";
    }
    if (type == "square") {
        return "\u25AA ";
    }
    if (type == "decimal" || type == "decimal-leading-zero") {
        if (ordinal < 0) {
            ordinal = 0;
        }
        return std::to_string(ordinal) + ". ";
    }
    // `disc` and any not-yet-supported keyword: filled bullet + space suffix.
    if (type.empty() || type == "disc" || type == "disclosure-closed" ||
        type == "disclosure-open") {
        return "\u2022 ";
    }
    // Unknown keyword: CSS says fall back to decimal.
    if (ordinal < 0) {
        ordinal = 0;
    }
    return std::to_string(ordinal) + ". ";
}

std::string_view list_style_type_of(const html::Node& node) {
    const std::string* type = node.style("list-style-type");
    if (type == nullptr || type->empty()) {
        return "disc";
    }
    return *type;
}

/// List items that belong to @p list. A nested `ol` / `ul` / `menu` opens its own counter, so its
/// items are not counted here. A `div` wrapper still is: the item's list owner is the ancestor
/// list, which is how `<ol><div><li>` numbers in a browser.
void collect_list_items(const html::Node& scope, std::vector<const html::Node*>& out) {
    for (const auto& child : scope.children) {
        if (child == nullptr) {
            continue;
        }
        if (is_list_container_tag(child->tag)) {
            continue;
        }
        if (child->tag == "li" || html::is_list_item(child.get())) {
            out.push_back(child.get());
            continue;
        }
        collect_list_items(*child, out);
    }
}

/// HTML `ol reversed`: the marker (and `counter(list-item)` on that item) counts down.
/// `start` is the first item's ordinal; without it the first ordinal is the item count.
/// `li value` sets that item, and the next item continues downward. Any other list keeps
/// @p fallback, which is the forward counter the cascade already computed.
int reversed_list_ordinal(const html::Node& node, int fallback) {
    const html::Node* list = node.parent;
    while (list != nullptr && !is_list_container_tag(list->tag)) {
        list = list->parent;
    }
    if (list == nullptr || list->tag != "ol" || list->attrs.find("reversed") == list->attrs.end()) {
        return fallback;
    }
    std::vector<const html::Node*> items;
    collect_list_items(*list, items);
    int ordinal = static_cast<int>(items.size());
    const auto start = list->attrs.find("start");
    if (start != list->attrs.end()) {
        ordinal = parse_html_integer(start->second, 1);
    }
    for (const html::Node* item : items) {
        const auto value = item->attrs.find("value");
        if (value != item->attrs.end()) {
            ordinal = parse_html_integer(value->second, ordinal);
        }
        if (item == &node) {
            return ordinal;
        }
        --ordinal;
    }
    return fallback;
}

/// `list-style-image` when it is a `url()`. `none` and a gradient (no `url()`) leave the
/// `list-style-type` glyph in place. The sentinel is the same one `content: url()` uses, so the
/// marker becomes a replaced box.
std::optional<std::string> list_style_image_marker(const html::Node& node) {
    const std::string* image = node.style("list-style-image");
    if (image == nullptr || image->empty() || *image == "none") {
        return std::nullopt;
    }
    const size_t open = image->find("url(");
    if (open == std::string::npos) {
        return std::nullopt;
    }
    const size_t close = image->find(')', open + 4);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    std::string inside = image->substr(open + 4, close - (open + 4));
    while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.front())) != 0) {
        inside.erase(inside.begin());
    }
    while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.back())) != 0) {
        inside.pop_back();
    }
    if (inside.size() >= 2 && (inside.front() == '"' || inside.front() == '\'') &&
        inside.back() == inside.front()) {
        inside = inside.substr(1, inside.size() - 2);
    }
    if (inside.empty()) {
        return std::nullopt;
    }
    std::string sentinel;
    sentinel.push_back('\x1f');
    sentinel += "url:";
    sentinel += inside;
    sentinel.push_back('\x1f');
    return sentinel;
}

/// The characters a generated element shows, or nothing when it generates no box at all. `content`
/// other than `none` / `normal` generates the box, and `content: ""` still generates an empty one
/// (CSS Pseudo-Elements section 4.2), so an empty result is not the same as no result.
/// @p list_item_ordinal is the current `list-item` counter for `counter(list-item)` substitution.
std::optional<std::string> pseudo_content(const StyleMap& map, const html::Node& element,
                                          int list_item_ordinal = 0,
                                          const AuthorCounterState* author_counters = nullptr) {
    auto it = map.find("content");
    if (it == map.end() || it->second.empty()) {
        return std::nullopt;
    }
    const std::string& value = it->second.str();
    if (value == "none" || value == "normal") {
        return std::nullopt;
    }
    auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    auto trim = [&](std::string& s) {
        while (!s.empty() && is_space(s.front())) {
            s.erase(s.begin());
        }
        while (!s.empty() && is_space(s.back())) {
            s.pop_back();
        }
    };
    std::string out;
    size_t i = 0;
    while (i < value.size()) {
        const char c = value[i];
        if (is_space(c)) {
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < value.size() && value[i] != c) {
                if (value[i] == '\\' && i + 1 < value.size()) {
                    ++i;
                }
                out.push_back(value[i]);
                ++i;
            }
            if (i < value.size()) {
                ++i;
            }
            continue;
        }
        // `attr(name)` / `attr(name, "fallback")` (CSS Content 3). HTML attributes are untyped
        // strings here. A missing attribute substitutes the fallback; a present empty attribute
        // stays empty. Type-or-unit arguments after the name are ignored for now.
        if (value.compare(i, 5, "attr(") == 0) {
            const size_t close = value.find(')', i + 5);
            if (close == std::string::npos) {
                break;
            }
            std::string inside = value.substr(i + 5, close - (i + 5));
            std::string fallback;
            size_t comma = std::string::npos;
            bool in_str = false;
            char quote = 0;
            for (size_t k = 0; k < inside.size(); ++k) {
                const char ch = inside[k];
                if (in_str) {
                    if (ch == '\\' && k + 1 < inside.size()) {
                        ++k;
                        continue;
                    }
                    if (ch == quote) {
                        in_str = false;
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    in_str = true;
                    quote = ch;
                    continue;
                }
                if (ch == ',') {
                    comma = k;
                    break;
                }
            }
            if (comma != std::string::npos) {
                fallback = inside.substr(comma + 1);
                inside.erase(comma);
                trim(fallback);
            }
            trim(inside);
            // Drop an optional type/unit token after the attribute name.
            {
                size_t name_end = 0;
                while (name_end < inside.size() &&
                       (std::isalnum(static_cast<unsigned char>(inside[name_end])) != 0 ||
                        inside[name_end] == '-' || inside[name_end] == '_')) {
                    ++name_end;
                }
                inside.resize(name_end);
            }
            auto unquote = [&](std::string s) {
                trim(s);
                if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') &&
                    s.back() == s.front()) {
                    const char q = s.front();
                    std::string raw;
                    for (size_t k = 1; k + 1 < s.size(); ++k) {
                        if (s[k] == '\\' && k + 1 + 1 <= s.size()) {
                            ++k;
                            raw.push_back(s[k]);
                            continue;
                        }
                        if (s[k] == q) {
                            break;
                        }
                        raw.push_back(s[k]);
                    }
                    return raw;
                }
                return s;
            };
            const auto attr = element.attrs.find(inside);
            if (attr != element.attrs.end()) {
                out += attr->second;
            } else if (!fallback.empty()) {
                out += unquote(std::move(fallback));
            }
            i = close + 1;
            continue;
        }
        // `counters(name, "sep")` before `counter(` (prefix overlap).
        if (value.compare(i, 9, "counters(") == 0) {
            const size_t close = value.find(')', i + 9);
            if (close == std::string::npos) {
                break;
            }
            std::string inside = value.substr(i + 9, close - (i + 9));
            trim(inside);
            std::string name;
            std::string sep = ".";
            size_t comma = std::string::npos;
            bool in_str = false;
            char quote = 0;
            for (size_t k = 0; k < inside.size(); ++k) {
                const char ch = inside[k];
                if (in_str) {
                    if (ch == '\\' && k + 1 < inside.size()) {
                        ++k;
                        continue;
                    }
                    if (ch == quote) {
                        in_str = false;
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    in_str = true;
                    quote = ch;
                    continue;
                }
                if (ch == ',') {
                    comma = k;
                    break;
                }
            }
            if (comma == std::string::npos) {
                name = inside;
            } else {
                name = inside.substr(0, comma);
                std::string rest = inside.substr(comma + 1);
                trim(name);
                trim(rest);
                if (rest.size() >= 2 && (rest.front() == '"' || rest.front() == '\'') &&
                    rest.back() == rest.front()) {
                    sep = rest.substr(1, rest.size() - 2);
                }
            }
            trim(name);
            if (name == "list-item") {
                out += std::to_string(std::max(0, list_item_ordinal));
            } else if (author_counters != nullptr) {
                out += author_counters->get_all(name, sep);
            } else {
                out += "0";
            }
            i = close + 1;
            continue;
        }
        // `counter(name)` / `counter(name, decimal)`: digits only. Authors who want a suffix
        // write it as a following string (`". "`). `list-item` uses the list-item ordinal.
        if (value.compare(i, 8, "counter(") == 0) {
            const size_t close = value.find(')', i + 8);
            if (close == std::string::npos) {
                break;
            }
            std::string inside = value.substr(i + 8, close - (i + 8));
            trim(inside);
            std::string name = inside;
            const size_t comma = inside.find(',');
            if (comma != std::string::npos) {
                name = inside.substr(0, comma);
                trim(name);
            }
            if (name == "list-item") {
                out += std::to_string(std::max(0, list_item_ordinal));
            } else if (author_counters != nullptr) {
                out += std::to_string(author_counters->get(name));
            } else {
                out += "0";
            }
            i = close + 1;
            continue;
        }
        // `url("…")` / `url(…)`: leave a marker so commit_pseudos can attach attrs["src"].
        // Other functions are skipped so their arguments are not mistaken for a string.
        if (value.compare(i, 4, "url(") == 0) {
            const size_t close = value.find(')', i + 4);
            if (close == std::string::npos) {
                break;
            }
            std::string inside = value.substr(i + 4, close - (i + 4));
            trim(inside);
            if (inside.size() >= 2 && (inside.front() == '"' || inside.front() == '\'') &&
                inside.back() == inside.front()) {
                inside = inside.substr(1, inside.size() - 2);
            }
            // Encode as a private sentinel the box tree recognises; not shown as glyph text.
            out += "\x1f";
            out += "url:";
            out += inside;
            out += "\x1f";
            i = close + 1;
            continue;
        }
        const size_t close = value.find(')', i);
        const size_t space = value.find_first_of(" \t", i);
        if (close != std::string::npos && (space == std::string::npos || close < space)) {
            i = close + 1;
        } else if (space != std::string::npos) {
            i = space;
        } else {
            break;
        }
    }
    return out;
}

/// Resolved marker characters for a list item. Keeps specified `content` (with counter
/// substitution) separate from `content: normal` + `list-style-type` synthesis.
std::optional<std::string>
resolve_marker_text(const html::Node& node, const StyleMap* marker_map, int list_item_ordinal,
                    const AuthorCounterState* author_counters = nullptr) {
    if (marker_map != nullptr) {
        auto it = marker_map->find("content");
        if (it != marker_map->end() && !it->second.empty() && it->second != "normal") {
            if (it->second == "none") {
                return std::nullopt;
            }
            return pseudo_content(*marker_map, node, list_item_ordinal, author_counters);
        }
    }
    if (std::optional<std::string> image = list_style_image_marker(node)) {
        return image;
    }
    const std::string_view type = list_style_type_of(node);
    std::string text = marker_text_from_type(type, list_item_ordinal);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

/// A generated element's box is inline by default (CSS Pseudo-Elements section 4.2). The box tree
/// would otherwise make it a block, because its generated tag is not one of the inline tags, so
/// the default is written here. An out-of-flow or floating box is blockified (CSS Display
/// section 2.7), which is what lets an absolutely positioned `::before` be a block container.
void normalize_generated(StyleMap& map) {
    auto display = map.find("display");
    if (display == map.end()) {
        display = map.emplace("display", "inline").first;
    }
    if (display->second != "inline" && display->second != "inline-block" &&
        display->second != "inline-flex" && display->second != "inline-grid" &&
        display->second != "inline-table") {
        return;
    }
    const auto position = map.find("position");
    const bool out_of_flow =
        position != map.end() && (position->second == "absolute" || position->second == "fixed");
    const auto floated = map.find("float");
    const bool floating = floated != map.end() && floated->second != "none";
    if (!out_of_flow && !floating) {
        return;
    }
    if (display->second == "inline" || display->second == "inline-block") {
        display->second = "block";
    } else if (display->second == "inline-flex") {
        display->second = "flex";
    } else if (display->second == "inline-grid") {
        display->second = "grid";
    } else {
        display->second = "table";
    }
}

/// Whether the two computed styles of a generated element differ in a way that moves its box: the
/// box follows `display` / `position` (outer, inner and out-of-flow), and `content` is the text the
/// box tree put in it. Everything else is a restyle the box re-interns in place.
bool pseudo_structure_changed(const StyleMap& previous, const StyleMap& next) {
    for (const char* key : {"display", "position", "content", "float"}) {
        if (map_val(&previous, key) != map_val(&next, key)) {
            return true;
        }
    }
    return false;
}

/// Stores the generated elements a node just got. A generated element that no longer has a
/// declaration block, or whose `content` stops generating a box, is dropped - the box tree rebuilds
/// from these nodes, so removing one here removes the box.
/// @p list_item_ordinal is the node's `list-item` counter after reset/increment/set (0 if not a
/// list item). Marker text is resolved here so ShareKey-shared style maps never store per-node
/// numbers.
void commit_pseudos(Engine::CascadeResult& result, html::Node* node,
                    const std::array<std::shared_ptr<const StyleMap>, kPseudoCount>& maps,
                    int list_item_ordinal = 0,
                    const AuthorCounterState* author_counters = nullptr) {
    CascadeAcc pseudo_clock(cascade_split_armed() ? &g_cascade_split.pseudo_ms : nullptr);
    bool structural = false;
    bool restyle = false;
    if (html::is_list_item(node)) {
        list_item_ordinal = reversed_list_ordinal(*node, list_item_ordinal);
    }
    for (int i = 0; i < kPseudoCount; ++i) {
        const auto& previous = node->pseudo[i];
        const auto& next = maps[i];
        const auto kind = static_cast<html::PseudoElement>(i);
        // `::marker` and `::placeholder` generate their box from the element itself (a list item,
        // a control with a placeholder attribute), so they are evaluated even with no author rule
        // matching them. Everything else needs a matched rule to exist at all.
        if (kind != html::PseudoElement::Marker && kind != html::PseudoElement::Placeholder &&
            previous == nullptr && next == nullptr) {
            continue;
        }
        std::optional<std::string> text;
        bool generates = false;
        if (kind == html::PseudoElement::Placeholder) {
            // HTML rendering: the placeholder shows whenever the attribute is set, whether or not
            // an author `::placeholder` rule matched. `next` carries a style for it, nothing more.
            generates = node->attrs.count("placeholder") != 0 &&
                        !node->attrs.at("placeholder").empty();
            if (generates) {
                text = node->attrs.at("placeholder");
            }
        } else if (kind == html::PseudoElement::Marker) {
            // CSS Lists §3.1: only list items. Default markers come from list-style-type even when
            // no author `::marker` rule matched (`next` may be null).
            if (html::is_list_item(node)) {
                text = resolve_marker_text(*node, next.get(), list_item_ordinal, author_counters);
                generates = text.has_value();
            }
        } else {
            generates =
                next != nullptr &&
                pseudo_content(*next, *node, list_item_ordinal, author_counters).has_value();
            if (generates) {
                text = pseudo_content(*next, *node, list_item_ordinal, author_counters);
            }
        }
        auto& slot = node->pseudo_node[i];
        if (!generates) {
            if (slot != nullptr) {
                structural = true;
            }
            slot.reset();
        } else {
            const std::string new_text = text.value_or(std::string());
            if (slot == nullptr) {
                slot = std::make_unique<html::Node>();
                slot->tag = kPseudoTag[i];
                slot->parent = node;
                structural = true;
            } else if (next != nullptr && previous != nullptr &&
                       pseudo_structure_changed(*previous, *next)) {
                structural = true;
            } else if (slot->text != new_text ||
                       (slot->attrs.count("src") != 0) !=
                           (new_text.find("\x1furl:") != std::string::npos)) {
                // Counter / list-style / content:url() changes rewrite generated text or image.
                structural = true;
            } else if (next != nullptr && previous != nullptr && *previous != *next) {
                restyle = true;
            }
            if (kind == html::PseudoElement::Marker && next == nullptr) {
                // Default marker (content: normal + list-style-type): no author ::marker map.
                // Build a private inheriting map so paint sees the list item's color/font.
                auto map = std::make_shared<StyleMap>();
                if (node->computed != nullptr) {
                    for (const auto& kv : *node->computed) {
                        if (name_inherits(kv.first)) {
                            map->emplace(kv.first, kv.second);
                        }
                    }
                }
                normalize_generated(*map);
                slot->computed = std::move(map);
            } else if (kind == html::PseudoElement::Placeholder && next == nullptr) {
                // No author `::placeholder` rule: inherit from the control and take Chromium's
                // default placeholder colour (`html.css`: rgb(117, 117, 117)) so the run does not
                // paint in the control's own colour.
                auto map = std::make_shared<StyleMap>();
                if (node->computed != nullptr) {
                    for (const auto& kv : *node->computed) {
                        if (name_inherits(kv.first)) {
                            map->emplace(kv.first, kv.second);
                        }
                    }
                }
                // `emplace` leaves the inherited colour in place. The control's own colour is
                // already in the map, and the placeholder must replace it.
                (*map)["color"] = "#757575";
                normalize_generated(*map);
                slot->computed = std::move(map);
            } else {
                slot->computed = next;
            }
            // Peel `url(...)` sentinels out of the resolved content into attrs["src"].
            std::string visible;
            std::string image_src;
            {
                const std::string& raw = new_text;
                size_t p = 0;
                while (p < raw.size()) {
                    if (raw[p] == '\x1f' && raw.compare(p, 5, "\x1furl:") == 0) {
                        const size_t end = raw.find('\x1f', p + 5);
                        if (end == std::string::npos) {
                            break;
                        }
                        if (image_src.empty()) {
                            image_src = raw.substr(p + 5, end - (p + 5));
                        }
                        p = end + 1;
                        continue;
                    }
                    visible.push_back(raw[p]);
                    ++p;
                }
            }
            slot->text = std::move(visible);
            if (!image_src.empty()) {
                slot->attrs["src"] = std::move(image_src);
            } else {
                slot->attrs.erase("src");
            }
        }
        node->pseudo[i] = next;
    }
    if (structural) {
        result.reattach.push_back(node);
    } else if (restyle) {
        result.layout.push_back(node);
    }
}
}  // namespace detail
}  // namespace css
}  // namespace zui
