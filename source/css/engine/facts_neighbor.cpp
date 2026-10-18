// Neighbor / class-set / :has() / sibling-edge facts collected from loaded selectors.

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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {

using namespace detail;

/// The `kBit*` a plain pseudo-class tests; 0 for one the key does not describe (the matcher returns
/// false for those anyway, so they are constant).
uint32_t pseudo_class_bit(const lxb_css_selector_t* sel) {
    if (sel->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS) {
        return 0;
    }
    switch (sel->u.pseudo.type) {
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER:
            return kBitHover;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ACTIVE:
            return kBitActive;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS:
            return kBitFocus;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_VISIBLE:
            return kBitFocusVisible;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_MODAL:
            return kBitModal;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_CHECKED:
            return kBitChecked;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_DISABLED:
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ENABLED:
            // `:enabled` is the negation of the same bit; either way the bit value separates the
            // two states.
            return kBitDisabled;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_EMPTY:
            return kBitEmpty;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FIRST_CHILD:
            return kBitFirst;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LAST_CHILD:
            return kBitLast;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FIRST_OF_TYPE:
            return kBitFirstType;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LAST_OF_TYPE:
            return kBitLastType;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ONLY_CHILD:
            return kBitFirst | kBitLast;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ONLY_OF_TYPE:
            return kBitFirstType | kBitLastType;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_PLACEHOLDER_SHOWN:
            return kBitPlaceholderShown;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_WITHIN:
            return kBitFocusWithin;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_REQUIRED:
            return kBitRequired;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_OPTIONAL:
            return kBitOptional;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_READ_WRITE:
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_READ_ONLY:
            // `:read-only` is the complement of the same bit.
            return kBitReadWrite;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_DEFAULT:
            return kBitDefault;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_INDETERMINATE:
            return kBitIndeterminate;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LINK:
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ANY_LINK:
            return kBitLink;
        default:
            return 0;
    }
}

bool is_nth_function(unsigned type) {
    return type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_CHILD ||
           type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD ||
           type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_OF_TYPE ||
           type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE;
}

bool is_nth_last_function(unsigned type) {
    return type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD ||
           type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE;
}

/// True when one alternative of a nested selector list spans more than one compound (`.a .b`), i.e.
/// it needs an ancestor chain of its own.
bool alternative_is_complex(const lxb_css_selector_list_t* list) {
    for (const lxb_css_selector_t* s = list->last; s != nullptr; s = s->prev) {
        if (s->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
            return true;
        }
    }
    return false;
}

void collect_neighbor_chain(const lxb_css_selector_t* last, SelectorFacts& facts,
                            NeighborRole role, const HasAnchor* outer);
/// The compound a `:has()` hangs on; defined next to the closure's anchor collector below.
HasAnchor build_has_anchor(const lxb_css_selector_t* has);
/// Tag / id / class of the compound `sel` sits in, ignoring `:has()` / `:not()` / `:is()` /
/// `:where()` (those do not name a different element). An empty result matches every element.
HasAnchor compound_anchor(const lxb_css_selector_t* sel);
bool anchor_unconstrained(const HasAnchor& anchor);

/// One simple selector's contribution to a bucket. `subject` marks the compound the rule's subject
/// sits on, whose own tag / class / id / attributes the share key already carries; everything else
/// (a nested `:is()` around an attribute, a pseudo-class bit) is what the extra table exists for.
/// `outer` is the compound a nested `:not()` / `:is()` / `:where()` was entered from, so a
/// `:has()` that is the whole argument (`.app:not(:has(#id))`) keeps `.app` as its anchor.
void add_neighbor_fact(const lxb_css_selector_t* sel, SelectorFacts& facts, NeighborRole role,
                       const HasAnchor* outer) {
    if (role == NeighborRole::Subject) {
        // The subject compound's own tag / class / id / attribute / plain pseudo-class tests are
        // already in the share key, and the cross-frame key is copied from it verbatim. The one
        // shape the key cannot express is a nested `:is()` / `:not()` / `:where()` around a
        // selector that needs its own ancestor chain (`:is(.a .b)`): there the alternative has to be
        // re-matched with an ancestor chain the key does not carry, so the memo stands down.
        if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION) {
            const auto type = sel->u.pseudo.type;
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT ||
                type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS ||
                type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE) {
                const auto* nested =
                    static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data);
                for (const lxb_css_selector_list_t* alt = nested; alt != nullptr; alt = alt->next) {
                    if (alternative_is_complex(alt)) {
                        facts.memo_unsafe = true;
                    }
                }
            }
        }
        return;
    }
    NeighborFacts& bucket = neighbor_bucket(facts, role);
    switch (sel->type) {
        case LXB_CSS_SELECTOR_TYPE_ELEMENT:
            bucket.used = true;
            bucket.tags.insert(std::string(view(sel->name)));
            break;
        case LXB_CSS_SELECTOR_TYPE_ID:
            bucket.used = true;
            bucket.ids.insert(std::string(view(sel->name)));
            break;
        case LXB_CSS_SELECTOR_TYPE_CLASS:
            bucket.used = true;
            bucket.classes.insert(std::string(view(sel->name)));
            break;
        case LXB_CSS_SELECTOR_TYPE_ATTRIBUTE:
            bucket.used = true;
            bucket.attrs.insert(std::string(view(sel->name)));
            break;
        case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS: {
            const uint32_t bit = pseudo_class_bit(sel);
            if (bit != 0) {
                bucket.used = true;
                bucket.pseudo_mask |= bit;
            }
            break;
        }
        case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION: {
            const auto type = sel->u.pseudo.type;
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS) {
                const auto* nested =
                    static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data);
                if (nested != nullptr) {
                    bucket.used = true;
                    HasAnchor anchor = build_has_anchor(sel);
                    // `.app:not(:has(#id))` parses the function as its own compound, so the
                    // anchor walk sees no class. The element that can flip it is still `.app`.
                    if (outer != nullptr && anchor_unconstrained(anchor)) {
                        anchor = *outer;
                    }
                    bucket.has_lists.push_back(NeighborHas{std::move(anchor), nested});
                }
                break;
            }
            if (is_nth_function(type)) {
                // The position that decides `:nth-*()` is carried by the key's own position fields,
                // so the compound adds no further facet. `:nth-last-*()` is what puts the sibling
                // totals in the key.
                if (is_nth_last_function(type)) {
                    facts.uses_nth_last = true;
                }
                break;
            }
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT ||
                type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS ||
                type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE) {
                const auto* nested =
                    static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data);
                HasAnchor compound = compound_anchor(sel);
                const HasAnchor* next = anchor_unconstrained(compound) ? outer : &compound;
                for (const lxb_css_selector_list_t* alt = nested; alt != nullptr; alt = alt->next) {
                    collect_neighbor_chain(alt->last, facts, role, next);
                }
                break;
            }
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_LANG) {
                bucket.used = true;
                bucket.lang = true;
                break;
            }
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_DIR) {
                bucket.used = true;
                bucket.dir = true;
                break;
            }
            // Any other function shape is `false` in the matcher (the `nth` family is above), so it
            // contributes nothing that can move.
            break;
        }
        default:
            // `*`, a pseudo-element and anything the key does not describe: constant or structural,
            // nothing to hash.
            break;
    }
}

void collect_neighbor_chain(const lxb_css_selector_t* last, SelectorFacts& facts,
                            NeighborRole role, const HasAnchor* outer) {
    if (last == nullptr) {
        return;
    }
    const lxb_css_selector_t* s = last;
    for (;;) {
        add_neighbor_fact(s, facts, role, outer);
        if (s->prev == nullptr) {
            break;
        }
        if (s->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
            role = step_role(role, s->combinator);
            // A new compound is not nested in the previous compound's `:not()`.
            outer = nullptr;
        }
        s = s->prev;
    }
}

/// The compound a state pseudo (`:hover` / `:active`) sits in: what the designated element has to
/// look like for the rule to matter. Both match the element and its ancestor chain (Selectors 4
/// section 6.2), so the chain a pointer produces is exactly what has to be tested against this.
void collect_state_target(const lxb_css_selector_t* pseudo, uint32_t pseudo_type,
                          std::vector<HoverTarget>& out) {
    HoverTarget target;
    const lxb_css_selector_t* left = pseudo;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = pseudo;; s = s->prev) {
        switch (s->type) {
            case LXB_CSS_SELECTOR_TYPE_ELEMENT:
                target.tag = view(s->name);
                break;
            case LXB_CSS_SELECTOR_TYPE_ID:
                target.id = view(s->name);
                break;
            case LXB_CSS_SELECTOR_TYPE_CLASS:
                target.classes.push_back(view(s->name));
                break;
            case LXB_CSS_SELECTOR_TYPE_ANY:
                break;
            case LXB_CSS_SELECTOR_TYPE_ATTRIBUTE:
                // `input[type=checkbox]:hover`: the attribute half is evaluated by the same matcher
                // the cascade uses, so it does not make the compound match every node.
                target.tests.push_back(s);
                break;
            case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS:
                // The state pseudo this target is for lives on the compound itself; every other
                // plain pseudo-class (`:checked`, `:focus`, …) is node-local and evaluated below.
                if (s->u.pseudo.type != pseudo_type) {
                    target.tests.push_back(s);
                }
                break;
            case LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT:
                // The pseudo-element belongs to the same element (`a:hover::before`), so it does
                // not narrow which node the hover can restyle.
                break;
            default:
                // `:not(…)` / `:is()` / `:has()` (they need the document, and `:has()` walks a
                // subtree): this test cannot evaluate it here, so the compound has to be treated
                // as a possible match.
                target.any = true;
                break;
        }
        if (s == left) {
            break;
        }
    }
    out.push_back(std::move(target));
}

/// The compound a `:has()` sits on: what a write has to be inside to flip the function. The
/// function node itself is skipped (it does not narrow which element the anchor is), and so is a
/// pseudo-element on the anchor (`a:has(x)::before` still anchors on `a`).
HasAnchor build_has_anchor(const lxb_css_selector_t* has) {
    HasAnchor anchor;
    const lxb_css_selector_t* left = has;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = has;; s = s->prev) {
        const bool is_the_has_itself =
            s->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION &&
            s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS;
        if (!is_the_has_itself) {
            switch (s->type) {
                case LXB_CSS_SELECTOR_TYPE_ELEMENT:
                    anchor.tag = view(s->name);
                    break;
                case LXB_CSS_SELECTOR_TYPE_ID:
                    anchor.id = view(s->name);
                    break;
                case LXB_CSS_SELECTOR_TYPE_CLASS:
                    anchor.classes.push_back(view(s->name));
                    break;
                case LXB_CSS_SELECTOR_TYPE_ATTRIBUTE:
                    anchor.tests.push_back(s);
                    break;
                case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS:
                    if (s->u.pseudo.type != LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER) {
                        anchor.tests.push_back(s);
                    }
                    break;
                case LXB_CSS_SELECTOR_TYPE_ANY:
                case LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT:
                    break;
                default:
                    anchor.any = true;
                    break;
            }
        }
        if (s == left) {
            break;
        }
    }
    const auto* nested = static_cast<const lxb_css_selector_list_t*>(has->u.pseudo.data);
    anchor.argument_id_only = nested != nullptr;
    for (const lxb_css_selector_list_t* list = nested; list != nullptr && anchor.argument_id_only;
         list = list->next) {
        const lxb_css_selector_t* last = list->last;
        if (last == nullptr) {
            anchor.argument_id_only = false;
            break;
        }
        const lxb_css_selector_t* arg_left = last;
        while (arg_left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE &&
               arg_left->prev != nullptr) {
            arg_left = arg_left->prev;
        }
        // A combinator inside the argument (`.a .b`, `+`, `~`, `>`) is not a single compound.
        if (arg_left->prev != nullptr) {
            anchor.argument_id_only = false;
            break;
        }
        std::string_view id;
        for (const lxb_css_selector_t* s = last;; s = s->prev) {
            if (s->type == LXB_CSS_SELECTOR_TYPE_ID) {
                if (!id.empty()) {
                    anchor.argument_id_only = false;
                    break;
                }
                id = view(s->name);
            } else if (s->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS &&
                       s->type != LXB_CSS_SELECTOR_TYPE_ANY &&
                       s->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
                // A tag, class, attribute, or nested functional is not "the element with this id".
                anchor.argument_id_only = false;
                break;
            }
            if (s == arg_left) {
                break;
            }
        }
        if (id.empty()) {
            anchor.argument_id_only = false;
        }
        if (!anchor.argument_id_only) {
            break;
        }
        anchor.argument_ids.push_back(id);
    }
    if (!anchor.argument_id_only) {
        anchor.argument_ids.clear();
    }
    return anchor;
}

void collect_has_anchor(const lxb_css_selector_t* has, SelectorFacts& facts) {
    facts.has_anchors.push_back(build_has_anchor(has));
}

bool anchor_unconstrained(const HasAnchor& anchor) {
    return !anchor.any && anchor.tag.empty() && anchor.id.empty() && anchor.classes.empty() &&
           anchor.tests.empty();
}

/// The element a nested `:not()` / `:is()` / `:where()` is tested against. `:has()` and those
/// functions themselves are skipped: `.app:not(:has(#id))` anchors on `.app`, not on every node.
HasAnchor compound_anchor(const lxb_css_selector_t* sel) {
    HasAnchor anchor;
    if (sel == nullptr) {
        anchor.any = true;
        return anchor;
    }
    const lxb_css_selector_t* left = sel;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = sel;; s = s->prev) {
        const bool skipped = s->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION &&
                             (s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS ||
                              s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT ||
                              s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS ||
                              s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE);
        if (!skipped) {
            switch (s->type) {
                case LXB_CSS_SELECTOR_TYPE_ELEMENT:
                    anchor.tag = view(s->name);
                    break;
                case LXB_CSS_SELECTOR_TYPE_ID:
                    anchor.id = view(s->name);
                    break;
                case LXB_CSS_SELECTOR_TYPE_CLASS:
                    anchor.classes.push_back(view(s->name));
                    break;
                case LXB_CSS_SELECTOR_TYPE_ATTRIBUTE:
                    anchor.tests.push_back(s);
                    break;
                case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS:
                    if (s->u.pseudo.type != LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER) {
                        anchor.tests.push_back(s);
                    }
                    break;
                case LXB_CSS_SELECTOR_TYPE_ANY:
                case LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT:
                    break;
                default:
                    anchor.any = true;
                    break;
            }
        }
        if (s == left) {
            break;
        }
    }
    return anchor;
}

/// Tag, id, and classes of one compound. `any` is set when those three cannot describe it, or when
/// the compound names none of them (a bare pseudo, `*`).
struct CompoundIdentity {
    std::string_view tag;
    std::string_view id;
    std::vector<std::string_view> classes;
    bool any = false;
};

CompoundIdentity read_compound(const lxb_css_selector_t* rightmost) {
    CompoundIdentity out;
    if (rightmost == nullptr) {
        out.any = true;
        return out;
    }
    const lxb_css_selector_t* left = rightmost;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = rightmost;; s = s->prev) {
        switch (s->type) {
            case LXB_CSS_SELECTOR_TYPE_ELEMENT:
                out.tag = view(s->name);
                break;
            case LXB_CSS_SELECTOR_TYPE_ID:
                out.id = view(s->name);
                break;
            case LXB_CSS_SELECTOR_TYPE_CLASS:
                out.classes.push_back(view(s->name));
                break;
            case LXB_CSS_SELECTOR_TYPE_ANY:
            case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS:
            case LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT:
                break;
            default:
                out.any = true;
                break;
        }
        if (s == left) {
            break;
        }
    }
    if (out.tag.empty() && out.id.empty() && out.classes.empty()) {
        out.any = true;
    }
    return out;
}

/// `:has()` with a sibling combinator closer to the subject than the function. The subject then
/// sits outside the anchor, and the anchor's subtree is not a bound.
void note_lateral_has(const lxb_css_selector_t* sel, SelectorFacts& facts) {
    bool sibling_closer_to_subject = false;
    for (const lxb_css_selector_t* cursor = sel; cursor != nullptr;) {
        const lxb_css_selector_t* left = cursor;
        while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
            left = left->prev;
        }
        if (sibling_closer_to_subject) {
            for (const lxb_css_selector_t* s = cursor;; s = s->prev) {
                if (s->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION &&
                    s->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS) {
                    facts.has_lateral = true;
                    return;
                }
                if (s == left) {
                    break;
                }
            }
        }
        if (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_SIBLING ||
            left->combinator == LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING) {
            sibling_closer_to_subject = true;
        }
        cursor = left->prev;
    }
}

void collect_sibling_edges(const lxb_css_selector_t* sel, SelectorFacts& facts) {
    for (const lxb_css_selector_t* cursor = sel; cursor != nullptr;) {
        const lxb_css_selector_t* left = cursor;
        while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
            left = left->prev;
        }
        if ((left->combinator == LXB_CSS_SELECTOR_COMBINATOR_SIBLING ||
             left->combinator == LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING) &&
            left->prev != nullptr) {
            CompoundIdentity id = read_compound(left->prev);
            SiblingEdge edge;
            edge.tag = id.tag;
            edge.id = id.id;
            edge.classes = std::move(id.classes);
            edge.following = left->combinator == LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING;
            edge.any = id.any;
            facts.sibling_edges.push_back(std::move(edge));
        }
        cursor = left->prev;
    }
}

/// Walks one comma alternative right to left, and into `:is()` / `:not()` / `:where()` / `:has()`
/// when it finds one. An id inside `:has()` belongs to a descendant, but it is still an id a
/// selector names - and the question the share key asks is only "could any selector see this
/// element's id".

}  // namespace css
}  // namespace zui
