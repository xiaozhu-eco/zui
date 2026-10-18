#ifndef ZUI_CSS_TRANSITION_H
#define ZUI_CSS_TRANSITION_H

#include "css/engine/engine.h"
#include "html/document.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace zui {
namespace css {

struct Xform {
    float tx = 0;
    float ty = 0;
    float sx = 1;
    float sy = 1;
    float rot = 0;
    bool tx_pct = false;
    bool ty_pct = false;

    bool is_identity() const { return tx == 0 && ty == 0 && sx == 1 && sy == 1 && rot == 0; }

    float used_tx(float width) const { return tx_pct ? tx * 0.01f * width : tx; }
    float used_ty(float height) const { return ty_pct ? ty * 0.01f * height : ty; }
};

Xform parse_transform(std::string_view css);
float parse_opacity(std::string_view css);

/// Containing-block / used-size facts for resolving `%` and `auto` length endpoints.
struct LengthBasis {
    float inline_px = 0;
    float block_px = 0;
    float used_width = 0;
    float used_height = 0;
    bool has_inline = false;
    bool has_block = false;
    bool has_used_width = false;
    bool has_used_height = false;
};

using LengthBasisFn = std::function<LengthBasis(const html::Node*)>;

/// Running CSS transitions. Specified style stays on interned Node::computed;
/// sample() writes the used overlay on Node::used.
class Transitions {
public:
    Transitions();
    ~Transitions();

    Transitions(const Transitions&) = delete;
    Transitions& operator=(const Transitions&) = delete;

    void clear();
    void cancel_subtree(html::Node* dead);
    /// Optional basis resolves `%` / `auto` (with `interpolate-size: allow-keywords`) at sync.
    void set_length_basis(LengthBasisFn fn);
    void sync(const Engine::CascadeResult& cr, double t);
    /// After layout: resolve any still-pending `%` / `auto` endpoints from fresh boxes.
    void resolve_pending();
    void sample(double t);

    bool active() const;
    bool needs_visual() const;
    /// True when the last sample() wrote a used overlay the painter reads. A running transition is
    /// a paint input of its own: `write_used` writes `Node::used` outside the interned style, so no
    /// cascade and no layout sees it move. The host paints a frame whenever this is set, or the
    /// screen keeps the last frame an input happened to paint - the colour transition freezes at
    /// whatever intermediate value that was (docs/PERF.md I6).
    bool repaint_pending() const;
    /// True after sample() when a width/height/margin/padding overlay was written or cleared.
    bool needs_layout() const;
    /// Nodes whose length overlay changed in the last sample(); host marks them dirty and re-layouts.
    const std::vector<html::Node*>& layout_nodes() const;
    /// Nodes whose `transform` overlay changed in the last sample(). A transform never enters
    /// layout, but it does move the box in the world, so the host marks their principal boxes for
    /// the next placement (docs/PERF_REFACTOR.md stage P).
    const std::vector<html::Node*>& visual_nodes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace css
}  // namespace zui

#endif
