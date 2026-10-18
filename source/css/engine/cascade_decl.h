#ifndef ZUI_SOURCE_CSS_ENGINE_CASCADE_DECL_H
#define ZUI_SOURCE_CSS_ENGINE_CASCADE_DECL_H

// Declaration cascade types: origin/layer ranking, indexed rules, and declaration sinks.
// Included from cascade_internal.h so existing engine TUs keep compiling.

#include "css/engine/engine.h"
#include "css/values.h"

#include <lexbor/css/at_rule.h>
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/pseudo_const.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace css {
namespace detail {

/// Cascade origin. Alone it is not enough: CSS Cascade 5 section 6.1 sorts by origin **and**
/// importance together (UA !important beats author normal and author !important).
enum OriginRank : std::uint8_t { kOriginEmpty = 0, kOriginUa = 1, kOriginAuthor = 2 };

/// Weakest to strongest for the origins zui models (no user sheet / animations / transitions).
enum CascadeLayer : std::uint8_t {
    kLayerEmpty = 0,
    kLayerUaNormal = 1,
    kLayerAuthorNormal = 2,
    kLayerAuthorImportant = 3,
    kLayerUaImportant = 4,
};

/// CSS Cascade 5 section 6.4.4 cascade layers, flattened to one integer per layer. The ranks are
/// assigned depth-first with a layer's own declarations **after** its sub-layers (the same rule
/// that puts unlayered declarations after every named layer), so "later rank wins" is exactly the
/// normal-declaration order and "earlier rank wins" is the important one.
struct LayerTree {
    struct Node {
        std::string local;  ///< This layer's own name inside its parent.
        std::vector<int> kids;
        int rank = 0;  ///< Position in the flattened layer order; filled by `rank_layers`.
    };
    std::vector<Node> nodes{Node{}};  // Node 0 is the root (the unlayered top level).
    int anonymous = 0;
    /// `@layer` at-rule -> the rank of the layer its block styles. Filled by `collect_layer_order`
    /// before any node is visited, so the cascade is a map lookup per rule.
    std::unordered_map<const lxb_css_rule_t*, int> rule_rank;

    /// The unlayered top-level slot, which ends up after every named layer.
    int unlayered() const { return nodes.front().rank; }

    int child(int parent, const std::string& local) {
        for (int kid : nodes[static_cast<size_t>(parent)].kids) {
            if (nodes[static_cast<size_t>(kid)].local == local) {
                return kid;
            }
        }
        Node node;
        node.local = local;
        nodes.push_back(std::move(node));
        const int id = static_cast<int>(nodes.size()) - 1;
        nodes[static_cast<size_t>(parent)].kids.push_back(id);
        return id;
    }

    /// `a.b.c` names a chain: each segment is a sub-layer of the one before it.
    int path(int parent, std::string_view dotted) {
        int at = parent;
        size_t begin = 0;
        for (size_t i = 0; i <= dotted.size(); ++i) {
            if (i != dotted.size() && dotted[i] != '.') {
                continue;
            }
            const std::string local(dotted.substr(begin, i - begin));
            begin = i + 1;
            if (!local.empty()) {
                at = child(at, local);
            }
        }
        return at;
    }
};

/// One `@container` the indexed rule sits inside. Every gate has to match (nested queries).
struct ContainerGate {
    std::string name;
    std::string condition;
};

/// One style rule, in stylesheet order, after `@media` / `@supports` that do not apply this frame
/// have been dropped. `@container` stays on the rule: it depends on the node.
struct IndexedRule {
    const lxb_css_rule_style_t* style = nullptr;
    std::string_view source;
    OriginRank origin = kOriginAuthor;
    int layer_rank = 0;
    std::vector<ContainerGate> gates;
    /// One selector of a comma group. Matching tests this list and does not walk `next`.
    /// Null only when the style rule has no selector list.
    const lxb_css_selector_list_t* only = nullptr;
    /// The subject is a single id, class, or type, optionally with one trailing pseudo-element
    /// and no combinator. The bucket already proved that simple selector, so matching would
    /// only re-test it.
    bool trivial = false;
    /// Subject-compound state bits (`kBitHover` and the other live flags, plus `kBitRootEdge`).
    /// Every bit in `require_state` must be set. Any bit in `reject_state` (`:not(:hover)`)
    /// fails the selector. Both stay zero when the subject has no such pseudo. A miss here
    /// skips `matches_selector`; a hit still runs it. The mask never accepts a rule.
    std::uint32_t require_state = 0;
    std::uint32_t reject_state = 0;
    /// The rule is inside an `@media` group. Viewport size is not part of the environment
    /// generation; a later cascade drops the memo entry when this selector leaves the index.
    bool from_media = false;
};

/// Subject buckets for one cascade. Each selector in a comma group is its own slot, so a node
/// is tested only against the alternative whose subject names that node. The bucket is a
/// necessary condition: `matches_selector` still decides. A subject compound with no id, class,
/// or type (`*`, `:hover`, `[attr]`, `:not()`, `:is()`) is universal.
struct RuleIndex {
    std::vector<IndexedRule> rules;
    /// `std::less<>` finds a `string_view` without allocating a key. A cold frame looks these up
    /// once per element.
    std::map<std::string, std::vector<std::uint32_t>, std::less<>> by_id;
    std::map<std::string, std::vector<std::uint32_t>, std::less<>> by_class;
    /// Tag keys are ASCII-lowercase. HTML tag match is case-insensitive.
    std::map<std::string, std::vector<std::uint32_t>, std::less<>> by_tag;
    std::vector<std::uint32_t> universal;
};

/// Everything the cascade sorts one declaration by, in order (CSS Cascade 5 section 6.4): origin
/// and importance, element-attached styles, layers, specificity, order of appearance.
struct DeclKey {
    CascadeLayer band = kLayerEmpty;
    bool element_attached = false;
    int layer = 0;
    uint32_t spec = 0;
    int order = -1;
};

struct Winner {
    ::zui::Atom value;
    uint32_t spec = 0;
    int order = -1;
    OriginRank origin = kOriginEmpty;
    bool important = false;
    int layer = 0;
    bool element_attached = false;
};

struct PendingShort {
    ::zui::Atom name;
    ::zui::Atom value;
    uint32_t spec = 0;
    int order = -1;
    OriginRank origin = kOriginEmpty;
    bool important = false;
    int layer = 0;
    bool element_attached = false;
};

/// One declaration set: the winners plus the shorthands that still need the custom properties
/// they refer to. The cascade produces one per generated element as well as one for the element.
struct DeclSink {
    /// Winners and the revert history. A flat table: a cold miss used to allocate one
    /// unordered_map node per property, twice (the winner and the history).
    ::zui::FlatMap<Winner> win;
    /// Every declaration that applied, including ones that lost. `revert` / `revert-layer` roll
    /// the winning origin or layer away and cascade again; the losers are the only record of
    /// what that second pass can still see. Kept on the pristine sink so a memo hit finalizes
    /// the same stack.
    ::zui::FlatMap<std::vector<Winner>> decls;
    std::vector<PendingShort> pending;

    /// Whether any declaration block matched this target at all. A generated element only exists
    /// when one did, which is what keeps `::before` off every element in the document.
    bool matched() const { return !win.empty() || !pending.empty(); }

    /// Which finalize passes this sink can actually change. A cold miss used to walk every
    /// property looking for a `var()`, a logical longhand, or a CSS-wide keyword that almost no
    /// rule in the document sets. Bits are OR'd from the cached declaration expansion.
    static constexpr std::uint8_t kFinCustom = 1u;
    static constexpr std::uint8_t kFinSub = 2u;
    static constexpr std::uint8_t kFinWide = 4u;
    static constexpr std::uint8_t kFinLogical = 8u;
    static constexpr std::uint8_t kFinRevert = 16u;
    std::uint8_t fin_flags = 0;
};

/// Which declaration set a rule feeds. Values 1..4 match `html::PseudoElement` + 1 so
/// `static_cast<PseudoTarget>(i + 1)` works for box pseudos. `Selection` is a highlight pseudo
/// and `Backdrop` is a paint-only top-layer dim: both widen DeclSinks only and must not enter
/// `PseudoElement` / `pseudo_node`.
enum class PseudoTarget : std::uint8_t {
    None = 0,
    Before = 1,
    After = 2,
    Placeholder = 3,
    Marker = 4,
    Selection = 5,
    Backdrop = 6
};

constexpr int kPseudoCount = static_cast<int>(html::PseudoElement::Count);
constexpr int kTargetCount = kPseudoCount + 3;
static_assert(static_cast<int>(PseudoTarget::Selection) == kPseudoCount + 1);
static_assert(static_cast<int>(PseudoTarget::Backdrop) == kPseudoCount + 2);

constexpr int sink_index(PseudoTarget t) {
    return t == PseudoTarget::None ? 0 : static_cast<int>(t);
}

/// All the declaration sets one node gets. One traversal fills them: a selector-list item targets
/// exactly one of them, because a pseudo-element may only be the last simple selector.
struct DeclSinks {
    DeclSink sets[kTargetCount];
    /// `@media` selectors that matched this node. The cross-frame memo keeps the list so a
    /// viewport move can drop only those entries. Unconditional rules are not recorded: a sheet
    /// change clears the memo instead.
    std::vector<const lxb_css_selector_list_t*> media_rules;

    DeclSink& for_target(PseudoTarget t) { return sets[sink_index(t)]; }
    const DeclSink& for_target(PseudoTarget t) const { return sets[sink_index(t)]; }
};

using StyleMap = ::zui::StyleMap;

}  // namespace detail
}  // namespace css
}  // namespace zui

#endif
