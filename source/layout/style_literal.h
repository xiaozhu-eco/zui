#ifndef ZUI_LAYOUT_STYLE_LITERAL_H
#define ZUI_LAYOUT_STYLE_LITERAL_H

// Property names in layout are string literals. Hashing those characters on every box, under
// /Od, costs more than the lookup itself. This cache keys the literal's address and interns it
// once in *this module's* atom pool — the same pool that built `Node::computed` when cascade and
// layout live in one binary. `StyleMap::find(Atom)` is pointer identity, so this must not be
// called from another binary than the one that cascaded the node.
//
// `/Ob0` does not inline, so each `style_literal_slow` call poisons a fresh frame. An intern miss
// reads the same map once per longhand. `SlotGuard` copies the values the miss actually asks for
// into a stack array, and the `style_literal` macro indexes that array. The index is a constant
// expression (`integral_constant`), which this compiler folds even at /Od /Ob0. A helper function
// would still be a call. A name that is not in the table, or a node other than the one the guard
// gathered, takes `style_literal_slow` — a negative index is never a subscript.

#include "html/document.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace zui {
namespace layout {

inline const std::string* style_literal_slow(const html::Node* node, const char* key);

namespace style_literal_detail {

struct LiteralSlot {
    const char* key = nullptr;
    Atom value;
};

struct LiteralCache {
    std::vector<LiteralSlot> slots;
    std::size_t live = 0;

    static std::size_t hash_ptr(const char* text) {
        return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(text) * 0x9E3779B97F4A7C15ull);
    }

    void rehash(std::size_t cap) {
        std::vector<LiteralSlot> next(cap);
        const std::size_t mask = cap - 1;
        for (const LiteralSlot& slot : slots) {
            if (slot.key == nullptr) {
                continue;
            }
            std::size_t i = hash_ptr(slot.key) & mask;
            while (next[i].key != nullptr) {
                i = (i + 1) & mask;
            }
            next[i] = slot;
        }
        slots.swap(next);
    }

    const Atom& intern(const char* text) {
        if (slots.empty() || (live + 1) * 10 >= slots.size() * 7) {
            rehash(slots.empty() ? 128 : slots.size() * 2);
        }
        const std::size_t mask = slots.size() - 1;
        std::size_t i = hash_ptr(text) & mask;
        for (;;) {
            if (slots[i].key == nullptr) {
                slots[i].key = text;
                slots[i].value = Atom::intern(text);
                ++live;
                return slots[i].value;
            }
            if (slots[i].key == text) {
                return slots[i].value;
            }
            i = (i + 1) & mask;
        }
    }
};

inline LiteralCache& cache() {
    static LiteralCache stored;
    return stored;
}

// Every string literal passed to `style_literal` on the intern path. A missing name compiles and
// falls through to the slow call; it does not read a neighbour's slot.
inline constexpr const char* kNames[] = {
    "align-content",
    "align-items",
    "align-self",
    "aspect-ratio",
    "border-bottom-left-radius",
    "border-bottom-right-radius",
    "border-bottom-style",
    "border-bottom-width",
    "border-collapse",
    "border-left-style",
    "border-left-width",
    "border-right-style",
    "border-right-width",
    "border-spacing",
    "border-top-left-radius",
    "border-top-right-radius",
    "border-top-style",
    "border-top-width",
    "bottom",
    "box-sizing",
    "break-after",
    "break-before",
    "break-inside",
    "caption-side",
    "clear",
    "column-count",
    "column-fill",
    "column-gap",
    "column-span",
    "column-width",
    "columns",
    "contain",
    "contain-intrinsic-height",
    "contain-intrinsic-size",
    "contain-intrinsic-width",
    "container-type",
    "content-visibility",
    "direction",
    "display",
    "filter",
    "flex-basis",
    "flex-direction",
    "flex-grow",
    "flex-shrink",
    "flex-wrap",
    "float",
    "font-family",
    "font-kerning",
    "font-size",
    "font-style",
    "font-weight",
    "grid-area",
    "grid-auto-columns",
    "grid-auto-flow",
    "grid-auto-rows",
    "grid-column",
    "grid-column-end",
    "grid-column-start",
    "grid-row",
    "grid-row-end",
    "grid-row-start",
    "grid-template-areas",
    "grid-template-columns",
    "grid-template-rows",
    "height",
    "justify-content",
    "justify-items",
    "justify-self",
    "left",
    "letter-spacing",
    "line-height",
    "list-style-position",
    "margin-bottom",
    "margin-left",
    "margin-right",
    "margin-top",
    "max-height",
    "max-width",
    "min-height",
    "min-width",
    "order",
    "orphans",
    "outline-offset",
    "outline-style",
    "outline-width",
    "overflow-wrap",
    "overflow-x",
    "overflow-y",
    "padding-bottom",
    "padding-left",
    "padding-right",
    "padding-top",
    "page-break-after",
    "page-break-before",
    "page-break-inside",
    "perspective",
    "position",
    "right",
    "row-gap",
    "scrollbar-gutter",
    "scrollbar-width",
    "table-layout",
    "text-align",
    "text-indent",
    "text-overflow",
    "text-transform",
    "top",
    "transform",
    "vertical-align",
    "visibility",
    "white-space",
    "width",
    "will-change",
    "word-break",
    "word-spacing",
    "word-wrap",
    "writing-mode",
    // Paint reads these by literal. They sit at the end so the indexes above stay put.
    // A name missing from this list still resolves, through the slow map probe.
    "appearance",
    "backdrop-filter",
    "background-attachment",
    "background-blend-mode",
    "background-clip",
    "background-color",
    "background-image",
    "background-origin",
    "background-position",
    "background-repeat",
    "background-size",
    "border-bottom-color",
    "border-image-outset",
    "border-image-repeat",
    "border-image-slice",
    "border-image-source",
    "border-image-width",
    "border-left-color",
    "border-right-color",
    "border-top-color",
    "box-shadow",
    "clip-path",
    "color",
    "color-scheme",
    "column-rule",
    "column-rule-color",
    "column-rule-style",
    "column-rule-width",
    "empty-cells",
    "mask-image",
    "mix-blend-mode",
    "object-fit",
    "object-position",
    "opacity",
    "outline-color",
    "scrollbar-color",
    "text-decoration",
    "text-decoration-color",
    "text-decoration-line",
    "text-decoration-skip-ink",
    "text-decoration-style",
    "text-decoration-thickness",
    "text-emphasis",
    "text-emphasis-color",
    "text-emphasis-position",
    "text-emphasis-style",
    "text-shadow",
    "text-underline-offset",
};

inline constexpr int kNameCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

constexpr int name_index(const char* key) {
    if (key == nullptr) {
        return -1;
    }
    for (int i = 0; i < kNameCount; ++i) {
        const char* a = kNames[i];
        const char* b = key;
        for (;;) {
            if (*a != *b) {
                break;
            }
            if (*a == '\0') {
                return i;
            }
            ++a;
            ++b;
        }
    }
    return -1;
}

template <int Index>
struct NameSlot {
    static constexpr bool known = Index >= 0;
    static constexpr int load = Index >= 0 ? Index : 0;
};

inline thread_local const std::string** tl_props = nullptr;
inline thread_local const html::Node* tl_node = nullptr;

struct NameIndex {
    static constexpr std::size_t kCap = 256;
    int slot[kCap];
    Atom atom[kCap];

    NameIndex() {
        for (std::size_t i = 0; i < kCap; ++i) {
            slot[i] = -1;
        }
        for (int n = 0; n < kNameCount; ++n) {
            const Atom key = Atom::intern(kNames[n]);
            std::size_t i = static_cast<std::size_t>(key.hash()) & (kCap - 1);
            while (slot[i] >= 0) {
                i = (i + 1) & (kCap - 1);
            }
            slot[i] = n;
            atom[i] = key;
        }
    }

    int find(const Atom& key) const {
        std::size_t i = static_cast<std::size_t>(key.hash()) & (kCap - 1);
        for (std::size_t n = 0; n < kCap; ++n) {
            if (slot[i] < 0) {
                return -1;
            }
            if (atom[i] == key) {
                return slot[i];
            }
            i = (i + 1) & (kCap - 1);
        }
        return -1;
    }
};

inline void fill_slots(const html::Node* node, const std::string** out) {
    for (int i = 0; i < kNameCount; ++i) {
        out[i] = nullptr;
    }
    if (node == nullptr || !node->computed) {
        return;
    }
    static const NameIndex index;
    // Raw items. The range-for on a debug vector is a checked iterator per property, and every
    // intern miss and every painted box walks the map once.
    const FlatMap<Atom>::value_type* items = node->computed->items_ptr();
    const std::size_t n = node->computed->size();
    for (std::size_t p = 0; p < n; ++p) {
        const Atom::Rec* value = items[p].second.rec_;
        if (value->text.empty()) {
            continue;
        }
        const Atom::Rec* key = items[p].first.rec_;
        std::size_t i = static_cast<std::size_t>(key->hash) & (NameIndex::kCap - 1);
        for (std::size_t step = 0; step < NameIndex::kCap; ++step) {
            if (index.slot[i] < 0) {
                break;
            }
            if (index.atom[i].rec_ == key) {
                out[index.slot[i]] = &value->text;
                break;
            }
            i = (i + 1) & (NameIndex::kCap - 1);
        }
    }
}

/// Gathers `node`'s computed longhands for the miss that is about to read them. Nested interns
/// save and restore the outer node: a parent walk must not see the child's slots.
class SlotGuard {
public:
    explicit SlotGuard(const html::Node* node) : prev_props_(tl_props), prev_node_(tl_node) {
        if (node == nullptr) {
            tl_props = nullptr;
            tl_node = nullptr;
            return;
        }
        fill_slots(node, slots_);
        tl_props = slots_;
        tl_node = node;
    }

    ~SlotGuard() {
        tl_props = prev_props_;
        tl_node = prev_node_;
    }

    SlotGuard(const SlotGuard&) = delete;
    SlotGuard& operator=(const SlotGuard&) = delete;

private:
    const std::string* slots_[kNameCount]{};
    const std::string** prev_props_;
    const html::Node* prev_node_;
};

}  // namespace style_literal_detail

/// Same result as `Node::style` for a literal compiled into this module. An empty computed value
/// is a miss, matching `Node::style`.
inline const std::string* style_literal_slow(const html::Node* node, const char* key) {
    if (node == nullptr || key == nullptr || !node->computed) {
        return nullptr;
    }
    const auto it = node->computed->find(style_literal_detail::cache().intern(key));
    if (it == node->computed->end() || it->second.empty()) {
        return nullptr;
    }
    return it->second.ptr();
}

}  // namespace layout
}  // namespace zui

// The index expression is the fold. Do not hide it in a function or a lambda: /Ob0 emits the call.
#define style_literal(node, key)                                                                       \
    ((::zui::layout::style_literal_detail::tl_props != nullptr &&                                      \
      (node) == ::zui::layout::style_literal_detail::tl_node &&                                        \
      ::zui::layout::style_literal_detail::NameSlot<                                                   \
          ::zui::layout::style_literal_detail::name_index(key)>::known)                                \
         ? ::zui::layout::style_literal_detail::tl_props                                               \
               [::std::integral_constant<                                                             \
                   int, ::zui::layout::style_literal_detail::NameSlot<                                 \
                            ::zui::layout::style_literal_detail::name_index(key)>::load>::value]       \
         : ::zui::layout::style_literal_slow((node), (key)))

#endif
