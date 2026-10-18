#ifndef ZUI_SOURCE_CSS_ENGINE_CASCADE_WALK_H
#define ZUI_SOURCE_CSS_ENGINE_CASCADE_WALK_H

// Walk state, counters, diagnostic split clocks, and the engine TU function surface.

#include "css/engine/cascade_share.h"

#include <cctype>
#include <chrono>
#include <cstdlib>

namespace zui {
namespace css {
namespace detail {

int parse_html_integer(std::string_view raw, int fallback);

/// Implicit `list-item` counter nesting (CSS Lists section 4.6 subset). Only list containers push
/// a scope.
struct ListItemCounterState {
    std::vector<int> stack;

    void push_scope(int reset_to) { stack.push_back(reset_to); }
    void pop_scope() {
        if (!stack.empty()) {
            stack.pop_back();
        }
    }
    int increment() {
        if (stack.empty()) {
            stack.push_back(0);
        }
        return ++stack.back();
    }
    void set(int value) {
        if (stack.empty()) {
            stack.push_back(0);
        }
        stack.back() = value;
    }
};

/// Author-named counters (CSS Lists / Generated Content). `list-item` stays on
/// ListItemCounterState.
struct AuthorCounterState {
    std::unordered_map<std::string, std::vector<int>> stacks;

    struct Op {
        std::string name;
        int value = 0;
    };

    static std::vector<Op> parse_list(std::string_view css, int default_value) {
        std::vector<Op> ops;
        auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        size_t i = 0;
        while (i < css.size()) {
            while (i < css.size() && is_space(css[i])) {
                ++i;
            }
            if (i >= css.size()) {
                break;
            }
            if (css.compare(i, 4, "none") == 0 &&
                (i + 4 >= css.size() || is_space(css[i + 4]) || css[i + 4] == ',')) {
                break;
            }
            size_t name_end = i;
            while (name_end < css.size() &&
                   (std::isalnum(static_cast<unsigned char>(css[name_end])) != 0 ||
                    css[name_end] == '-' || css[name_end] == '_')) {
                ++name_end;
            }
            if (name_end == i) {
                break;
            }
            Op op;
            op.name.assign(css.data() + static_cast<std::ptrdiff_t>(i), name_end - i);
            op.value = default_value;
            i = name_end;
            while (i < css.size() && is_space(css[i])) {
                ++i;
            }
            if (i < css.size() && (css[i] == '-' || css[i] == '+' ||
                                   std::isdigit(static_cast<unsigned char>(css[i])) != 0)) {
                const size_t num_begin = i;
                if (css[i] == '-' || css[i] == '+') {
                    ++i;
                }
                while (i < css.size() && std::isdigit(static_cast<unsigned char>(css[i])) != 0) {
                    ++i;
                }
                op.value = parse_html_integer(css.substr(num_begin, i - num_begin), default_value);
            }
            if (op.name != "list-item") {
                ops.push_back(std::move(op));
            }
        }
        return ops;
    }

    std::vector<std::string> apply_reset(const StyleMap* style) {
        std::vector<std::string> pushed;
        if (style == nullptr) {
            return pushed;
        }
        const auto it = style->find("counter-reset");
        if (it == style->end() || it->second.empty() || it->second == "none") {
            return pushed;
        }
        for (const Op& op : parse_list(it->second, 0)) {
            stacks[op.name].push_back(op.value);
            pushed.push_back(op.name);
        }
        return pushed;
    }

    void apply_increment(const StyleMap* style) {
        if (style == nullptr) {
            return;
        }
        const auto it = style->find("counter-increment");
        if (it == style->end() || it->second.empty() || it->second == "none") {
            return;
        }
        for (const Op& op : parse_list(it->second, 1)) {
            auto& stack = stacks[op.name];
            if (stack.empty()) {
                stack.push_back(0);
            }
            stack.back() += op.value;
        }
    }

    void pop_resets(const std::vector<std::string>& pushed) {
        for (const std::string& name : pushed) {
            auto it = stacks.find(name);
            if (it == stacks.end() || it->second.empty()) {
                continue;
            }
            it->second.pop_back();
            if (it->second.empty()) {
                stacks.erase(it);
            }
        }
    }

    int get(std::string_view name) const {
        const auto it = stacks.find(std::string(name));
        if (it == stacks.end() || it->second.empty()) {
            return 0;
        }
        return it->second.back();
    }

    std::string get_all(std::string_view name, std::string_view sep) const {
        const auto it = stacks.find(std::string(name));
        if (it == stacks.end() || it->second.empty()) {
            return "0";
        }
        std::string out;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            if (i > 0) {
                out.append(sep);
            }
            out += std::to_string(it->second[i]);
        }
        return out;
    }
};

struct CascadeStats {
    int nodes = 0;
    int cache_hits = 0;
    int cache_misses = 0;
};

/// `:has()` evaluations since the current `Engine::cascade` began. The cascade runs on the frame
/// thread and `Engine::cascade` resets this at its start; `Engine::has_calls()` reads it. The
/// variable is inline so the selector matcher and the cascade entry share one counter.
inline std::size_t g_has_calls = 0;

/// Temporary phase split for a cold-frame diagnosis. Reset at each cascade() entry.
struct CascadeSplit {
    double index_ms = 0;
    double match_ms = 0;
    double apply_ms = 0;
    double fin_ms = 0;
    double key_ms = 0;
    double pre_ms = 0;
    double fin_copy = 0;
    double fin_put = 0;
    double fin_var = 0;
    double fin_inh = 0;
    double fin_rest = 0;
    double fin_pseudo = 0;
    double commit_ms = 0;
    double pseudo_ms = 0;
    double find_ms = 0;
    double memo_ms = 0;
    double share_ms = 0;
    double name_ms = 0;
    double pre_node_ms = 0;
    double match_merge = 0;
    double match_rest = 0;
    /// Trivial selectors that skipped `matches_selector`. Diagnostic; not a work unit.
    std::uint64_t trivial = 0;
    /// Non-trivial selector shapes that still enter `matches_selector`. Diagnostic.
    std::uint64_t n_compound = 0;
    std::uint64_t n_desc = 0;
    std::uint64_t n_other = 0;
    /// `DeclSinks` construction on a share miss. Diagnostic.
    double sink_ms = 0;
    /// `match_from` inside `matches_selector`. Diagnostic.
    double body_ms = 0;
    /// `apply_decls` wall time. Diagnostic.
    double decl_ms = 0;
    /// `expansion_for` inside `apply_decls`. Diagnostic.
    double exp_ms = 0;
    /// Winner publish inside `apply_decls`, after expansion. Diagnostic.
    double dput_ms = 0;
    /// Selector-shape classification before `matches_selector`. Diagnostic.
    double shape_walk_ms = 0;
    void reset() {
        index_ms = 0;
        match_ms = 0;
        apply_ms = 0;
        fin_ms = 0;
        key_ms = 0;
        pre_ms = 0;
        fin_copy = 0;
        fin_put = 0;
        fin_var = 0;
        fin_inh = 0;
        fin_rest = 0;
        fin_pseudo = 0;
        commit_ms = 0;
        pseudo_ms = 0;
        find_ms = 0;
        memo_ms = 0;
        share_ms = 0;
        name_ms = 0;
        pre_node_ms = 0;
        match_merge = 0;
        match_rest = 0;
        trivial = 0;
        n_compound = 0;
        n_desc = 0;
        n_other = 0;
        sink_ms = 0;
        body_ms = 0;
        decl_ms = 0;
        exp_ms = 0;
        dput_ms = 0;
        shape_walk_ms = 0;
    }
};
inline CascadeSplit g_cascade_split;

/// Diagnostic only. Armed when `ZUI_PERF` is set. A null slot is a no-op so the default
/// Debug path does not take a clock. Strip with the other probe clocks.
inline bool cascade_split_armed() {
    static const bool armed = std::getenv("ZUI_PERF") != nullptr;
    return armed;
}

struct CascadeAcc {
    double* slot = nullptr;
    std::chrono::steady_clock::time_point t0{};
    explicit CascadeAcc(double* slot_in) : slot(slot_in) {
        if (slot != nullptr) {
            t0 = std::chrono::steady_clock::now();
        }
    }
    ~CascadeAcc() { stop(); }
    void stop() {
        if (slot != nullptr) {
            *slot += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                         .count();
            slot = nullptr;
        }
    }
    CascadeAcc(const CascadeAcc&) = delete;
    CascadeAcc& operator=(const CascadeAcc&) = delete;
};

/// Drops the per-cascade expansion cache. `record_history` is false when no sheet and no style
/// attribute in this document contains `revert` / `revert-layer`: the loser stack exists only so
/// those keywords can roll back to an earlier declaration.
void reset_decl_expansion(bool record_history);

/// Where a node sits in the tree, as the walk accumulated it: the frame-internal share signature,
/// the cross-frame ancestor signature, and the sibling facts. `cascade_children` fills it for each
/// child; the root starts from a default value.
struct WalkPos {
    uint64_t parent_sig = 0;
    uint64_t ancestor_sig = 0;
    uint64_t sibling_sig = 0;
    std::string_view prev_tag;
    uint32_t pos_bits = 0;
    uint32_t nth_index = 0;
    uint32_t nth_type_index = 0;
    uint64_t nth_of_sig = 0;
    uint32_t siblings = 0;
    uint32_t siblings_of_type = 0;
};

struct CascadeIn {
    const std::vector<lxb_css_stylesheet*>& sheets;
    const std::vector<std::string>& sources;
    MediaState media;
    bool allow_cache = true;
    CascadeStats* stats = nullptr;
    /// The `id` names the loaded selectors mention. An element keeps its own id in the share key
    /// only when one of them could see it: two elements that differ only in an id no selector names
    /// cannot style differently.
    const std::unordered_set<std::string>* selector_ids = nullptr;
    /// Some selector tests the `id` attribute, so no id is safe to drop.
    bool selector_id_attribute = false;
    /// Attribute names any loaded selector tests. Those values fingerprint into the share key.
    const std::unordered_set<std::string>* selector_attrs = nullptr;
    /// Subject-compound `:has()` arguments and their anchors. Match bits fingerprint into
    /// ShareKey::has_sig. A node the anchor cannot select does not walk.
    const std::vector<SubjectHasFact>* subject_has = nullptr;
    /// A loaded selector uses an `:nth-*()` function, so a node's own 1-based position (and same
    /// type position) belongs in the share key even though siblings otherwise look alike.
    bool selector_uses_nth = false;
    /// A loaded selector tests the position from the end (`:nth-last-child()` / `:last-child` /
    /// `:nth-last-of-type()`), so the sibling totals belong in the cross-frame key.
    bool selector_uses_nth_last = false;
    /// The `of S` lists those `:nth-*()` functions carry.
    const std::vector<const lxb_css_selector_list_t*>* nth_of_lists = nullptr;
    /// Cascade layer ranks (CSS Cascade 5 section 6.4.4), built once per cascade() call.
    const LayerTree* layers = nullptr;
    /// Document-order `list-item` counter scopes.
    ListItemCounterState* list_counters = nullptr;
    /// Author `counter-reset` / `counter-increment` scopes (names other than `list-item`).
    AuthorCounterState* author_counters = nullptr;
    /// When non-null, only these nodes re-match and re-commit (docs/PERF.md I3).
    const std::unordered_set<html::Node*>* closure = nullptr;
    /// The cross-frame memo, its node map and the generations the key carries (capability A). Null
    /// turns the memo off; the walk then behaves exactly as it did before stage 2.
    CascadeMemo* memo = nullptr;
    /// Facts a loaded selector tests on ancestor-side compounds; feeds `ancestor_sig`.
    const NeighborFacts* ancestor_facts = nullptr;
    /// Facts a loaded selector tests on sibling-side compounds; feeds the preceding-sibling chain.
    const NeighborFacts* sibling_facts = nullptr;
    /// A loaded selector uses `:lang()`. The resolved language belongs in the share key.
    bool selector_uses_lang = false;
    /// A loaded selector uses `:dir()`. The resolved directionality belongs in the share key.
    bool selector_uses_dir = false;
    /// Closure plus every ancestor of a closure node. A child outside this set cannot contain a
    /// node the cascade still has to match, so the walk skips it after the list-item bump.
    /// Null when the cascade is the whole document.
    const std::unordered_set<html::Node*>* walk_set = nullptr;
    /// Built once per `cascade()` from the sheets and this frame's media. Null only when a caller
    /// constructs `CascadeIn` by hand; `Engine::cascade` always sets it.
    const RuleIndex* rule_index = nullptr;
    /// When set, `WalkPos::nth_index` is a bit per pattern (bit 0 is patterns[0]) instead of the
    /// raw sibling index. Null keeps the raw index. Odd and even stay apart because they miss
    /// each other's An+B.
    const std::vector<NthPattern>* nth_patterns = nullptr;
    /// `kHtmlStateBits` the loaded selectors actually test. `html_pseudo_bits` skips the rest.
    /// A hand-built `CascadeIn` that leaves this at 0 computes none of them; `Engine::cascade`
    /// always passes the mask collected with the other selector facts.
    uint32_t html_state_bits = 0;
    /// Compounds that carry `:hover` / `:active`. A node none of them can match keeps that bit out
    /// of the cross-frame key: the pointer still sets the flag on the whole ancestor chain, and a
    /// later viewport cascade must not rematch `html` because the cursor rested on it.
    const std::vector<HoverTarget>* hover_targets = nullptr;
    const std::vector<HoverTarget>* active_targets = nullptr;
};

// ---- selector_match.cpp: lexbor selector matching on the zui tree ---------------------------

std::string_view view(const lexbor_str_t& s);
std::string serialize_name(const lxb_css_rule_declaration_t* decl);
std::string serialize_value(const lxb_css_rule_declaration_t* decl);
bool match_list(html::Node* node, const html::Document& doc, const lxb_css_selector_list_t* list);
bool match_has(html::Node* anchor, const html::Document& doc, const lxb_css_selector_list_t* list);
/// Indexes element ids for the duration of one cascade. `:has(#id)` then checks that element
/// instead of walking every descendant of every anchor. Nested cascades restore the previous index.
class HasIdIndex {
public:
    explicit HasIdIndex(const html::Document& doc);
    ~HasIdIndex();
    HasIdIndex(const HasIdIndex&) = delete;
    HasIdIndex& operator=(const HasIdIndex&) = delete;

private:
    void* store_ = nullptr;
    void* previous_ = nullptr;
};
bool match_from(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel);
bool is_has_selector(const lxb_css_selector_t* sel);
void gather_related(html::Node* origin, unsigned combinator, std::vector<html::Node*>& out);
bool match_plain_pseudo_class(const html::Node* node, uint32_t type, const html::Node* root,
                              const html::Document* doc);
/// HTML pseudo-class bits (`:placeholder-shown`, `:focus-within`, `:required`, `:optional`,
/// `:read-write`, `:default`, `:indeterminate`, `:link`). `:read-only` is the complement of
/// `kBitReadWrite`. `doc` resolves `form="id"`; a null doc still sees an ancestor `<form>`.
uint32_t html_pseudo_bits(const html::Node& node, const html::Document* doc, uint32_t mask);
/// The element's language: its own `lang`, else `xml:lang`, else the same walk up the ancestors.
/// ASCII-lowercased. Empty when no ancestor names a language.
std::string resolved_language(const html::Node& node);
/// HTML directionality of @p node (Selectors `:dir()`). True when it resolves to rtl.
/// CSS `direction` is not an input. `dir=auto` and a `bdi` with a missing or invalid `dir` read
/// the first strong character; a `type=tel` input with no `dir` is ltr.
bool element_is_rtl(const html::Node& node);
/// True when @p node's directionality is computed from its contents, so a text change under it
/// can flip `:dir()` on it and on descendants that inherit from it.
bool direction_from_contents(const html::Node& node);
bool match_attr(const html::Node* node, const lxb_css_selector_t* sel);

// ---- rule_cascade.cpp: declaration serialization, layer order, rule traversal ---------------

std::string_view trim(std::string_view text);
CascadeLayer cascade_layer(OriginRank origin, bool important);
bool declaration_wins(const DeclKey& a, const DeclKey& b);
void collect_layer_order(lxb_css_rule_t* rule, LayerTree& tree, int parent, std::string_view source,
                         const MediaState& media);
void finish_layers(LayerTree& tree);
void visit_rules(lxb_css_rule_t* rule, html::Node* node, const html::Document& doc, int& order,
                 DeclSinks& sinks, OriginRank origin, bool from_style_attr, std::string_view source,
                 const MediaState& media, const LayerTree& layers, int layer_rank);
RuleIndex build_rule_index(const std::vector<lxb_css_stylesheet*>& sheets,
                           const std::vector<std::string>& sources, const MediaState& media,
                           const LayerTree& layers);
/// Serialize every stylesheet declaration into the expansion cache. Sheet load already parsed
/// these; doing it again on the first cascade was the cold `exp` time. Pointers stay valid until
/// `clear_decl_expansion` (the next sheet generation).
void prewarm_decl_expansion(const std::vector<lxb_css_stylesheet*>& sheets,
                            const std::vector<std::string>& sources);
void apply_indexed(const RuleIndex& index, html::Node* node, const html::Document& doc, int& order,
                   DeclSinks& sinks);
/// True when @p rule matches @p node. Used for `@media` selectors that newly entered the index.
bool media_rule_matches(const IndexedRule& rule, html::Node* node, const html::Document& doc);
/// A `style` attribute is a declaration list at style-attribute specificity. One process parser
/// is reused; the text is not wrapped in a stylesheet.
void apply_inline_style(std::string_view text, int& order, DeclSinks& sinks, int layer_rank);

// ---- computed_style.cpp: inheritance, var()/logical-property resolution, finalize_sink -----

bool inheritable(std::string_view name);
/// `inheritable` / `propagates_to_child`, with the answer cached on the interned name.
bool name_inherits(const ::zui::Atom& name);
bool name_propagates(const ::zui::Atom& name);
void finalize_sink(StyleMap& specified, DeclSink& sink, const StyleMap* parent,
                   const html::Node* custom_origin, const MediaState& media,
                   const FinalizeTrace* trace = nullptr);
/// True for a property name that propagates from a parent's computed map to a child's: an
/// inheritable longhand or a custom property. Custom properties stay on the node that set them;
/// `var()` walks ancestors instead of copying the set onto every element.
bool propagates_to_child(std::string_view name);

// ---- style_diff.cpp: paint/layout/reattach diff and commit --------------------------------

bool affects_layout(std::string_view n);
bool affects_layout(const ::zui::Atom& name);
std::string_view map_val(const StyleMap* m, const char* k);
void commit_computed(Engine::CascadeResult& result, html::Node* node,
                     std::shared_ptr<const StyleMap> specified);

// ---- generated_content.cpp: ::before/::after/::marker and counters -------------------------

bool is_list_container_tag(std::string_view tag);
void normalize_generated(StyleMap& map);
void commit_pseudos(Engine::CascadeResult& result, html::Node* node,
                    const std::array<std::shared_ptr<const StyleMap>, kPseudoCount>& maps,
                    int list_item_ordinal, const AuthorCounterState* author_counters);

// ---- selection_style.cpp: ::selection / ::backdrop -----------------------------------------

std::shared_ptr<const html::SelectionStyle> finalize_selection_sink(DeclSink& sink,
                                                                   const StyleMap* originating,
                                                                   const MediaState& media);
/// True when @p a holds the same used selection colours as @p b (a null @p a means "no author
/// contribution"). The cross-frame memo uses it to keep the stored pointer when a re-substitution
/// produced the same colours.
bool selection_equal(const html::SelectionStyle* a, const html::SelectionStyle& b);
void commit_selection(html::Node* node, const std::shared_ptr<const html::SelectionStyle>& contrib);
void commit_backdrop(html::Node* node, const std::shared_ptr<const StyleMap>& map);

// ---- cascade_walk.cpp: the cascade tree walk -----------------------------------------------

uint64_t share_sig(const ShareKey& k);
/// The node-local pseudo-class bits of one element, without the position-derived ones (which
/// `WalkPos::pos_bits` carries).
uint32_t node_state_bits(const html::Node& node, const html::Document* doc, uint32_t html_mask);
/// Drops `:hover` / `:active` bits the loaded compounds cannot match on @p node.
uint32_t key_state_bits(const html::Node& node, uint32_t bits,
                        const std::vector<HoverTarget>* hover,
                        const std::vector<HoverTarget>* active);
/// Hashes the facets of @p node that a non-subject compound in @p facts can observe.
uint64_t neighbor_fact_sig(const html::Node& node, uint32_t bits, const NeighborFacts& facts);
void cascade_node(html::Node* node, const html::Document& doc, const CascadeIn& in,
                  Engine::CascadeResult& result, ShareCache& cache, const WalkPos& pos);
bool facets_match(const html::Node& node, const std::vector<const lxb_css_selector_t*>& tests);

// ---- split-TU internals (same algorithms; declarations only) ----------------------

PseudoTarget pseudo_target_of(std::uint32_t id);
std::string_view canonical_name(const lxb_css_rule_declaration_t* decl);
void apply_decls(const lxb_css_rule_declaration_list_t* decls, uint32_t spec, OriginRank origin,
                 int& order, DeclSink& sink, std::string_view source, int layer_rank,
                 bool element_attached);
bool matches_selector(html::Node* node, const html::Document& doc,
                      const lxb_css_selector_list_t* list, PseudoTarget& target);
bool container_name_has(std::string_view list, std::string_view name);

const std::string* ancestor_custom(const StyleMap* parent, const html::Node* origin,
                                   std::string_view name);
void resolve_specified(StyleMap& specified, const StyleMap* parent, const html::Node* custom_origin,
                       const MediaState& media, const FinalizeTrace* trace);
void resolve_wide_keywords(StyleMap& specified, const StyleMap* parent);
void resolve_logical_box_edges(StyleMap& specified);
void resolve_revert(StyleMap& specified, DeclSink& sink, const StyleMap* parent,
                    const html::Node* custom_origin, const MediaState& media,
                    const FinalizeTrace* trace,
                    const std::unordered_set<::zui::Atom, ::zui::AtomHash>* local_names);
void resolve_overflow_axes(StyleMap& specified);
void normalize_font_family(StyleMap& specified);
DeclKey winner_key(const Winner& w);

void mix_u64(uint64_t& h, uint64_t v);
void cascade_children(html::Node* node, const html::Document& doc, const CascadeIn& in,
                      Engine::CascadeResult& result, ShareCache& cache, const WalkPos& parent_pos);
void remember_style_attr(MemoEntry& entry, bool has_style, std::string_view text);
bool style_attr_same(const MemoEntry& entry, bool has_style, std::string_view text);
bool media_selectors_stale(const MemoEntry& entry, html::Node* node, const html::Document& doc,
                           const CascadeIn& in);
bool anchor_compound_matches(const html::Node& node, const HasAnchor& anchor);
uint64_t parent_name_sig(const StyleMap* map);

struct Finalized {
    std::shared_ptr<const StyleMap> style;
    std::array<std::shared_ptr<const StyleMap>, kPseudoCount> pseudo;
    std::shared_ptr<const html::SelectionStyle> selection;
    std::shared_ptr<const StyleMap> backdrop;
    std::vector<ReadValue> reads;
    std::vector<ParentOffer> taken;
    bool reads_pseudo = false;
};

bool before_after_generates(const DeclSink& sink);
bool highlight_depends_on_origin(const DeclSink& sink);
void finalize_payload(const DeclSinks& pristine, const StyleMap* inherited, const html::Node* node,
                      const MediaState& media, Finalized& out);
bool parent_offers_hold(const MemoEntry& entry, const StyleMap* parent_map);
bool reads_hold(const MemoEntry& entry, const html::Node* node);
std::shared_ptr<const StyleMap> reuse_if_equal(std::shared_ptr<const StyleMap> fresh,
                                               const std::shared_ptr<const StyleMap>& stored);

html::Node* prev_element(html::Node* node);
bool match_simple(html::Node* node, const html::Document& doc, const lxb_css_selector_t* sel);
bool matches_nth_pseudo(html::Node* node, const html::Document& doc, unsigned type,
                        const void* data);
bool match_chain(html::Node* node, const html::Document& doc,
                 const lxb_css_selector_t* const* chain, size_t chain_n, size_t index);
bool match_relative(html::Node* origin, const html::Document& doc, unsigned comb,
                    const lxb_css_selector_t* const* chain, size_t chain_n);
bool language_matches_list(const html::Node& node, std::string_view list);
int nth_position(html::Node* node, const html::Document& doc, bool of_type, bool from_end,
                 const lxb_css_selector_list_t* of);
bool is_first_element(const html::Node* node);
bool is_last_element(const html::Node* node);
bool is_first_of_type(const html::Node* node);
bool is_last_of_type(const html::Node* node);
int element_position(const html::Node* node);
int element_position_from_end(const html::Node* node);
int type_position(const html::Node* node);
int type_position_from_end(const html::Node* node);
bool matches_anb(long a, long b, int position);

}  // namespace detail
}  // namespace css
}  // namespace zui

#endif
