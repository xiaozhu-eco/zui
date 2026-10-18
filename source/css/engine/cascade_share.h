#ifndef ZUI_SOURCE_CSS_ENGINE_CASCADE_SHARE_H
#define ZUI_SOURCE_CSS_ENGINE_CASCADE_SHARE_H

// ShareKey, neighbor facts, and the cross-frame cascade memo.

#include "css/engine/cascade_decl.h"

#include <unordered_set>

namespace zui {
namespace css {
namespace detail {

constexpr uint32_t kBitHover = 1u << 0;
constexpr uint32_t kBitActive = 1u << 1;
constexpr uint32_t kBitFocus = 1u << 2;
constexpr uint32_t kBitChecked = 1u << 3;
constexpr uint32_t kBitDisabled = 1u << 4;
constexpr uint32_t kBitFirst = 1u << 5;
constexpr uint32_t kBitLast = 1u << 6;
constexpr uint32_t kBitFirstType = 1u << 7;
constexpr uint32_t kBitLastType = 1u << 8;
constexpr uint32_t kBitEmpty = 1u << 9;
/// Sibling-position bits. They split otherwise identical siblings and, through `parent_sig`,
/// every descendant. A sheet that never tests the matching pseudo-class must not pay that split.
constexpr uint32_t kStructuralShareBits =
    kBitFirst | kBitLast | kBitFirstType | kBitLastType | kBitEmpty;
constexpr uint32_t kBitFocusVisible = 1u << 10;
constexpr uint32_t kBitModal = 1u << 11;
/// Cross-frame key only (capability A): the document element is the element without a parent.
/// `:root` reads that, and the frame-internal ShareKey already separates the root through
/// `parent_sig`, so this bit is not mixed into `ShareKey::bits`.
constexpr uint32_t kBitRootEdge = 1u << 12;
/// HTML/Selectors state that is not a single live flag. `:read-only` is the complement of
/// `kBitReadWrite` (every element that is not `:read-write`). `:visited` matches nothing, so it
/// has no bit. Required and optional are separate: a `div` matches neither.
constexpr uint32_t kBitPlaceholderShown = 1u << 13;
constexpr uint32_t kBitFocusWithin = 1u << 14;
constexpr uint32_t kBitRequired = 1u << 15;
constexpr uint32_t kBitOptional = 1u << 16;
constexpr uint32_t kBitReadWrite = 1u << 17;
constexpr uint32_t kBitDefault = 1u << 18;
constexpr uint32_t kBitIndeterminate = 1u << 19;
constexpr uint32_t kBitLink = 1u << 20;
/// HTML/Selectors bits `html_pseudo_bits` computes. A sheet that tests none of them must not
/// walk ancestors (`:read-write`) or the document (`:default`) just to fill a share key.
constexpr uint32_t kHtmlStateBits = kBitPlaceholderShown | kBitFocusWithin | kBitRequired |
                                    kBitOptional | kBitReadWrite | kBitDefault |
                                    kBitIndeterminate | kBitLink;

struct ShareKey {
    std::string_view tag;
    std::string_view cls;
    std::string_view id;
    std::string_view prev_tag;
    uint64_t parent_sig = 0;
    /// Fingerprint of attribute name/value pairs that loaded selectors can see.
    uint64_t attrs_sig = 0;
    /// Fingerprint of subject-compound `:has()` match results for this node.
    uint64_t has_sig = 0;
    /// 1-based element position and same-type position, and the sibling counts an `:nth-*()`
    /// function's `of S` clause sees. All three are filled only when a loaded selector uses an
    /// `:nth-*()` function: an ordinary sheet must keep sharing one computed map between siblings,
    /// and the reader's 2000-row list depends on that.
    uint32_t nth_index = 0;
    uint32_t nth_type_index = 0;
    uint64_t nth_of_sig = 0;
    /// Resolved language (`lang`, else `xml:lang`, inherited). 0 when the sheet has no `:lang()`.
    uint64_t lang_sig = 0;
    /// HTML directionality. 0 when the sheet has no `:dir()`; 1 is ltr and 2 is rtl.
    uint64_t dir_sig = 0;
    uint32_t bits = 0;
    bool operator==(const ShareKey& o) const {
        return parent_sig == o.parent_sig && attrs_sig == o.attrs_sig && has_sig == o.has_sig &&
               nth_index == o.nth_index && nth_type_index == o.nth_type_index &&
               nth_of_sig == o.nth_of_sig && lang_sig == o.lang_sig && dir_sig == o.dir_sig &&
               bits == o.bits && tag == o.tag && cls == o.cls && id == o.id && prev_tag == o.prev_tag;
    }
};

struct ShareKeyHash {
    // Straight-line mix. A lambda and `std::hash<T>{}` are each a call under /Ob0, and every
    // visited node hashes this key at least once. The text loops stay in this function.
    size_t operator()(const ShareKey& k) const {
        size_t h = 0;
#define ZUI_SHARE_MIX(v) h ^= static_cast<size_t>(v) + 0x9e3779b9u + (h << 6) + (h >> 2)
#define ZUI_SHARE_TEXT(text)                                                                   \
        do {                                                                                   \
            const std::string_view zui_sv_ = (text);                                           \
            const char* zui_p_ = zui_sv_.data();                                               \
            const std::size_t zui_n_ = zui_sv_.size();                                         \
            size_t hv = 14695981039346656037ull;                                               \
            for (std::size_t zui_i_ = 0; zui_i_ < zui_n_; ++zui_i_) {                          \
                hv ^= static_cast<unsigned char>(zui_p_[zui_i_]);                              \
                hv *= 1099511628211ull;                                                        \
            }                                                                                  \
            ZUI_SHARE_MIX(hv);                                                                 \
        } while (0)
        ZUI_SHARE_MIX(k.parent_sig);
        ZUI_SHARE_MIX(k.attrs_sig);
        ZUI_SHARE_MIX(k.has_sig);
        ZUI_SHARE_MIX(k.nth_index);
        ZUI_SHARE_MIX(k.nth_type_index);
        ZUI_SHARE_MIX(k.nth_of_sig);
        ZUI_SHARE_MIX(k.lang_sig);
        ZUI_SHARE_MIX(k.dir_sig);
        ZUI_SHARE_MIX(k.bits);
        ZUI_SHARE_TEXT(k.tag);
        ZUI_SHARE_TEXT(k.cls);
        ZUI_SHARE_TEXT(k.id);
        ZUI_SHARE_TEXT(k.prev_tag);
#undef ZUI_SHARE_TEXT
#undef ZUI_SHARE_MIX
        return h;
    }
};

/// Everything the cascade computes for one node: the element's own style plus the ones for its
/// generated elements. They are cached together, because one selector traversal fills them all.
/// `selection` is only this node's cascaded ::selection contribution (share-safe); parent
/// highlight merge runs per node on commit, including on a ShareKey cache hit.
/// `backdrop` is the cascaded `::backdrop` map (paint-only; share-safe with `kBitModal`).
struct Computed {
    std::shared_ptr<const StyleMap> style;
    std::array<std::shared_ptr<const StyleMap>, kPseudoCount> pseudo;
    std::shared_ptr<const html::SelectionStyle> selection;
    std::shared_ptr<const StyleMap> backdrop;
};

/// One custom-property lookup the node's own declaration set made while its computed style was
/// finalized (docs/PERF_REFACTOR.md capability A). `local` marks a name the node's own declaration
/// set provides: it can only move when the sheet generation moves, and that generation is already
/// part of the cross-frame key. `pseudo` marks a read made while finalizing a generated element or
/// a highlight pseudo, whose inherited map is the element's own computed map rather than the
/// parent's; a hit that carries one goes down the re-substitution path. `present` / `value` are the
/// value the lookup returned when the memo entry was written.
struct ReadValue {
    ::zui::Atom name;
    bool local = false;
    bool pseudo = false;
    bool present = false;
    ::zui::Atom value;
};

/// One property the node takes **from its parent's committed map** while its computed style is
/// built: an inheritable longhand the node does not declare, or a custom property this node
/// re-emits. A key hit has to see every one of these still carrying the same value, and the
/// parent's offered *name set* unchanged, or the stored map would be stale.
struct ParentOffer {
    ::zui::Atom name;
    ::zui::Atom value;
};

/// What one `finalize_sink` pass hands back for the cross-frame memo (docs/PERF_REFACTOR.md
/// capability A). Null keeps the pass free of bookkeeping.
struct FinalizeTrace {
    std::vector<ReadValue>* reads = nullptr;
    std::vector<ParentOffer>* taken = nullptr;
    /// The map being finalized is a generated element / highlight pseudo rather than the element
    /// itself, so its reads cannot be verified against the parent node's map.
    bool pseudo = false;
};

/// A `:has()` sitting on a non-subject compound, with the anchor compound it hangs on. The anchor
/// check is what keeps the key build from walking a subtree for every node: only a node the anchor
/// compound selects can flip the function, and for `:root:has(…)` that is one node per frame.
struct NeighborHas {
    HasAnchor anchor;
    const lxb_css_selector_list_t* list = nullptr;
};

/// Facts a loaded selector tests on a node that is **not** the subject compound: the ancestor-side
/// compounds and the sibling-side compounds of every complex selector. Collected once per sheet
/// generation (docs/PERF_REFACTOR.md capability P3), so building a cross-frame key hashes this
/// table against the node's own values and never re-scans an ancestor chain or a sibling chain.
/// Class names a non-subject compound tests. `contains` takes a `string_view` so the per-node
/// signature does not allocate a key just to probe the set (Debug heap, once per class token).
class FactClassSet {
public:
    bool empty() const { return items_.empty(); }

    void insert(std::string value) {
        if (contains(value)) {
            return;
        }
        items_.push_back(std::move(value));
        if (slots_.size() < (items_.size() << 1)) {
            const std::size_t grown = slots_.empty() ? 8 : (slots_.size() << 1);
            slots_.assign(grown, 0);
            for (std::uint32_t i = 0; i < items_.size(); ++i) {
                place(i);
            }
        } else {
            place(static_cast<std::uint32_t>(items_.size() - 1));
        }
    }

    bool contains(std::string_view key) const {
        if (slots_.empty()) {
            return false;
        }
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size() - 1);
        std::uint32_t i = static_cast<std::uint32_t>(hash_text(key) & mask);
        for (;;) {
            const std::uint32_t slot = slots_[i];
            if (slot == 0) {
                return false;
            }
            if (items_[slot - 1] == key) {
                return true;
            }
            i = (i + 1) & mask;
        }
    }

private:
    static std::uint64_t hash_text(std::string_view key) {
        std::uint64_t h = 14695981039346656037ull;
        for (unsigned char c : key) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    void place(std::uint32_t index) {
        const std::uint32_t mask = static_cast<std::uint32_t>(slots_.size() - 1);
        std::uint32_t i = static_cast<std::uint32_t>(hash_text(items_[index]) & mask);
        while (slots_[i] != 0) {
            i = (i + 1) & mask;
        }
        slots_[i] = index + 1;
    }

    std::vector<std::string> items_;
    /// 0 is empty. A stored index is `items_` position + 1. Size is always a power of two.
    std::vector<std::uint32_t> slots_;
};

struct NeighborFacts {
    /// Some loaded selector has a non-subject compound, so `ancestor_sig` / `sibling_sig` carry
    /// information at all.
    bool used = false;
    /// Some compound tests the position from the end (`:nth-last-child()` / `:nth-last-of-type()`),
    /// so the sibling totals belong in the key.
    bool nth_last = false;
    std::unordered_set<std::string> tags;
    std::unordered_set<std::string> ids;
    FactClassSet classes;
    std::unordered_set<std::string> attrs;
    /// The node-local pseudo-classes a non-subject compound tests (hover / focus / disabled / the
    /// structural ones), as `kBit*` bits.
    uint32_t pseudo_mask = 0;
    /// A non-subject compound tests `:lang()`. The resolved language is mixed into the signature.
    bool lang = false;
    /// A non-subject compound tests `:dir()`. The resolved directionality is mixed in.
    bool dir = false;
    /// `:has()` argument lists sitting on a non-subject compound. They are the one facet that needs
    /// a real evaluation while the key is built; `cascade.key_evals` counts them.
    std::vector<NeighborHas> has_lists;
    /// `kStructuralShareBits` that any loaded selector tests, subject compounds included. The walk
    /// reads this off the ancestor table because that object is always built and already passed in.
    /// A zero mask means those position bits must stay out of the share key.
    uint32_t structural_share_bits = 0;
};

/// The parts of a cross-frame key that are computed numbers. They name no memory a node owns, so an
/// entry may keep them for as long as it lives.
struct CrossNumbers {
    uint64_t attrs_sig = 0;
    uint64_t has_sig = 0;
    uint64_t ancestor_sig = 0;
    uint64_t sibling_sig = 0;
    uint64_t nth_of_sig = 0;
    uint32_t nth_index = 0;
    uint32_t nth_type_index = 0;
    uint32_t siblings = 0;
    uint32_t siblings_of_type = 0;
    uint64_t lang_sig = 0;
    /// HTML directionality. 0 when the sheet has no `:dir()`; 1 is ltr and 2 is rtl.
    uint64_t dir_sig = 0;
    uint32_t bits = 0;
    bool operator==(const CrossNumbers& o) const {
        return attrs_sig == o.attrs_sig && has_sig == o.has_sig &&
               ancestor_sig == o.ancestor_sig && sibling_sig == o.sibling_sig &&
               nth_of_sig == o.nth_of_sig && nth_index == o.nth_index &&
               nth_type_index == o.nth_type_index && siblings == o.siblings &&
               siblings_of_type == o.siblings_of_type && lang_sig == o.lang_sig &&
               dir_sig == o.dir_sig && bits == o.bits;
    }
};

/// The cross-frame key of one visit (docs/PERF_REFACTOR.md capability A). Built before any rule
/// runs, so it must carry every input the node's own match and the inherited-copy step depend on.
/// It is not the ShareKey: it has no `parent_sig` (which would make the root's class change bust
/// every node), it adds the ancestor signature, the preceding-sibling signature and the sibling
/// totals the from-end `:nth-*()` functions read. The text borrows the node's own strings - this
/// key is built and compared inside one visit, and the memo stores `MemoKey` instead.
struct CrossKey : CrossNumbers {
    std::string_view tag;
    std::string_view cls;
    std::string_view id;
};

/// The cross-frame key as the memo holds it: the same numbers, and a copy of the text. A live
/// node's `class_name` is rebuilt by `remove_class` and appended to by `add_class` while an entry
/// lives, and the string it grows out of is freed by that rebuild; a view into it was read after
/// the buffer it named had died (the reader's scroll classes change on a schedule). `MemoEntry`'s
/// `style_attr` keeps a copy for the same reason.
struct MemoKey : CrossNumbers {
    std::string tag;
    std::string cls;
    std::string id;

    void assign(const CrossKey& o) {
        static_cast<CrossNumbers&>(*this) = o;
        if (std::string_view(tag) != o.tag) {
            tag.assign(o.tag.data(), o.tag.size());
        }
        if (std::string_view(cls) != o.cls) {
            cls.assign(o.cls.data(), o.cls.size());
        }
        if (std::string_view(id) != o.id) {
            id.assign(o.id.data(), o.id.size());
        }
    }

    bool matches(const CrossKey& o) const {
        return static_cast<const CrossNumbers&>(*this) == static_cast<const CrossNumbers&>(o) &&
               std::string_view(tag) == o.tag && std::string_view(cls) == o.cls &&
               std::string_view(id) == o.id;
    }
};

/// The pristine declaration sets of one node: what the rules won, before `finalize_sink` rewrites
/// them. Shared by every node that committed the same computed style in this frame (`ShareEntry`).
struct MemoPayload {
    DeclSinks sinks;
    /// What the node that first produced this declaration set took from its parent, and the
    /// offered-name signature of that parent. Every node sharing the payload shares the parent map
    /// (the ShareKey carries `parent_sig`), so these are the template an entry installed from the
    /// frame cache starts from; the entry owns its own copy from then on.
    std::vector<ParentOffer> taken;
    uint64_t parent_name_sig = 0;
    std::vector<ReadValue> reads;
    bool reads_pseudo = false;
};

/// One entry of the cross-frame memo. `computed` and `reads` are per node: two nodes may share the
/// payload through the frame cache while their own inherited maps and read values differ.
struct MemoEntry {
    MemoKey key;
    uint64_t sheets_gen = 0;
    uint64_t env_gen = 0;
    /// The parent's committed map when the entry was written. Pointer identity is the cheap test
    /// that nothing the node takes from its parent has moved; a cascade that leaves a map unchanged
    /// keeps the same pointer (see the re-substitution path).
    const StyleMap* parent_computed = nullptr;
    /// Every property this node took from the parent's map, and the parent's offered name set.
    std::vector<ParentOffer> taken;
    uint64_t parent_name_sig = 0;
    std::shared_ptr<const MemoPayload> payload;
    std::vector<ReadValue> reads;
    Computed computed;
    /// A generated element baked a custom-property read into its stored map. A literal
    /// `::selection` / `::backdrop` does not: the hit path re-merges the parent highlight in
    /// `commit_selection`, and a `var()` / `currentcolor` highlight still sets this flag.
    bool reads_pseudo = false;
    /// The `style` attribute this entry was built from. Empty when the node had none. A change
    /// is a key miss: the stored sinks already contain that attribute's declarations.
    std::string style_attr;
};

/// Drops cached longhands. Defined in `rule_cascade.cpp`. Declared here so `bump_sheets` can
/// invalidate declaration pointers when the sheet generation moves.
void clear_decl_expansion();

/// The cross-frame cascade memo (docs/PERF_REFACTOR.md capability A). Lives in `css::Engine`, keyed
/// by node pointer, and is dropped by the same `forget_subtree` the App already calls when it
/// detaches nodes.
struct CascadeMemo {
    std::unordered_map<const html::Node*, MemoEntry> nodes;
    /// Bumped by every sheet change (load / added `<style>` / theme file). Entries from another
    /// generation never match, so the rule list runs again.
    uint64_t sheets_gen = 1;
    /// Bumped when `env()` inputs or `@container` sizes move. Viewport width and height are not
    /// in this generation: those flip `@media` selectors, which `active_media` handles per node.
    uint64_t env_gen = 0;
    uint64_t media_sig = 0;
    bool media_sig_known = false;

    void bump_sheets() {
        ++sheets_gen;
        media_sig_known = false;
        media_known = false;
        last_media.clear();
        active_media = nullptr;
        born_media = nullptr;
        nodes.clear();
        // Declaration objects die with the sheets this generation replaced.
        clear_decl_expansion();
    }
    void bump_env() { ++env_gen; }
    /// `@media` selectors in the previous cascade's index, sorted by pointer. A viewport move
    /// that does not change `env_gen` still drops an entry whose recorded selector is absent.
    std::vector<const lxb_css_selector_list_t*> last_media;
    bool media_known = false;
    /// Set for the duration of one `Engine::cascade` when the environment generation did not
    /// move. Null means "do not apply the media-selector check" (first cascade, or a real bump).
    const std::vector<const lxb_css_selector_list_t*>* active_media = nullptr;
    const std::vector<const IndexedRule*>* born_media = nullptr;
    /// Erases @p dead and its subtree; the App calls it before the nodes are freed.
    void forget(const html::Node* dead);
};

/// One frame-internal share-cache slot: the pristine declaration sets that produced the entry, and
/// the computed maps every node with the same ShareKey committed. The payload is what the
/// cross-frame memo copies when a share hit installs the node's entry.
struct ShareEntry {
    std::shared_ptr<const MemoPayload> payload;
    Computed computed;
};

using ShareCache = std::unordered_map<ShareKey, ShareEntry, ShareKeyHash>;

}  // namespace detail
}  // namespace css
}  // namespace zui

#endif
