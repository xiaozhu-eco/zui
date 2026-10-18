#ifndef ZUI_CSS_ENGINE_H
#define ZUI_CSS_ENGINE_H

#include "html/document.h"

#include <zui/host_surface.h>
#include <zui/host_types.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lxb_css_stylesheet;
struct lxb_css_selector_list;
struct lxb_css_selector;

namespace zui {
namespace css {

namespace detail {
struct CascadeMemo;
struct NeighborFacts;
}  // namespace detail

enum class StyleDiff { None = 0, Paint = 1, Layout = 2, Reattach = 3 };

/// One `:hover` compound in a loaded sheet: what an element has to look like for the rule to
/// matter (`#card:hover`, `a:hover`, `.row:hover`). The views point into the parsed sheet, so
/// they are valid only until the sheets are replaced.
/// One `:nth-*()` An+B pattern from the loaded sheets. The share key stores which of these
/// patterns match the element, not the raw sibling index, so every even row can share one map.
struct NthPattern {
    long a = 0;
    long b = 0;
    bool of_type = false;
    bool from_end = false;
    const lxb_css_selector_list* of = nullptr;
    bool operator==(const NthPattern& other) const {
        return a == other.a && b == other.b && of_type == other.of_type &&
               from_end == other.from_end && of == other.of;
    }
};

struct HoverTarget {
    std::string_view tag;
    std::string_view id;
    std::vector<std::string_view> classes;
    /// The other simple selectors of the compound (attributes, plain pseudo-classes like
    /// `:checked` / `:disabled`). The cascade's own matcher evaluates them, so the compound counts
    /// as a possible hover match only when they hold for the chain node.
    std::vector<const lxb_css_selector*> tests;
    /// The compound carries a simple selector this test cannot evaluate (`:not()` / `:is()` /
    /// `:has()`, or a shape the matcher does not know), so it has to count as matching every node.
    bool any = false;
};

/// The compound a `:has()` function sits on (its "anchor": `:root` in `:root:has(...)`, `.app` in
/// `.app:has(#view:checked) .cols`). A write can only flip that `:has()` at an anchor that is the
/// written node itself or one of its ancestors - the argument only ever looks *down*, and the
/// subject of a descendant selector is inside the anchor's subtree - so the closure contribution
/// of `:has()` is the matching anchors' subtrees, not the whole document (`closure_nodes`,
/// docs/PERF.md section 2.4 rule 3). Views point into the parsed sheet, like HoverTarget.
struct HasAnchor {
    std::string_view tag;
    std::string_view id;
    std::vector<std::string_view> classes;
    std::vector<const lxb_css_selector*> tests;
    /// The anchor compound carries a simple selector this test cannot evaluate (`:not()` / `:is()`
    /// / `:has()` / an `nth-*` function), so any node counts as an anchor.
    bool any = false;
    /// Every alternative of this `:has()` is one compound that names an id and only state
    /// pseudo-classes (no combinator, tag, class, or attribute). The function flips only when one
    /// of those elements changes, so a write that does not contain them does not expand the anchor.
    bool argument_id_only = false;
    std::vector<std::string_view> argument_ids;
};

/// One `:has()` on a subject compound, plus the anchor that compound can select. The share-key
/// walk skips the subtree search when the node cannot match the anchor: a `div` cannot match
/// `.entry:has(:checked)`, so that bit stays clear and the two divs still share a style.
struct SubjectHasFact {
    HasAnchor anchor;
    const lxb_css_selector_list* list = nullptr;
};

/// The left compound of a `+` / `~` combinator. A write pulls the following siblings only when the
/// written node matches this compound: `#theme:checked ~ .app` does not pull the siblings of a day
/// cell, and `.item ~ .item` still pulls the following items.
struct SiblingEdge {
    std::string_view tag;
    std::string_view id;
    std::vector<std::string_view> classes;
    /// `~` takes every following sibling. `+` takes the next one.
    bool following = false;
    /// The left compound is `*`, only a pseudo, or a shape tag/id/class cannot describe. Every
    /// element matches.
    bool any = false;
};

/// Parses stylesheets with lexbor, matches selectors on the zui tree, cascades.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Reads UA CSS plus Document::stylesheets. Keeps parsed sheets for restyle.
    bool load_sheets(html::Document& doc, std::string& error);

    /// Parses one extra stylesheet file and keeps it for later cascade().
    bool add_sheet_file(std::string_view path, std::string& error);

    /// Parses one extra stylesheet from memory — a `<style>` element's text, or a fragment's
    /// inline sheet — and keeps it for later cascade().
    bool add_sheet_text(std::string_view css, std::string& error);

    /// Replaces the theme pack stylesheet. Empty @p path removes it.
    bool set_theme_file(std::string_view path, std::string& error);

    struct CascadeResult {
        std::vector<html::Node*> reattach;
        std::vector<html::Node*> layout;
        struct PaintChange {
            html::Node* node = nullptr;
            std::shared_ptr<const StyleMap> previous;
        };
        std::vector<PaintChange> paint;
        /// Nodes whose only change is a paint property (`StyleDiff::Paint`). Their geometry is
        /// untouched, but the box's interned `LayoutStyle` still has to be re-derived, because the
        /// painter reads radii / border styles out of it (docs/PERF.md I6, docs/PERF_REFACTOR.md
        /// stage 1). This is not `paint`: that list is the transition feed and only carries
        /// interpolable properties.
        std::vector<html::Node*> paint_style;
    };

    /// Matches and writes Node::computed. Viewport is the media-query viewport.
    /// Host media defaults (hover, light, no reduced motion) when the overload without
    /// `host` is used — App/backends must pass `HostMediaFacts` for live OS preferences.
    CascadeResult cascade(html::Document& doc, float viewport_width = 0, float viewport_height = 0);
    CascadeResult cascade(html::Document& doc, float viewport_width, float viewport_height,
                          const HostMediaFacts& host);
    /// Same, but only nodes in @p closure may re-match and re-commit. Nodes outside it keep their
    /// computed style; the walk still visits them for sibling / counter bookkeeping, but they are
    /// not `cascade.visited` work (docs/PERF.md I3). Pass nullptr for a full cascade.
    CascadeResult cascade(html::Document& doc, float viewport_width, float viewport_height,
                          const HostMediaFacts& host,
                          const std::unordered_set<html::Node*>* closure);

    /// Upper bound of the nodes whose computed style may change when a write dirties @p roots
    /// themselves (not their subtrees) or the subtrees of @p dirty_subtrees - the closure of
    /// docs/PERF.md section 2.4. An author counter, or a `:has()` that sits further from the
    /// subject than a `+` / `~` (the subject is outside the anchor), keeps the whole document:
    /// correct, just not tight. A `:has()` whose argument is only an id expands its anchor when
    /// that id is inside the write, or when a structural write (@p structural) removed it from
    /// under the anchor. A `+` / `~` adds the following siblings of a node that matches the left
    /// compound, not the following siblings of every write. Never returns a set missing a node
    /// the cascade could change.
    std::unordered_set<html::Node*> closure_nodes(
        const html::Document& doc, const std::vector<html::Node*>& dirty_subtrees,
        bool structural = false) const;
    /// True when `closure_nodes` cannot be tighter than the whole document for this page.
    bool closure_is_global() const { return closure_global_; }

    /// `:has()` evaluations in the last `cascade()` call. The scaling gate reads this instead of a
    /// clock: a page whose sibling count grows tenfold must not grow this with the square.
    std::size_t has_calls() const { return has_calls_; }

    /// True when some loaded stylesheet has a `:hover` rule that could match @p node, i.e. when
    /// that node's hover state can change computed style. A page whose sheets never test `:hover`
    /// then skips the whole cascade a bare pointer move would otherwise cost.
    bool hover_may_change_style(const html::Node& node) const;

    /// True when some loaded stylesheet has an `:active` rule that could match @p node. Same shape
    /// as `hover_may_change_style`: a page whose sheets never test `:active` pays nothing when a
    /// press sets the bit.
    bool active_may_change_style(const html::Node& node) const;

    /// True when a `:has()` rule's anchor compound matches @p node, i.e. when a change inside that
    /// node's subtree can flip the function (docs/PERF.md section 2.4 rule 3).
    bool has_anchor_matches(const html::Node& node) const;

    /// Drops every cross-frame memo entry for @p dead and its subtree. The App calls this from its
    /// own `forget_subtree` before the nodes are freed (docs/PERF_REFACTOR.md capability P3).
    void forget_subtree(const html::Node* dead);
    /// False when a loaded selector has a shape the cross-frame key cannot describe (a nested
    /// complex `:is()` / `:not()` / `:where()` on the subject compound), so the cascade runs with
    /// the memo off rather than risk a stale reuse.
    bool memo_active() const { return memo_safe_; }

    /// Original stylesheet text, including `@keyframes` rules lexbor does not keep.
    const std::vector<std::string>& sheet_sources() const { return sources_; }

private:
    std::vector<lxb_css_stylesheet*> sheets_;
    /// Original text. `Animations` reads `@keyframes` from here.
    std::vector<std::string> sources_;
    /// The bytes lexbor tokenized. `@keyframes` is stripped before parse, so prelude offsets
    /// (`@media`) address this buffer, not `sources_`.
    std::vector<std::string> parsed_sources_;
    lxb_css_stylesheet* theme_sheet_ = nullptr;
    /// The `id` names any loaded selector mentions, and whether one of them tests the `id`
    /// attribute at all. An element's own id only belongs in its share key when a selector could
    /// see it (see `cascade_node`); the reader's table of contents is 2000 `id="chN"` buttons that
    /// no selector names, and carrying those ids made every one of them a private key.
    std::unordered_set<std::string> selector_ids_;
    bool selector_tests_id_attribute_ = false;
    /// Attribute names any loaded selector tests (`input[type=checkbox]`, …). Those values belong
    /// in the share key; other attributes stay out so unrelated `data-*` churn does not bust the
    /// cache.
    std::unordered_set<std::string> selector_attrs_;
    /// `:has()` arguments that sit on a selector's subject compound, with the anchor compound.
    /// Match results are mixed into ShareKey so siblings that differ only in descendants do not
    /// share computed style. Nodes the anchor cannot select skip the walk.
    std::vector<SubjectHasFact> subject_has_;
    /// A loaded selector uses an `:nth-*()` function, so a node's 1-based position among its
    /// element siblings belongs in its share key.
    bool selector_uses_nth_ = false;
    /// A loaded selector tests the position from the end (`:nth-last-child()` /
    /// `:nth-last-of-type()`), so the sibling totals belong in the cross-frame key.
    bool selector_uses_nth_last_ = false;
    /// The loaded selectors can all be described by the cross-frame key (see `memo_active`).
    bool memo_safe_ = true;
    /// The `of S` lists those functions carry. Their per-node preceding/following sibling counts
    /// are mixed into the share key next to the position.
    std::vector<const lxb_css_selector_list*> nth_of_lists_;
    /// Distinct `:nth-*()` An+B patterns. Empty when there are none or more than 31, which keeps
    /// the raw sibling index in the share key. At most 31 so the match result fits in one word.
    std::vector<NthPattern> nth_patterns_;
    /// A loaded sheet has an `@layer` at-rule. Pages without one skip building the layer order, so
    /// the extra walk costs nothing on an ordinary page.
    bool has_layers_ = false;
    /// A loaded sheet has an `@container` rule, so the published query-container sizes feed the
    /// environment generation (docs/PERF_REFACTOR.md capability A).
    bool has_container_rules_ = false;
    /// A loaded selector puts the subject outside a `:has()` anchor (a sibling combinator sits
    /// closer to the subject than the `:has()`), or an author counter can see past the write:
    /// the closure of any write is then the whole document.
    bool closure_global_ = false;
    /// A loaded selector uses `+` / `~`. The edges themselves are `sibling_edges_`; this flag is
    /// the fast "the page has any" bit.
    bool closure_sibling_ = false;
    /// A loaded selector uses `:nth-*(An+B of S)`: a sibling's match of `S` shifts the count for
    /// the siblings after it, so `closure_nodes` adds the written node's parent subtree.
    bool closure_nth_of_ = false;
    /// A loaded selector uses `:focus-within`. `closure_nodes` adds each dirty node's ancestors
    /// as individual elements (not their other descendants).
    bool closure_focus_within_ = false;
    /// A loaded selector uses `:indeterminate`. A dirty radio pulls the rest of its name group
    /// into the closure so an unchecked peer drops the pseudo too.
    bool closure_indeterminate_ = false;
    /// A loaded selector uses `:lang()`. The resolved language is mixed into the share key.
    bool selector_uses_lang_ = false;
    /// A loaded selector uses `:dir()`. The resolved directionality is mixed into the share key,
    /// and `closure_nodes` adds the subtree of a dirty node's content-dependent ancestor.
    bool selector_uses_dir_ = false;
    /// `kHtmlStateBits` tested by the loaded selectors. Zero until facts are collected, and zero
    /// when no sheet names those pseudos — the cascade then skips the per-node HTML-state walk.
    uint32_t selector_html_state_bits_ = 0;
    /// Every `:hover` compound the loaded sheets carry (see HoverTarget).
    std::vector<HoverTarget> hover_targets_;
    /// Every `:active` compound the loaded sheets carry (same shape).
    std::vector<HoverTarget> active_targets_;
    /// Every compound carrying `:has()` (see HasAnchor). A write with no matching anchor on its
    /// ancestor chain cannot flip any `:has()`, so its closure stays tight.
    std::vector<HasAnchor> has_anchors_;
    /// Every `+` / `~` left compound (see SiblingEdge). Empty when the page has no sibling combinator.
    std::vector<SiblingEdge> sibling_edges_;
    std::size_t has_calls_ = 0;
    /// The cross-frame cascade memo of docs/PERF_REFACTOR.md capability A. Incomplete here: the
    /// entry types are `source/css` only, so the definition lives in `cascade_internal.h`.
    std::unique_ptr<detail::CascadeMemo> memo_;
    /// Facts the loaded selectors test on the ancestor side and on the preceding-sibling side.
    /// Collected with the other selector facts, once per sheet generation.
    std::unique_ptr<detail::NeighborFacts> ancestor_facts_;
    std::unique_ptr<detail::NeighborFacts> sibling_facts_;
    void clear_sheets();
    /// Recomputes `selector_ids_` / `selector_tests_id_attribute_` / `selector_attrs_` /
    /// `subject_has_` from every loaded sheet.
    void refresh_selector_facts();
};

}  // namespace css
}  // namespace zui

#endif  // ZUI_CSS_ENGINE_H
