#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "log/log.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {

void paint_box_column_rule(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d) {
    const bool paint_self = d.paint_self;
    const float op = d.op;
    const float dx = d.dx;
    const float dy = d.dy;

    // CSS Multi-column Layout: `column-rule` paints in the middle of each column-gap.
    // groove / ridge use the same split as a vertical border edge. inset and outset follow
    // the collapsing-border treatment Edge paints for a rule (CSS Backgrounds 3): inset is
    // ridge, outset is groove. A single edge has no inside, so the side of the stroke does
    // not change those two.
    if (paint_self && box.style_ptr != nullptr &&
        box.style().inner == layout::DisplayInner::Multicol) {
        const layout::LayoutStyle& ms = box.style();
        auto length_px = [&](std::string_view raw) -> float {
            raw = trim_sv_local(raw);
            if (raw.empty()) {
                return 0.f;
            }
            char* end = nullptr;
            const float v = std::strtof(std::string(raw).c_str(), &end);
            if (end == nullptr || end == raw.data()) {
                return 0.f;
            }
            const std::string_view unit = trim_sv_local(std::string_view(end));
            if (unit.empty() || unit == "px") {
                return v;
            }
            if (unit == "em" || unit == "rem") {
                return v * ms.font_size;
            }
            return 0.f;
        };
        auto is_rule_style = [](const std::string& t) {
            return t == "none" || t == "hidden" || t == "dotted" || t == "dashed" || t == "solid" ||
                   t == "double" || t == "groove" || t == "ridge" || t == "inset" || t == "outset";
        };
        auto looks_like_color = [](const std::string& t) {
            return !t.empty() &&
                   (t.front() == '#' || t.rfind("rgb", 0) == 0 || t.rfind("hsl", 0) == 0 ||
                    t == "currentcolor" || t == "transparent");
        };
        float rule_w = 0.f;
        if (const std::string* wv = computed(box, "column-rule-width")) {
            rule_w = length_px(*wv);
        }
        std::string rule_style = "none";
        if (const std::string* sv = computed(box, "column-rule-style")) {
            rule_style = lower_ascii(*sv);
        }
        Color rule_c = paint_color(box, computed(box, "color"));
        if (const std::string* cv = computed(box, "column-rule-color")) {
            rule_c = paint_color(box, cv);
        }
        if (const std::string* sh = computed(box, "column-rule")) {
            // Shorthand: [<style> || <color> || <width>], any order.
            const std::string raw = *sh;
            std::string token;
            for (std::size_t i = 0; i <= raw.size(); ++i) {
                const bool end = i == raw.size();
                if (!end && !std::isspace(static_cast<unsigned char>(raw[i]))) {
                    token.push_back(raw[i]);
                    continue;
                }
                if (token.empty()) {
                    continue;
                }
                const std::string t = lower_ascii(token);
                if (is_rule_style(t)) {
                    rule_style = t;
                } else if (looks_like_color(t)) {
                    rule_c = paint_color(box, &token);
                } else {
                    const float w = length_px(t);
                    if (w > 0.f) {
                        rule_w = w;
                    }
                }
                token.clear();
            }
        }
        layout::BorderLineStyle line = layout::BorderLineStyle::None;
        if (rule_style == "solid") {
            line = layout::BorderLineStyle::Solid;
        } else if (rule_style == "dashed") {
            line = layout::BorderLineStyle::Dashed;
        } else if (rule_style == "dotted") {
            line = layout::BorderLineStyle::Dotted;
        } else if (rule_style == "double") {
            line = layout::BorderLineStyle::Double;
        } else if (rule_style == "groove") {
            line = layout::BorderLineStyle::Groove;
        } else if (rule_style == "ridge") {
            line = layout::BorderLineStyle::Ridge;
        } else if (rule_style == "inset") {
            line = layout::BorderLineStyle::Inset;
        } else if (rule_style == "outset") {
            line = layout::BorderLineStyle::Outset;
        }
        if (line != layout::BorderLineStyle::None && rule_w > 0.5f) {
            rule_c = apply_opacity(rule_c, op);
            if (!color_is_transparent(rule_c)) {
                const float content_inline = std::max(0.f, box.geo.content_width);
                const float gap = ms.column_gap_percent ? content_inline * ms.column_gap / 100.f
                                                        : std::max(0.f, ms.column_gap);
                int count = ms.column_count > 0 ? ms.column_count : 0;
                const bool width_auto = ms.column_width_auto || ms.column_width <= 0.f;
                if (count == 0 && width_auto) {
                    count = 1;
                } else if (width_auto) {
                    count = std::max(1, count);
                } else {
                    const float step = ms.column_width + gap;
                    const int fits =
                        step > 0.f ? static_cast<int>(std::floor((content_inline + gap) / step))
                                   : 1;
                    count = count == 0 ? std::max(1, fits) : std::min(count, std::max(1, fits));
                }
                const float col_w =
                    std::max(0.f, (content_inline - gap * static_cast<float>(count - 1)) /
                                      static_cast<float>(std::max(1, count)));
                const Rect content = content_rect(box, dx, dy);
                const float rule_h = std::max(0.f, content.h);
                if (count > 1 && rule_h > 0.f && col_w > 0.f) {
                    for (int i = 0; i < count - 1; ++i) {
                        const float gap_x = content.x + col_w * static_cast<float>(i + 1) +
                                            gap * static_cast<float>(i);
                        const float rx = gap_x + (gap - rule_w) * 0.5f;
                        // One vertical stroke, as a left edge. Groove is dark then light;
                        // ridge is the reverse. Inset and outset are rewritten first, so
                        // double's two stripes stay the same colour either way.
                        layout::BorderLineStyle painted = line;
                        if (line == layout::BorderLineStyle::Inset) {
                            painted = layout::BorderLineStyle::Ridge;
                        } else if (line == layout::BorderLineStyle::Outset) {
                            painted = layout::BorderLineStyle::Groove;
                        }
                        paint_border_edge(out, box, false, Rect{rx, content.y, rule_w, rule_h}, rule_c, painted,
                                   border_dark_outer_for(painted, true), true);
                    }
                }
            }
        }
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
