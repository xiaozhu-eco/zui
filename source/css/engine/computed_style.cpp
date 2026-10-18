// Computed declaration maps: the inherited-property set, var()/env() substitution, CSS-wide
// keywords, logical-to-physical box edges, and the inheritance pass that produces the map a node
// commits. Style diffing (which kind of work a changed map implies) lives in `style_diff.cpp`.

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

const char* kInherit[] = {
    "color", "font-family", "font-size", "font-weight", "font-style", "font-stretch",
    "font-variant", "font-kerning", "font-feature-settings",
    // CSS 2.1 / CSS Inline 3: vertical-align does not inherit. Table UA rules
    // above set middle on row groups and bare rows instead.
    "line-height", "visibility", "letter-spacing", "word-spacing", "direction", "writing-mode",
    "text-orientation", "text-combine-upright", "text-align", "text-align-last", "text-indent",
    "text-transform", "white-space", "word-break", "overflow-wrap", "word-wrap", "line-break",
    "hyphens", "tab-size",
    // CSS UI 4: cursor inherits so a container can set pointer/text once.
    "cursor",
    // CSS UI 4: pointer-events inherits. A container's `none` has to reach
    // descendants unless they set `auto` again.
    "pointer-events",
    // CSS 2.1 section 17.6.1: `empty-cells` inherits, so a table sets it once
    // and every cell picks it up.
    "empty-cells",
    // CSS Lists 3: list-style-* inherit so `ol { list-style-type: decimal }`
    // reaches each `li` (initial type is `disc`, not `none`).
    "list-style-type", "list-style-position", "list-style-image",
    // CSS Values 5: interpolate-size inherits; :root { allow-keywords } opts in.
    "interpolate-size",
    // CSS 2.1 / Tables / Text / Fragmentation / Color Adjustment extras.
    "border-collapse", "border-spacing", "caption-side", "quotes", "orphans", "widows",
    "text-shadow", "caret-color", "accent-color", "color-scheme", "print-color-adjust",
    "image-rendering", "font-size-adjust", "text-underline-position", "text-emphasis",
    "text-emphasis-color", "text-emphasis-style", "text-emphasis-position"};

bool inheritable(std::string_view name) {
    // The property list is fixed, and the cross-frame memo calls this once per entry of every
    // parent map it checks, so the linear scan is replaced by one lookup table built once.
    static const std::unordered_set<std::string_view> kSet = [] {
        std::unordered_set<std::string_view> set;
        for (const char* n : kInherit) {
            set.insert(n);
        }
        return set;
    }();
    return kSet.find(name) != kSet.end();
}

bool name_inherits(const ::zui::Atom& name) {
    if (!name.inherit_known()) {
        name.set_inherit(inheritable(name));
    }
    return name.inherit_yes();
}

bool name_propagates(const ::zui::Atom& name) {
    return name_inherits(name) || is_custom_property(name);
}

bool propagates_to_child(std::string_view name) {
    return inheritable(name) || is_custom_property(name);
}

void finalize_sink(StyleMap& specified, DeclSink& sink, const StyleMap* parent,
                   const html::Node* custom_origin, const MediaState& media,
                   const FinalizeTrace* trace) {
    // The custom properties this declaration set provides itself. A pending shorthand that reads
    // one of them cannot move unless the sheet generation does; one that reads anything else is an
    // external read the cross-frame memo has to verify. Built only when a shorthand or a revert
    // will query it: an empty set still allocates its bucket array, and most sinks have neither.
    const bool need_local = trace != nullptr && trace->reads != nullptr &&
                            (!sink.pending.empty() ||
                             (sink.fin_flags & DeclSink::kFinRevert) != 0);
    std::optional<std::unordered_set<::zui::Atom, ::zui::AtomHash>> local_store;
    const std::unordered_set<::zui::Atom, ::zui::AtomHash>* local_names = nullptr;
    if (need_local) {
        local_store.emplace();
        local_names = &*local_store;
        const FlatMap<Winner>::value_type* src = sink.win.items_ptr();
        const std::size_t n = sink.win.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (is_custom_property(src[i].first)) {
                local_store->insert(src[i].first);
            }
        }
    }
    {
        CascadeAcc put_clock(cascade_split_armed() ? &g_cascade_split.fin_put : nullptr);
        const FlatMap<Winner>::value_type* src = sink.win.items_ptr();
        const std::size_t n = sink.win.size();
        // A fresh map has no keys yet, and winners are unique, so each insert skips the probe.
        if (n != 0 && specified.empty()) {
            specified.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                specified.append_new(src[i].first, src[i].second.value);
            }
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                specified.emplace(src[i].first, src[i].second.value);
            }
        }
    }
    {
        CascadeAcc var_clock(cascade_split_armed() ? &g_cascade_split.fin_var : nullptr);
        if ((sink.fin_flags & (DeclSink::kFinCustom | DeclSink::kFinSub)) != 0) {
            resolve_specified(specified, parent, custom_origin, media, trace);
        }
        if (!sink.pending.empty()) {
        // A shorthand is expanded only after substitution, and `var()` there names a custom
        // property that usually lives on an ancestor (`border: 1px solid var(--line)`). The
        // longhand path already walks ancestors; this lookup has to as well, or the shorthand is
        // dropped and a user-agent border stays in force. A custom property this element declared
        // but could not compute is invalid here: it must not fall through to the inherited value.
        CustomLookup lookup = [&](std::string_view name) -> std::optional<std::string> {
            const std::string key(name);
            auto it = specified.find(key);
            if (it == specified.end()) {
                const bool local_decl = sink.win.find(key) != sink.win.end();
                const std::string* inherited = nullptr;
                if (!local_decl && is_custom_property(name)) {
                    inherited = ancestor_custom(parent, custom_origin, name);
                }
                if (trace != nullptr && trace->reads != nullptr) {
                    ReadValue note;
                    note.name = key;
                    note.local = local_decl;
                    note.pseudo = trace->pseudo;
                    note.present = inherited != nullptr;
                    if (inherited != nullptr) {
                        note.value = *inherited;
                    }
                    trace->reads->push_back(std::move(note));
                }
                if (inherited == nullptr) {
                    return std::nullopt;
                }
                return *inherited;
            }
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = it->first;
                note.local = local_names != nullptr &&
                             local_names->find(it->first) != local_names->end();
                note.pseudo = trace->pseudo;
                note.present = true;
                note.value = it->second;
                trace->reads->push_back(std::move(note));
            }
            return it->second.str();
        };
        for (const PendingShort& p : sink.pending) {
            std::optional<std::string> sub = substitute(p.value, lookup, media);
            if (!sub) {
                continue;
            }
            std::vector<std::pair<std::string, std::string>> parts;
            if (!expand_shorthand_value(p.name.str(), *sub, parts)) {
                continue;
            }
            for (const auto& part : parts) {
                Winner cand;
                cand.value = part.second;
                cand.spec = p.spec;
                cand.order = p.order;
                cand.origin = p.origin;
                cand.important = p.important;
                cand.layer = p.layer;
                cand.element_attached = p.element_attached;
                sink.decls[part.first].push_back(cand);
                bool replace = true;
                auto existing = sink.win.find(part.first);
                if (existing != sink.win.end()) {
                    const Winner& slot = existing->second;
                    replace = declaration_wins(winner_key(cand), winner_key(slot));
                }
                if (!replace) {
                    continue;
                }
                specified[part.first] = part.second;
                if (part.second == "inherit" || part.second == "initial" || part.second == "unset") {
                    sink.fin_flags =
                        static_cast<std::uint8_t>(sink.fin_flags | DeclSink::kFinWide);
                }
                if (part.second == "revert" || part.second == "revert-layer" ||
                    part.second == "revert-rule") {
                    sink.fin_flags =
                        static_cast<std::uint8_t>(sink.fin_flags | DeclSink::kFinRevert);
                }
                const std::string_view expanded = part.first;
                if (expanded.find("inline") != std::string_view::npos ||
                    expanded.find("-block") != std::string_view::npos) {
                    sink.fin_flags =
                        static_cast<std::uint8_t>(sink.fin_flags | DeclSink::kFinLogical);
                }
                sink.win[part.first] = std::move(cand);
            }
        }
        }
    }
    {
        CascadeAcc rest_clock(cascade_split_armed() ? &g_cascade_split.fin_rest : nullptr);
        if ((sink.fin_flags & DeclSink::kFinRevert) != 0) {
            resolve_revert(specified, sink, parent, custom_origin, media, trace, local_names);
        }
    }
    {
        CascadeAcc inh_clock(cascade_split_armed() ? &g_cascade_split.fin_inh : nullptr);
        if (parent != nullptr) {
            const StyleMap::value_type* src = parent->items_ptr();
            const std::size_t n = parent->size();
            const std::uint32_t* slots = specified.slot_ptr();
            const StyleMap::value_type* items = specified.items_ptr();
            std::size_t nslots = specified.slot_n();
            bool items_empty = specified.empty();
            for (std::size_t s = 0; s < n; ++s) {
                const ::zui::Atom& name = src[s].first;
                const ::zui::Atom::Rec* rec = name.rec_;
                std::uint8_t flags = rec->flags;
                if ((flags & ::zui::Atom::kInheritKnown) == 0) {
                    name.set_inherit(inheritable(name));
                    flags = rec->flags;
                }
                if ((flags & ::zui::Atom::kInheritYes) == 0) {
                    continue;
                }
                bool found = false;
                if (slots != nullptr && items != nullptr && nslots != 0 && !items_empty) {
                    const std::size_t mask = nslots - 1;
                    std::size_t i = static_cast<std::size_t>(rec->hash) & mask;
                    for (std::size_t k = 0; k < nslots; ++k) {
                        const std::uint32_t slot = slots[i];
                        if (slot == StyleMap::kProbeEmpty) {
                            break;
                        }
                        if (slot != StyleMap::kProbeTomb && items[slot - 1].first.rec_ == rec) {
                            found = true;
                            break;
                        }
                        i = (i + 1) & mask;
                    }
                }
                if (found) {
                    continue;
                }
                if (trace != nullptr && trace->taken != nullptr) {
                    trace->taken->push_back(ParentOffer{name, src[s].second});
                }
                specified.append_new(name, src[s].second);
                slots = specified.slot_ptr();
                items = specified.items_ptr();
                nslots = specified.slot_n();
                items_empty = false;
            }
        }
    }
    {
        CascadeAcc rest_clock(cascade_split_armed() ? &g_cascade_split.fin_rest : nullptr);
        // `font-family` inherits, and the parent already serialized it. Re-parsing that list on every
        // descendant allocated a fresh string per miss. A shorthand `font` lands in `sink.win` too.
        const bool own_font_family = sink.win.find("font-family") != sink.win.end();
        if ((sink.fin_flags & DeclSink::kFinWide) != 0) {
            resolve_wide_keywords(specified, parent);
        }
        if ((sink.fin_flags & DeclSink::kFinLogical) != 0) {
            resolve_logical_box_edges(specified);
        }
        resolve_overflow_axes(specified);
        if (own_font_family) {
            normalize_font_family(specified);
        }
    }
}
}  // namespace detail
}  // namespace css
}  // namespace zui
