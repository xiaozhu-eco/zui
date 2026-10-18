// Used-value mapping: CSS-wide keywords, logical box edges, font-family, revert, overflow.

#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include <cctype>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace css {
namespace detail {

// CSS-wide keywords belong to the computed value, not to layout: `font-size: inherit`
// must arrive there as the parent's used size. `inherit` (and `unset` on an inherited
// property) copies the parent's computed value; `initial` (and `unset` on anything
// else) drops the declaration so the initial value comes from the layout defaults.
// Runs after the inherit pass, otherwise the dropped names would be copied straight
// back out of the parent and `initial` would behave like `inherit`.
void resolve_wide_keywords(StyleMap& specified, const StyleMap* parent) {
    std::vector<::zui::Atom> drop;
    for (auto& kv : specified) {
        const bool inherit = kv.second == "inherit";
        const bool initial = kv.second == "initial";
        const bool unset = kv.second == "unset";
        if (!inherit && !initial && !unset) {
            continue;
        }
        if ((inherit || (unset && name_inherits(kv.first))) && parent != nullptr) {
            const auto it = parent->find(kv.first);
            if (it != parent->end()) {
                kv.second = it->second;
                continue;
            }
        }
        drop.push_back(kv.first);
    }
    for (const ::zui::Atom& name : drop) {
        specified.erase(name);
    }
}

/// CSS Logical Properties 1: map margin/padding logical longhands (and the two-value
/// `*-block` / `*-inline` shorthands) onto physical longhands using the element's own
/// `writing-mode` + `direction`. Physical declarations already in the map win; this only
/// fills sides the cascade did not set physically. Lexbor does not know these names, so they
/// arrive as custom-typed declarations and stay in the map as specified.
enum class LogicalBoxSide : std::uint8_t { InlineStart, InlineEnd, BlockStart, BlockEnd };
enum class PhysicalBoxSide : std::uint8_t { Top, Right, Bottom, Left };

bool writing_mode_horizontal(std::string_view wm) { return wm.empty() || wm == "horizontal-tb"; }

bool writing_mode_flipped_blocks(std::string_view wm) {
    return wm == "vertical-rl" || wm == "sideways-rl";
}

PhysicalBoxSide physical_box_side(std::string_view wm, std::string_view direction,
                                  LogicalBoxSide side) {
    const bool rtl = direction == "rtl";
    if (writing_mode_horizontal(wm)) {
        switch (side) {
            case LogicalBoxSide::InlineStart:
                return rtl ? PhysicalBoxSide::Right : PhysicalBoxSide::Left;
            case LogicalBoxSide::InlineEnd:
                return rtl ? PhysicalBoxSide::Left : PhysicalBoxSide::Right;
            case LogicalBoxSide::BlockStart:
                return PhysicalBoxSide::Top;
            case LogicalBoxSide::BlockEnd:
                return PhysicalBoxSide::Bottom;
        }
    }
    switch (side) {
        case LogicalBoxSide::InlineStart:
            return rtl ? PhysicalBoxSide::Bottom : PhysicalBoxSide::Top;
        case LogicalBoxSide::InlineEnd:
            return rtl ? PhysicalBoxSide::Top : PhysicalBoxSide::Bottom;
        case LogicalBoxSide::BlockStart:
            return writing_mode_flipped_blocks(wm) ? PhysicalBoxSide::Right : PhysicalBoxSide::Left;
        case LogicalBoxSide::BlockEnd:
            return writing_mode_flipped_blocks(wm) ? PhysicalBoxSide::Left : PhysicalBoxSide::Right;
    }
    return PhysicalBoxSide::Top;
}

const char* physical_padding_name(PhysicalBoxSide side) {
    switch (side) {
        case PhysicalBoxSide::Top:
            return "padding-top";
        case PhysicalBoxSide::Right:
            return "padding-right";
        case PhysicalBoxSide::Bottom:
            return "padding-bottom";
        case PhysicalBoxSide::Left:
            return "padding-left";
    }
    return "padding-top";
}

const char* physical_margin_name(PhysicalBoxSide side) {
    switch (side) {
        case PhysicalBoxSide::Top:
            return "margin-top";
        case PhysicalBoxSide::Right:
            return "margin-right";
        case PhysicalBoxSide::Bottom:
            return "margin-bottom";
        case PhysicalBoxSide::Left:
            return "margin-left";
    }
    return "margin-top";
}

/// First whitespace-separated token, and the optional second (else a copy of the first).
void split_one_or_two(std::string_view text, std::string& first, std::string& second) {
    first.clear();
    second.clear();
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
        ++i;
    }
    const std::size_t a0 = i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
        ++i;
    }
    first.assign(text.data() + a0, i - a0);
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
        ++i;
    }
    if (i >= text.size()) {
        second = first;
        return;
    }
    const std::size_t b0 = i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
        ++i;
    }
    second.assign(text.data() + b0, i - b0);
    if (second.empty()) {
        second = first;
    }
}

const std::string* map_get(const StyleMap& specified, const char* key) {
    const auto it = specified.find(key);
    return it == specified.end() ? nullptr : it->second.ptr();
}

void put_physical_if_absent(StyleMap& specified, const char* physical, std::string_view value) {
    if (value.empty()) {
        return;
    }
    if (specified.find(physical) != specified.end()) {
        return;
    }
    specified.emplace(physical, value);
}

void resolve_axis_pair(StyleMap& specified, std::string_view wm, std::string_view direction,
                       bool padding, const char* shorthand, const char* start_name,
                       const char* end_name, LogicalBoxSide start_side, LogicalBoxSide end_side) {
    std::string start_val;
    std::string end_val;
    bool has_start = false;
    bool has_end = false;
    if (const std::string* sh = map_get(specified, shorthand)) {
        split_one_or_two(*sh, start_val, end_val);
        has_start = !start_val.empty();
        has_end = !end_val.empty();
    }
    if (const std::string* s = map_get(specified, start_name)) {
        start_val = *s;
        has_start = true;
    }
    if (const std::string* e = map_get(specified, end_name)) {
        end_val = *e;
        has_end = true;
    }
    auto physical_name = padding ? physical_padding_name : physical_margin_name;
    if (has_start) {
        put_physical_if_absent(
            specified, physical_name(physical_box_side(wm, direction, start_side)), start_val);
    }
    if (has_end) {
        put_physical_if_absent(specified, physical_name(physical_box_side(wm, direction, end_side)),
                               end_val);
    }
}

void resolve_logical_box_edges(StyleMap& specified) {
    // Padding, margin, and the min/max logical sizes do not inherit, so a map that never named
    // one has nothing to alias. The probes below build strings even on a miss.
    bool maybe_logical = false;
    for (const auto& kv : specified) {
        const std::string_view name = kv.first;
        if (name.find("inline") != std::string_view::npos ||
            name.find("-block") != std::string_view::npos) {
            maybe_logical = true;
            break;
        }
    }
    if (!maybe_logical) {
        return;
    }
    std::string_view wm = "horizontal-tb";
    std::string_view direction = "ltr";
    if (const std::string* v = map_get(specified, "writing-mode")) {
        wm = *v;
    }
    if (const std::string* v = map_get(specified, "direction")) {
        direction = *v;
    }
    resolve_axis_pair(specified, wm, direction, true, "padding-block", "padding-block-start",
                      "padding-block-end", LogicalBoxSide::BlockStart, LogicalBoxSide::BlockEnd);
    resolve_axis_pair(specified, wm, direction, true, "padding-inline", "padding-inline-start",
                      "padding-inline-end", LogicalBoxSide::InlineStart, LogicalBoxSide::InlineEnd);
    resolve_axis_pair(specified, wm, direction, false, "margin-block", "margin-block-start",
                      "margin-block-end", LogicalBoxSide::BlockStart, LogicalBoxSide::BlockEnd);
    resolve_axis_pair(specified, wm, direction, false, "margin-inline", "margin-inline-start",
                      "margin-inline-end", LogicalBoxSide::InlineStart, LogicalBoxSide::InlineEnd);
    // CSS Logical Properties 1: the logical size limits are physical aliases too -
    // `min-inline-size` is `min-width` in a horizontal writing mode and `min-height` in a vertical
    // one, and the `max-` pair mirrors that. Only a physical axis the author left alone is filled,
    // so an author's own `min-width` still wins. Chrome reports the physical longhand, which is why
    // the UA sheet's `fieldset { min-inline-size: min-content }` reads as `min-width: min-content`
    // (measured with build/computed_probe.py).
    const bool horizontal = writing_mode_horizontal(wm);
    const std::pair<const char*, const char*> kLogicalSizes[] = {
        {"min-inline-size", horizontal ? "min-width" : "min-height"},
        {"min-block-size", horizontal ? "min-height" : "min-width"},
        {"max-inline-size", horizontal ? "max-width" : "max-height"},
        {"max-block-size", horizontal ? "max-height" : "max-width"},
    };
    for (const auto& alias : kLogicalSizes) {
        const auto it = specified.find(alias.first);
        if (it != specified.end()) {
            put_physical_if_absent(specified, alias.second, it->second);
        }
    }
}

/// A `font-family` family name that is a single CSS identifier is serialized bare; anything else
/// becomes a string. The zmd page writes `"STSong", "SimSun"` and Chrome answers `stsong, simsun`
/// (the golden records it), so the computed value re-serializes the list instead of echoing the
/// source. The CSS-wide keywords and `default` stay quoted - they are reserved family names
/// (CSS Fonts 4 section 2.1).
bool css_ident_start(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
}

bool css_ident_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '-' || c >= 0x80;
}

bool single_ident(std::string_view name) {
    if (name.empty() || !css_ident_start(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    for (char c : name) {
        if (!css_ident_char(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool reserved_family(std::string_view name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
           lower == "revert-layer" || lower == "default";
}

std::string font_family_token(const std::string& name) {
    if (single_ident(name) && !reserved_family(name)) {
        return name;
    }
    std::string quoted = "\"";
    for (char c : name) {
        if (c == '"' || c == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

void normalize_font_family(StyleMap& specified) {
    const auto it = specified.find("font-family");
    if (it == specified.end() || it->second.empty()) {
        return;
    }
    const std::string& value = it->second.str();
    std::string out;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) != 0) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        std::string name;
        if (value[i] == '"' || value[i] == '\'') {
            const char quote = value[i++];
            while (i < value.size() && value[i] != quote) {
                if (value[i] == '\\' && i + 1 < value.size()) {
                    ++i;
                }
                name.push_back(value[i++]);
            }
            if (i < value.size()) {
                ++i;
            }
        } else {
            while (i < value.size() && value[i] != ',') {
                name.push_back(value[i++]);
            }
            while (!name.empty() &&
                   std::isspace(static_cast<unsigned char>(name.back())) != 0) {
                name.pop_back();
            }
        }
        if (!name.empty()) {
            if (!out.empty()) {
                out += ", ";
            }
            out += font_family_token(name);
        }
        while (i < value.size() && value[i] != ',') {
            ++i;
        }
        if (i < value.size()) {
            ++i;
        }
    }
    if (!out.empty()) {
        it->second = out;
    }
}

/// CSS Overflow 3 section 3: `overflow-x` and `overflow-y` compute as a pair. When exactly one
/// axis is `visible`, that axis computes to `auto`; the other keeps what it specified. Without
/// this, `overflow-x: auto` on the file-explorer page's `.crumbs` left `overflow-y` at `visible`,
/// so the trace reported a value Chrome does not (measured: both axes `auto`) and the layout had
/// no reason to reserve the horizontal scrollbar the other axis asks for.
///
/// A missing entry is the initial `visible`, so `overflow-y: auto` alone also moves `overflow-x`.
bool revert_keyword(std::string_view value, bool& layer_only) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    const size_t n = end - begin;
    auto equals = [&](const char* literal, size_t len) {
        if (n != len) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            if (std::tolower(static_cast<unsigned char>(value[begin + i])) !=
                static_cast<unsigned char>(literal[i])) {
                return false;
            }
        }
        return true;
    };
    if (equals("revert-layer", 12)) {
        layer_only = true;
        return true;
    }
    if (equals("revert", 6)) {
        layer_only = false;
        return true;
    }
    return false;
}

DeclKey winner_key(const Winner& w) {
    return DeclKey{cascade_layer(w.origin, w.important), w.element_attached, w.layer, w.spec,
                   w.order};
}

/// `revert` drops the whole origin, important and normal together (author revert leaves the
/// UA sheet; a UA revert is unset). `revert-layer` drops that layer at both importance
/// levels and every declaration that sorts between the layer's important level and its
/// normal level — later layers, which are weaker when important and stronger when normal.
/// An important style attribute is the exception in Cascade 5: it drops only the style
/// attribute, not the author-important rules underneath it.
bool same_revert_slot(const Winner& cand, const Winner& dropped, bool layer_only) {
    if (!layer_only) {
        if (dropped.origin == kOriginUa) {
            return true;
        }
        return cand.origin == dropped.origin;
    }
    if (cand.origin != dropped.origin) {
        return false;
    }
    if (dropped.element_attached) {
        return cand.element_attached;
    }
    if (cand.element_attached) {
        return false;
    }
    if (cand.layer == dropped.layer) {
        return true;
    }
    return cand.layer > dropped.layer;
}

/// CSS Cascade 4/5: when the cascaded value is `revert` or `revert-layer`, throw away that
/// origin (or that layer, plus what sorts between its two importance levels) and cascade again. Repeats until the value is an
/// ordinary one. Nothing left behaves as `unset` — the property is removed so the inherit
/// pass can fill it. Runs before inheritance. `var()` that substitutes to a keyword uses the
/// declaration that contained the `var()`, and a custom property is resolved before a longhand
/// reads it so a stale `revert` token is not treated as the custom's value.
void resolve_revert(StyleMap& specified, DeclSink& sink, const StyleMap* parent,
                    const html::Node* custom_origin, const MediaState& media,
                    const FinalizeTrace* trace,
                    const std::unordered_set<::zui::Atom, ::zui::AtomHash>* local_names) {
    std::unordered_set<::zui::Atom, ::zui::AtomHash> visiting;
    std::function<void(const ::zui::Atom&)> resolve_one;
    CustomLookup lookup = [&](std::string_view name) -> std::optional<std::string> {
        const ::zui::Atom key = ::zui::Atom::intern(name);
        if (visiting.find(key) != visiting.end()) {
            return std::nullopt;
        }
        auto it = specified.find(key);
        bool layer = false;
        const bool keyword = it != specified.end() && revert_keyword(it->second, layer);
        if (it != specified.end() && !keyword) {
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = it->first;
                note.local = local_names != nullptr && local_names->find(it->first) != local_names->end();
                note.pseudo = trace->pseudo;
                note.present = true;
                note.value = it->second;
                trace->reads->push_back(std::move(note));
            }
            return it->second.str();
        }
        if (is_custom_property(key) && sink.decls.find(key) != sink.decls.end() &&
            (keyword || it == specified.end())) {
            resolve_one(key);
            it = specified.find(key);
            if (it != specified.end()) {
                if (trace != nullptr && trace->reads != nullptr) {
                    ReadValue note;
                    note.name = it->first;
                    note.local =
                        local_names != nullptr && local_names->find(it->first) != local_names->end();
                    note.pseudo = trace->pseudo;
                    note.present = true;
                    note.value = it->second;
                    trace->reads->push_back(std::move(note));
                }
                return it->second.str();
            }
        }
        if (is_custom_property(key)) {
            const std::string* inherited = ancestor_custom(parent, custom_origin, key);
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = key;
                note.pseudo = trace->pseudo;
                note.present = inherited != nullptr;
                if (inherited != nullptr) {
                    note.value = *inherited;
                }
                trace->reads->push_back(std::move(note));
            }
            if (inherited != nullptr) {
                return *inherited;
            }
        }
        if (trace != nullptr && trace->reads != nullptr) {
            ReadValue note;
            note.name = key;
            note.pseudo = trace->pseudo;
            trace->reads->push_back(std::move(note));
        }
        return std::nullopt;
    };
    resolve_one = [&](const ::zui::Atom& name) {
        if (!visiting.insert(name).second) {
            return;
        }
        auto found = sink.decls.find(name);
        if (found == sink.decls.end() || found->second.empty()) {
            specified.erase(name);
            sink.win.erase(name);
            visiting.erase(name);
            return;
        }
        std::vector<Winner> alive = found->second;
        const size_t guard = alive.size() + 1;
        for (size_t step = 0; step < guard; ++step) {
            if (alive.empty()) {
                specified.erase(name);
                sink.win.erase(name);
                visiting.erase(name);
                return;
            }
            size_t best = 0;
            for (size_t i = 1; i < alive.size(); ++i) {
                if (declaration_wins(winner_key(alive[i]), winner_key(alive[best]))) {
                    best = i;
                }
            }
            std::string value = alive[best].value.str();
            if (contains_substitution(value)) {
                std::optional<std::string> sub = substitute(value, lookup, media);
                if (!sub) {
                    specified.erase(name);
                    sink.win.erase(name);
                    visiting.erase(name);
                    return;
                }
                value = std::move(*sub);
            }
            bool layer = false;
            if (!revert_keyword(value, layer)) {
                specified[name] = value;
                Winner kept = alive[best];
                kept.value = std::move(value);
                sink.win[name] = std::move(kept);
                visiting.erase(name);
                return;
            }
            const Winner dropped = alive[best];
            std::vector<Winner> next;
            next.reserve(alive.size());
            for (const Winner& cand : alive) {
                if (!same_revert_slot(cand, dropped, layer)) {
                    next.push_back(cand);
                }
            }
            if (next.size() == alive.size()) {
                specified.erase(name);
                sink.win.erase(name);
                visiting.erase(name);
                return;
            }
            alive = std::move(next);
        }
        specified.erase(name);
        sink.win.erase(name);
        visiting.erase(name);
    };

    std::vector<::zui::Atom> todo;
    for (const auto& kv : specified) {
        bool layer = false;
        if (revert_keyword(kv.second, layer)) {
            todo.push_back(kv.first);
        }
    }
    for (const ::zui::Atom& name : todo) {
        auto it = specified.find(name);
        bool layer = false;
        if (it == specified.end() || !revert_keyword(it->second, layer)) {
            continue;
        }
        resolve_one(name);
    }
}

void resolve_overflow_axes(StyleMap& specified) {
    const auto x = specified.find("overflow-x");
    const auto y = specified.find("overflow-y");
    const bool x_visible = x == specified.end() || x->second == "visible";
    const bool y_visible = y == specified.end() || y->second == "visible";
    if (x_visible == y_visible) {
        return;
    }
    specified[x_visible ? "overflow-x" : "overflow-y"] = "auto";
}

}  // namespace detail
}  // namespace css
}  // namespace zui
