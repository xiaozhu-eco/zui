// Sibling walk: nth / of-type counts, preceding-sibling signature, child cascade.

#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include "perf/perf_budget.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <chrono>

namespace zui {
namespace css {
namespace detail {

// Sibling tag counts for :first-of-type / :last-of-type / :nth-of-type. A hashed
// `unordered_map<string, int>` allocates a bucket array per parent under /Od, and a cold cascade
// walks every parent. A parent has a handful of child tag names, so a stack table is the same
// counts with no heap traffic. More than 16 distinct tags spills to a vector.
struct TagCounts {
    struct Slot {
        std::string_view tag;
        int count = 0;
    };
    Slot stack[16];
    int n = 0;
    std::vector<Slot> overflow;

    int add(std::string_view tag) {
        for (int i = 0; i < n; ++i) {
            if (stack[i].tag == tag) {
                return ++stack[i].count;
            }
        }
        for (Slot& slot : overflow) {
            if (slot.tag == tag) {
                return ++slot.count;
            }
        }
        if (n < 16) {
            stack[n] = Slot{tag, 1};
            ++n;
            return 1;
        }
        overflow.push_back(Slot{tag, 1});
        return 1;
    }

    int get(std::string_view tag) const {
        for (int i = 0; i < n; ++i) {
            if (stack[i].tag == tag) {
                return stack[i].count;
            }
        }
        for (const Slot& slot : overflow) {
            if (slot.tag == tag) {
                return slot.count;
            }
        }
        return 0;
    }
};

namespace {

/// `An+B` matches 1-based positions `a*n + b` for integer `n >= 0`. Same rule as selector_match.
bool pattern_matches(long a, long b, int position) {
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

}  // namespace

void cascade_children(html::Node* node, const html::Document& doc, const CascadeIn& in,
                      Engine::CascadeResult& result, ShareCache& cache, const WalkPos& parent_pos) {
    // Exclusive time of this walk, not the recursive `cascade_node`. Diagnostic; strip with the
    // other phase clocks. A lambda would be a call under /Ob0, so the pause is a macro.
    const bool child_armed = cascade_split_armed();
    double* child_slot = child_armed ? &g_cascade_split.exp_ms : nullptr;
    std::chrono::steady_clock::time_point child_t0 =
        child_armed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#define ZUI_CHILD_PAUSE()                                                                          \
    do {                                                                                           \
        if (child_slot != nullptr) {                                                               \
            *child_slot += std::chrono::duration<double, std::milli>(                             \
                               std::chrono::steady_clock::now() - child_t0)                       \
                               .count();                                                           \
        }                                                                                          \
    } while (0)
#define ZUI_CHILD_RESUME()                                                                         \
    do {                                                                                           \
        if (child_slot != nullptr) {                                                               \
            child_t0 = std::chrono::steady_clock::now();                                           \
        }                                                                                          \
    } while (0)
    size_t el_total = 0;
    TagCounts type_total;
    for (const auto& ch : node->children) {
        if (html::is_element(ch.get())) {
            ++el_total;
            type_total.add(ch->tag);
        }
    }
    // An `:nth-child(An+B of S)` term is decided by how many siblings before (and after) the node
    // match `S`. Those counts depend on the whole sibling list, so they are gathered here, once per
    // parent, and fingerprint the children's share keys. Sheets without an `of S` clause skip all
    // of this.
    const size_t of_count = in.nth_of_lists != nullptr ? in.nth_of_lists->size() : 0;
    std::vector<int> of_total(of_count, 0);
    std::vector<unsigned char> of_matches(el_total * of_count, 0);
    if (of_count != 0) {
        size_t element = 0;
        for (const auto& ch : node->children) {
            if (!html::is_element(ch.get())) {
                continue;
            }
            for (size_t i = 0; i < of_count; ++i) {
                // docs/PERF_REFACTOR.md capability P3: the key needs this count, so the `of S`
                // test is a selector evaluation the key build spends.
                perf::note_cascade_key_eval();
                if (match_list(ch.get(), doc, (*in.nth_of_lists)[i])) {
                    of_matches[element * of_count + i] = 1;
                    ++of_total[i];
                }
            }
            ++element;
        }
    }
    size_t el_index = 0;
    TagCounts type_seen;
    std::vector<int> of_seen(of_count, 0);
    const bool nth_fingerprint = in.nth_patterns != nullptr && !in.nth_patterns->empty() &&
                                 in.nth_patterns->size() <= 31;
    std::vector<int> of_pos(nth_fingerprint ? of_count : 0, 0);
    std::vector<int> of_pos_end(nth_fingerprint ? of_count : 0, 0);
    html::Node* prev_el = nullptr;
    // The preceding-sibling chain the sibling-side compounds (`+` / `~`) can observe. Accumulated
    // once per parent, so a node's cross-frame key never re-scans the siblings on its left.
    const NeighborFacts* sibling_facts = in.sibling_facts;
    uint64_t prev_chain = 0;
    for (const auto& ch : node->children) {
        uint32_t pos = 0;
        uint32_t nth_index = 0;
        uint32_t nth_type_index = 0;
        uint64_t nth_of_sig = 0;
        if (html::is_element(ch.get())) {
            if (el_index == 0) {
                pos |= kBitFirst;
            }
            if (el_index + 1 == el_total) {
                pos |= kBitLast;
            }
            const int seen = type_seen.add(ch->tag);
            if (seen == 1) {
                pos |= kBitFirstType;
            }
            if (seen == type_total.get(ch->tag)) {
                pos |= kBitLastType;
            }
            nth_index = static_cast<uint32_t>(el_index + 1);
            nth_type_index = static_cast<uint32_t>(seen);
            for (size_t i = 0; i < of_count; ++i) {
                const bool matched = of_matches[el_index * of_count + i] != 0;
                const int before = of_seen[i];
                const int after = of_total[i] - before - (matched ? 1 : 0);
                if (nth_fingerprint && i < of_pos.size()) {
                    of_pos[i] = matched ? before + 1 : 0;
                    of_pos_end[i] = matched ? after + 1 : 0;
                }
                mix_u64(nth_of_sig, static_cast<uint64_t>(i));
                mix_u64(nth_of_sig, static_cast<uint64_t>(before));
                mix_u64(nth_of_sig, static_cast<uint64_t>(after));
                if (matched) {
                    ++of_seen[i];
                }
            }
            if (nth_fingerprint) {
                // Bit p is "pattern p matches this element". Siblings that miss and match the same
                // patterns share one computed map; odd and even do not, because each An+B is its
                // own bit. The raw index stays out so a 200-row list is two keys, not 200.
                uint32_t bits = 0;
                const int from_end = static_cast<int>(el_total - el_index);
                const auto& patterns = *in.nth_patterns;
                for (size_t p = 0; p < patterns.size(); ++p) {
                    const NthPattern& pat = patterns[p];
                    int position = 0;
                    if (pat.of != nullptr) {
                        int of_index = -1;
                        if (in.nth_of_lists != nullptr) {
                            for (size_t i = 0; i < in.nth_of_lists->size(); ++i) {
                                if ((*in.nth_of_lists)[i] == pat.of) {
                                    of_index = static_cast<int>(i);
                                    break;
                                }
                            }
                        }
                        if (of_index >= 0 && static_cast<size_t>(of_index) < of_pos.size()) {
                            position = pat.from_end ? of_pos_end[static_cast<size_t>(of_index)]
                                                    : of_pos[static_cast<size_t>(of_index)];
                        }
                    } else if (pat.of_type) {
                        const int type_total_n = type_total.get(ch->tag);
                        position = pat.from_end ? type_total_n - static_cast<int>(nth_type_index) + 1
                                                : static_cast<int>(nth_type_index);
                    } else {
                        position = pat.from_end ? from_end : static_cast<int>(nth_index);
                    }
                    if (pattern_matches(pat.a, pat.b, position)) {
                        bits |= 1u << p;
                    }
                }
                nth_index = bits;
                nth_type_index = 0;
                nth_of_sig = 0;
            } else if (!in.selector_uses_nth) {
                nth_index = 0;
                nth_type_index = 0;
                nth_of_sig = 0;
            }
            ++el_index;
        }
        WalkPos child_pos;
        child_pos.parent_sig = parent_pos.parent_sig;
        child_pos.ancestor_sig = parent_pos.ancestor_sig;
        child_pos.sibling_sig = prev_chain;
        child_pos.prev_tag =
            prev_el != nullptr ? std::string_view(prev_el->tag) : std::string_view{};
        child_pos.pos_bits = pos;
        child_pos.nth_index = nth_index;
        child_pos.nth_type_index = nth_type_index;
        child_pos.nth_of_sig = nth_of_sig;
        child_pos.siblings = static_cast<uint32_t>(el_total);
        child_pos.siblings_of_type =
            html::is_element(ch.get()) ? static_cast<uint32_t>(type_total.get(ch->tag)) : 0;
        // A child that is neither in the closure nor an ancestor of one cannot hold a node the
        // cascade still has to match. The list-item counter still counts it: a later item inside
        // the closure needs the ordinal, and a nested list is its own scope so the skipped
        // subtree's items do not leak. Author counters are absent here — they force a whole
        // document cascade, which has no walk set.
        const bool skip_walk = in.walk_set != nullptr &&
                               in.walk_set->find(ch.get()) == in.walk_set->end();
        if (skip_walk) {
            if (in.list_counters != nullptr && html::is_list_item(ch.get())) {
                const int ordinal = in.list_counters->increment();
                if (ch->tag == "li") {
                    const auto vit = ch->attrs.find("value");
                    if (vit != ch->attrs.end()) {
                        in.list_counters->set(parse_html_integer(vit->second, ordinal));
                    }
                }
            }
        } else {
            ZUI_CHILD_PAUSE();
            cascade_node(ch.get(), doc, in, result, cache, child_pos);
            ZUI_CHILD_RESUME();
        }
        if (html::is_element(ch.get())) {
            if (sibling_facts != nullptr && sibling_facts->used) {
                const uint32_t offered = key_state_bits(
                    *ch, pos | node_state_bits(*ch, &doc, in.html_state_bits), in.hover_targets,
                    in.active_targets);
                mix_u64(prev_chain, neighbor_fact_sig(*ch, offered, *sibling_facts));
            }
            prev_el = ch.get();
        }
    }
    ZUI_CHILD_PAUSE();
#undef ZUI_CHILD_RESUME
#undef ZUI_CHILD_PAUSE
}

}  // namespace detail
}  // namespace css
}  // namespace zui
