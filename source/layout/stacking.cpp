#include "layout/stacking.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace zui {
namespace layout {
namespace {

const std::string* computed(const Box& box, const char* key) {
    return box.node == nullptr ? nullptr : box.node->style(key);
}

bool differs(const Box& box, const char* key, const char* initial) {
    const std::string* value = computed(box, key);
    return value != nullptr && *value != initial;
}

bool not_none(const Box& box, const char* key) { return differs(box, key, "none"); }

/// Whether a space- or comma-separated value lists `word` as a whole word. `contain` and
/// `will-change` both take such lists.
bool has_word(const std::string* value, const char* word) {
    if (value == nullptr || word == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(word);
    std::size_t pos = 0;
    while ((pos = value->find(word, pos)) != std::string::npos) {
        const auto boundary = [](unsigned char c) { return !std::isalnum(c) && c != '-'; };
        const bool left = pos == 0 || boundary(static_cast<unsigned char>((*value)[pos - 1]));
        const bool right =
            pos + n >= value->size() || boundary(static_cast<unsigned char>((*value)[pos + n]));
        if (left && right) {
            return true;
        }
        pos += n;
    }
    return false;
}

/// CSS Position 3 section 9.3: `z-index` is `auto`, or an integer. A value that is neither is an
/// invalid declaration, which the cascade drops - this engine keeps the raw computed text, so an
/// unparsable value has to fall back to `auto` here too.
void used_z_index(const Box& box, bool* z_auto, int* z_index) {
    const std::string* value = computed(box, "z-index");
    if (value == nullptr || value->empty() || *value == "auto") {
        *z_auto = true;
        *z_index = 0;
        return;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value->c_str(), &end, 10);
    if (end == value->c_str() || *end != '\0') {
        *z_auto = true;
        *z_index = 0;
        return;
    }
    *z_auto = false;
    *z_index = static_cast<int>(std::max(-2147483647L, std::min(2147483647L, parsed)));
}

/// The value the painter is about to use, including a running opacity transition's overlay
/// (`html::Node::used`). Reading the animated value is what keeps the context-ness of a box in step
/// with the colour it paints - LAYOUT.md section 11 says this walk reads the painting style.
/// `opacity` is `<number>` or `<percentage>` (CSS Color 4), clamped to 0..1.
float parse_opacity(const std::string* value) {
    if (value == nullptr) {
        return 1.f;
    }
    const char* text = value->c_str();
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (end == text) {
        return 1.f;
    }
    float used = parsed;
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end == '%') {
        used *= 0.01f;
    }
    return std::max(0.f, std::min(1.f, used));
}

float used_opacity(const Box& box) {
    if (box.node != nullptr && (box.node->used.flags & html::VisualUsed::kOpacity)) {
        return box.node->used.opacity;
    }
    const std::string* value = computed(box, "opacity");
    return parse_opacity(value);
}

/// `will-change` names the properties whose stacking effect it borrows (LAYOUT.md section 11).
bool will_change_context(const Box& box) {
    const std::string* value = computed(box, "will-change");
    return has_word(value, "opacity") || has_word(value, "transform") ||
           has_word(value, "filter") || has_word(value, "perspective") ||
           has_word(value, "mix-blend-mode");
}

struct Entry {
    Box* box = nullptr;
    // CSS 2.1 appendix E: 0 negative contexts; 1-3 the in-flow non-positioned group, split the
    // way that appendix splits it (block-level, floats, inline content); 4 contexts with
    // z-index 0/auto and positioned boxes with `z-index: auto`; 5 positive contexts.
    int bucket = 1;
    int z = 0;
    int order = 0;
    int position = 0;
};

Entry make_entry(Box* child, const StackingInfo& info, int position) {
    Entry entry;
    entry.box = child;
    entry.z = info.z_index;
    entry.order = info.order;
    entry.position = position;
    if (info.context && !info.z_auto && info.z_index < 0) {
        entry.bucket = 0;
    } else if (info.context && !info.z_auto && info.z_index > 0) {
        entry.bucket = 5;
    } else if (info.context || info.positioned) {
        entry.bucket = 4;
    } else if (info.floating) {
        entry.bucket = 2;
    } else if (info.inline_level) {
        entry.bucket = 3;
    } else {
        entry.bucket = 1;
    }
    return entry;
}

/// `Full` adds in-flow non-positioned children of @p host. `LiftOnly` walks only for positioned /
/// stacking-context descendants that belong to an ancestor stacking context (appendix E flatten).
enum class CollectMode { Full, LiftOnly };

void collect_stacking(const Box& host, bool host_is_context, CollectMode mode,
                      std::vector<Entry>& entries, int& seq) {
    for (std::size_t i = 0; i < host.children.size(); ++i) {
        Box* child = host.children[i];
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const StackingInfo info = stacking_info(*child);
        const int position = seq++;

        // Non-auto z-index stacking contexts always participate in the nearest ancestor context
        // and trap their own descendants.
        if (info.context && !info.z_auto) {
            if (host_is_context) {
                entries.push_back(make_entry(child, info, position));
            }
            continue;
        }

        // Positioned boxes and other stacking contexts (opacity/transform/… with z-auto).
        if (info.positioned || info.context) {
            if (host_is_context) {
                entries.push_back(make_entry(child, info, position));
            }
            // z-index:auto positioned boxes are not contexts: lift their descendants into the
            // same ancestor context. A real context (even with z-auto) traps its subtree.
            if (!info.context) {
                collect_stacking(*child, host_is_context, CollectMode::LiftOnly, entries, seq);
            }
            continue;
        }

        // In-flow, non-positioned, non-context.
        if (mode == CollectMode::Full) {
            entries.push_back(make_entry(child, info, position));
        }
        if (host_is_context) {
            collect_stacking(*child, true, CollectMode::LiftOnly, entries, seq);
        }
    }
}

/// True when a descendant under non-context boxes must paint in @p box's stacking context.
bool has_deep_stacking_participant(const Box& box) {
    for (const Box* child : box.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const StackingInfo info = stacking_info(*child);
        if (info.context && !info.z_auto) {
            return true;
        }
        if (info.positioned || info.context) {
            return true;
        }
        if (!info.context && has_deep_stacking_participant(*child)) {
            return true;
        }
    }
    return false;
}

}  // namespace

StackingInfo stacking_info(const Box& box) {
    StackingInfo info;
    if (box.style_ptr == nullptr) {
        return info;
    }
    const LayoutStyle& style = box.style();
    used_z_index(box, &info.z_auto, &info.z_index);
    info.order = style.flex_order;
    // The root element is a stacking context, and so is a box that the tree marked as a flex or
    // grid item when its `z-index` is not `auto`.
    const bool z_applies =
        !info.z_auto &&
        (style.position_absolute || style.position_relative || box.is_flex_item ||
         box.is_grid_item);
    info.positioned = style.position_relative || style.position_absolute ||
                      style.position_fixed || style.position_sticky;
    info.floating = style.floating;
    info.inline_level = !style.floating && style.outer == DisplayOuter::Inline;
    info.context = box.parent == nullptr || z_applies || style.position_fixed ||
                   style.position_sticky || style.contain_layout || style.contain_paint ||
                   used_opacity(box) < 1.f || not_none(box, "transform") ||
                   not_none(box, "filter") || not_none(box, "perspective") ||
                   not_none(box, "backdrop-filter") || not_none(box, "clip-path") ||
                   not_none(box, "mask") || differs(box, "isolation", "auto") ||
                   differs(box, "mix-blend-mode", "normal") || will_change_context(box);
    return info;
}

bool stacking_negative_z(const Box& box) {
    const StackingInfo info = stacking_info(box);
    return info.context && !info.z_auto && info.z_index < 0;
}

bool stacking_skip_lifted_child(const Box& host, const Box& child) {
    // A stacking context (and the root) paints its own participants, including deep ones.
    if (host.parent == nullptr || stacking_info(host).context) {
        return false;
    }
    const StackingInfo ci = stacking_info(child);
    // Under a non-context host, positioned boxes and stacking contexts belong to the nearest
    // ancestor stacking context (CSS 2.1 appendix E flatten through `z-index: auto`).
    return ci.positioned || ci.context;
}

bool has_stacking_children(const Box& box) {
    // CSS 2.1 appendix E steps 3-5: a block's in-flow block-level children paint first, then its
    // floats, then its inline content - so a float that comes after a block still paints over that
    // block's background, and inline content that comes before a float still paints after it. When
    // the tree order already has that shape (the common case) the painter walks `children` straight
    // through, and `rank` is what decides it: block 0, float 1, inline 2, ascending.
    int previous_rank = -1;
    for (const Box* child : box.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const StackingInfo info = stacking_info(*child);
        // `order` only decides the sequence inside one context, but it is still a reordering the
        // painter has to apply (LAYOUT.md section 11), so it takes the ordered path as well.
        if (info.context || info.positioned || info.order != 0) {
            return true;
        }
        const int rank = info.floating ? 1 : (info.inline_level ? 2 : 0);
        if (rank < previous_rank) {
            return true;
        }
        previous_rank = std::max(previous_rank, rank);
    }
    // A stacking context must also take the ordered path when a positioned / z-index participant
    // sits under a non-context descendant (`z-index: auto` deep lift).
    const StackingInfo self = stacking_info(box);
    if (box.parent == nullptr || self.context) {
        return has_deep_stacking_participant(box);
    }
    return false;
}

void stacking_paint_order(const Box& box, std::vector<Box*>& order,
                          std::size_t* before_own_inlines) {
    order.clear();
    if (before_own_inlines != nullptr) {
        *before_own_inlines = 0;
    }
    const bool host_is_context = box.parent == nullptr || stacking_info(box).context;
    std::vector<Entry> entries;
    entries.reserve(box.children.size());
    int seq = 0;
    collect_stacking(box, host_is_context, CollectMode::Full, entries, seq);
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.bucket != right.bucket) {
            return left.bucket < right.bucket;
        }
        if (left.bucket == 0 || left.bucket == 5) {
            if (left.z != right.z) {
                return left.z < right.z;
            }
        }
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.position < right.position;
    });
    for (const Entry& entry : entries) {
        if (before_own_inlines != nullptr && entry.bucket <= 2) {
            ++(*before_own_inlines);
        }
        order.push_back(entry.box);
    }
}

}  // namespace layout
}  // namespace zui
