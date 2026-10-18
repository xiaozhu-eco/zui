// :nth-child / :nth-of-type position counting and An+B matching.

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

html::Node* prev_element(html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return nullptr;
    }
    html::Node* prev = nullptr;
    for (const auto& ch : node->parent->children) {
        if (ch.get() == node) {
            return prev;
        }
        if (html::is_element(ch.get())) {
            prev = ch.get();
        }
    }
    return nullptr;
}

/// Selectors 4 `:first-child` / `:nth-child(1)`: first among **element** siblings. Text and
/// comment nodes do not disqualify the first element child. Must stay aligned with ShareKey
/// `kBitFirst`, which also indexes elements only.
bool is_first_element(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return false;
    }
    for (const auto& ch : node->parent->children) {
        if (html::is_element(ch.get())) {
            return ch.get() == node;
        }
    }
    return false;
}

bool is_last_element(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return false;
    }
    const auto& kids = node->parent->children;
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (html::is_element(it->get())) {
            return it->get() == node;
        }
    }
    return false;
}

bool is_first_of_type(const html::Node* node) {
    if (node->parent == nullptr) {
        return true;
    }
    for (const auto& ch : node->parent->children) {
        if (ch->tag == node->tag) {
            return ch.get() == node;
        }
    }
    return true;
}

bool is_last_of_type(const html::Node* node) {
    if (node->parent == nullptr) {
        return true;
    }
    const html::Node* last = nullptr;
    for (const auto& ch : node->parent->children) {
        if (ch->tag == node->tag) {
            last = ch.get();
        }
    }
    return last == node;
}

/// Selectors 4 `:nth-child(An+B)` family. These need the node's 1-based position among its
/// **element** siblings (text and comment nodes do not count), and the same position restricted to
/// siblings of the same type for the `of-type` forms. A node with no parent has no position.
int element_position(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    int index = 0;
    for (const auto& ch : node->parent->children) {
        if (!html::is_element(ch.get())) {
            continue;
        }
        ++index;
        if (ch.get() == node) {
            return index;
        }
    }
    return 0;
}

int element_position_from_end(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    int index = 0;
    const auto& kids = node->parent->children;
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (!html::is_element(it->get())) {
            continue;
        }
        ++index;
        if (it->get() == node) {
            return index;
        }
    }
    return 0;
}

int type_position(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    int index = 0;
    for (const auto& ch : node->parent->children) {
        if (ch->tag != node->tag) {
            continue;
        }
        ++index;
        if (ch.get() == node) {
            return index;
        }
    }
    return 0;
}

int type_position_from_end(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    int index = 0;
    const auto& kids = node->parent->children;
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (it->get()->tag != node->tag) {
            continue;
        }
        ++index;
        if (it->get() == node) {
            return index;
        }
    }
    return 0;
}

/// `An+B` terms match the 1-based positions `a*n + b` for integer `n >= 0` (CSS Syntax 3 section
/// "The An+B microsyntax"). Positions are positive, so `b` alone decides the `a == 0` case.
bool matches_anb(long a, long b, int position) {
    if (position <= 0) {
        return false;
    }
    const long delta = static_cast<long>(position) - b;
    if (a == 0) {
        return delta == 0;
    }
    if (delta % a != 0) {
        return false;
    }
    return delta / a >= 0;
}

/// The 1-based position `:nth-*()` sees. Without `of S` every element sibling counts; with it only
/// the siblings that match the list do, so the position cannot be read off the child index. The
/// element itself has to match `of S` as well, otherwise it is not in the counted set at all.

int nth_position(html::Node* node, const html::Document& doc, bool of_type, bool from_end,
                 const lxb_css_selector_list_t* of) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    const auto counts = [&](html::Node* sibling) {
        if (of_type && sibling->tag != node->tag) {
            return false;
        }
        return of == nullptr || match_list(sibling, doc, of);
    };
    int index = 0;
    const auto& kids = node->parent->children;
    if (from_end) {
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            html::Node* sibling = it->get();
            if (!html::is_element(sibling)) {
                continue;
            }
            if (sibling == node) {
                return counts(sibling) ? index + 1 : 0;
            }
            if (counts(sibling)) {
                ++index;
            }
        }
        return 0;
    }
    for (const auto& child : kids) {
        html::Node* sibling = child.get();
        if (!html::is_element(sibling)) {
            continue;
        }
        if (sibling == node) {
            return counts(sibling) ? index + 1 : 0;
        }
        if (counts(sibling)) {
            ++index;
        }
    }
    return 0;
}

/// Selectors 4 `:nth-child()` / `:nth-last-child()` / `:nth-of-type()` / `:nth-last-of-type()`.
/// lexbor hands the parsed `An+B [of S]` over as `lxb_css_selector_anb_of_t`; `:nth-col()` /
/// `:nth-last-col()` describe table columns and have no meaning here.
bool matches_nth_pseudo(html::Node* node, const html::Document& doc, unsigned type,
                        const void* data) {
    bool of_type = false;
    bool from_end = false;
    switch (type) {
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_CHILD:
            break;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD:
            from_end = true;
            break;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_OF_TYPE:
            of_type = true;
            break;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_OF_TYPE:
            of_type = true;
            from_end = true;
            break;
        default:
            return false;
    }
    const auto* anb_of = static_cast<const lxb_css_selector_anb_of_t*>(data);
    if (anb_of == nullptr) {
        return false;
    }
    const int position = nth_position(node, doc, of_type, from_end, anb_of->of);
    return matches_anb(anb_of->anb.a, anb_of->anb.b, position);
}

}  // namespace detail
}  // namespace css
}  // namespace zui
