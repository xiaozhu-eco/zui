// Per-node specified and computed application: match, finalize, commit, recurse.

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
#include <chrono>

namespace zui {
namespace css {
namespace detail {

void cascade_node(html::Node* node, const html::Document& doc, const CascadeIn& in,
                  Engine::CascadeResult& result, ShareCache& cache, const WalkPos& pos) {
    if (node->tag.empty() || node->tag == "#text") {
        cascade_children(node, doc, in, result, cache, pos);
        return;
    }

    CascadeAcc pre_node(cascade_split_armed() ? &g_cascade_split.pre_node_ms : nullptr);
    // CSS Lists §4.6 / HTML Rendering: list containers open a `list-item` counter scope.
    // `ol[start]` resets to start-1 so the first item's increment yields `start`.
    bool pushed_list_scope = false;
    if (in.list_counters != nullptr && is_list_container_tag(node->tag)) {
        int reset_to = 0;
        if (node->tag == "ol") {
            const auto it = node->attrs.find("start");
            if (it != node->attrs.end()) {
                const int start = parse_html_integer(it->second, 1);
                reset_to = (start < 1 ? 1 : start) - 1;
            }
        }
        in.list_counters->push_scope(reset_to);
        pushed_list_scope = true;
    }

    // What this node offers as an *ancestor* or as a *preceding sibling* to everything below it.
    // Folded into the children's `ancestor_sig`; computed even for a node outside the closure so a
    // descendant's key is never built from a chain with a hole in it.
    const uint32_t self_bits = key_state_bits(
        *node,
        pos.pos_bits | node_state_bits(*node, &doc, in.html_state_bits) |
            (node->parent == nullptr ? kBitRootEdge : 0),
        in.hover_targets, in.active_targets);
    uint64_t self_facts = 0;
    if (in.ancestor_facts != nullptr && in.ancestor_facts->used) {
        self_facts = neighbor_fact_sig(*node, self_bits, *in.ancestor_facts);
        // The node's own preceding siblings are part of what a descendant of this node sees.
        mix_u64(self_facts, pos.sibling_sig);
        for (const NeighborHas& has : in.ancestor_facts->has_lists) {
            // Only a node the anchor compound selects can flip the function, so the subtree walk
            // happens for that node alone - not for every node the table happens to be folded into.
            if (!anchor_compound_matches(*node, has.anchor)) {
                continue;
            }
            perf::note_cascade_key_eval();
            if (match_has(node, doc, has.list)) {
                mix_u64(self_facts, reinterpret_cast<uint64_t>(has.list));
            }
        }
    }
    WalkPos child_pos = pos;
    child_pos.ancestor_sig = pos.ancestor_sig;
    mix_u64(child_pos.ancestor_sig, self_facts);

    // docs/PERF.md I3: a node outside the closure keeps its computed style. The walk still counts
    // it into the `list-item` counter scope, because a later node inside the closure needs the
    // right ordinal; nothing else about it is work. The parent fingerprint is the node pointer
    // rather than the share hash the full path uses, so two skipped parents can never share one.
    if (in.closure != nullptr && in.closure->find(node) == in.closure->end()) {
        if (in.list_counters != nullptr && html::is_list_item(node)) {
            const int ordinal = in.list_counters->increment();
            if (node->tag == "li") {
                const auto vit = node->attrs.find("value");
                if (vit != node->attrs.end()) {
                    in.list_counters->set(parse_html_integer(vit->second, ordinal));
                }
            }
        }
        child_pos.parent_sig = reinterpret_cast<uint64_t>(node);
        pre_node.stop();
        cascade_children(node, doc, in, result, cache, child_pos);
        if (pushed_list_scope && in.list_counters != nullptr) {
            in.list_counters->pop_scope();
        }
        return;
    }

    pre_node.stop();
    CascadeAcc key_clock(cascade_split_armed() ? &g_cascade_split.key_ms : nullptr);
    ShareKey key;
    key.tag = node->tag;
    key.cls = node->class_name;
    // The id belongs in the key only when a selector could see it (`CascadeIn::selector_ids`). The
    // reader's table of contents is 2000 `id="chN"` buttons that no rule names, and keeping the id
    // made every one of them a private key - so the share cache never merged them and each one ran
    // the whole stylesheet, `:has()` included.
    const bool share_id =
        in.selector_id_attribute ||
        (in.selector_ids != nullptr && in.selector_ids->find(node->id) != in.selector_ids->end());
    key.id = share_id ? std::string_view(node->id) : std::string_view{};
    key.prev_tag = pos.prev_tag;
    key.parent_sig = pos.parent_sig;
    key.nth_index = pos.nth_index;
    key.nth_type_index = pos.nth_type_index;
    key.nth_of_sig = pos.nth_of_sig;
    // Mix every attribute a selector can see into the key so `input[type=checkbox]` and
    // `input[type=text]` do not share one computed map. Names no selector mentions stay out.
    if (in.selector_attrs != nullptr && !in.selector_attrs->empty() && !node->attrs.empty()) {
        uint64_t sig = 0;
        for (const std::string& name : *in.selector_attrs) {
            const auto it = node->attrs.find(name);
            if (it == node->attrs.end()) {
                continue;
            }
            mix_u64(sig, std::hash<std::string>{}(name));
            mix_u64(sig, std::hash<std::string>{}(it->second));
        }
        key.attrs_sig = sig;
    }
    // Relative `:has()` on the subject compound can differ between siblings that share tag/class.
    // Document-scoped `:root:has(...) .x` keeps `:has()` off the subject, so those stay cacheable
    // without per-node fingerprinting here.
    if (in.subject_has != nullptr && !in.subject_has->empty()) {
        uint64_t sig = 0;
        for (std::size_t i = 0; i < in.subject_has->size(); ++i) {
            const SubjectHasFact& has = (*in.subject_has)[i];
            // `.entry:has(:checked)` can only flip on an entry. Walking every node's subtree
            // for that argument made the file list's cold cascade the :has() calls themselves.
            if (!anchor_compound_matches(*node, has.anchor)) {
                continue;
            }
            perf::note_cascade_key_eval();
            if (match_has(node, doc, has.list)) {
                sig |= (1ull << (i & 63));
            }
        }
        key.has_sig = sig;
    }
    // A position bit no selector tests (:first-child, :last-child, :*-of-type, :empty, :only-*)
    // must not split the key. Through parent_sig that bit otherwise makes every row of a grid a
    // private style, and every cell under it runs the stylesheet again.
    const uint32_t seen_structural =
        in.ancestor_facts != nullptr ? in.ancestor_facts->structural_share_bits : kStructuralShareBits;
    const uint32_t unused_structural = kStructuralShareBits & ~seen_structural;
    key.bits = self_bits & ~unused_structural;
    if (in.selector_uses_lang) {
        key.lang_sig = std::hash<std::string>{}(resolved_language(*node));
    }
    if (in.selector_uses_dir) {
        key.dir_sig = element_is_rtl(*node) ? 2u : 1u;
    }
    // docs/PERF_REFACTOR.md stage 0: the structural key is built here, before the lookup. It counts
    // the same nodes as `cascade_visited` today; capability P3 stops building one per visited node.
    perf::note_cascade_share_key();
    key_clock.stop();

    static const std::string style_key("style");
    const auto style_attr = node->attrs.find(style_key);
    const bool has_style_attr = style_attr != node->attrs.end() && !style_attr->second.empty();
    if (in.stats != nullptr) {
        ++in.stats->nodes;
    }
    perf::note_cascade_visited();

    // The cross-frame key of docs/PERF_REFACTOR.md capability A: every input the node's own match
    // and the parent-contribution copy depend on. It carries no `parent_sig`; the ancestor side is
    // folded in as `ancestor_sig` from the selectors' own ancestor compounds.
    CrossKey cross;
    cross.tag = key.tag;
    cross.cls = key.cls;
    cross.id = key.id;
    cross.attrs_sig = key.attrs_sig;
    cross.has_sig = key.has_sig;
    cross.ancestor_sig = pos.ancestor_sig;
    cross.sibling_sig = pos.sibling_sig;
    cross.nth_of_sig = key.nth_of_sig;
    cross.nth_index = key.nth_index;
    cross.nth_type_index = key.nth_type_index;
    cross.siblings = in.selector_uses_nth_last ? pos.siblings : 0;
    cross.siblings_of_type = in.selector_uses_nth_last ? pos.siblings_of_type : 0;
    cross.lang_sig = key.lang_sig;
    cross.dir_sig = key.dir_sig;
    cross.bits = key.bits;
    if (in.memo != nullptr) {
        CascadeAcc find_clock(cascade_split_armed() ? &g_cascade_split.find_ms : nullptr);
        const auto found = in.memo->nodes.find(node);
        find_clock.stop();
        if (found != in.memo->nodes.end()) {
            MemoEntry& entry = found->second;
            const bool style_same = style_attr_same(
                entry, has_style_attr,
                has_style_attr ? std::string_view(style_attr->second) : std::string_view{});
            if (entry.sheets_gen == in.memo->sheets_gen && entry.env_gen == in.memo->env_gen &&
                entry.key.matches(cross) && style_same &&
                !media_selectors_stale(entry, node, doc, in)) {
                const StyleMap* parent_map =
                    node->parent != nullptr ? node->parent->computed.get() : nullptr;
                const bool holds = !entry.reads_pseudo && parent_offers_hold(entry, parent_map) &&
                                   reads_hold(entry, node);
                if (holds) {
                    // Nothing the node's own declarations or its parent's contribution supply has
                    // moved, so the stored computed maps are still the answer.
                    perf::note_cascade_memo_hit();
                    commit_computed(result, node, entry.computed.style);
                    std::vector<std::string> author_pushed;
                    if (in.author_counters != nullptr) {
                        author_pushed =
                            in.author_counters->apply_reset(entry.computed.style.get());
                        in.author_counters->apply_increment(entry.computed.style.get());
                    }
                    int list_ordinal = 0;
                    if (in.list_counters != nullptr && html::is_list_item(node)) {
                        list_ordinal = in.list_counters->increment();
                        if (node->tag == "li") {
                            const auto vit = node->attrs.find("value");
                            if (vit != node->attrs.end()) {
                                const int set_to = parse_html_integer(vit->second, list_ordinal);
                                in.list_counters->set(set_to);
                                list_ordinal = set_to;
                            }
                        }
                    }
                    commit_pseudos(result, node, entry.computed.pseudo, list_ordinal,
                                   in.author_counters);
                    commit_selection(node, entry.computed.selection);
                    commit_backdrop(node, entry.computed.backdrop);
                    child_pos.parent_sig = share_sig(key);
                    cascade_children(node, doc, in, result, cache, child_pos);
                    if (in.author_counters != nullptr) {
                        in.author_counters->pop_resets(author_pushed);
                    }
                    if (pushed_list_scope && in.list_counters != nullptr) {
                        in.list_counters->pop_scope();
                    }
                    return;
                }
                // The match is unchanged (the key stands), but the parent's contribution or a
                // generated element moved: re-substitute the stored declarations instead of
                // running the rule list again.
                perf::note_cascade_resubst();
                Finalized again;
                finalize_payload(entry.payload->sinks, parent_map, node, in.media, again);
                again.style = reuse_if_equal(std::move(again.style), entry.computed.style);
                for (int i = 0; i < kPseudoCount; ++i) {
                    again.pseudo[i] =
                        reuse_if_equal(std::move(again.pseudo[i]), entry.computed.pseudo[i]);
                }
                again.backdrop =
                    reuse_if_equal(std::move(again.backdrop), entry.computed.backdrop);
                if (again.selection != nullptr && entry.computed.selection != nullptr &&
                    selection_equal(entry.computed.selection.get(), *again.selection)) {
                    again.selection = entry.computed.selection;
                }
                commit_computed(result, node, again.style);
                std::vector<std::string> author_pushed;
                if (in.author_counters != nullptr) {
                    author_pushed = in.author_counters->apply_reset(again.style.get());
                    in.author_counters->apply_increment(again.style.get());
                }
                int list_ordinal = 0;
                if (in.list_counters != nullptr && html::is_list_item(node)) {
                    list_ordinal = in.list_counters->increment();
                    if (node->tag == "li") {
                        const auto vit = node->attrs.find("value");
                        if (vit != node->attrs.end()) {
                            const int set_to = parse_html_integer(vit->second, list_ordinal);
                            in.list_counters->set(set_to);
                            list_ordinal = set_to;
                        }
                    }
                }
                commit_pseudos(result, node, again.pseudo, list_ordinal, in.author_counters);
                commit_selection(node, again.selection);
                commit_backdrop(node, again.backdrop);
                CascadeAcc memo_clock(cascade_split_armed() ? &g_cascade_split.memo_ms : nullptr);
                entry.taken = std::move(again.taken);
                entry.parent_name_sig = parent_name_sig(parent_map);
                entry.parent_computed = parent_map;
                entry.reads = std::move(again.reads);
                entry.computed.style = again.style;
                entry.computed.pseudo = again.pseudo;
                entry.computed.selection = again.selection;
                entry.computed.backdrop = again.backdrop;
                memo_clock.stop();
                child_pos.parent_sig = share_sig(key);
                cascade_children(node, doc, in, result, cache, child_pos);
                if (in.author_counters != nullptr) {
                    in.author_counters->pop_resets(author_pushed);
                }
                if (pushed_list_scope && in.list_counters != nullptr) {
                    in.list_counters->pop_scope();
                }
                return;
            }
        }
        perf::note_cascade_memo_miss();
    }

    // An inline `style` does not change which stylesheet rules match. Reuse the pristine
    // declaration set from a node with the same share key, then apply this attribute on top.
    // The cached computed style stays the stylesheet-only result so a later node without an
    // attribute still commits that map.
    if (in.allow_cache && has_style_attr) {
        CascadeAcc name_clock(cascade_split_armed() ? &g_cascade_split.name_ms : nullptr);
        const auto hit = cache.find(key);
        name_clock.stop();
        if (hit != cache.end() && hit->second.payload != nullptr) {
            if (in.stats != nullptr) {
                ++in.stats->cache_hits;
            }
            CascadeAcc body_clock(cascade_split_armed() ? &g_cascade_split.body_ms : nullptr);
            DeclSinks sinks = hit->second.payload->sinks;
            body_clock.stop();
            const LayerTree* layers = in.layers;
            const int unlayered = layers != nullptr ? layers->unlayered() : 0;
            int order = 0;
            apply_inline_style(style_attr->second, order, sinks, unlayered);
            const StyleMap* inherited =
                node->parent != nullptr ? node->parent->computed.get() : nullptr;
            Finalized fin;
            finalize_payload(sinks, inherited, node, in.media, fin);
            commit_computed(result, node, fin.style);
            std::vector<std::string> author_pushed;
            if (in.author_counters != nullptr) {
                author_pushed = in.author_counters->apply_reset(fin.style.get());
                in.author_counters->apply_increment(fin.style.get());
            }
            int list_ordinal = 0;
            if (in.list_counters != nullptr && html::is_list_item(node)) {
                list_ordinal = in.list_counters->increment();
                if (node->tag == "li") {
                    const auto vit = node->attrs.find("value");
                    if (vit != node->attrs.end()) {
                        const int set_to = parse_html_integer(vit->second, list_ordinal);
                        in.list_counters->set(set_to);
                        list_ordinal = set_to;
                    }
                }
            }
            commit_pseudos(result, node, fin.pseudo, list_ordinal, in.author_counters);
            commit_selection(node, fin.selection);
            commit_backdrop(node, fin.backdrop);
            if (in.memo != nullptr) {
                CascadeAcc memo_clock(cascade_split_armed() ? &g_cascade_split.memo_ms : nullptr);
                auto payload = std::make_shared<MemoPayload>();
                payload->sinks = std::move(sinks);
                payload->taken = fin.taken;
                payload->parent_name_sig = parent_name_sig(inherited);
                payload->reads = fin.reads;
                payload->reads_pseudo = fin.reads_pseudo;
                MemoEntry& entry = in.memo->nodes[node];
                entry.key.assign(cross);
                entry.sheets_gen = in.memo->sheets_gen;
                entry.env_gen = in.memo->env_gen;
                entry.parent_computed = inherited;
                entry.payload = std::move(payload);
                entry.taken = fin.taken;
                entry.parent_name_sig = entry.payload->parent_name_sig;
                entry.reads = fin.reads;
                entry.reads_pseudo = fin.reads_pseudo;
                entry.computed.style = fin.style;
                entry.computed.pseudo = fin.pseudo;
                entry.computed.selection = fin.selection;
                entry.computed.backdrop = fin.backdrop;
                remember_style_attr(entry, true, style_attr->second);
            }
            child_pos.parent_sig = share_sig(key);
            cascade_children(node, doc, in, result, cache, child_pos);
            if (in.author_counters != nullptr) {
                in.author_counters->pop_resets(author_pushed);
            }
            if (pushed_list_scope && in.list_counters != nullptr) {
                in.list_counters->pop_scope();
            }
            return;
        }
    }

    if (in.allow_cache && !has_style_attr) {
        CascadeAcc name_clock(cascade_split_armed() ? &g_cascade_split.name_ms : nullptr);
        const auto hit = cache.find(key);
        name_clock.stop();
        if (hit != cache.end() && hit->second.computed.style != nullptr) {
            if (in.stats != nullptr) {
                ++in.stats->cache_hits;
            }
            commit_computed(result, node, hit->second.computed.style);
            // Order: reset -> increment -> set (CSS Lists section 4).
            std::vector<std::string> author_pushed;
            if (in.author_counters != nullptr) {
                author_pushed = in.author_counters->apply_reset(hit->second.computed.style.get());
                in.author_counters->apply_increment(hit->second.computed.style.get());
            }
            int list_ordinal = 0;
            if (in.list_counters != nullptr && html::is_list_item(node)) {
                list_ordinal = in.list_counters->increment();
                if (node->tag == "li") {
                    const auto vit = node->attrs.find("value");
                    if (vit != node->attrs.end()) {
                        const int set_to = parse_html_integer(vit->second, list_ordinal);
                        in.list_counters->set(set_to);
                        list_ordinal = set_to;
                    }
                }
            }
            commit_pseudos(result, node, hit->second.computed.pseudo, list_ordinal,
                           in.author_counters);
            // ShareKey hit still merges parent ::selection (contribution alone is share-safe).
            commit_selection(node, hit->second.computed.selection);
            commit_backdrop(node, hit->second.computed.backdrop);
            // The node never ran its own rule list; give it the entry the share hit was built on so
            // the next frame can key it without touching the rule list either.
            if (in.memo != nullptr) {
                CascadeAcc memo_clock(cascade_split_armed() ? &g_cascade_split.memo_ms : nullptr);
                MemoEntry& entry = in.memo->nodes[node];
                entry.key.assign(cross);
                entry.sheets_gen = in.memo->sheets_gen;
                entry.env_gen = in.memo->env_gen;
                entry.parent_computed =
                    node->parent != nullptr ? node->parent->computed.get() : nullptr;
                entry.payload = hit->second.payload;
                entry.taken = hit->second.payload->taken;
                entry.parent_name_sig = hit->second.payload->parent_name_sig;
                entry.reads = hit->second.payload->reads;
                entry.reads_pseudo = hit->second.payload->reads_pseudo;
                entry.computed = hit->second.computed;
                remember_style_attr(entry, false, {});
            }
            // Descendants of display:none still have computed values. The box tree
            // does not generate boxes for them; skipping the cascade left hidden
            // controls at initial position and display.
            child_pos.parent_sig = share_sig(key);
            cascade_children(node, doc, in, result, cache, child_pos);
            if (in.author_counters != nullptr) {
                in.author_counters->pop_resets(author_pushed);
            }
            if (pushed_list_scope && in.list_counters != nullptr) {
                in.list_counters->pop_scope();
            }
            return;
        }
    }
    if (in.stats != nullptr) {
        ++in.stats->cache_misses;
    }
    // The rule list runs for every node that reaches this point: a cross-frame key miss, a share
    // cache miss, or a node with an inline `style`. docs/PERF_REFACTOR.md stage 0 records the total
    // (R0); capability A (stage 2) is what lowers it.
    perf::note_cascade_rule_run();

    CascadeAcc sink_clock(cascade_split_armed() ? &g_cascade_split.sink_ms : nullptr);
    DeclSinks sinks;
    sink_clock.stop();
    int order = 0;
    const LayerTree* layers = in.layers;
    const int unlayered = layers != nullptr ? layers->unlayered() : 0;
    if (in.rule_index != nullptr) {
        apply_indexed(*in.rule_index, node, doc, order, sinks);
    }
    if (has_style_attr && in.allow_cache && cache.find(key) == cache.end()) {
        auto sheet = std::make_shared<MemoPayload>();
        sheet->sinks = sinks;
        ShareEntry sheet_entry;
        sheet_entry.payload = std::move(sheet);
        cache.emplace(key, std::move(sheet_entry));
    }
    if (has_style_attr) {
        apply_inline_style(style_attr->second, order, sinks, unlayered);
    }
    const StyleMap* inherited = node->parent != nullptr ? node->parent->computed.get() : nullptr;
    Finalized fin;
    CascadeAcc body_clock(cascade_split_armed() ? &g_cascade_split.body_ms : nullptr);
    auto payload = std::make_shared<MemoPayload>();
    payload->sinks = std::move(sinks);
    body_clock.stop();
    finalize_payload(payload->sinks, inherited, node, in.media, fin);
    payload->taken = fin.taken;
    payload->parent_name_sig = parent_name_sig(inherited);
    payload->reads = fin.reads;
    payload->reads_pseudo = fin.reads_pseudo;
    commit_computed(result, node, fin.style);
    std::vector<std::string> author_pushed;
    if (in.author_counters != nullptr) {
        author_pushed = in.author_counters->apply_reset(fin.style.get());
        in.author_counters->apply_increment(fin.style.get());
    }
    int list_ordinal = 0;
    if (in.list_counters != nullptr && html::is_list_item(node)) {
        list_ordinal = in.list_counters->increment();
        if (node->tag == "li") {
            const auto vit = node->attrs.find("value");
            if (vit != node->attrs.end()) {
                const int set_to = parse_html_integer(vit->second, list_ordinal);
                in.list_counters->set(set_to);
                list_ordinal = set_to;
            }
        }
    }
    commit_pseudos(result, node, fin.pseudo, list_ordinal, in.author_counters);
    commit_selection(node, fin.selection);
    commit_backdrop(node, fin.backdrop);
    if (in.allow_cache && !has_style_attr) {
        ShareEntry entry;
        entry.payload = payload;
        entry.computed.style = fin.style;
        entry.computed.pseudo = fin.pseudo;
        entry.computed.selection = fin.selection;
        entry.computed.backdrop = fin.backdrop;
        cache.emplace(key, std::move(entry));
    }
    if (in.memo != nullptr) {
        CascadeAcc memo_clock(cascade_split_armed() ? &g_cascade_split.memo_ms : nullptr);
        MemoEntry& entry = in.memo->nodes[node];
        entry.key.assign(cross);
        entry.sheets_gen = in.memo->sheets_gen;
        entry.env_gen = in.memo->env_gen;
        entry.parent_computed = inherited;
        entry.payload = payload;
        entry.taken = fin.taken;
        entry.parent_name_sig = payload->parent_name_sig;
        entry.reads = std::move(fin.reads);
        entry.reads_pseudo = fin.reads_pseudo;
        entry.computed.style = fin.style;
        entry.computed.pseudo = fin.pseudo;
        entry.computed.selection = fin.selection;
        entry.computed.backdrop = fin.backdrop;
        remember_style_attr(entry, has_style_attr,
                            has_style_attr ? std::string_view(style_attr->second)
                                           : std::string_view{});
    }
    child_pos.parent_sig = share_sig(key);
    cascade_children(node, doc, in, result, cache, child_pos);
    if (in.author_counters != nullptr) {
        in.author_counters->pop_resets(author_pushed);
    }
    if (pushed_list_scope && in.list_counters != nullptr) {
        in.list_counters->pop_scope();
    }
}

}  // namespace detail
}  // namespace css
}  // namespace zui
