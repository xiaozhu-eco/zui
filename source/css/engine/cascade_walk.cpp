// The cascade tree walk: build the per-node ShareKey and the cross-frame key, consult the
// cross-frame memo and the intra-frame share cache, run the rules for a miss, commit computed /
// generated / selection styles, and recurse with sibling position and counter state. This is the
// only place the phase files above are driven from.
//
// Capability A (docs/PERF_REFACTOR.md): a node that ran its rule list stores the pristine
// declaration sets, what it read / took from its parent, and the computed maps it committed. The
// next frame builds the cross-frame key before matching; when the key, the sheet generation, the
// environment generation, the parent's offered properties and the read values all stand still, the
// rule list does not run again.

#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include "perf/perf_budget.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

void remember_style_attr(MemoEntry& entry, bool has_style, std::string_view text) {
    if (!has_style) {
        if (!entry.style_attr.empty()) {
            entry.style_attr.clear();
        }
        return;
    }
    if (entry.style_attr != text) {
        entry.style_attr.assign(text.data(), text.size());
    }
}

bool style_attr_same(const MemoEntry& entry, bool has_style, std::string_view text) {
    if (!has_style) {
        return entry.style_attr.empty();
    }
    return entry.style_attr == text;
}

/// The environment generation did not move, but an `@media` selector this node matched is gone,
/// or a selector that newly entered the index matches it. The stored sinks still name the old
/// set, so the caller re-runs the rule list instead of re-substituting them.
bool media_selectors_stale(const MemoEntry& entry, html::Node* node, const html::Document& doc,
                           const CascadeIn& in) {
    const CascadeMemo* memo = in.memo;
    if (memo == nullptr || memo->active_media == nullptr) {
        return false;
    }
    if (entry.payload != nullptr) {
        for (const lxb_css_selector_list_t* sel : entry.payload->sinks.media_rules) {
            if (!std::binary_search(memo->active_media->begin(), memo->active_media->end(), sel)) {
                return true;
            }
        }
    }
    if (memo->born_media != nullptr) {
        for (const IndexedRule* rule : *memo->born_media) {
            if (rule != nullptr && media_rule_matches(*rule, node, doc)) {
                return true;
            }
        }
    }
    return false;
}

uint64_t share_sig(const ShareKey& k) {
    const uint64_t sig = static_cast<uint64_t>(ShareKeyHash{}(k));
    return sig;
}

void mix_u64(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }


uint32_t node_state_bits(const html::Node& node, const html::Document* doc, uint32_t html_mask) {
    uint32_t bits = 0;
    if (node.hovered) {
        bits |= kBitHover;
    }
    if (node.active) {
        bits |= kBitActive;
    }
    if (node.focused) {
        bits |= kBitFocus;
    }
    if (node.focused && node.focus_visible) {
        bits |= kBitFocusVisible;
    }
    if (node.modal) {
        bits |= kBitModal;
    }
    if (node.checked) {
        bits |= kBitChecked;
    }
    if (node.disabled) {
        bits |= kBitDisabled;
    }
    if (node.children.empty() && node.text.empty() && !node.whitespace_child) {
        bits |= kBitEmpty;
    }
    bits |= html_pseudo_bits(node, doc, html_mask);
    return bits;
}

uint64_t neighbor_fact_sig(const html::Node& node, uint32_t bits, const NeighborFacts& facts) {
    if (!facts.used) {
        return 0;
    }
    uint64_t sig = 0;
    // Only the facets the loaded selectors actually test are hashed, and every lookup is a set
    // membership test, so the cost is the node's own class / attribute count rather than the size
    // of the table.
    if (!facts.tags.empty() && facts.tags.find(node.tag) != facts.tags.end()) {
        mix_u64(sig, std::hash<std::string>{}(node.tag));
    }
    if (!facts.ids.empty() && !node.id.empty() && facts.ids.find(node.id) != facts.ids.end()) {
        mix_u64(sig, std::hash<std::string>{}(node.id));
    }
    if (!facts.classes.empty() && !node.class_name.empty()) {
        const std::string_view classes = node.class_name;
        std::size_t i = 0;
        while (i < classes.size()) {
            while (i < classes.size() &&
                   std::isspace(static_cast<unsigned char>(classes[i])) != 0) {
                ++i;
            }
            const std::size_t begin = i;
            while (i < classes.size() &&
                   std::isspace(static_cast<unsigned char>(classes[i])) == 0) {
                ++i;
            }
            if (i == begin) {
                continue;
            }
            const std::string_view token = classes.substr(begin, i - begin);
            if (facts.classes.contains(token)) {
                mix_u64(sig, std::hash<std::string_view>{}(token));
            }
        }
    }
    if (!facts.attrs.empty() && !node.attrs.empty()) {
        for (const auto& kv : node.attrs) {
            if (facts.attrs.find(kv.first) == facts.attrs.end()) {
                continue;
            }
            mix_u64(sig, std::hash<std::string>{}(kv.first));
            mix_u64(sig, std::hash<std::string>{}(kv.second));
        }
    }
    if (facts.pseudo_mask != 0) {
        mix_u64(sig, static_cast<uint64_t>(bits & facts.pseudo_mask));
    }
    if (facts.lang) {
        mix_u64(sig, std::hash<std::string>{}(resolved_language(node)));
    }
    if (facts.dir) {
        mix_u64(sig, element_is_rtl(node) ? 2u : 1u);
    }
    return sig;
}

/// True when @p node is a node the `:has()` anchor compound could select, i.e. one whose subtree
/// can flip the function. The tag / id / class checks mirror `Engine::has_anchor_matches`; the rest
/// goes through the shared compound matcher.
bool anchor_compound_matches(const html::Node& node, const HasAnchor& anchor) {
    if (anchor.any) {
        return true;
    }
    if (!anchor.tag.empty() && anchor.tag != node.tag) {
        return false;
    }
    if (!anchor.id.empty() && anchor.id != node.id) {
        return false;
    }
    for (std::string_view cls : anchor.classes) {
        if (!html::has_class_token(node.class_name, cls)) {
            return false;
        }
    }
    return facets_match(node, anchor.tests);
}


/// The offered-name signature of a parent map: the names of every property the parent hands a child
/// (an inheritable longhand or a custom property). A key hit compares it, so a parent that gains or
/// loses such a name cannot leave a child on its stored map.
uint64_t parent_name_sig(const StyleMap* map) {
    uint64_t sig = 0;
    if (map != nullptr) {
        for (const auto& kv : *map) {
            if (name_propagates(kv.first)) {
                mix_u64(sig, kv.first.hash());
            }
        }
    }
    return sig;
}


/// `::before` / `::after` generate a box only when `content` is not `normal` or `none` (CSS
/// Pseudo-Elements §4.1). A universal rule that only sets `box-sizing` still matches, and
/// finalizing that empty box was a full style pass per element. A `var()` or any other value
/// can still generate, so only the two keywords and a missing `content` skip the pass.
bool before_after_generates(const DeclSink& sink) {
    const auto it = sink.win.find("content");
    if (it != sink.win.end()) {
        const ::zui::Atom& value = it->second.value;
        return !(value == "none" || value == "normal");
    }
    for (const PendingShort& pending : sink.pending) {
        if (pending.name == "content") {
            return true;
        }
    }
    return false;
}

/// `currentcolor` is resolved from the originating element's computed color while the highlight
/// sink is finalized, and that resolution is stored as an RGBA. A later color change has to
/// re-substitute. `var()` / `env()` are already on `DeclSink::fin_flags`.
bool value_has_currentcolor(std::string_view value) {
    constexpr char kNeedle[] = "currentcolor";
    if (value.size() < 12) {
        return false;
    }
    for (std::size_t i = 0; i + 12 <= value.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < 12; ++j) {
            unsigned char c = static_cast<unsigned char>(value[i + j]);
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<unsigned char>(c - 'A' + 'a');
            }
            if (c != static_cast<unsigned char>(kNeedle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

/// True when a highlight sink's cascaded contribution can move without the element's own
/// `taken` / `reads` sets moving. A literal `::selection` (the calendar's universal rule) is not
/// one of these: forcing a re-substitution for it walked every element on the next frame.
bool highlight_depends_on_origin(const DeclSink& sink) {
    if ((sink.fin_flags & (DeclSink::kFinCustom | DeclSink::kFinSub)) != 0) {
        return true;
    }
    for (const auto& kv : sink.win) {
        if (value_has_currentcolor(kv.second.value)) {
            return true;
        }
    }
    for (const PendingShort& pending : sink.pending) {
        if (value_has_currentcolor(pending.value)) {
            return true;
        }
    }
    return false;
}

/// Runs one node's pristine declaration sets into computed maps. This is exactly the tail of a cold
/// cascade: `finalize_sink` folds the pending shorthands into the sink, so the caller's copy is
/// finalised and the stored payload stays pristine.
void finalize_payload(const DeclSinks& pristine, const StyleMap* inherited, const html::Node* node,
                      const MediaState& media, Finalized& out) {
    CascadeAcc fin_clock(cascade_split_armed() ? &g_cascade_split.fin_ms : nullptr);
    // The pristine payload is shared by every node with this key. Finalize mutates the sink
    // (shorthand expansion), so it needs a private copy. A fresh `DeclSinks` allocates seven
    // maps per miss; keeping one scratch reuses that capacity for the rest of the frame.
    thread_local DeclSinks scratch;
    {
        CascadeAcc copy_clock(cascade_split_armed() ? &g_cascade_split.fin_copy : nullptr);
        scratch = pristine;
    }
    DeclSinks& sinks = scratch;
    FinalizeTrace trace;
    trace.reads = &out.reads;
    trace.taken = &out.taken;
    const html::Node* origin = node != nullptr ? node->parent : nullptr;
    auto specified = std::make_shared<StyleMap>();
    finalize_sink(*specified, sinks.for_target(PseudoTarget::None), inherited, origin, media,
                  &trace);
    for (int i = 0; i < kPseudoCount; ++i) {
        const PseudoTarget t = static_cast<PseudoTarget>(i + 1);
        if (!sinks.for_target(t).matched()) {
            continue;
        }
        if ((t == PseudoTarget::Before || t == PseudoTarget::After) &&
            !before_after_generates(sinks.for_target(t))) {
            continue;
        }
        auto map = std::make_shared<StyleMap>();
        FinalizeTrace pseudo_trace;
        pseudo_trace.reads = &out.reads;
        pseudo_trace.pseudo = true;
        const std::size_t reads_before = out.reads.size();
        finalize_sink(*map, sinks.for_target(t), specified.get(), origin, media, &pseudo_trace);
        if (out.reads.size() != reads_before) {
            out.reads_pseudo = true;
        }
        if (t != PseudoTarget::Placeholder) {
            normalize_generated(*map);
        }
        out.pseudo[i] = std::move(map);
    }
    // A matched highlight used to force re-substitution on every later frame. That is required
    // only when the contribution itself reads the originating element (`var()`, `env()`,
    // `currentcolor`). Parent-highlight inheritance is applied in `commit_selection`, which the
    // memo hit still calls. Check the pristine sink: finalize expands shorthands and would hide
    // the `var()`.
    DeclSink& selection_sink = sinks.for_target(PseudoTarget::Selection);
    if (highlight_depends_on_origin(selection_sink)) {
        out.reads_pseudo = true;
    }
    out.selection = finalize_selection_sink(selection_sink, specified.get(), media);
    DeclSink& backdrop_sink = sinks.for_target(PseudoTarget::Backdrop);
    if (backdrop_sink.matched()) {
        if (highlight_depends_on_origin(backdrop_sink)) {
            out.reads_pseudo = true;
        }
        auto map = std::make_shared<StyleMap>();
        FinalizeTrace backdrop_trace;
        backdrop_trace.reads = &out.reads;
        backdrop_trace.pseudo = true;
        const std::size_t reads_before = out.reads.size();
        finalize_sink(*map, backdrop_sink, specified.get(), origin, media, &backdrop_trace);
        if (out.reads.size() != reads_before) {
            out.reads_pseudo = true;
        }
        out.backdrop = std::move(map);
    }
    out.style = std::move(specified);
}

/// True when every property the node took from its parent still carries the same value and the
/// parent still offers the same names.
bool parent_offers_hold(const MemoEntry& entry, const StyleMap* parent_map) {
    if (entry.parent_computed == parent_map) {
        // The parent committed the identical map object, so nothing it offers can have moved.
        return true;
    }
    if (parent_name_sig(parent_map) != entry.parent_name_sig) {
        return false;
    }
    for (const ParentOffer& offer : entry.taken) {
        if (parent_map == nullptr) {
            return false;
        }
        const auto it = parent_map->find(offer.name);
        if (it == parent_map->end() || it->second != offer.value) {
            return false;
        }
    }
    return true;
}

/// True when the custom properties the node read still carry the values the entry recorded. The
/// offered properties above already cover the re-emitted names; this is the read set of
/// docs/PERF_REFACTOR.md capability A, and it keeps a stale `var()` result from being reused if the
/// re-emission ever stops carrying a name a declaration read.
bool reads_hold(const MemoEntry& entry, const html::Node* node) {
    const html::Node* parent = node != nullptr ? node->parent : nullptr;
    for (const ReadValue& read : entry.reads) {
        if (read.local) {
            continue;
        }
        bool present = false;
        bool same = false;
        for (const html::Node* n = parent; n != nullptr && !present; n = n->parent) {
            if (n->computed == nullptr) {
                continue;
            }
            const auto it = n->computed->find(read.name);
            if (it != n->computed->end()) {
                present = true;
                same = it->second == read.value;
            }
        }
        if (read.present != present || (present && !same)) {
            return false;
        }
    }
    return true;
}

/// A re-substitution that produced the same map keeps the stored pointer: the next frame's
/// inheritance check reads pointer identity first, so a chain that did not really move must not
/// look like it did.
std::shared_ptr<const StyleMap> reuse_if_equal(std::shared_ptr<const StyleMap> fresh,
                                               const std::shared_ptr<const StyleMap>& stored) {
    if (fresh == nullptr || stored == nullptr) {
        return fresh == stored ? stored : std::move(fresh);
    }
    if (*fresh == *stored) {
        return stored;
    }
    return fresh;
}


void CascadeMemo::forget(const html::Node* dead) {
    if (dead == nullptr) {
        return;
    }
    std::vector<const html::Node*> stack{dead};
    while (!stack.empty()) {
        const html::Node* node = stack.back();
        stack.pop_back();
        nodes.erase(node);
        for (const auto& child : node->children) {
            if (child != nullptr) {
                stack.push_back(child.get());
            }
        }
    }
}

/// Every facet of a compound other than the tag / id / class checks: attributes and plain
/// pseudo-classes, evaluated with the matcher the cascade uses. Both are node-local (no document),
/// so a hover target and a `:has()` anchor can share the check.
bool facets_match(const html::Node& node, const std::vector<const lxb_css_selector_t*>& tests) {
    for (const lxb_css_selector_t* test : tests) {
        const bool ok = test->type == LXB_CSS_SELECTOR_TYPE_ATTRIBUTE
                            ? match_attr(&node, test)
                            : match_plain_pseudo_class(&node, test->u.pseudo.type, nullptr, nullptr);
        if (!ok) {
            return false;
        }
    }
    return true;
}
}  // namespace detail
}  // namespace css
}  // namespace zui
