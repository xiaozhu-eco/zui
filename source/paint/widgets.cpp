#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include <algorithm>
#include <string>

namespace zui {
namespace paint {

using namespace painter_detail;

void Painter::paint_native_widgets(const layout::Box& box, DisplayList& out, float dx, float dy,
                                   float op, bool paint_self) const {
    if (paint_self && box.node != nullptr &&
        (html::is_checkbox(box.node) || html::is_radio(box.node))) {
        // CSS UI 4: `appearance: none` keeps author background/border only; `auto` (initial)
        // paints the native widget chrome on top.
        const bool radio = html::is_radio(box.node);
        const std::string* appearance = computed(box, "appearance");
        const bool native = appearance == nullptr || *appearance == "auto" ||
                            *appearance == "initial" || (!radio && *appearance == "checkbox") ||
                            (radio && *appearance == "radio");
        if (native) {
            const Rect r = border_rect(box, dx, dy);
            const bool on = box.node->checked;
            if (radio) {
                const float rad = std::min(r.w, r.h) * 0.5f;
                const Color face = on ? Color{47, 111, 235, 255} : Color{255, 255, 255, 255};
                const Color edge = on ? Color{47, 111, 235, 255} : Color{138, 148, 166, 255};
                out.fill_rect(r, apply_opacity(face, op), rad);
                out.stroke_rect(r, apply_opacity(edge, op), 1.f, rad);
                if (on && r.w > 4.f && r.h > 4.f) {
                    const float dot = std::min(r.w, r.h) * 0.4f;
                    const Rect inner{r.x + (r.w - dot) * 0.5f, r.y + (r.h - dot) * 0.5f, dot, dot};
                    out.fill_rect(inner, apply_opacity(Color{255, 255, 255, 255}, op), dot * 0.5f);
                }
            } else {
                const Color face = on ? Color{47, 111, 235, 255} : Color{255, 255, 255, 255};
                const Color edge = on ? Color{47, 111, 235, 255} : Color{138, 148, 166, 255};
                const float box_r = std::min(3.f, std::min(r.w, r.h) * 0.2f);
                out.fill_rect(r, apply_opacity(face, op), box_r);
                out.stroke_rect(r, apply_opacity(edge, op), 1.f, box_r);
                if (on && r.w > 2.f && r.h > 2.f) {
                    const Color tick = apply_opacity(Color{255, 255, 255, 255}, op);
                    const float thick = std::max(1.5f, std::min(r.w, r.h) * 0.14f);
                    auto stroke = [&](float x0, float y0, float x1, float y1) {
                        const int steps = 7;
                        for (int i = 0; i <= steps; ++i) {
                            const float u = static_cast<float>(i) / static_cast<float>(steps);
                            const float x = r.x + (x0 + (x1 - x0) * u) * r.w - thick * 0.5f;
                            const float y = r.y + (y0 + (y1 - y0) * u) * r.h - thick * 0.5f;
                            out.fill_rect(Rect{x, y, thick, thick}, tick, thick * 0.5f);
                        }
                    };
                    stroke(0.22f, 0.52f, 0.42f, 0.76f);
                    stroke(0.42f, 0.76f, 0.80f, 0.28f);
                }
            }
        }
    }

    // The menulist button keeps its chevron while the option list is open. The list is a
    // separate fixed popup, not a replacement for the control.
    if (paint_self && box.node != nullptr && html::is_select(box.node)) {
        if (appearance_native(box, "menulist")) {
            const Rect r = border_rect(box, dx, dy);
            const float gutter = std::min(18.f, std::max(10.f, box.geo.padding.right));
            const float cx = r.x + r.w - box.geo.border.right - gutter * 0.5f;
            const float cy = r.y + r.h * 0.5f;
            const float arm = std::min(5.f, gutter * 0.35f);
            const float thick = 1.5f;
            const Color tip = apply_opacity(Color{60, 60, 60, 255}, op);
            auto stroke = [&](float x0, float y0, float x1, float y1) {
                const int steps = 5;
                for (int i = 0; i <= steps; ++i) {
                    const float u = static_cast<float>(i) / static_cast<float>(steps);
                    const float x = x0 + (x1 - x0) * u - thick * 0.5f;
                    const float y = y0 + (y1 - y0) * u - thick * 0.5f;
                    out.fill_rect(Rect{x, y, thick, thick}, tip, thick * 0.5f);
                }
            };
            stroke(cx - arm, cy - arm * 0.35f, cx, cy + arm * 0.45f);
            stroke(cx, cy + arm * 0.45f, cx + arm, cy - arm * 0.35f);
        }
    }

    // input[type=range]: track + thumb. appearance:none keeps author background/border only.
    if (paint_self && box.node != nullptr && html::is_range(box.node) &&
        appearance_native(box, "slider-horizontal")) {
        const Rect r = border_rect(box, dx, dy);
        if (r.w > 2.f && r.h > 2.f) {
            const float min_v = attr_float(box.node, "min", 0.f);
            float max_v = attr_float(box.node, "max", 100.f);
            if (max_v < min_v) {
                max_v = min_v;
            }
            const float mid = min_v + (max_v - min_v) * 0.5f;
            float cur = node_float_value(box.node, mid);
            cur = std::clamp(cur, min_v, max_v);
            const float t = (max_v > min_v) ? (cur - min_v) / (max_v - min_v) : 0.f;

            const float track_h = std::max(3.f, std::min(6.f, r.h * 0.28f));
            const float track_y = r.y + (r.h - track_h) * 0.5f;
            const float thumb = std::min(r.h, std::max(10.f, r.h * 0.85f));
            const float travel = std::max(0.f, r.w - thumb);
            const float thumb_x = r.x + travel * t;
            const float thumb_y = r.y + (r.h - thumb) * 0.5f;

            const Color track_c = apply_opacity(Color{196, 196, 196, 255}, op);
            const Color thumb_face = apply_opacity(Color{47, 111, 235, 255}, op);
            const Color thumb_edge = apply_opacity(Color{30, 80, 190, 255}, op);
            out.fill_rect(Rect{r.x, track_y, r.w, track_h}, track_c, track_h * 0.5f);
            const Rect thumb_r{thumb_x, thumb_y, thumb, thumb};
            out.fill_rect(thumb_r, thumb_face, thumb * 0.5f);
            out.stroke_rect(thumb_r, thumb_edge, 1.f, thumb * 0.5f);
        }
    }

    // <progress>: determinate fill from value/max. appearance:none suppresses the fill chrome.
    if (paint_self && box.node != nullptr && html::is_progress(box.node) &&
        appearance_native(box, "progress-bar")) {
        const Rect r = border_rect(box, dx, dy);
        if (r.w > 2.f && r.h > 2.f) {
            float max_v = attr_float(box.node, "max", 1.f);
            if (max_v <= 0.f) {
                max_v = 1.f;
            }
            // Missing value → indeterminate: leave the UA background, no fill.
            if (!box.node->value.empty() ||
                box.node->attrs.find("value") != box.node->attrs.end()) {
                float cur = node_float_value(box.node, 0.f);
                if (box.node->value.empty()) {
                    cur = attr_float(box.node, "value", 0.f);
                }
                cur = std::clamp(cur, 0.f, max_v);
                const float t = cur / max_v;
                const float inset = std::min(1.f, std::min(r.w, r.h) * 0.08f);
                const Rect inner{r.x + inset, r.y + inset, std::max(0.f, r.w - inset * 2.f),
                                 std::max(0.f, r.h - inset * 2.f)};
                const float fill_w = inner.w * t;
                if (fill_w > 0.5f) {
                    out.fill_rect(Rect{inner.x, inner.y, fill_w, inner.h},
                                  apply_opacity(Color{47, 111, 235, 255}, op), 1.f);
                }
            }
        }
    }

    // <meter>: the same scalar fraction as progress, from min/max/value (defaults 0/1/0).
    if (paint_self && box.node != nullptr && box.node->tag == "meter" &&
        appearance_native(box, "meter")) {
        const Rect r = border_rect(box, dx, dy);
        if (r.w > 2.f && r.h > 2.f) {
            float min_v = attr_float(box.node, "min", 0.f);
            float max_v = attr_float(box.node, "max", 1.f);
            if (max_v <= min_v) {
                max_v = min_v + 1.f;
            }
            float cur = node_float_value(box.node, min_v);
            if (box.node->value.empty()) {
                cur = attr_float(box.node, "value", min_v);
            }
            cur = std::clamp(cur, min_v, max_v);
            const float t = (cur - min_v) / (max_v - min_v);
            const float inset = std::min(1.f, std::min(r.w, r.h) * 0.08f);
            const Rect inner{r.x + inset, r.y + inset, std::max(0.f, r.w - inset * 2.f),
                             std::max(0.f, r.h - inset * 2.f)};
            const float fill_w = inner.w * t;
            if (fill_w > 0.5f) {
                out.fill_rect(Rect{inner.x, inner.y, fill_w, inner.h},
                              apply_opacity(Color{47, 111, 235, 255}, op), 1.f);
            }
        }
    }

    // input[type=file]: button slot + filename (or "No file chosen"). appearance:none suppresses.
    if (paint_self && box.node != nullptr && html::is_file(box.node) &&
        appearance_native(box, "auto")) {
        const Rect r = border_rect(box, dx, dy);
        if (r.w > 8.f && r.h > 4.f) {
            const float pad = 2.f;
            const float btn_w = std::min(r.w * 0.42f, std::max(72.f, r.h * 4.f));
            const Rect btn{r.x + pad, r.y + pad, std::max(0.f, btn_w - pad),
                           std::max(0.f, r.h - pad * 2.f)};
            const Color face = apply_opacity(Color{240, 240, 240, 255}, op);
            const Color edge = apply_opacity(Color{118, 118, 118, 255}, op);
            const Color label = apply_opacity(Color{32, 32, 32, 255}, op);
            const Color muted = apply_opacity(Color{110, 110, 110, 255}, op);
            out.fill_rect(btn, face, 2.f);
            out.stroke_rect(btn, edge, 1.f, 2.f);

            const float font_px = std::max(11.f, std::min(box.style().font_size, btn.h * 0.7f));
            const std::string& family = box.style().font_family;
            const int weight = box.style().font_weight > 0 ? box.style().font_weight : 400;
            // Approximate centre: advance of "Choose File" ~ 0.55em * 11 chars.
            const float btn_label_w = font_px * 0.52f * 11.f;
            const float btn_tx = btn.x + std::max(2.f, (btn.w - btn_label_w) * 0.5f);
            const float btn_ty = btn.y + btn.h * 0.72f;
            out.draw_text(btn_tx, btn_ty, label, "Choose File", font_px, family, weight);

            const float name_x = btn.x + btn.w + 6.f;
            const float name_y = r.y + r.h * 0.72f;
            const std::string& shown =
                box.node->value.empty() ? std::string("No file chosen") : box.node->value;
            out.draw_text(name_x, name_y, box.node->value.empty() ? muted : label, shown, font_px,
                          family, weight);
        }
    }
}

}  // namespace paint
}  // namespace zui
