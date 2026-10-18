#ifndef ZUI_SOURCE_CSS_ENGINE_FACTS_INTERNAL_H
#define ZUI_SOURCE_CSS_ENGINE_FACTS_INTERNAL_H

// Selector-fact collection types shared by facts.cpp and facts_neighbor.cpp.

#include "css/engine/cascade_internal.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace zui {
namespace css {

using namespace detail;

/// The selector facts the share key needs: every `id` name the sheets mention, whether one of
/// them tests the `id` attribute at all (then no id may be dropped), every other attribute name a
/// selector tests, and every `:has()` argument list that sits on a subject compound.
struct SelectorFacts {
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> attrs;
    bool id_attribute = false;
    /// Some stylesheet has an `@layer` at-rule, so the layer order has to be walked before a
    /// cascade. Pages without one skip the walk entirely.
    bool uses_layer = false;
    /// A loaded sheet has an `@container` rule: its match reads the query-container sizes layout
    /// publishes, so those belong in the environment generation. Pages without one skip the
    /// per-frame container signature entirely.
    bool uses_container = false;
    /// Some selector uses an `:nth-*()` function: node positions belong in the share key.
    bool uses_nth = false;
    /// Some selector reads the position from the end (`:nth-last-child()` / `:nth-last-of-type()`):
    /// the sibling totals belong in the cross-frame key (docs/PERF_REFACTOR.md capability A).
    bool uses_nth_last = false;
    /// A selector uses `+` / `~`: a change can re-match a sibling outside the changed subtree, so
    /// the style closure has to be the whole document (docs/PERF.md section 2.4 rule 3).
    bool uses_sibling = false;
    /// A selector uses `:has()`: a change can re-match an ancestor (or the document), so the style
    /// closure has to be the whole document.
    bool uses_has = false;
    std::vector<SubjectHasFact> subject_has;
    /// `of S` lists carried by those `:nth-*()` functions. Kept unique-by-pointer so one rule that
    /// appears in five sheets does not count its siblings five times.
    std::vector<const lxb_css_selector_list_t*> nth_of;
    /// Distinct An+B patterns. A null `anb` payload blocks the fingerprint and the walk keeps the
    /// raw index, which cannot mis-share.
    std::vector<NthPattern> nth_patterns;
    bool nth_raw = false;
    /// Compounds that carry `:hover`; see HoverTarget.
    std::vector<HoverTarget> hover_targets;
    /// Compounds that carry `:active`; same shape.
    std::vector<HoverTarget> active_targets;
    /// Compounds that carry `:has()`; see HasAnchor.
    std::vector<HasAnchor> has_anchors;
    /// A single complex selector mixes `:has()` with `+` / `~`, so the rule's subject can sit
    /// outside the anchor's subtree: its closure has to stay the whole document. Set only when the
    /// sibling combinator is closer to the subject than the `:has()` (`.anchor:has(.x) ~ .b`).
    /// `#theme ~ .app .entry:has(.x)` keeps the `:has()` on the subject side and stays local.
    bool has_lateral = false;
    /// Left compounds of `+` / `~`. A write adds following siblings only when it matches one.
    std::vector<SiblingEdge> sibling_edges;
    /// The cross-frame memo's own tables (docs/PERF_REFACTOR.md capability A): what the loaded
    /// selectors test on a compound that is **not** the subject, split into the ancestor side and
    /// the sibling side, plus the subject compound's own non-plain facets.
    NeighborFacts ancestor_facts;
    NeighborFacts sibling_facts;
    NeighborFacts subject_facts;
    /// A subject compound carries `:is()` / `:not()` / `:where()` around a selector that needs its
    /// own ancestor chain (`:is(.a .b)`), a shape the cross-frame key cannot reduce to node-local
    /// facts: the memo is turned off for such a page rather than risk a stale hit.
    bool memo_unsafe = false;
    /// `:focus-within` is in the sheet. The closure adds ancestors of a dirty node, not the
    /// document.
    bool uses_focus_within = false;
    /// `:indeterminate` is in the sheet. A dirty radio pulls the rest of its name group in.
    bool uses_indeterminate = false;
    /// `:lang()` is in the sheet. The resolved language belongs in the share key.
    bool uses_lang = false;
    /// `:dir()` is in the sheet. The resolved directionality belongs in the share key, and a
    /// text change under `dir=auto` / `bdi` pulls that subtree into the closure.
    bool uses_dir = false;
    /// `kStructuralShareBits` tested anywhere in the loaded selectors. Sibling position stays out
    /// of the share key when this mask does not name the bit.
    uint32_t structural_share_bits = 0;
    /// Every `pseudo_class_bit` the loaded selectors test, including bits outside
    /// `kStructuralShareBits`. `kHtmlStateBits` of this mask gates `html_pseudo_bits`.
    uint32_t state_bits = 0;
};

/// Which of the three cross-frame-key tables a compound feeds: the one hashed against the node's
/// own values, the one folded down the ancestor chain, or the one accumulated across the preceding
/// siblings.
enum class NeighborRole { Subject, Ancestor, Sibling };

inline NeighborFacts& neighbor_bucket(SelectorFacts& facts, NeighborRole role) {
    switch (role) {
        case NeighborRole::Subject:
            return facts.subject_facts;
        case NeighborRole::Sibling:
            return facts.sibling_facts;
        case NeighborRole::Ancestor:
        default:
            return facts.ancestor_facts;
    }
}

inline NeighborRole step_role(NeighborRole role, unsigned combinator) {
    if (role == NeighborRole::Sibling) {
        // A compound to the left of a sibling is still a sibling-side dependency; the preceding
        // sibling's own ancestor signature is mixed into the sibling chain for the rest.
        return NeighborRole::Sibling;
    }
    if (combinator == LXB_CSS_SELECTOR_COMBINATOR_SIBLING ||
        combinator == LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING) {
        return NeighborRole::Sibling;
    }
    return NeighborRole::Ancestor;
}

uint32_t pseudo_class_bit(const lxb_css_selector_t* sel);
bool is_nth_function(unsigned type);
bool is_nth_last_function(unsigned type);
bool alternative_is_complex(const lxb_css_selector_list_t* list);
void collect_neighbor_chain(const lxb_css_selector_t* last, SelectorFacts& facts,
                            NeighborRole role, const HasAnchor* outer);
HasAnchor build_has_anchor(const lxb_css_selector_t* has);
HasAnchor compound_anchor(const lxb_css_selector_t* sel);
bool anchor_unconstrained(const HasAnchor& anchor);
void add_neighbor_fact(const lxb_css_selector_t* sel, SelectorFacts& facts, NeighborRole role,
                       const HasAnchor* outer);
void collect_state_target(const lxb_css_selector_t* pseudo, uint32_t pseudo_type,
                          std::vector<HoverTarget>& targets);
void collect_has_anchor(const lxb_css_selector_t* has, SelectorFacts& facts);
void note_lateral_has(const lxb_css_selector_t* sel, SelectorFacts& facts);
void collect_sibling_edges(const lxb_css_selector_t* sel, SelectorFacts& facts);
void collect_selector_facts(const lxb_css_selector_t* sel, SelectorFacts& facts);
void collect_subject_has(const lxb_css_selector_t* last, SelectorFacts& facts);
void collect_rule_facts(const lxb_css_rule_t* rule, SelectorFacts& facts);

}  // namespace css
}  // namespace zui

#endif
