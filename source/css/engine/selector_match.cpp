// Selector matching: does one html::Node match a parsed lexbor selector? Everything here is
// node-local or walks the document for combinators, `:has()`, and positional pseudo-classes.
// `rule_cascade.cpp` calls match_list() once per rule; the sibling `:has()` / hover facts and
// the cascade walk reuse the narrower entry points declared in cascade_internal.h.

#include "ascii.h"
#include "css/engine/cascade_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

using html::iequals;
using html::has_class_token;

struct HasIdStore {
    std::unordered_map<std::string_view, std::vector<html::Node*>> ids;
};

namespace {
thread_local HasIdStore* g_has_id_index = nullptr;
}  // namespace

HasIdIndex::HasIdIndex(const html::Document& doc) : previous_(g_has_id_index) {
    auto* store = new HasIdStore;
    store_ = store;
    // Preorder, so the first duplicate id is the one document order would name first.
    std::vector<html::Node*> pending;
    if (doc.root != nullptr) {
        pending.push_back(doc.root.get());
    }
    while (!pending.empty()) {
        html::Node* node = pending.back();
        pending.pop_back();
        if (node == nullptr) {
            continue;
        }
        if (html::is_element(node) && !node->id.empty()) {
            store->ids[node->id].push_back(node);
        }
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    g_has_id_index = store;
}

HasIdIndex::~HasIdIndex() {
    g_has_id_index = static_cast<HasIdStore*>(previous_);
    delete static_cast<HasIdStore*>(store_);
    store_ = nullptr;
}

bool match_simple(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel);
bool matches_nth_pseudo(html::Node* node, const html::Document& doc, unsigned type,
                        const void* data);
void gather_descendants(html::Node* n, std::vector<html::Node*>& out);

void gather_related(html::Node* origin, unsigned combinator, std::vector<html::Node*>& out) {
    if (origin == nullptr) {
        return;
    }
    switch (combinator) {
        case LXB_CSS_SELECTOR_COMBINATOR_CHILD:
            for (const auto& ch : origin->children) {
                if (html::is_element(ch.get())) {
                    out.push_back(ch.get());
                }
            }
            break;
        case LXB_CSS_SELECTOR_COMBINATOR_SIBLING:
            if (origin->parent != nullptr) {
                bool seen = false;
                for (const auto& ch : origin->parent->children) {
                    if (seen && html::is_element(ch.get())) {
                        out.push_back(ch.get());
                        break;
                    }
                    if (ch.get() == origin) {
                        seen = true;
                    }
                }
            }
            break;
        case LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING:
            if (origin->parent != nullptr) {
                bool seen = false;
                for (const auto& ch : origin->parent->children) {
                    if (seen && html::is_element(ch.get())) {
                        out.push_back(ch.get());
                    }
                    if (ch.get() == origin) {
                        seen = true;
                    }
                }
            }
            break;
        default:
            // Descendant, and any combinator this switch does not name. A direct walk: the old
            // `std::function` lambda allocated on every `:has()`, and a file tree paid that once
            // per row per argument.
            gather_descendants(origin, out);
            break;
    }
}

void gather_descendants(html::Node* n, std::vector<html::Node*>& out) {
    for (const auto& ch : n->children) {
        html::Node* el = ch.get();
        if (!html::is_element(el)) {
            continue;
        }
        out.push_back(el);
        gather_descendants(el, out);
    }
}

bool match_chain(html::Node* node, const html::Document& doc,
                 const lxb_css_selector_t* const* chain, size_t chain_n, size_t index) {
    if (node == nullptr || index >= chain_n || !match_simple(node, doc, chain[index])) {
        return false;
    }
    if (index + 1 == chain_n) {
        return true;
    }
    const unsigned comb = chain[index + 1]->combinator;
    if (comb == LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
        return match_chain(node, doc, chain, chain_n, index + 1);
    }
    std::vector<html::Node*> next;
    gather_related(node, comb, next);
    for (html::Node* n : next) {
        if (match_chain(n, doc, chain, chain_n, index + 1)) {
            return true;
        }
    }
    return false;
}

// `:has()` relative to `origin`. Descendant (the implied combinator) searches in place and stops
// at the first hit, instead of allocating every descendant and then testing them.
bool match_relative(html::Node* origin, const html::Document& doc, unsigned comb,
                    const lxb_css_selector_t* const* chain, size_t chain_n) {
    if (origin == nullptr) {
        return false;
    }
    if (comb == LXB_CSS_SELECTOR_COMBINATOR_DESCENDANT) {
        for (const auto& ch : origin->children) {
            html::Node* el = ch.get();
            if (!html::is_element(el)) {
                continue;
            }
            if (match_chain(el, doc, chain, chain_n, 0) ||
                match_relative(el, doc, comb, chain, chain_n)) {
                return true;
            }
        }
        return false;
    }
    std::vector<html::Node*> seeds;
    gather_related(origin, comb, seeds);
    for (html::Node* n : seeds) {
        if (match_chain(n, doc, chain, chain_n, 0)) {
            return true;
        }
    }
    return false;
}

/// `:has(#id)` and `:has(#id:checked)` name one element. The id index answers that without
/// walking the anchor's descendants. A relative selector with no id, or with another compound,
/// returns false here and the caller keeps the tree walk. `*matched` is the answer when this
/// returns true.
bool id_has_decided(html::Node* anchor, const html::Document& doc,
                    const lxb_css_selector_t* const* chain, size_t chain_n, unsigned comb,
                    bool& matched) {
    matched = false;
    if (g_has_id_index == nullptr || chain_n == 0) {
        return false;
    }
    if (comb != LXB_CSS_SELECTOR_COMBINATOR_DESCENDANT &&
        comb != LXB_CSS_SELECTOR_COMBINATOR_CHILD) {
        return false;
    }
    const lxb_css_selector_t* id_sel = nullptr;
    for (size_t i = 0; i < chain_n; ++i) {
        if (i > 0 && chain[i]->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
            return false;
        }
        if (chain[i]->type == LXB_CSS_SELECTOR_TYPE_ID) {
            if (id_sel != nullptr) {
                return false;
            }
            id_sel = chain[i];
        }
    }
    if (id_sel == nullptr) {
        return false;
    }
    const auto found = g_has_id_index->ids.find(view(id_sel->name));
    if (found == g_has_id_index->ids.end()) {
        return true;
    }
    for (html::Node* target : found->second) {
        if (!match_chain(target, doc, chain, chain_n, 0)) {
            continue;
        }
        if (comb == LXB_CSS_SELECTOR_COMBINATOR_CHILD) {
            if (target->parent == anchor) {
                matched = true;
                return true;
            }
            continue;
        }
        for (html::Node* parent = target->parent; parent != nullptr; parent = parent->parent) {
            if (parent == anchor) {
                matched = true;
                return true;
            }
        }
    }
    return true;
}

bool match_has(html::Node* anchor, const html::Document& doc, const lxb_css_selector_list_t* list) {
    if (anchor == nullptr) {
        return false;
    }
    ++g_has_calls;
    for (; list != nullptr; list = list->next) {
        if (list->last == nullptr) {
            continue;
        }
        // The argument is a handful of compounds. Building it on the stack keeps a cold cascade
        // from allocating a chain vector once per element per `:has()`.
        const lxb_css_selector_t* rev[32];
        int n = 0;
        bool overflow = false;
        for (const lxb_css_selector_t* s = list->last; s != nullptr; s = s->prev) {
            if (n == 32) {
                overflow = true;
                break;
            }
            rev[n++] = s;
        }
        if (overflow || n == 0) {
            std::vector<const lxb_css_selector_t*> chain;
            for (const lxb_css_selector_t* s = list->last; s != nullptr; s = s->prev) {
                chain.push_back(s);
            }
            std::reverse(chain.begin(), chain.end());
            unsigned comb = chain.front()->combinator;
            if (comb == LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
                comb = LXB_CSS_SELECTOR_COMBINATOR_DESCENDANT;
            }
            bool matched = false;
            if (id_has_decided(anchor, doc, chain.data(), chain.size(), comb, matched)) {
                if (matched) {
                    return true;
                }
                continue;
            }
            if (match_relative(anchor, doc, comb, chain.data(), chain.size())) {
                return true;
            }
            continue;
        }
        const lxb_css_selector_t* chain[32];
        for (int i = 0; i < n; ++i) {
            chain[i] = rev[n - 1 - i];
        }
        unsigned comb = chain[0]->combinator;
        // Selectors 4: a relative selector with no combinator implies the descendant combinator.
        if (comb == LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
            comb = LXB_CSS_SELECTOR_COMBINATOR_DESCENDANT;
        }
        bool matched = false;
        if (id_has_decided(anchor, doc, chain, static_cast<size_t>(n), comb, matched)) {
            if (matched) {
                return true;
            }
            continue;
        }
        if (match_relative(anchor, doc, comb, chain, static_cast<size_t>(n))) {
            return true;
        }
    }
    return false;
}

bool match_attr(const html::Node* node, const lxb_css_selector_t* sel) {
    // A node carries a handful of attributes. Probing the map with a temporary `std::string`
    // allocated on every attribute selector, including the ones that miss.
    const std::string_view name = view(sel->name);
    const std::string* have_owned = nullptr;
    for (const auto& kv : node->attrs) {
        if (kv.first == name) {
            have_owned = &kv.second;
            break;
        }
    }
    const bool exists = have_owned != nullptr;
    const lxb_css_selector_attribute_t& a = sel->u.attribute;
    if (a.value.data == nullptr || a.value.length == 0) {
        return exists;
    }
    if (!exists) {
        return false;
    }
    const std::string_view want = view(a.value);
    const std::string_view have = *have_owned;
    auto cmp = [&](std::string_view x, std::string_view y) {
        return a.modifier == LXB_CSS_SELECTOR_MODIFIER_I ? iequals(x, y) : x == y;
    };
    switch (a.match) {
        case LXB_CSS_SELECTOR_MATCH_EQUAL:
            return cmp(have, want);
        case LXB_CSS_SELECTOR_MATCH_PREFIX:
            return have.size() >= want.size() && cmp(have.substr(0, want.size()), want);
        case LXB_CSS_SELECTOR_MATCH_SUFFIX:
            return have.size() >= want.size() && cmp(have.substr(have.size() - want.size()), want);
        case LXB_CSS_SELECTOR_MATCH_SUBSTRING:
            if (a.modifier == LXB_CSS_SELECTOR_MODIFIER_I) {
                auto lower = [](std::string_view s) {
                    std::string o(s);
                    for (char& c : o) {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    return o;
                };
                return lower(have).find(lower(want)) != std::string::npos;
            }
            return have.find(want) != std::string_view::npos;
        case LXB_CSS_SELECTOR_MATCH_INCLUDE:
            return has_class_token(have, want);
        case LXB_CSS_SELECTOR_MATCH_DASH:
            return cmp(have, want) ||
                   (have.size() > want.size() && cmp(have.substr(0, want.size()), want) &&
                    have[want.size()] == '-');
        default:
            return false;
    }
}

bool match_simple(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel);

/// The one simple selector whose match walks a subtree (`:has()`).
bool is_has_selector(const lxb_css_selector_t* sel) {
    return sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION &&
           sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS;
}

bool match_from(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel) {
    if (node == nullptr || sel == nullptr) {
        return false;
    }
    // The simple selectors of one compound are linked through `prev` with the CLOSE combinator, and
    // all of them test the same node - so the order between them is free, and `:has()` goes last.
    // Without that, `:root:has(#font_sans:checked) .ch` walks the whole subtree of every ancestor of
    // every `.ch` before `:root` gets to reject it on the tag, which is what made the reader's
    // table of contents re-match a document-sized tree per chapter.
    const lxb_css_selector_t* left = sel;
    while (left->combinator == LXB_CSS_SELECTOR_COMBINATOR_CLOSE && left->prev != nullptr) {
        left = left->prev;
    }
    for (const lxb_css_selector_t* s = sel;; s = s->prev) {
        if (!is_has_selector(s) && !match_simple(node, doc, s)) {
            return false;
        }
        if (s == left) {
            break;
        }
    }
    for (const lxb_css_selector_t* s = sel;; s = s->prev) {
        if (is_has_selector(s) && !match_simple(node, doc, s)) {
            return false;
        }
        if (s == left) {
            break;
        }
    }
    if (left->prev == nullptr) {
        return true;
    }
    switch (left->combinator) {
        case LXB_CSS_SELECTOR_COMBINATOR_CHILD:
            return node->parent != nullptr && match_from(node->parent, doc, left->prev);
        case LXB_CSS_SELECTOR_COMBINATOR_DESCENDANT:
            for (html::Node* p = node->parent; p != nullptr; p = p->parent) {
                if (match_from(p, doc, left->prev)) {
                    return true;
                }
            }
            return false;
        case LXB_CSS_SELECTOR_COMBINATOR_SIBLING:
            return match_from(prev_element(node), doc, left->prev);
        case LXB_CSS_SELECTOR_COMBINATOR_FOLLOWING:
            for (html::Node* p = prev_element(node); p != nullptr; p = prev_element(p)) {
                if (match_from(p, doc, left->prev)) {
                    return true;
                }
            }
            return false;
        case LXB_CSS_SELECTOR_COMBINATOR_CLOSE:
            // Unreachable: the loop above consumed every CLOSE link. Kept so a future selector
            // shape that reaches here is treated as "the same node", not as no match.
            return match_from(node, doc, left->prev);
        default:
            return false;
    }
}

bool match_simple(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel) {
    switch (sel->type) {
        case LXB_CSS_SELECTOR_TYPE_ANY:
            return true;
        case LXB_CSS_SELECTOR_TYPE_ELEMENT:
            return iequals(node->tag, view(sel->name));
        case LXB_CSS_SELECTOR_TYPE_ID:
            return node->id == view(sel->name);
        case LXB_CSS_SELECTOR_TYPE_CLASS:
            return has_class_token(node->class_name, view(sel->name));
        case LXB_CSS_SELECTOR_TYPE_ATTRIBUTE:
            return match_attr(node, sel);
        case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS:
            return match_plain_pseudo_class(node, sel->u.pseudo.type, doc.root.get(), &doc);
        case LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION:
            if (sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_DIR) {
                const auto* contains =
                    static_cast<const lxb_css_selector_contains_t*>(sel->u.pseudo.data);
                if (contains == nullptr || contains->str.data == nullptr) {
                    return false;
                }
                const std::string_view arg(reinterpret_cast<const char*>(contains->str.data),
                                           contains->str.length);
                const bool rtl = element_is_rtl(*node);
                if (iequals(arg, "rtl")) {
                    return rtl;
                }
                if (iequals(arg, "ltr")) {
                    return !rtl;
                }
                return false;
            }
            if (sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_LANG) {
                const auto* contains =
                    static_cast<const lxb_css_selector_contains_t*>(sel->u.pseudo.data);
                if (contains == nullptr || contains->str.data == nullptr) {
                    return false;
                }
                return language_matches_list(
                    *node, std::string_view(reinterpret_cast<const char*>(contains->str.data),
                                            contains->str.length));
            }
            if (sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT) {
                return !match_list(node, doc,
                                   static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data));
            }
            if (sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS ||
                sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE) {
                return match_list(node, doc,
                                  static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data));
            }
            if (sel->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS) {
                return match_has(node, doc,
                                 static_cast<const lxb_css_selector_list_t*>(sel->u.pseudo.data));
            }
            return matches_nth_pseudo(node, doc, sel->u.pseudo.type, sel->u.pseudo.data);
        default:
            return false;
    }
}

bool match_list(html::Node* node, const html::Document& doc, const lxb_css_selector_list_t* list) {
    for (; list != nullptr; list = list->next) {
        if (list->last != nullptr && match_from(node, doc, list->last)) {
            return true;
        }
    }
    return false;
}


}  // namespace detail
}  // namespace css
}  // namespace zui
