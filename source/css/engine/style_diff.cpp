#include "css/engine/cascade_internal.h"

#include "perf/perf_budget.h"

#include <chrono>

// StyleDiff: comparing a node's new computed declaration set with the one it had, and reporting
// the cheapest kind of work that can carry the change (paint-only, layout, or a box rebuild).
// `Engine::cascade` collects those lists in CascadeResult; `commit_computed` is the single place
// that swaps a node's computed map and decides whether the paint epoch moves.

namespace zui {
namespace css {
namespace detail {

bool affects_layout(const ::zui::Atom& name) {
    if (name.layout_known()) {
        return name.layout_yes();
    }
    const bool yes = affects_layout(std::string_view(name));
    name.set_layout(yes);
    return yes;
}

bool affects_layout(std::string_view n) {
    return n == "display" || n == "width" || n == "height" || n == "min-width" ||
           n == "min-height" || n == "max-width" || n == "max-height" || n == "margin" ||
           n == "margin-top" || n == "margin-right" || n == "margin-bottom" || n == "margin-left" ||
           n == "margin-block" || n == "margin-block-start" || n == "margin-block-end" ||
           n == "margin-inline" || n == "margin-inline-start" || n == "margin-inline-end" ||
           n == "padding" || n == "padding-top" || n == "padding-right" || n == "padding-bottom" ||
           n == "padding-left" || n == "padding-block" || n == "padding-block-start" ||
           n == "padding-block-end" || n == "padding-inline" || n == "padding-inline-start" ||
           n == "padding-inline-end" || n == "border" || n == "border-width" ||
           n == "border-top-width" || n == "border-right-width" || n == "border-bottom-width" ||
           n == "border-left-width" || n == "box-sizing" || n == "overflow" || n == "overflow-x" ||
           n == "overflow-y" || n == "position" || n == "top" || n == "right" || n == "bottom" ||
           n == "left" || n == "flex" || n == "flex-direction" || n == "flex-wrap" ||
           n == "flex-grow" || n == "flex-shrink" || n == "flex-basis" || n == "align-items" ||
           n == "align-self" || n == "justify-content" || n == "gap" || n == "row-gap" ||
           n == "column-gap" || n == "grid-template-columns" || n == "grid-template-rows" ||
           n == "grid-auto-columns" || n == "grid-auto-rows" || n == "grid-auto-flow" ||
           n == "grid-column" || n == "grid-column-start" || n == "grid-column-end" ||
           n == "grid-row" || n == "grid-row-start" || n == "grid-row-end" ||
           n == "justify-items" || n == "align-content" || n == "order" || n == "font-size" ||
           n == "font-family" || n == "line-height" || n == "text-align" || n == "vertical-align" ||
           n == "white-space" || n == "z-index" || n == "content-visibility" || n == "list-style" ||
           n == "list-style-type" || n == "list-style-position" || n == "list-style-image" ||
           n == "direction" || n == "writing-mode";
}

std::string_view map_val(const StyleMap* m, const char* k) {
    if (m == nullptr) {
        return {};
    }
    auto it = m->find(k);
    if (it == m->end()) {
        return {};
    }
    return it->second;
}

namespace {

bool interpolable_changed(const StyleMap* oldm, const StyleMap* neu) {
    if (oldm == nullptr || neu == nullptr) {
        return false;
    }
    // Interned once. A `const char*` lookup hashes the name on every property of every commit.
    static const ::zui::Atom keys[] = {
        ::zui::Atom::intern("color"),
        ::zui::Atom::intern("background-color"),
        ::zui::Atom::intern("border-top-color"),
        ::zui::Atom::intern("border-right-color"),
        ::zui::Atom::intern("border-bottom-color"),
        ::zui::Atom::intern("border-left-color"),
        ::zui::Atom::intern("opacity"),
        ::zui::Atom::intern("transform"),
        ::zui::Atom::intern("width"),
        ::zui::Atom::intern("height"),
        ::zui::Atom::intern("margin-top"),
        ::zui::Atom::intern("margin-right"),
        ::zui::Atom::intern("margin-bottom"),
        ::zui::Atom::intern("margin-left"),
        ::zui::Atom::intern("padding-top"),
        ::zui::Atom::intern("padding-right"),
        ::zui::Atom::intern("padding-bottom"),
        ::zui::Atom::intern("padding-left")};
    for (const ::zui::Atom& key : keys) {
        const auto old_it = oldm->find(key);
        const auto new_it = neu->find(key);
        const bool old_miss = old_it == oldm->end();
        const bool new_miss = new_it == neu->end();
        if (old_miss && new_miss) {
            continue;
        }
        if (old_miss || new_miss || old_it->second != new_it->second) {
            return true;
        }
    }
    return false;
}

void note_diff(Engine::CascadeResult& result, html::Node* node, StyleDiff d) {
    if (d == StyleDiff::Reattach) {
        result.reattach.push_back(node);
    } else if (d == StyleDiff::Layout) {
        result.layout.push_back(node);
    }
}

StyleDiff diff_maps(const html::Node* node, const StyleMap& specified) {
    StyleDiff d = StyleDiff::None;
    if (!node->computed) {
        return StyleDiff::Layout;
    }
    static const ::zui::Atom kDisplay = ::zui::Atom::intern("display");
    static const ::zui::Atom kPosition = ::zui::Atom::intern("position");
    auto bump = [&](StyleDiff n) {
        if (n > d) {
            d = n;
        }
    };
    for (const auto& kv : specified) {
        const auto old = node->computed->find(kv.first);
        // Both sides are interned atoms: equal text is the same pool record.
        if (old != node->computed->end() && old->second == kv.second) {
            continue;
        }
        if (kv.first == kDisplay || kv.first == kPosition) {
            bump(StyleDiff::Reattach);
        } else if (affects_layout(kv.first)) {
            bump(StyleDiff::Layout);
        } else {
            bump(StyleDiff::Paint);
        }
    }
    for (const auto& kv : *node->computed) {
        if (specified.find(kv.first) != specified.end()) {
            continue;
        }
        if (kv.first == kDisplay || kv.first == kPosition) {
            bump(StyleDiff::Reattach);
        } else if (affects_layout(kv.first)) {
            bump(StyleDiff::Layout);
        }
    }
    return d;
}

}  // namespace

void commit_computed(Engine::CascadeResult& result, html::Node* node,
                     std::shared_ptr<const StyleMap> specified) {
    CascadeAcc commit_clock(cascade_split_armed() ? &g_cascade_split.commit_ms : nullptr);
    if (node->computed == specified) {
        // The cross-frame memo hands back the map the node already committed (docs/PERF_REFACTOR.md
        // capability A): no diff, no paint stamp, nothing to report.
        return;
    }
    const StyleDiff diff = diff_maps(node, *specified);
    if (diff != StyleDiff::None) {
        perf::note_cascade_recomputed();
        // A computed style moved, whether or not it is a layout property: the frame that paints
        // next is painting something else. Section 9 step D's retained display lists are dropped
        // on this stamp, which is the difference between a re-recorded frame and a stale one.
        html::note_paint_change_at(node);
    }
    note_diff(result, node, diff);
    if (diff == StyleDiff::Paint) {
        // The transition feed (`result.paint`) only carries interpolable properties, so a radius or
        // a border style would never reach it. The box tree still has to re-intern this node's
        // LayoutStyle before paint reads it (docs/PERF_REFACTOR.md stage 1).
        result.paint_style.push_back(node);
    }
    if (interpolable_changed(node->computed.get(), specified.get())) {
        result.paint.push_back({node, node->computed});
    }
    node->computed = std::move(specified);
}

}  // namespace detail
}  // namespace css
}  // namespace zui
