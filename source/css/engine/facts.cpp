// The selector facts derived from the loaded sheets and the closure bound they set: the hover
// targets, the `:has()` anchors and the id / attribute / nth-dependency sets (docs/PERF.md).
// The Engine's own surface: the user-agent stylesheet, sheet load / theme replacement, the
// selector facts and closure bound derived from the loaded sheets, and the cascade entry point
// that drives `cascade_walk.cpp`. The cascade phases themselves live in the sibling files:
// selector_match, rule_cascade, computed_style, style_diff, generated_content, selection_style.

#include "css/engine/engine.h"
#include "css/engine/cascade_internal.h"
#include "css/engine/facts_internal.h"
#include "css/engine/http_fetch.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include "perf/perf_budget.h"
#include "trace/trace.h"

#include <lexbor/css/at_rule.h>
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/pseudo_const.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

bool state_targets_match(const std::vector<HoverTarget>& targets, const html::Node& node) {
    for (const HoverTarget& target : targets) {
        if (target.any) {
            return true;
        }
        if (!target.tag.empty() && target.tag != node.tag) {
            continue;
        }
        if (!target.id.empty() && target.id != node.id) {
            continue;
        }
        bool all_classes = true;
        for (std::string_view cls : target.classes) {
            if (!html::has_class_token(node.class_name, cls)) {
                all_classes = false;
                break;
            }
        }
        if (!all_classes) {
            continue;
        }
        if (facets_match(node, target.tests)) {
            return true;
        }
    }
    return false;
}

}  // namespace

uint32_t detail::key_state_bits(const html::Node& node, uint32_t bits,
                                const std::vector<HoverTarget>* hover,
                                const std::vector<HoverTarget>* active) {
    if ((bits & kBitHover) != 0 && (hover == nullptr || !state_targets_match(*hover, node))) {
        bits &= ~kBitHover;
    }
    if ((bits & kBitActive) != 0 && (active == nullptr || !state_targets_match(*active, node))) {
        bits &= ~kBitActive;
    }
    return bits;
}

bool Engine::hover_may_change_style(const html::Node& node) const {
    return state_targets_match(hover_targets_, node);
}

bool Engine::active_may_change_style(const html::Node& node) const {
    return state_targets_match(active_targets_, node);
}

bool Engine::has_anchor_matches(const html::Node& node) const {
    for (const HasAnchor& anchor : has_anchors_) {
        if (anchor.any) {
            return true;
        }
        if (!anchor.tag.empty() && anchor.tag != node.tag) {
            continue;
        }
        if (!anchor.id.empty() && anchor.id != node.id) {
            continue;
        }
        bool all_classes = true;
        for (std::string_view cls : anchor.classes) {
            if (!html::has_class_token(node.class_name, cls)) {
                all_classes = false;
                break;
            }
        }
        if (all_classes && facets_match(node, anchor.tests)) {
            return true;
        }
    }
    return false;
}


void collect_selector_facts(const lxb_css_selector_t* sel, SelectorFacts& facts) {
    note_lateral_has(sel, facts);
    collect_sibling_edges(sel, facts);
    for (; sel != nullptr; sel = sel->prev) {
        if (sel->combinator == LXB_CSS_SELECTOR_COMBINATOR_SIBLING ||
            sel->combinator == LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING) {
            facts.uses_sibling = true;
        }
        if (sel->type == LXB_CSS_SELECTOR_TYPE_ID) {
            facts.ids.insert(std::string(view(sel->name)));
            continue;
        }
        if (sel->type == LXB_CSS_SELECTOR_TYPE_ATTRIBUTE) {
            const std::string_view name = view(sel->name);
            facts.attrs.insert(std::string(name));
            if (name == "id") {
                facts.id_attribute = true;
            }
            continue;
        }
        if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS) {
            const uint32_t bit = pseudo_class_bit(sel);
            facts.structural_share_bits |= bit & kStructuralShareBits;
            facts.state_bits |= bit;
            const auto pseudo = sel->u.pseudo.type;
            if (pseudo == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_WITHIN) {
                facts.uses_focus_within = true;
            } else if (pseudo == LXB_CSS_SELECTOR_PSEUDO_CLASS_INDETERMINATE) {
                facts.uses_indeterminate = true;
            }
            if (pseudo == LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER) {
                collect_state_target(sel, LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER, facts.hover_targets);
            } else if (pseudo == LXB_CSS_SELECTOR_PSEUDO_CLASS_ACTIVE) {
                collect_state_target(sel, LXB_CSS_SELECTOR_PSEUDO_CLASS_ACTIVE,
                                     facts.active_targets);
            }
            continue;
        }
        if (sel->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION) {
            continue;
        }
        const auto type = sel->u.pseudo.type;
        if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_CHILD ||
            type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD ||
            type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_OF_TYPE ||
            type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE) {
            facts.uses_nth = true;
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD ||
                type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE) {
                facts.uses_nth_last = true;
            }
            const auto* anb_of = static_cast<const lxb_css_selector_anb_of_t*>(sel->u.pseudo.data);
            if (anb_of == nullptr) {
                facts.nth_raw = true;
            } else {
                NthPattern pat;
                pat.a = anb_of->anb.a;
                pat.b = anb_of->anb.b;
                pat.of_type = type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_OF_TYPE ||
                              type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE;
                pat.from_end = type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD ||
                               type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE;
                pat.of = anb_of->of;
                if (std::find(facts.nth_patterns.begin(), facts.nth_patterns.end(), pat) ==
                    facts.nth_patterns.end()) {
                    facts.nth_patterns.push_back(pat);
                }
            }
            if (anb_of != nullptr && anb_of->of != nullptr &&
                std::find(facts.nth_of.begin(), facts.nth_of.end(), anb_of->of) ==
                    facts.nth_of.end()) {
                facts.nth_of.push_back(anb_of->of);
            }
            if (anb_of != nullptr && anb_of->of != nullptr) {
                for (const lxb_css_selector_list_t* nested = anb_of->of; nested != nullptr;
                     nested = nested->next) {
                    collect_selector_facts(nested->last, facts);
                }
            }
            continue;
        }
        if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_LANG) {
            facts.uses_lang = true;
        }
        if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_DIR) {
            facts.uses_dir = true;
        }
        if (type != LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT &&
            type != LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS &&
            type != LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE &&
            type != LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS) {
            continue;
        }
        if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS) {
            facts.uses_has = true;
            collect_has_anchor(sel, facts);
        }
        const auto* nested = static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data);
        for (; nested != nullptr; nested = nested->next) {
            collect_selector_facts(nested->last, facts);
        }
    }
}

/// `:has()` lists that belong to the rightmost compound (the subject). Those are the ones whose
/// truth value can split otherwise-identical ShareKeys.
void collect_subject_has(const lxb_css_selector_t* last, SelectorFacts& facts) {
    if (last == nullptr) {
        return;
    }
    const lxb_css_selector_t* left = last;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = last;; s = s->prev) {
        if (is_has_selector(s)) {
            const auto* nested = static_cast<const lxb_css_selector_list_t*>(s->u.pseudo.data);
            if (nested != nullptr) {
                SubjectHasFact fact;
                fact.anchor = build_has_anchor(s);
                fact.list = nested;
                facts.subject_has.push_back(std::move(fact));
            }
        }
        if (s == left) {
            break;
        }
    }
}

void collect_rule_facts(const lxb_css_rule_t* rule, SelectorFacts& facts) {
    for (; rule != nullptr; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_LIST || rule->type == LXB_CSS_RULE_STYLESHEET) {
            collect_rule_facts(lxb_css_rule_list(rule)->first, facts);
            continue;
        }
        if (rule->type == LXB_CSS_RULE_AT_RULE) {
            auto* at = lxb_css_rule_at(rule);
            if (at->type == LXB_CSS_AT_RULE_MEDIA && at->u.media != nullptr &&
                at->u.media->block != nullptr) {
                collect_rule_facts(at->u.media->block->first, facts);
            }
            if (at->type == LXB_CSS_AT_RULE__CUSTOM && at->u.custom != nullptr &&
                at->u.custom->block != nullptr) {
                const std::string_view cname = view(at->u.custom->name);
                // `@layer` / `@supports` bodies hold ordinary style rules; their `id`s and
                // attributes count for the share key just like an unlayered rule's do.
                facts.uses_layer = cname == "layer" || facts.uses_layer;
                facts.uses_container = cname == "container" || facts.uses_container;
                if (cname == "supports" && !supports_matches(trim(view(at->u.custom->prelude)))) {
                    continue;
                }
                collect_rule_facts(at->u.custom->block->first, facts);
            }
            continue;
        }
        if (rule->type != LXB_CSS_RULE_STYLE) {
            continue;
        }
        auto* style = lxb_css_rule_style(rule);
        for (const lxb_css_selector_list_t* list = style->selector; list != nullptr;
             list = list->next) {
            collect_selector_facts(list->last, facts);
            collect_subject_has(list->last, facts);
            // Capability A: the compounds that are *not* the subject, classified into the ancestor
            // side and the sibling side. Collected here, once per sheet generation, so building a
            // cross-frame key never re-walks a selector chain.
            collect_neighbor_chain(list->last, facts, NeighborRole::Subject, nullptr);
        }
        if (style->child != nullptr) {
            collect_rule_facts(&style->child->rule, facts);
        }
    }
}

void Engine::refresh_selector_facts() {
    SelectorFacts facts;
    for (lxb_css_stylesheet* sheet : sheets_) {
        if (sheet != nullptr && sheet->root != nullptr) {
            collect_rule_facts(sheet->root, facts);
        }
    }
    // An author `counter-reset` / `counter-increment` is applied while a node is matched, so
    // skipping a node would leave every later counter stale. `content: counter(x)` only *reads*
    // the state, and the implicit `list-item` counter is maintained even for skipped nodes, so the
    // hazard is the two declarations. A substring test over the sheet text is enough: it may
    // over-approximate (a comment mentioning the property), never under.
    bool author_counters = false;
    // `sheets_[0]` is the user agent sheet; its `counter-reset: list-item` is dump fidelity and is
    // driven by container tags in `cascade_node`, not by declarations (see the UA sheet comment).
    for (std::size_t i = 1; i < sources_.size(); ++i) {
        const std::string& source = sources_[i];
        if (source.find("counter-reset") != std::string::npos ||
            source.find("counter-increment") != std::string::npos) {
            author_counters = true;
            break;
        }
    }
    // `:nth-*(An+B of S)` counts siblings that match `S`, so a class change on one sibling can
    // re-match another one. That dependency stays inside the parent, so it bounds the closure
    // rather than making it the whole document.
    // A `:has()` no longer forces the whole document: a write can only flip the function at an
    // anchor that is the written node or one of its ancestors (HasAnchor). An id-only argument
    // expands that anchor only when the write contains the id (or a structural write removed it).
    // `has_lateral` stays the whole-document bound, and only when the sibling combinator is closer
    // to the subject than the `:has()`. `+` / `~` contribute `sibling_edges_`, not every sibling
    // of every write.
    closure_global_ = facts.has_lateral || author_counters;
    closure_sibling_ = facts.uses_sibling;
    sibling_edges_ = std::move(facts.sibling_edges);
    closure_nth_of_ = !facts.nth_of.empty();
    closure_focus_within_ = facts.uses_focus_within;
    closure_indeterminate_ = facts.uses_indeterminate;
    selector_uses_lang_ = facts.uses_lang;
    selector_uses_dir_ = facts.uses_dir;
    selector_html_state_bits_ = facts.state_bits & kHtmlStateBits;
    selector_ids_ = std::move(facts.ids);
    selector_attrs_ = std::move(facts.attrs);
    selector_tests_id_attribute_ = facts.id_attribute;
    selector_uses_nth_ = facts.uses_nth;
    selector_uses_nth_last_ = facts.uses_nth_last;
    if (!facts.nth_raw && facts.nth_patterns.size() <= 31) {
        nth_patterns_ = std::move(facts.nth_patterns);
    } else {
        nth_patterns_.clear();
    }
    has_layers_ = facts.uses_layer;
    has_container_rules_ = facts.uses_container;
    nth_of_lists_ = std::move(facts.nth_of);
    hover_targets_ = std::move(facts.hover_targets);
    active_targets_ = std::move(facts.active_targets);
    has_anchors_ = std::move(facts.has_anchors);
    // Capability A: the three tables are the whole input the cross-frame key needs from the sheets,
    // and a sheet change invalidates every entry anyway (the generation moves). A page whose loaded
    // selectors carry a nested complex `:is()` on the subject stands the memo down.
    memo_safe_ = !facts.memo_unsafe;
    facts.ancestor_facts.structural_share_bits = facts.structural_share_bits;
    ancestor_facts_ = std::make_unique<NeighborFacts>(std::move(facts.ancestor_facts));
    sibling_facts_ = std::make_unique<NeighborFacts>(std::move(facts.sibling_facts));
    if (memo_ != nullptr) {
        memo_->bump_sheets();
    }
    subject_has_.clear();
    subject_has_.reserve(facts.subject_has.size());
    for (SubjectHasFact& fact : facts.subject_has) {
        subject_has_.push_back(std::move(fact));
    }
    // Declaration pointers are stable until the next sheet generation, which already cleared the
    // expansion cache above. Serializing them here keeps that work out of the first cascade.
    const std::vector<std::string>& text =
        parsed_sources_.size() == sources_.size() ? parsed_sources_ : sources_;
    prewarm_decl_expansion(sheets_, text);
}

}  // namespace css
}  // namespace zui
