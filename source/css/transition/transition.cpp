#include "css/transition/transition.h"
#include "css/transition/internal.h"

#include "perf/perf_budget.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace zui {
namespace css {

using namespace transition_detail;

namespace transition_detail {

bool under(html::Node* n, html::Node* root) {
    for (; n != nullptr; n = n->parent) {
        if (n == root) {
            return true;
        }
    }
    return false;
}

void clear_used_tree(html::Node* n) {
    if (n == nullptr) {
        return;
    }
    n->used = {};
    for (auto& ch : n->children) {
        clear_used_tree(ch.get());
    }
}

}  // namespace transition_detail

struct Transitions::Impl {
    std::vector<Record> records;
    std::vector<html::Node*> layout_nodes;
    std::vector<html::Node*> visual_nodes;
    LengthBasisFn basis;
    bool visual = false;
    bool layout = false;
    bool repaint = false;
};

Transitions::Transitions() : impl_(std::make_unique<Impl>()) {}
Transitions::~Transitions() = default;

void Transitions::clear() {
    // Every overlay the records held is about to be forgotten; whatever is left in `used` is the
    // state the next frame paints with, so the paint clock moves.
    for (const Record& record : impl_->records) {
        html::note_paint_change(record.node);
    }
    for (html::Node* node : impl_->visual_nodes) {
        html::note_paint_change(node);
    }
    impl_->records.clear();
    impl_->layout_nodes.clear();
    impl_->visual_nodes.clear();
    impl_->visual = false;
    impl_->layout = false;
    impl_->repaint = false;
}

void Transitions::set_length_basis(LengthBasisFn fn) { impl_->basis = std::move(fn); }

bool Transitions::active() const { return !impl_->records.empty(); }
bool Transitions::needs_visual() const { return impl_->visual; }
bool Transitions::repaint_pending() const { return impl_->repaint; }
bool Transitions::needs_layout() const { return impl_->layout; }
const std::vector<html::Node*>& Transitions::layout_nodes() const { return impl_->layout_nodes; }
const std::vector<html::Node*>& Transitions::visual_nodes() const { return impl_->visual_nodes; }

void Transitions::cancel_subtree(html::Node* dead) {
    if (dead == nullptr) {
        return;
    }
    auto& records = impl_->records;
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [&](const Record& r) { return under(r.node, dead); }),
                  records.end());
    clear_used_tree(dead);
    html::note_paint_change(dead);
    impl_->visual = false;
    impl_->layout = false;
    impl_->repaint = false;
    impl_->layout_nodes.clear();
    impl_->visual_nodes.clear();
    for (const Record& r : records) {
        if (r.prop == Prop::Transform) {
            impl_->visual = true;
            impl_->visual_nodes.push_back(r.node);
        }
        if (is_length_prop(r.prop)) {
            impl_->layout = true;
        }
    }
}

void Transitions::sync(const Engine::CascadeResult& cr, double t) {
    const Prop props[] = {Prop::Color,         Prop::Background,    Prop::BorderTop,
                          Prop::BorderRight,   Prop::BorderBottom,  Prop::BorderLeft,
                          Prop::Opacity,       Prop::Transform,     Prop::Width,
                          Prop::Height,        Prop::MarginTop,     Prop::MarginRight,
                          Prop::MarginBottom,  Prop::MarginLeft,    Prop::PaddingTop,
                          Prop::PaddingRight,  Prop::PaddingBottom, Prop::PaddingLeft};
    for (const auto& ch : cr.paint) {
        html::Node* node = ch.node;
        if (node == nullptr || ch.previous == nullptr || node->computed == nullptr) {
            continue;
        }
        const StyleMap* oldm = ch.previous.get();
        const StyleMap* newm = node->computed.get();
        Timing timing = parse_timing(newm);
        if (timing.props.empty()) {
            // Nothing on this node declares a transition, so no property of it can start one. The
            // per-property loop below is what costs: each iteration derives two specified values,
            // and a specified value is a map lookup plus a parse (a color, a length, a transform).
            // A theme change re-styles the whole document and lands every node with a changed
            // interpolable property in `cr.paint`; on the studio's 80x80 grid that was 6788 nodes
            // x 18 properties of parsing for 22 transitions that actually started.
            continue;
        }
        const bool allow_auto = allow_keyword_size(node);
        LengthBasis basis{};
        if (impl_->basis) {
            basis = impl_->basis(node);
        }
        for (Prop prop : props) {
            // Does this node transition this property at all? A `transition: background-color ...`
            // declaration covers one longhand; the other seventeen are not this node's business and
            // must not pay for a specified value each. The shorthand's coverage is decided by
            // `prop_index` (`transition_prop_matches`), so a `border-color` entry still reaches all
            // four border longhands.
            const int idx = prop_index(timing, css_name(prop));
            if (idx < 0) {
                continue;
            }
            Value from = specified_value(node, oldm, prop);
            Value to = specified_value(node, newm, prop);
            perf::note_transition_value();
            perf::note_transition_value();
            if (is_length_prop(prop)) {
                // Reject endpoints we cannot represent at all.
                if (!(from.length_ok || from.length_percent || from.length_auto) ||
                    !(to.length_ok || to.length_percent || to.length_auto)) {
                    continue;
                }
                // auto↔auto and auto without interpolate-size never start.
                if ((from.length_auto || to.length_auto) && !allow_auto) {
                    continue;
                }
                if (from.length_auto && to.length_auto) {
                    continue;
                }
                // Resolve % immediately against the current CB. Resolve `from: auto` against the
                // pre-change used size. Leave `to: auto` pending until after the new style layouts.
                try_resolve_length(prop, from, basis, allow_auto);
                if (!to.length_auto) {
                    try_resolve_length(prop, to, basis, allow_auto);
                }
                if (!from.length_ok && !from.length_auto && !from.length_percent) {
                    continue;
                }
                if (!to.length_ok && !to.length_auto && !to.length_percent) {
                    continue;
                }
                // Both sides still unresolved → cannot start.
                if (!from.length_ok && !to.length_ok && !to.length_auto) {
                    continue;
                }
            }
            if (value_eq(prop, from, to)) {
                continue;
            }
            const float dur = at_or(timing.durations, idx, 0);
            if (dur <= 0.0005f) {
                continue;
            }
            Record* rec = nullptr;
            for (Record& r : impl_->records) {
                if (r.node == node && r.prop == prop) {
                    rec = &r;
                    break;
                }
            }
            const bool pending =
                is_length_prop(prop) && (!from.length_ok || !to.length_ok);
            if (rec != nullptr) {
                if (!pending && value_eq(prop, rec->to, to)) {
                    continue;
                }
                const float p = sample_easing(progress_of(*rec, t), rec->ease);
                const double ran = std::max(0.0, t - rec->start - static_cast<double>(rec->delay));
                const bool reversing = !pending && !rec->pending && value_eq(prop, rec->from, to);
                if (!rec->pending && rec->from.length_ok && rec->to.length_ok) {
                    rec->from = lerp_value(prop, rec->from, rec->to, p);
                } else {
                    rec->from = from;
                }
                rec->to = to;
                rec->pending = pending;
                rec->start = t;
                rec->delay = 0;
                // A reversal takes as long as the forward run already did. Any other
                // retarget keeps the duration written in the stylesheet.
                rec->duration = reversing ? std::max(1.0f / 60.0f, static_cast<float>(ran)) : dur;
                rec->ease = ease_at(timing, idx);
            } else {
                Record r;
                r.node = node;
                r.prop = prop;
                r.from = from;
                r.to = to;
                r.start = t;
                r.duration = dur;
                r.delay = at_or(timing.delays, idx, 0);
                r.ease = ease_at(timing, idx);
                r.pending = pending;
                impl_->records.push_back(r);
            }
        }
    }
}

void Transitions::resolve_pending() {
    if (!impl_->basis) {
        return;
    }
    for (Record& r : impl_->records) {
        if (!r.pending || r.node == nullptr || !is_length_prop(r.prop)) {
            continue;
        }
        const bool allow_auto = allow_keyword_size(r.node);
        const LengthBasis basis = impl_->basis(r.node);
        try_resolve_length(r.prop, r.from, basis, allow_auto);
        try_resolve_length(r.prop, r.to, basis, allow_auto);
        if (r.from.length_ok && r.to.length_ok) {
            r.pending = false;
        }
    }
    // Drop length records that can never resolve.
    auto& records = impl_->records;
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [](const Record& r) {
                                     if (!r.pending || !is_length_prop(r.prop)) {
                                         return false;
                                     }
                                     // Still waiting on auto/percent with a chance after another layout.
                                     return !r.from.length_auto && !r.from.length_percent &&
                                            !r.from.length_ok;
                                 }),
                  records.end());
}

void Transitions::sample(double t) {
    impl_->visual = false;
    impl_->layout = false;
    impl_->repaint = false;
    impl_->layout_nodes.clear();
    impl_->visual_nodes.clear();
    auto note_layout = [&](html::Node* node) {
        impl_->layout = true;
        if (node == nullptr) {
            return;
        }
        for (html::Node* n : impl_->layout_nodes) {
            if (n == node) {
                return;
            }
        }
        impl_->layout_nodes.push_back(node);
    };
    auto note_visual = [&](html::Node* node) {
        if (node == nullptr) {
            return;
        }
        for (html::Node* n : impl_->visual_nodes) {
            if (n == node) {
                return;
            }
        }
        impl_->visual_nodes.push_back(node);
    };
    auto& records = impl_->records;
    for (size_t i = 0; i < records.size();) {
        Record& r = records[i];
        if (r.node == nullptr) {
            records.erase(records.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (r.pending || (is_length_prop(r.prop) && (!r.from.length_ok || !r.to.length_ok))) {
            ++i;
            continue;
        }
        const float p = sample_easing(progress_of(r, t), r.ease);
        Value cur = lerp_value(r.prop, r.from, r.to, p);
        // The overlay this writes is a paint input the cascade cannot see, so the frame that
        // sampled it has to paint - `needs_visual` only covers the placement a transform needs.
        impl_->repaint = true;
        write_used(r.node, r.prop, cur);
        if (r.prop == Prop::Transform) {
            impl_->visual = true;
            note_visual(r.node);
        }
        if (is_length_prop(r.prop)) {
            note_layout(r.node);
        }
        const bool done = (t - r.start) >= static_cast<double>(r.delay + r.duration) - 1e-6;
        if (done) {
            write_used(r.node, r.prop, r.to);
            clear_flag(r.node, r.prop);
            if (is_length_prop(r.prop)) {
                note_layout(r.node);
            }
            records.erase(records.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}
}  // namespace css
}  // namespace zui
