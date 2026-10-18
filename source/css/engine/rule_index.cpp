// Subject-bucket rule index and applying indexed rules to a node.

#include "ascii.h"
#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include <lexbor/css/declaration.h>

#include "perf/perf_budget.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

namespace {

std::string ascii_lower(std::string_view text) { return ::zui::ascii_lower(text); }

enum class KeyKind : std::uint8_t { Universal, Id, Class, Tag };

struct SubjectKey {
    KeyKind kind = KeyKind::Universal;
    std::string name;
};

/// The subject compound is the rightmost one, after a trailing pseudo-element. An id beats a
/// class, and a class beats a type. Any one of those is enough to reject every other element.
/// A compound that has none of them can match an element the other buckets would skip.
SubjectKey subject_key(const lxb_css_selector_list_t* list) {
    SubjectKey out;
    if (list == nullptr || list->last == nullptr) {
        return out;
    }
    const lxb_css_selector_t* selector = list->last;
    if (selector->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
        selector = selector->prev;
    }
    if (selector == nullptr) {
        return out;
    }
    for (;;) {
        if (selector->type == LXB_CSS_SELECTOR_TYPE_ID && selector->name.data != nullptr &&
            selector->name.length > 0) {
            out.kind = KeyKind::Id;
            out.name.assign(view(selector->name));
        } else if (out.kind != KeyKind::Id && selector->type == LXB_CSS_SELECTOR_TYPE_CLASS &&
                   selector->name.data != nullptr && selector->name.length > 0 &&
                   out.kind != KeyKind::Class) {
            out.kind = KeyKind::Class;
            out.name.assign(view(selector->name));
        } else if (out.kind == KeyKind::Universal &&
                   selector->type == LXB_CSS_SELECTOR_TYPE_ELEMENT &&
                   selector->name.data != nullptr && selector->name.length > 0) {
            out.kind = KeyKind::Tag;
            out.name = ascii_lower(view(selector->name));
        }
        if (selector->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE || selector->prev == nullptr) {
            break;
        }
        selector = selector->prev;
    }
    if (out.name.empty()) {
        out.kind = KeyKind::Universal;
    }
    return out;
}

/// True when the bucket lookup is the whole match: one id, class, or type, or a lone
/// pseudo-element, and nothing else in the selector. A second simple (`div.cell`), a combinator,
/// or a pseudo-class still has to go through `matches_selector`.
bool selector_is_trivial(const lxb_css_selector_list_t* list) {
    if (list == nullptr || list->last == nullptr) {
        return false;
    }
    const lxb_css_selector_t* sel = list->last;
    if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
        if (pseudo_target_of(sel->u.pseudo.type) == PseudoTarget::None) {
            return false;
        }
        sel = sel->prev;
    }
    if (sel == nullptr) {
        return true;
    }
    if (sel->type != LXB_CSS_SELECTOR_TYPE_ID && sel->type != LXB_CSS_SELECTOR_TYPE_CLASS &&
        sel->type != LXB_CSS_SELECTOR_TYPE_ELEMENT) {
        return false;
    }
    if (sel->name.data == nullptr || sel->name.length == 0) {
        return false;
    }
    return sel->prev == nullptr;
}

/// Bit 31 is never a live node flag. `:visited` matches nothing, so a subject that requires it
/// fails before `matches_selector`. `:not(:visited)` does not set the bit.
constexpr std::uint32_t kBitUnmatchable = 1u << 31;

std::uint32_t plain_state_bit(unsigned type) {
    switch (type) {
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER:
            return kBitHover;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ACTIVE:
            return kBitActive;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS:
            return kBitFocus;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_VISIBLE:
            return kBitFocusVisible;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_CHECKED:
            return kBitChecked;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_DISABLED:
            return kBitDisabled;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_MODAL:
            return kBitModal;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ROOT:
            return kBitRootEdge;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_VISITED:
            return kBitUnmatchable;
        default:
            return 0;
    }
}

/// Subject compound only. A `:hover` on an ancestor is a different compound and must not reject
/// the subject. `:is()` / `:where()` / `:has()` are not inspected: a state inside them is not
/// required. `:not(:hover)` rejects when the bit is set; `:not(:enabled)` is left to the matcher
/// because a non-control matches it.
void subject_state_masks(const lxb_css_selector_list_t* list, std::uint32_t& require,
                         std::uint32_t& reject) {
    require = 0;
    reject = 0;
    if (list == nullptr || list->last == nullptr) {
        return;
    }
    const lxb_css_selector_t* sel = list->last;
    if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
        sel = sel->prev;
    }
    while (sel != nullptr) {
        if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS) {
            const unsigned type = sel->u.pseudo.type;
            if (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_ENABLED) {
                reject |= kBitDisabled;
            } else if (const std::uint32_t bit = plain_state_bit(type)) {
                require |= bit;
            }
        } else if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION &&
                   sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT) {
            const auto* nested =
                static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data);
            if (nested != nullptr && nested->next == nullptr && nested->last != nullptr &&
                nested->last->prev == nullptr &&
                nested->last->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS) {
                const unsigned type = nested->last->u.pseudo.type;
                const std::uint32_t bit = plain_state_bit(type);
                // `:not(:enabled)` matches non-controls. `:not(:visited)` matches everyone.
                if (bit != 0 && bit != kBitUnmatchable &&
                    type != LXB_CSS_SELECTOR_PSEUDO_CLASS_ENABLED) {
                    reject |= bit;
                }
            }
        }
        if (sel->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE || sel->prev == nullptr) {
            break;
        }
        sel = sel->prev;
    }
}

PseudoTarget trivial_pseudo_target(const lxb_css_selector_list_t* list) {
    if (list == nullptr || list->last == nullptr ||
        list->last->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
        return PseudoTarget::None;
    }
    return pseudo_target_of(list->last->u.pseudo.type);
}

/// One selector, one slot. A grouped rule used to share a slot across every subject, and the
/// match then walked the whole group. A `div` paid for `header`, `footer`, and the rest.
void bucket_list(RuleIndex& index, std::uint32_t slot, const lxb_css_selector_list_t* list) {
    SubjectKey key = subject_key(list);
    if (key.kind == KeyKind::Universal) {
        index.universal.push_back(slot);
        return;
    }
    auto& map = key.kind == KeyKind::Id      ? index.by_id
                : key.kind == KeyKind::Class ? index.by_class
                                             : index.by_tag;
    map[std::move(key.name)].push_back(slot);
}

void index_list(RuleIndex& index, lxb_css_rule_t* rule, OriginRank origin, std::string_view source,
                const MediaState& media, const LayerTree& layers, int layer_rank,
                std::vector<ContainerGate>& gates, int media_depth) {
    for (; rule != nullptr; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_LIST || rule->type == LXB_CSS_RULE_STYLESHEET) {
            index_list(index, lxb_css_rule_list(rule)->first, origin, source, media, layers,
                       layer_rank, gates, media_depth);
            continue;
        }
        if (rule->type == LXB_CSS_RULE_AT_RULE) {
            auto* at = lxb_css_rule_at(rule);
            if (at->type == LXB_CSS_AT_RULE_MEDIA && at->u.media != nullptr &&
                at->u.media->block != nullptr && at->prelude_begin <= at->prelude_end &&
                at->prelude_end <= source.size()) {
                const std::string_view prelude =
                    source.substr(at->prelude_begin, at->prelude_end - at->prelude_begin);
                if (media_matches(prelude, media)) {
                    index_list(index, at->u.media->block->first, origin, source, media, layers,
                               layer_rank, gates, media_depth + 1);
                }
            }
            if (at->type == LXB_CSS_AT_RULE__CUSTOM && at->u.custom != nullptr &&
                at->u.custom->block != nullptr) {
                const std::string_view name = view(at->u.custom->name);
                if (name == "supports") {
                    if (supports_matches(trim(view(at->u.custom->prelude)))) {
                        index_list(index, at->u.custom->block->first, origin, source, media, layers,
                                   layer_rank, gates, media_depth);
                    }
                } else if (name == "container") {
                    std::string cq_name;
                    std::string_view cq_cond;
                    if (parse_container_prelude(trim(view(at->u.custom->prelude)), cq_name,
                                                cq_cond)) {
                        ContainerGate gate;
                        gate.name = std::move(cq_name);
                        gate.condition.assign(cq_cond);
                        gates.push_back(std::move(gate));
                        index_list(index, at->u.custom->block->first, origin, source, media, layers,
                                   layer_rank, gates, media_depth);
                        gates.pop_back();
                    }
                } else if (name == "layer") {
                    auto found = layers.rule_rank.find(rule);
                    const int inner = found == layers.rule_rank.end() ? layer_rank : found->second;
                    index_list(index, at->u.custom->block->first, origin, source, media, layers,
                               inner, gates, media_depth);
                }
            }
            continue;
        }
        if (rule->type != LXB_CSS_RULE_STYLE) {
            continue;
        }
        auto* style = lxb_css_rule_style(rule);
        for (const lxb_css_selector_list_t* list = style->selector; list != nullptr;
             list = list->next) {
            IndexedRule indexed;
            indexed.style = style;
            indexed.source = source;
            indexed.origin = origin;
            indexed.layer_rank = layer_rank;
            indexed.gates = gates;
            indexed.only = list;
            indexed.trivial = selector_is_trivial(list);
            indexed.from_media = media_depth > 0;
            subject_state_masks(list, indexed.require_state, indexed.reject_state);
            const auto slot = static_cast<std::uint32_t>(index.rules.size());
            index.rules.push_back(std::move(indexed));
            bucket_list(index, slot, list);
        }
        if (style->child != nullptr) {
            index_list(index, &style->child->rule, origin, source, media, layers, layer_rank,
                       gates, media_depth);
        }
    }
}

const std::vector<std::uint32_t>* bucket_slots(
    const std::map<std::string, std::vector<std::uint32_t>, std::less<>>& map, std::string_view key) {
    const auto found = map.find(key);
    return found == map.end() ? nullptr : &found->second;
}

bool gates_match(html::Node* node, const std::vector<ContainerGate>& gates) {
    for (const ContainerGate& gate : gates) {
        html::Node* container = nullptr;
        for (html::Node* parent = node != nullptr ? node->parent : nullptr; parent != nullptr;
             parent = parent->parent) {
            if (!parent->query_size_known) {
                continue;
            }
            if (!gate.name.empty()) {
                const std::string* cname = parent->style("container-name");
                if (cname == nullptr || !container_name_has(*cname, gate.name)) {
                    continue;
                }
            }
            container = parent;
            break;
        }
        if (container == nullptr || !container_condition_matches(gate.condition, container->query_width,
                                                                container->query_height)) {
            return false;
        }
    }
    return true;
}

}  // namespace

RuleIndex build_rule_index(const std::vector<lxb_css_stylesheet*>& sheets,
                           const std::vector<std::string>& sources, const MediaState& media,
                           const LayerTree& layers) {
    RuleIndex index;
    std::vector<ContainerGate> gates;
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        lxb_css_stylesheet* sheet = sheets[i];
        if (sheet == nullptr || sheet->root == nullptr) {
            continue;
        }
        const std::string_view source =
            i < sources.size() ? std::string_view(sources[i]) : std::string_view{};
        const OriginRank origin = i == 0 ? kOriginUa : kOriginAuthor;
        index_list(index, sheet->root, origin, source, media, layers, layers.unlayered(), gates, 0);
    }
    return index;
}

void apply_indexed(const RuleIndex& index, html::Node* node, const html::Document& doc, int& order,
                   DeclSinks& sinks) {
    CascadeAcc match_clock(cascade_split_armed() ? &g_cascade_split.match_ms : nullptr);
    // Each bucket is already in stylesheet order. Merging them keeps cascade order. The buffers
    // are reused across nodes so a cold frame does not allocate once per element.
    struct Merge {
        std::vector<const std::vector<std::uint32_t>*> buckets;
        std::vector<std::size_t> cursor;
    };
    thread_local Merge merge;
    merge.buckets.clear();
    merge.cursor.clear();
    auto push_bucket = [&](const std::vector<std::uint32_t>* list) {
        if (list == nullptr || list->empty()) {
            return;
        }
        merge.buckets.push_back(list);
        merge.cursor.push_back(0);
    };
    push_bucket(&index.universal);
    if (node != nullptr) {
        bool tag_lower = true;
        for (unsigned char c : node->tag) {
            if (c >= 'A' && c <= 'Z') {
                tag_lower = false;
                break;
            }
        }
        if (tag_lower) {
            push_bucket(bucket_slots(index.by_tag, node->tag));
        } else {
            push_bucket(bucket_slots(index.by_tag, ascii_lower(node->tag)));
        }
        if (!node->id.empty()) {
            push_bucket(bucket_slots(index.by_id, node->id));
        }
        std::string_view classes = node->class_name;
        std::size_t i = 0;
        while (i < classes.size()) {
            while (i < classes.size() && (classes[i] == ' ' || classes[i] == '\t' ||
                                          classes[i] == '\n' || classes[i] == '\r')) {
                ++i;
            }
            std::size_t j = i;
            while (j < classes.size() && classes[j] != ' ' && classes[j] != '\t' &&
                   classes[j] != '\n' && classes[j] != '\r') {
                ++j;
            }
            if (j > i) {
                push_bucket(bucket_slots(index.by_class, classes.substr(i, j - i)));
            }
            i = j;
        }
    }
    std::uint32_t previous = 0;
    bool have_previous = false;
    const std::size_t count = merge.buckets.size();
    thread_local std::vector<std::uint32_t> ordered;
    ordered.clear();
    for (;;) {
        std::uint32_t best = 0;
        int which = -1;
        for (std::size_t b = 0; b < count; ++b) {
            if (merge.cursor[b] >= merge.buckets[b]->size()) {
                continue;
            }
            const std::uint32_t slot = (*merge.buckets[b])[merge.cursor[b]];
            if (which < 0 || slot < best) {
                best = slot;
                which = static_cast<int>(b);
            }
        }
        if (which < 0) {
            break;
        }
        ++merge.cursor[static_cast<std::size_t>(which)];
        if (have_previous && best == previous) {
            continue;
        }
        previous = best;
        have_previous = true;
        ordered.push_back(best);
    }
    std::uint32_t state = 0;
    if (node->hovered) {
        state |= kBitHover;
    }
    if (node->active) {
        state |= kBitActive;
    }
    if (node->focused) {
        state |= kBitFocus;
    }
    if (node->focused && node->focus_visible) {
        state |= kBitFocusVisible;
    }
    if (node->modal) {
        state |= kBitModal;
    }
    if (node->checked) {
        state |= kBitChecked;
    }
    if (node->disabled) {
        state |= kBitDisabled;
    }
    if (doc.root != nullptr && node == doc.root.get()) {
        state |= kBitRootEdge;
    }
    auto note_media = [&](const lxb_css_selector_list_t* list) {
        if (list == nullptr) {
            return;
        }
        for (const lxb_css_selector_list_t* have : sinks.media_rules) {
            if (have == list) {
                return;
            }
        }
        sinks.media_rules.push_back(list);
    };
    for (const std::uint32_t best : ordered) {
        const IndexedRule& rule = index.rules[best];
        if (rule.style == nullptr) {
            continue;
        }
        if (!rule.gates.empty() && !gates_match(node, rule.gates)) {
            continue;
        }
        if (rule.require_state != 0 && (state & rule.require_state) != rule.require_state) {
            continue;
        }
        if (rule.reject_state != 0 && (state & rule.reject_state) != 0) {
            continue;
        }
        if (rule.trivial) {
            ++g_cascade_split.trivial;
            const PseudoTarget target = trivial_pseudo_target(rule.only);
            apply_decls(rule.style->declarations, rule.only->specificity, rule.origin, order,
                        sinks.for_target(target), rule.source, rule.layer_rank, false);
            if (rule.from_media) {
                note_media(rule.only);
            }
            continue;
        }
        // `only` is one alternative of a comma group. Walking `next` would retest the rest.
        const lxb_css_selector_list_t* const first =
            rule.only != nullptr ? rule.only : rule.style->selector;
        const lxb_css_selector_list_t* const end = rule.only != nullptr ? rule.only->next : nullptr;
        for (const lxb_css_selector_list_t* list = first; list != end; list = list->next) {
            PseudoTarget target = PseudoTarget::None;
            const bool matched = matches_selector(node, doc, list, target);
            if (!matched) {
                continue;
            }
            if (rule.from_media) {
                note_media(rule.only);
            }
            apply_decls(rule.style->declarations, list->specificity, rule.origin, order,
                        sinks.for_target(target), rule.source, rule.layer_rank, false);
        }
    }
}

bool media_rule_matches(const IndexedRule& rule, html::Node* node, const html::Document& doc) {
    if (rule.style == nullptr || node == nullptr) {
        return false;
    }
    if (!rule.gates.empty() && !gates_match(node, rule.gates)) {
        return false;
    }
    std::uint32_t state = 0;
    if (node->hovered) {
        state |= kBitHover;
    }
    if (node->active) {
        state |= kBitActive;
    }
    if (node->focused) {
        state |= kBitFocus;
    }
    if (node->focused && node->focus_visible) {
        state |= kBitFocusVisible;
    }
    if (node->modal) {
        state |= kBitModal;
    }
    if (node->checked) {
        state |= kBitChecked;
    }
    if (node->disabled) {
        state |= kBitDisabled;
    }
    if (doc.root != nullptr && node == doc.root.get()) {
        state |= kBitRootEdge;
    }
    if (rule.require_state != 0 && (state & rule.require_state) != rule.require_state) {
        return false;
    }
    if (rule.reject_state != 0 && (state & rule.reject_state) != 0) {
        return false;
    }
    if (rule.trivial) {
        return true;
    }
    const lxb_css_selector_list_t* const first =
        rule.only != nullptr ? rule.only : rule.style->selector;
    const lxb_css_selector_list_t* const end = rule.only != nullptr ? rule.only->next : nullptr;
    for (const lxb_css_selector_list_t* list = first; list != end; list = list->next) {
        PseudoTarget target = PseudoTarget::None;
        if (matches_selector(node, doc, list, target)) {
            return true;
        }
    }
    return false;
}


}  // namespace detail
}  // namespace css
}  // namespace zui
