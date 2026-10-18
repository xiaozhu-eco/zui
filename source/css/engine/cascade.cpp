// The cascade entry: the dirty-subtree closure and the `Engine::cascade` overloads that drive
// the phase walk in `cascade_walk.cpp`.
// The Engine's own surface: the user-agent stylesheet, sheet load / theme replacement, the
// selector facts and closure bound derived from the loaded sheets, and the cascade entry point
// that drives `cascade_walk.cpp`. The cascade phases themselves live in the sibling files:
// selector_match, rule_cascade, computed_style, style_diff, generated_content, selection_style.

#include "css/engine/engine.h"
#include "css/engine/cascade_internal.h"
#include "css/engine/http_fetch.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include "perf/perf_budget.h"
#include "log/log.h"
#include "trace/trace.h"

#include <lexbor/css/at_rule.h>
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/pseudo_const.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {

// The cascade phases live in sibling translation units (selector matching, rule cascade,
// computed style, generated content, selection, the tree walk); their internal API is declared
// in cascade_internal.h.
using namespace detail;

namespace {

/// `env()` inputs and the media features that are not the viewport size. Width and height are
/// absent on purpose: `vw` / `vh` resolve at layout intern, and an `@media` group that flips is
/// diffed per node against the selectors it matched. Hashing the pixel size here used to throw
/// away every memo entry on the first real viewport.
uint64_t media_signature(const MediaState& media) {
    uint64_t sig = 0;
    auto mix = [](uint64_t& h, uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    auto mix_float = [&mix](uint64_t& h, float v) {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "the signature wants one 32-bit field per float");
        std::memcpy(&bits, &v, sizeof(bits));
        mix(h, bits);
    };
    mix(sig, media.hover ? 1u : 0u);
    mix(sig, media.any_hover ? 1u : 0u);
    mix(sig, static_cast<uint64_t>(media.pointer));
    mix(sig, static_cast<uint64_t>(media.any_pointer));
    mix(sig, media.reduced_motion ? 1u : 0u);
    mix(sig, static_cast<uint64_t>(media.color_scheme));
    mix_float(sig, media.safe_top);
    mix_float(sig, media.safe_right);
    mix_float(sig, media.safe_bottom);
    mix_float(sig, media.safe_left);
    mix_float(sig, media.keyboard_bottom);
    return sig;
}

/// The published query-container sizes of the document, folded into one signature
/// (docs/PERF_REFACTOR.md capability A: an `@container` match reads them, and layout is what
/// writes them). Only walked for a page whose sheets carry an `@container` rule.
uint64_t container_signature(const html::Node* root) {
    uint64_t sig = 0;
    if (root == nullptr) {
        return sig;
    }
    std::vector<const html::Node*> stack{root};
    while (!stack.empty()) {
        const html::Node* node = stack.back();
        stack.pop_back();
        if (node->query_size_known) {
            uint32_t w_bits = 0;
            uint32_t h_bits = 0;
            std::memcpy(&w_bits, &node->query_width, sizeof(w_bits));
            std::memcpy(&h_bits, &node->query_height, sizeof(h_bits));
            sig ^= reinterpret_cast<uint64_t>(node) + 0x9e3779b97f4a7c15ull + (sig << 6) +
                   (sig >> 2);
            sig ^= static_cast<uint64_t>(w_bits) + 0x9e3779b97f4a7c15ull + (sig << 6) +
                   (sig >> 2);
            sig ^= static_cast<uint64_t>(h_bits) + 0x9e3779b97f4a7c15ull + (sig << 6) +
                   (sig >> 2);
        }
        for (const auto& child : node->children) {
            if (child != nullptr) {
                stack.push_back(child.get());
            }
        }
    }
    return sig;
}

}  // namespace

std::unordered_set<html::Node*> Engine::closure_nodes(
    const html::Document& doc, const std::vector<html::Node*>& dirty_subtrees,
    bool structural) const {
    std::unordered_set<html::Node*> out;
    if (doc.root == nullptr) {
        return out;
    }
    // `walked` is the set whose subtree was expanded. Ancestors (`:focus-within`) and radio peers
    // (`:indeterminate`) are inserted into `out` alone: treating them as walked would skip a later
    // dirty root that is one of those nodes and drop the rest of its subtree.
    std::unordered_set<html::Node*> walked;
    const auto add_subtree = [&out, &walked](html::Node* root) {
        if (root == nullptr) {
            return;
        }
        std::vector<html::Node*> stack{root};
        while (!stack.empty()) {
            html::Node* n = stack.back();
            stack.pop_back();
            if (html::is_element(n)) {
                out.insert(n);
                walked.insert(n);
            }
            for (const auto& ch : n->children) {
                if (ch != nullptr) {
                    stack.push_back(ch.get());
                }
            }
        }
    };
    const auto add_same_name_radios = [&out, &doc](const html::Node* radio) {
        const auto named = radio->attrs.find("name");
        // An empty or missing name is not a radio group (HTML). Peers must not restyle together.
        if (named == radio->attrs.end() || named->second.empty()) {
            return;
        }
        const std::string& name = named->second;
        std::vector<html::Node*> stack{doc.root.get()};
        while (!stack.empty()) {
            html::Node* cur = stack.back();
            stack.pop_back();
            if (html::is_radio(cur)) {
                std::string other;
                const auto other_named = cur->attrs.find("name");
                if (other_named != cur->attrs.end()) {
                    other = other_named->second;
                }
                if (other == name) {
                    out.insert(cur);
                }
            }
            for (const auto& ch : cur->children) {
                if (ch != nullptr) {
                    stack.push_back(ch.get());
                }
            }
        }
    };
    // A selector or counter that can see outside the changed subtree makes the closure the whole
    // document. Correct, just not tight (docs/PERF.md section 2.4).
    if (closure_global_) {
        add_subtree(doc.root.get());
        return out;
    }
    // Anchors whose subtree has already been added, for the `:has()` branch below.
    std::unordered_set<html::Node*> added_anchors;
    // Ids that sit inside this write. An id-only `:has()` expands its anchor only when one of
    // those ids is here, or when a structural write removed the id from under the anchor.
    std::unordered_set<std::string_view> dirty_ids;
    {
        std::vector<html::Node*> id_stack;
        id_stack.reserve(dirty_subtrees.size());
        for (html::Node* root : dirty_subtrees) {
            if (root != nullptr) {
                id_stack.push_back(root);
            }
        }
        while (!id_stack.empty()) {
            html::Node* cur = id_stack.back();
            id_stack.pop_back();
            if (html::is_element(cur) && !cur->id.empty()) {
                dirty_ids.insert(cur->id);
            }
            for (const auto& ch : cur->children) {
                if (ch != nullptr) {
                    id_stack.push_back(ch.get());
                }
            }
        }
    }
    const auto id_in_document = [&doc](std::string_view id) {
        return doc.ids.find(std::string(id)) != doc.ids.end();
    };
    const auto edge_matches = [](const html::Node& node, const SiblingEdge& edge) {
        if (edge.any) {
            return true;
        }
        if (!edge.tag.empty() && edge.tag != node.tag) {
            return false;
        }
        if (!edge.id.empty() && edge.id != node.id) {
            return false;
        }
        for (std::string_view cls : edge.classes) {
            if (!html::has_class_token(node.class_name, cls)) {
                return false;
            }
        }
        return true;
    };
    // An id-only `:has()` flips only at the named element. Any other argument can flip from a
    // change anywhere under the anchor, which is the previous bound.
    const auto anchor_expands = [&](const html::Node& anchor, html::Node* dirty_root) {
        for (const HasAnchor& spec : has_anchors_) {
            if (spec.any) {
                return true;
            }
            if (!spec.tag.empty() && spec.tag != anchor.tag) {
                continue;
            }
            if (!spec.id.empty() && spec.id != anchor.id) {
                continue;
            }
            bool all_classes = true;
            for (std::string_view cls : spec.classes) {
                if (!html::has_class_token(anchor.class_name, cls)) {
                    all_classes = false;
                    break;
                }
            }
            if (!all_classes || !facets_match(anchor, spec.tests)) {
                continue;
            }
            if (!spec.argument_id_only) {
                return true;
            }
            for (std::string_view id : spec.argument_ids) {
                if (dirty_ids.count(id) != 0) {
                    return true;
                }
                // The id is gone, and this write is what could have removed it from under the
                // anchor. A class or hover change that leaves the id where it is does not.
                if (structural && !id_in_document(id)) {
                    for (html::Node* p = dirty_root; p != nullptr; p = p->parent) {
                        if (p == &anchor) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    };
    for (html::Node* n : dirty_subtrees) {
        // An element already in `walked` was reached by an earlier root's `add_subtree`, which walked
        // that whole subtree - including everything below this one. Re-walking it adds nothing, and
        // the `:has()` / sibling / `:nth-*` / radio branches below were already run for those
        // elements too (the earlier walk visits every element of the subtree it added). A node that
        // is only in `out` (an ancestor, a radio peer) has not been expanded. Overlapping roots
        // happen for one write: a `set_model` marks the host, the cascade marks rows inside it.
        if (n != nullptr && html::is_element(n) && walked.count(n) != 0) {
            continue;
        }
        add_subtree(n);
        // `:focus-within` matches this node and every ancestor. Descendants of those ancestors
        // that are not under `n` do not match, so they stay out of the closure.
        if (closure_focus_within_ && n != nullptr) {
            for (html::Node* a = n->parent; a != nullptr; a = a->parent) {
                if (html::is_element(a)) {
                    out.insert(a);
                }
            }
        }
        // `:dir()` on `dir=auto` (and on a `bdi` with no valid dir) reads descendant text. The
        // nearest such ancestor is the one whose directionality can flip; descendants that inherit
        // from it sit in its subtree. An ancestor above that auto element does not see the text
        // (the auto element is excluded from its walk), so the walk stops there.
        if (selector_uses_dir_ && n != nullptr) {
            for (html::Node* a = n; a != nullptr; a = a->parent) {
                if (!html::is_element(a)) {
                    continue;
                }
                if (direction_from_contents(*a)) {
                    add_subtree(a);
                    break;
                }
            }
        }
        // A radio's `:indeterminate` flips for the whole name group, including peers outside the
        // dirty subtree. The matcher still filters by form owner; an extra peer restyle is cheap.
        if (closure_indeterminate_ && n != nullptr) {
            std::unordered_set<std::string> seen_names;
            std::vector<html::Node*> scan{n};
            while (!scan.empty()) {
                html::Node* cur = scan.back();
                scan.pop_back();
                if (html::is_radio(cur)) {
                    std::string name;
                    const auto named = cur->attrs.find("name");
                    if (named != cur->attrs.end()) {
                        name = named->second;
                    }
                    if (seen_names.insert(name).second) {
                        add_same_name_radios(cur);
                    }
                }
                for (const auto& ch : cur->children) {
                    if (ch != nullptr) {
                        scan.push_back(ch.get());
                    }
                }
            }
        }
        // `:nth-*(An+B of S)`: a sibling's class/attribute change shifts how many siblings match
        // `S`, which re-matches the siblings after it. The dependency is parent-local.
        if (closure_nth_of_ && n != nullptr && n->parent != nullptr) {
            add_subtree(n->parent);
        }
        // `+` / `~`: only a node that matches the left compound pulls the siblings after it.
        // `#theme:checked ~ .app` does not pull the siblings of a day cell. Every element of the
        // written subtree is tested, because a structural write can add or remove the left side
        // inside the root; following siblings outside that subtree are the part `add_subtree`
        // has not already covered.
        if (!sibling_edges_.empty() && n != nullptr) {
            std::vector<html::Node*> scan{n};
            while (!scan.empty()) {
                html::Node* cur = scan.back();
                scan.pop_back();
                if (html::is_element(cur) && cur->parent != nullptr) {
                    for (const SiblingEdge& edge : sibling_edges_) {
                        if (!edge_matches(*cur, edge)) {
                            continue;
                        }
                        bool after = false;
                        for (const auto& sibling : cur->parent->children) {
                            if (sibling.get() == cur) {
                                after = true;
                                continue;
                            }
                            if (!after) {
                                continue;
                            }
                            if (walked.count(sibling.get()) == 0) {
                                add_subtree(sibling.get());
                            }
                            if (!edge.following) {
                                break;
                            }
                        }
                    }
                }
                for (const auto& ch : cur->children) {
                    if (ch != nullptr) {
                        scan.push_back(ch.get());
                    }
                }
            }
        }
        // `:has()`: the function can only flip at an anchor that is the written node, a node inside
        // the written subtree (a structure write can add or remove the node the argument looks at),
        // or an ancestor of one of those. Every matching anchor's subtree is in the closure; a
        // write with no matching anchor adds nothing here.
        //
        // Every element of the written subtree asks about its whole ancestor chain, because the
        // write can have flipped `:has()` at any of them. Adding a match's subtree is the expensive
        // half, so each anchor is expanded once: a match found while walking one element is still a
        // match for the next one in this same write, and re-adding it re-walks the whole subtree.
        // `example/applications/reader`'s `:root:has(...)` theme and size rules anchor at the root,
        // so one `set_model` on the 2063-row contents host used to re-walk the document 2063 times
        // (307 ms of a 620 ms frame).
        if (!has_anchors_.empty() && n != nullptr) {
            std::vector<html::Node*> stack{n};
            while (!stack.empty()) {
                html::Node* cur = stack.back();
                stack.pop_back();
                if (!html::is_element(cur)) {
                    continue;
                }
                for (html::Node* a = cur; a != nullptr; a = a->parent) {
                    if (added_anchors.count(a) != 0) {
                        continue;
                    }
                    if (anchor_expands(*a, n) && added_anchors.insert(a).second) {
                        add_subtree(a);
                    }
                }
                for (const auto& ch : cur->children) {
                    if (ch != nullptr) {
                        stack.push_back(ch.get());
                    }
                }
            }
        }
    }
    return out;
}

Engine::CascadeResult Engine::cascade(html::Document& doc, float viewport_width,
                                      float viewport_height) {
    return cascade(doc, viewport_width, viewport_height, HostMediaFacts{});
}

Engine::CascadeResult Engine::cascade(html::Document& doc, float viewport_width,
                                      float viewport_height, const HostMediaFacts& host) {
    return cascade(doc, viewport_width, viewport_height, host, nullptr);
}

Engine::CascadeResult Engine::cascade(html::Document& doc, float viewport_width,
                                      float viewport_height, const HostMediaFacts& host,
                                      const std::unordered_set<html::Node*>* closure) {
    CascadeResult result;
    perf::note_cascade_round();
    if (doc.root == nullptr) {
        return result;
    }
    const auto cascade_started = std::chrono::steady_clock::now();
    ShareCache cache;
    MediaState media;
    media.width = viewport_width;
    media.height = viewport_height;
    media.hover = host.hover;
    media.any_hover = host.any_hover;
    media.pointer = host.pointer;
    media.any_pointer = host.any_pointer;
    media.reduced_motion = host.reduced_motion;
    media.color_scheme = host.color_scheme;
    media.safe_top = host.safe_top;
    media.safe_right = host.safe_right;
    media.safe_bottom = host.safe_bottom;
    media.safe_left = host.safe_left;
    media.keyboard_bottom = host.keyboard_bottom;
    // Capability A: the environment generation. `env()` and `@container` sizes cold-start every
    // entry. Viewport width and height do not: the rule index below says which `@media` groups
    // matched, and only the nodes those selectors name are stale.
    bool env_bumped = false;
    if (memo_ != nullptr) {
        uint64_t env_sig = media_signature(media);
        if (has_container_rules_) {
            // A published container size is an `@container` input. Layout writes it, so the
            // signature moves on the frame after a resize without the caller having to say so.
            env_sig ^= container_signature(doc.root.get()) + 0x9e3779b97f4a7c15ull + (env_sig << 6) +
                       (env_sig >> 2);
        }
        if (memo_->media_sig_known && memo_->media_sig != env_sig) {
            memo_->bump_env();
            perf::note_cascade_env_gen_bump();
            env_bumped = true;
        }
        memo_->media_sig = env_sig;
        memo_->media_sig_known = true;
    }
    // ShareCache is rebuilt for every cascade() call and only merges nodes that share a
    // ShareKey within that pass. Document-global :has() patterns such as
    // :root:has(#size_s:checked) .flow evaluate the same for every sibling, so keeping the
    // cache on is correct for them and is what keeps a long chapter's hundreds of .para
    // nodes from re-matching the whole stylesheet one-by-one (reader scroll was ~220ms
    // cascade per frame with this forced off). Relative :has() on the subject is fingerprinted
    // into ShareKey::has_sig via CascadeIn::subject_has.
    bool allow_cache = true;
    CascadeStats stats;
    g_has_calls = 0;
    g_cascade_split.reset();
    // Layer order first (CSS Cascade 5 section 6.4.4), then one pass over the sheets to fill in the
    // declaration sets: a rule's layer has to be known before it is compared against another.
    LayerTree layers;
    // Lexbor's prelude offsets address the buffer it tokenized. `@keyframes` is stripped before
    // parse, so `@media` that follows `@keyframes` must be sliced from `parsed_sources_`.
    // `sources_` stays the original text for the animation sampler.
    const std::vector<std::string>& token_text =
        parsed_sources_.size() == sources_.size() ? parsed_sources_ : sources_;
    if (has_layers_) {
        for (size_t i = 0; i < sheets_.size(); ++i) {
            lxb_css_stylesheet* sheet = sheets_[i];
            if (sheet == nullptr || sheet->root == nullptr) {
                continue;
            }
            const std::string_view source =
                i < token_text.size() ? std::string_view(token_text[i]) : std::string_view{};
            collect_layer_order(sheet->root, layers, 0, source, media);
        }
        finish_layers(layers);
    }
    ListItemCounterState list_counters;
    AuthorCounterState author_counters;
    // Ancestors of the closure have to be walked so a descendant still matches. Every other
    // child is outside the write and outside every selector that could see it.
    std::unordered_set<html::Node*> walk_storage;
    if (closure != nullptr) {
        walk_storage.reserve(closure->size() * 2 + 8);
        for (html::Node* node : *closure) {
            for (html::Node* a = node; a != nullptr; a = a->parent) {
                if (!walk_storage.insert(a).second) {
                    break;
                }
            }
        }
    }
    // Built once per round. `@media` / `@supports` that miss this frame never become rules, and
    // each node only matches the buckets its id, classes, and tag can name.
    bool record_history = false;
    const auto pre_started = std::chrono::steady_clock::now();
    for (const std::string& sheet : token_text) {
        if (sheet.find("revert") != std::string::npos) {
            record_history = true;
            break;
        }
    }
    if (!record_history) {
        std::vector<const html::Node*> pending;
        pending.push_back(doc.root.get());
        while (!pending.empty()) {
            const html::Node* node = pending.back();
            pending.pop_back();
            if (node == nullptr) {
                continue;
            }
            static const std::string style_key("style");
            const auto style_attr = node->attrs.find(style_key);
            if (style_attr != node->attrs.end() &&
                style_attr->second.find("revert") != std::string::npos) {
                record_history = true;
                break;
            }
            for (const auto& child : node->children) {
                pending.push_back(child.get());
            }
        }
    }
    g_cascade_split.pre_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pre_started)
            .count();
    reset_decl_expansion(record_history);
    const auto index_started = std::chrono::steady_clock::now();
    const RuleIndex rule_index = build_rule_index(sheets_, token_text, media, layers);
    g_cascade_split.index_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - index_started)
            .count();
    // Selectors admitted by `@media` this frame. Compared with the previous cascade when the
    // environment generation did not move, so a breakpoint only restyles the nodes it names.
    std::vector<const lxb_css_selector_list_t*> media_active;
    std::vector<const IndexedRule*> media_born;
    const bool reuse_media = memo_ != nullptr && memo_->media_known && !env_bumped;
    if (memo_ != nullptr) {
        media_active.reserve(rule_index.rules.size());
        for (const IndexedRule& rule : rule_index.rules) {
            if (rule.from_media && rule.only != nullptr) {
                media_active.push_back(rule.only);
            }
        }
        std::sort(media_active.begin(), media_active.end());
        media_active.erase(std::unique(media_active.begin(), media_active.end()), media_active.end());
        if (reuse_media) {
            for (const IndexedRule& rule : rule_index.rules) {
                if (!rule.from_media || rule.only == nullptr) {
                    continue;
                }
                if (!std::binary_search(memo_->last_media.begin(), memo_->last_media.end(),
                                        rule.only)) {
                    media_born.push_back(&rule);
                }
            }
            memo_->active_media = &media_active;
            memo_->born_media = &media_born;
        }
    }
    struct ClearMediaHold {
        CascadeMemo* memo = nullptr;
        ~ClearMediaHold() {
            if (memo != nullptr) {
                memo->active_media = nullptr;
                memo->born_media = nullptr;
            }
        }
    } clear_media_hold{memo_ != nullptr ? memo_.get() : nullptr};
    const CascadeIn in{sheets_,
                       token_text,
                       media,
                       allow_cache,
                       &stats,
                       &selector_ids_,
                       selector_tests_id_attribute_,
                       &selector_attrs_,
                       &subject_has_,
                       selector_uses_nth_,
                       selector_uses_nth_last_,
                       &nth_of_lists_,
                       &layers,
                       &list_counters,
                       &author_counters,
                       closure,
                       memo_active() ? memo_.get() : nullptr,
                       ancestor_facts_.get(),
                       sibling_facts_.get(),
                       selector_uses_lang_,
                       selector_uses_dir_,
                       closure != nullptr ? &walk_storage : nullptr,
                       &rule_index,
                       nth_patterns_.empty() ? nullptr : &nth_patterns_,
                       selector_html_state_bits_,
                       &hover_targets_,
                       &active_targets_};
    WalkPos root_pos;
    // One id index for every `:has(#id)` this pass evaluates, including the share-key walk.
    // Pages with no `:has()` skip the scan so a small cold frame does not pay for a map it
    // will never query.
    const bool sheets_use_has =
        !subject_has_.empty() ||
        (ancestor_facts_ != nullptr && !ancestor_facts_->has_lists.empty()) ||
        (sibling_facts_ != nullptr && !sibling_facts_->has_lists.empty());
    std::optional<HasIdIndex> has_ids;
    if (sheets_use_has) {
        has_ids.emplace(doc);
    }
    cascade_node(doc.root.get(), doc, in, result, cache, root_pos);
    if (memo_ != nullptr) {
        memo_->active_media = nullptr;
        memo_->born_media = nullptr;
        memo_->last_media = std::move(media_active);
        memo_->media_known = true;
    }
    has_calls_ = g_has_calls;
    // `cascade.nodes` is a work unit: `perf::note_cascade_visited` counts it, and the recorder
    // mirrors the window total at publish (docs/PERF.md section 6.1). Cache quality stays here,
    // one point per cascade round.
    if (zui::trace::active()) {
        zui::trace::counter("cascade.cache_hits", stats.cache_hits);
        zui::trace::counter("cascade.cache_misses", stats.cache_misses);
        zui::trace::counter("cascade.has_calls", static_cast<double>(has_calls_));
        if (stats.nodes > 0) {
            zui::trace::counter("cascade.cache_hit_pct", 100.0 *
                                                             static_cast<double>(stats.cache_hits) /
                                                             static_cast<double>(stats.nodes));
        }
    }
    // Console-readable cascade summary, opt-in through `ZUI_LOG=trace`. `-1` marks a round that
    // cascaded without a named closure (whole document). One line per round, not per node.
    const double cascade_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cascade_started)
            .count();
    ZUI_TRACE("[cascade] ms={:.1f} nodes={} hits={} misses={} has_calls={} closure={} index_rules={} universal={}"
              " pre={:.1f} index={:.1f} key={:.1f} match={:.1f} fin={:.1f}"
              " copy={:.1f} put={:.1f} var={:.1f} inh={:.1f} rest={:.1f} pseudo={:.1f}"
              " commit={:.1f} gen={:.1f} find={:.1f} memo={:.1f}"
              " share={:.1f} name={:.1f} prenode={:.1f} mrg={:.1f} mrest={:.1f} triv={}"
              " cmp={} desc={} oth={} sink={:.1f} body={:.1f} decl={:.1f} exp={:.1f} dput={:.1f} shw={:.1f}",
              cascade_ms, stats.nodes, stats.cache_hits, stats.cache_misses, has_calls_,
              closure != nullptr ? static_cast<int>(closure->size()) : -1, rule_index.rules.size(),
              rule_index.universal.size(), g_cascade_split.pre_ms, g_cascade_split.index_ms,
              g_cascade_split.key_ms, g_cascade_split.match_ms, g_cascade_split.fin_ms,
              g_cascade_split.fin_copy, g_cascade_split.fin_put, g_cascade_split.fin_var,
              g_cascade_split.fin_inh, g_cascade_split.fin_rest, g_cascade_split.fin_pseudo,
              g_cascade_split.commit_ms, g_cascade_split.pseudo_ms, g_cascade_split.find_ms,
              g_cascade_split.memo_ms, g_cascade_split.share_ms, g_cascade_split.name_ms,
              g_cascade_split.pre_node_ms, g_cascade_split.match_merge,
              g_cascade_split.match_rest, g_cascade_split.trivial, g_cascade_split.n_compound,
              g_cascade_split.n_desc, g_cascade_split.n_other, g_cascade_split.sink_ms,
              g_cascade_split.body_ms, g_cascade_split.decl_ms, g_cascade_split.exp_ms,
              g_cascade_split.dput_ms, g_cascade_split.shape_walk_ms);
    return result;
}

}  // namespace css
}  // namespace zui
