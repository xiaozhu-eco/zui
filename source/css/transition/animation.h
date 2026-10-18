#ifndef ZUI_CSS_ANIMATION_H
#define ZUI_CSS_ANIMATION_H

#include "css/engine/strip_keyframes.h"
#include "html/document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace css {

/// Running CSS animations. Specified style stays on `Node::computed`; `sample` writes the used
/// overlay on `Node::used`. The clock is the same `frame_t` the transition runner samples.
class Animations {
public:
    void clear();
    void cancel_subtree(html::Node* dead);
    /// Binds one animation per element from its computed `animation` and the `@keyframes` text in
    /// @p sources. @p t is the engine clock; an animation keeps the start time of the sync that
    /// first saw this declaration.
    void sync(html::Document& doc, const std::vector<std::string>& sources, double t);
    void sample(double t);

    bool active() const { return !runs_.empty(); }
    bool needs_visual() const { return !runs_.empty(); }
    bool repaint_pending() const { return repaint_; }
    /// True after sample() (or a sync that cleared one) when a width/height/margin/padding overlay
    /// was written or cleared. The host marks those nodes and lays out; paint reads the other
    /// channels straight off `Node::used`.
    bool needs_layout() const { return layout_; }
    const std::vector<html::Node*>& layout_nodes() const { return layout_nodes_; }

    struct Stop {
        float offset = 0.f;
        /// Longhands this keyframe declares. Only names the transition sampler already updates.
        ::zui::StyleMap decls;
    };
    struct Rule {
        std::string name;
        std::vector<Stop> stops;
    };

private:
    struct Run {
        html::Node* node = nullptr;
        std::string name;
        float duration = 0.f;
        float delay = 0.f;
        float x1 = 0.42f;
        float y1 = 0.f;
        float x2 = 0.58f;
        float y2 = 1.f;
        bool infinite = false;
        float iterations = 1.f;
        bool alternate = false;
        double start = 0;
        bool applied = false;
        /// Bits of `transition_detail::Prop` written by the last sample of this run.
        std::uint32_t applied_mask = 0;
        std::vector<Stop> stops;
        bool seen = false;
    };

    void parse_sources(const std::vector<std::string>& sources);
    const Rule* find_rule(std::string_view name) const;
    void clear_overlay(Run& run);
    void note_layout(html::Node* node);

    std::vector<Rule> rules_;
    std::vector<Run> runs_;
    std::size_t source_stamp_ = 0;
    bool repaint_ = false;
    bool layout_ = false;
    std::vector<html::Node*> layout_nodes_;
};

}  // namespace css
}  // namespace zui

#endif
