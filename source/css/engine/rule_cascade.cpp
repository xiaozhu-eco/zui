// Rule cascade: serialize a parsed declaration, order the cascade layers, and walk a stylesheet
// into per-pseudo-element declaration sinks. The actual winner selection lives in
// `declaration_wins`; `computed_style.cpp` turns the sinks into computed declaration maps.

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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

// ---- lexbor declaration serialization ------------------------------------------------------

std::string_view view(const lexbor_str_t& s) {
    if (s.data == nullptr || s.length == 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(s.data), s.length};
}

namespace {

struct Buf {
    std::string s;
};

lxb_status_t append_cb(const lxb_char_t* data, size_t len, void* ctx) {
    static_cast<Buf*>(ctx)->s.append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

/// Serializer target that stays on the stack for the short values a declaration usually has.
}  // namespace

std::string_view canonical_name(const lxb_css_rule_declaration_t* decl) {
    if (decl->type == LXB_CSS_PROPERTY__CUSTOM && decl->u.custom != nullptr &&
        decl->u.custom->name.data != nullptr) {
        return {reinterpret_cast<const char*>(decl->u.custom->name.data), decl->u.custom->name.length};
    }
    std::uintptr_t id = decl->type;
    if (decl->type == LXB_CSS_PROPERTY__UNDEF && decl->u.undef != nullptr) {
        id = decl->u.undef->type;
    }
    const lxb_css_entry_data_t* entry = lxb_css_property_by_id(id);
    if (entry == nullptr || entry->name == nullptr || entry->length == 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(entry->name), entry->length};
}

std::string serialize_name(const lxb_css_rule_declaration_t* decl) {
    if (decl->type == LXB_CSS_PROPERTY__CUSTOM && decl->u.custom != nullptr &&
        decl->u.custom->name.data != nullptr) {
        return {reinterpret_cast<const char*>(decl->u.custom->name.data),
                decl->u.custom->name.length};
    }
    Buf b;
    (void)lxb_css_property_serialize_name(
        decl->u.user, static_cast<lxb_css_property_type_t>(decl->type), append_cb, &b);
    return b.s;
}

std::string serialize_value(const lxb_css_rule_declaration_t* decl) {
    if (decl->type == LXB_CSS_PROPERTY__CUSTOM && decl->u.custom != nullptr &&
        decl->u.custom->value.data != nullptr) {
        return {reinterpret_cast<const char*>(decl->u.custom->value.data),
                decl->u.custom->value.length};
    }
    Buf b;
    (void)lxb_css_property_serialize(decl->u.user, static_cast<lxb_css_property_type_t>(decl->type),
                                     append_cb, &b);
    return b.s;
}

// ---- cascade order -------------------------------------------------------------------------

CascadeLayer cascade_layer(OriginRank origin, bool important) {
    if (origin == kOriginEmpty) {
        return kLayerEmpty;
    }
    if (origin == kOriginUa) {
        return important ? kLayerUaImportant : kLayerUaNormal;
    }
    return important ? kLayerAuthorImportant : kLayerAuthorNormal;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}
void rank_layers(LayerTree& tree, int node, int& next) {
    std::vector<int> kids = tree.nodes[static_cast<size_t>(node)].kids;
    for (int kid : kids) {
        rank_layers(tree, kid, next);
    }
    tree.nodes[static_cast<size_t>(node)].rank = next++;
}

/// Walks the sheets in cascade order and fills in the layer tree. `rule_rank` records, for every
/// `@layer` block, the tree node its styles belong to; `finish_layers` turns those into ranks.
void collect_layer_order(lxb_css_rule_t* rule, LayerTree& tree, int parent, std::string_view source,
                         const MediaState& media) {
    for (; rule != nullptr; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_LIST || rule->type == LXB_CSS_RULE_STYLESHEET) {
            collect_layer_order(lxb_css_rule_list(rule)->first, tree, parent, source, media);
            continue;
        }
        if (rule->type != LXB_CSS_RULE_AT_RULE) {
            continue;
        }
        auto* at = lxb_css_rule_at(rule);
        if (at->type == LXB_CSS_AT_RULE_MEDIA && at->u.media != nullptr &&
            at->u.media->block != nullptr && at->prelude_begin <= at->prelude_end &&
            at->prelude_end <= source.size()) {
            const std::string_view prelude =
                source.substr(at->prelude_begin, at->prelude_end - at->prelude_begin);
            // A layer inside a conditional group that does not apply is not declared either, so a
            // `@media print { @layer x { ... } }` cannot take an ordering slot on screen.
            if (media_matches(prelude, media)) {
                collect_layer_order(at->u.media->block->first, tree, parent, source, media);
            }
            continue;
        }
        if (at->type != LXB_CSS_AT_RULE__CUSTOM || at->u.custom == nullptr) {
            continue;
        }
        // `@supports` is a conditional group for layer order, same as `@media`.
        if (view(at->u.custom->name) == "supports" && at->u.custom->block != nullptr) {
            if (supports_matches(trim(view(at->u.custom->prelude)))) {
                collect_layer_order(at->u.custom->block->first, tree, parent, source, media);
            }
            continue;
        }
        if (view(at->u.custom->name) != "layer") {
            continue;
        }
        const std::string_view prelude = trim(view(at->u.custom->prelude));
        if (at->u.custom->block == nullptr) {
            // `@layer a, b.c;` declares a list of layers at this level, in order.
            size_t begin = 0;
            for (size_t i = 0; i <= prelude.size(); ++i) {
                if (i != prelude.size() && prelude[i] != ',') {
                    continue;
                }
                const std::string_view name = trim(prelude.substr(begin, i - begin));
                begin = i + 1;
                if (!name.empty()) {
                    tree.path(parent, name);
                }
            }
            continue;
        }
        int inner = parent;
        if (prelude.find(',') == std::string_view::npos) {
            // The block form takes one name; `@layer { ... }` is an anonymous layer, which still
            // gets its own slot before the unlayered declarations of its parent.
            inner = tree.path(
                parent, prelude.empty() ? "\x01anon" + std::to_string(tree.anonymous++) : prelude);
        }
        tree.rule_rank[rule] = inner;
        collect_layer_order(at->u.custom->block->first, tree, inner, source, media);
    }
}

/// Depth-first with each layer's own slot after its sub-layers. Node 0 (the root) ends up with the
/// highest rank, which is the unlayered slot.
void finish_layers(LayerTree& tree) {
    int next = 1;
    rank_layers(tree, 0, next);
    for (auto& entry : tree.rule_rank) {
        entry.second = tree.nodes[static_cast<size_t>(entry.second)].rank;
    }
}
bool declaration_wins(const DeclKey& a, const DeclKey& b) {
    if (a.band != b.band) {
        return a.band > b.band;
    }
    // A declaration in a `style` attribute beats a rule of the same origin and importance whatever
    // the rule's specificity or layer - the example/reader sets widths that way.
    if (a.element_attached != b.element_attached) {
        return a.element_attached;
    }
    if (a.layer != b.layer) {
        const bool important = a.band == kLayerAuthorImportant || a.band == kLayerUaImportant;
        return important ? a.layer < b.layer : a.layer > b.layer;
    }
    if (a.spec != b.spec) {
        return a.spec > b.spec;
    }
    return a.order >= b.order;
}
PseudoTarget pseudo_target_of(std::uint32_t id) {
    switch (id) {
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:
            return PseudoTarget::Before;
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:
            return PseudoTarget::After;
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER:
            return PseudoTarget::Placeholder;
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_MARKER:
            return PseudoTarget::Marker;
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION:
            return PseudoTarget::Selection;
        case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP:
            return PseudoTarget::Backdrop;
        default:
            return PseudoTarget::None;
    }
}

/// Matches one selector-list item and reports which declaration set it feeds. A pseudo-element
/// may only be the last simple selector, and an item that names one never matches the element
/// itself, so the item decides the target on its own. Unsupported highlight/tree-abiding pseudos
/// still map to `None` and match nothing; `::selection` feeds the Selection sink (no box).
/// CSS Containment: `container-name` is a space-separated list of custom idents. The keyword
/// `none` clears the list. A query matches when any ident equals the query name.
bool container_name_has(std::string_view list, std::string_view name) {
    if (name.empty()) {
        return false;
    }
    std::size_t begin = 0;
    while (begin < list.size() && std::isspace(static_cast<unsigned char>(list[begin]))) {
        ++begin;
    }
    std::size_t end = list.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(list[end - 1]))) {
        --end;
    }
    if (end - begin == 4 &&
        std::tolower(static_cast<unsigned char>(list[begin])) == 'n' &&
        std::tolower(static_cast<unsigned char>(list[begin + 1])) == 'o' &&
        std::tolower(static_cast<unsigned char>(list[begin + 2])) == 'n' &&
        std::tolower(static_cast<unsigned char>(list[begin + 3])) == 'e') {
        return false;
    }
    std::size_t i = begin;
    while (i < end) {
        while (i < end && std::isspace(static_cast<unsigned char>(list[i]))) {
            ++i;
        }
        if (i >= end) {
            break;
        }
        std::size_t j = i;
        while (j < end && !std::isspace(static_cast<unsigned char>(list[j]))) {
            ++j;
        }
        if (list.substr(i, j - i) == name) {
            return true;
        }
        i = j;
    }
    return false;
}

bool matches_selector(html::Node* node, const html::Document& doc,
                      const lxb_css_selector_list_t* list, PseudoTarget& target) {
    zui::perf::note_cascade_selector_test();
    target = PseudoTarget::None;
    if (list == nullptr || list->last == nullptr) {
        return false;
    }
    const lxb_css_selector_t* last = list->last;
    if (last->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) {
        target = pseudo_target_of(last->u.pseudo.type);
        if (target == PseudoTarget::None) {
            return false;
        }
        last = last->prev;
    }
    const bool matched = last == nullptr || match_from(node, doc, last);
    return matched;
}

void visit_rules(lxb_css_rule_t* rule, html::Node* node, const html::Document& doc, int& order,
                 DeclSinks& sinks, OriginRank origin, bool from_style_attr, std::string_view source,
                 const MediaState& media, const LayerTree& layers, int layer_rank) {
    for (; rule != nullptr; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_LIST || rule->type == LXB_CSS_RULE_STYLESHEET) {
            visit_rules(lxb_css_rule_list(rule)->first, node, doc, order, sinks, origin,
                        from_style_attr, source, media, layers, layer_rank);
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
                    visit_rules(at->u.media->block->first, node, doc, order, sinks, origin,
                                from_style_attr, source, media, layers, layer_rank);
                }
            }
            if (at->type == LXB_CSS_AT_RULE__CUSTOM && at->u.custom != nullptr &&
                at->u.custom->block != nullptr) {
                const std::string_view name = view(at->u.custom->name);
                if (name == "supports") {
                    if (supports_matches(trim(view(at->u.custom->prelude)))) {
                        visit_rules(at->u.custom->block->first, node, doc, order, sinks, origin,
                                    from_style_attr, source, media, layers, layer_rank);
                    }
                } else if (name == "container") {
                    // CSS Containment 3: match against the nearest (or named) query container's
                    // content-box size published by layout. Unknown size fails the query so the
                    // first cascade before layout does not apply container rules.
                    std::string cq_name;
                    std::string_view cq_cond;
                    if (parse_container_prelude(trim(view(at->u.custom->prelude)), cq_name,
                                                cq_cond)) {
                        html::Node* container = nullptr;
                        for (html::Node* p = node != nullptr ? node->parent : nullptr; p != nullptr;
                             p = p->parent) {
                            if (!p->query_size_known) {
                                continue;
                            }
                            if (!cq_name.empty()) {
                                const std::string* cname = p->style("container-name");
                                if (cname == nullptr || !container_name_has(*cname, cq_name)) {
                                    continue;
                                }
                            }
                            container = p;
                            break;
                        }
                        if (container != nullptr &&
                            container_condition_matches(cq_cond, container->query_width,
                                                        container->query_height)) {
                            visit_rules(at->u.custom->block->first, node, doc, order, sinks, origin,
                                        from_style_attr, source, media, layers, layer_rank);
                        }
                    }
                } else if (name == "layer") {
                    // `@layer a { ... }` styles layer `a`. The rank comes from the tree the
                    // layer-order pass built, so this is a lookup and not a second parse.
                    auto found = layers.rule_rank.find(rule);
                    const int inner = found == layers.rule_rank.end() ? layer_rank : found->second;
                    visit_rules(at->u.custom->block->first, node, doc, order, sinks, origin,
                                from_style_attr, source, media, layers, inner);
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
            PseudoTarget target = PseudoTarget::None;
            if (!matches_selector(node, doc, list, target)) {
                continue;
            }
            // A style attribute holds declarations for the element. A selector that also names a
            // pseudo-element only applies when the attribute really is on that pseudo, which it
            // never is.
            if (from_style_attr && target != PseudoTarget::None) {
                continue;
            }
            uint32_t spec = list->specificity;
            if (from_style_attr) {
                spec = lxb_css_selector_sp_up_s(spec);
            }
            apply_decls(style->declarations, spec, origin, order, sinks.for_target(target), source,
                        layer_rank, from_style_attr);
        }
        if (style->child != nullptr) {
            visit_rules(&style->child->rule, node, doc, order, sinks, origin, from_style_attr,
                        source, media, layers, layer_rank);
        }
    }
}

}  // namespace detail
}  // namespace css
}  // namespace zui
