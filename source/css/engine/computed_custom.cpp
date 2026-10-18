// Custom properties and var()/env() substitution at computed-value time.

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

const std::string* ancestor_custom(const StyleMap* parent, const html::Node* origin,
                                   std::string_view name) {
    if (parent != nullptr) {
        const auto it = parent->find(std::string(name));
        if (it != parent->end() && is_custom_property(it->first)) {
            return it->second.ptr();
        }
    }
    for (const html::Node* node = origin; node != nullptr; node = node->parent) {
        if (node->computed == nullptr) {
            continue;
        }
        const auto it = node->computed->find(std::string(name));
        if (it != node->computed->end()) {
            return it->second.ptr();
        }
    }
    return nullptr;
}

void resolve_specified(StyleMap& specified, const StyleMap* parent, const html::Node* custom_origin,
                       const MediaState& media, const FinalizeTrace* trace) {
    struct Slot {
        std::string raw;
        int state = 0;
        /// Seeded from the parent's map, so its value is external to this declaration set.
        bool from_parent = false;
        std::string computed;
    };
    std::unordered_map<::zui::Atom, Slot, ::zui::AtomHash> customs;
    std::vector<::zui::Atom> named;
    for (const auto& kv : specified) {
        if (!is_custom_property(kv.first)) {
            continue;
        }
        Slot slot;
        slot.raw = kv.second.str();
        customs[kv.first] = std::move(slot);
        named.push_back(kv.first);
    }
    for (const ::zui::Atom& name : named) {
        specified.erase(name);
    }
    CustomLookup lookup;
    lookup = [&](std::string_view name) -> std::optional<std::string> {
        auto it = customs.find(::zui::Atom::intern(name));
        if (it == customs.end()) {
            const std::string* inherited = ancestor_custom(parent, custom_origin, name);
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = std::string(name);
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
        Slot& slot = it->second;
        if (slot.state == 2) {
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = it->first;
                note.local = !slot.from_parent;
                note.pseudo = trace->pseudo;
                note.present = true;
                note.value = slot.computed;
                trace->reads->push_back(std::move(note));
            }
            return slot.computed;
        }
        if (slot.state == 1 || slot.state == 3) {
            if (trace != nullptr && trace->reads != nullptr) {
                ReadValue note;
                note.name = it->first;
                note.local = !slot.from_parent;
                note.pseudo = trace->pseudo;
                trace->reads->push_back(std::move(note));
            }
            slot.state = 3;
            return std::nullopt;
        }
        slot.state = 1;
        std::optional<std::string> sub = substitute(slot.raw, lookup, media);
        if (!sub) {
            slot.state = 3;
            return std::nullopt;
        }
        slot.computed = std::move(*sub);
        slot.state = 2;
        return slot.computed;
    };
    for (const ::zui::Atom& name : named) {
        (void)lookup(name);
    }
    for (auto& kv : customs) {
        if (kv.second.state == 2) {
            if (trace != nullptr && trace->taken != nullptr && kv.second.from_parent) {
                trace->taken->push_back(ParentOffer{kv.first, kv.second.computed});
            }
            specified.emplace(kv.first, kv.second.computed);
        }
    }
    std::vector<::zui::Atom> drop;
    for (auto& kv : specified) {
        if (is_custom_property(kv.first) || !contains_substitution(kv.second)) {
            continue;
        }
        std::optional<std::string> sub = substitute(kv.second, lookup, media);
        if (!sub) {
            drop.push_back(kv.first);
        } else {
            kv.second = std::move(*sub);
        }
    }
    for (const ::zui::Atom& name : drop) {
        specified.erase(name);
    }
}

}  // namespace detail
}  // namespace css
}  // namespace zui
