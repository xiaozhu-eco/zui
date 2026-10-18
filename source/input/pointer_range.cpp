#include "input/detail.h"
#include "input/interaction.h"

#include "layout/paint_transform.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "layout/viewport.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace {

html::Node* range_control(html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (!html::is_range(cursor)) {
            continue;
        }
        return cursor->disabled ? nullptr : cursor;
    }
    return nullptr;
}

float attr_number(const html::Node* node, const char* key, float fallback) {
    if (node == nullptr || key == nullptr) {
        return fallback;
    }
    const auto it = node->attrs.find(key);
    if (it == node->attrs.end() || it->second.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str()) {
        return fallback;
    }
    return v;
}

std::string format_scalar(float value) {
    const float nearest = std::round(value);
    if (std::fabs(value - nearest) < 1e-3f && std::fabs(nearest) < 1.0e7f) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
}

html::Node* color_control(html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (!html::input_type_is(cursor, "color")) {
            continue;
        }
        return cursor->disabled ? nullptr : cursor;
    }
    return nullptr;
}

/// Hue 0..360 across the control, full saturation. Left is red, the middle is cyan.
std::string color_hex_at(float t) {
    t = std::clamp(t, 0.f, 0.999f);
    const float h = t * 360.f;
    const float sector = h / 60.f;
    const int band = static_cast<int>(sector) % 6;
    const float frac = sector - std::floor(sector);
    const float q = 1.f - frac;
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    switch (band) {
    case 0: r = 1.f; g = frac; b = 0.f; break;
    case 1: r = q; g = 1.f; b = 0.f; break;
    case 2: r = 0.f; g = 1.f; b = frac; break;
    case 3: r = 0.f; g = q; b = 1.f; break;
    case 4: r = frac; g = 0.f; b = 1.f; break;
    default: r = 1.f; g = 0.f; b = q; break;
    }
    const auto byte = [](float channel) {
        return std::clamp(static_cast<int>(std::lround(channel * 255.f)), 0, 255);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", byte(r), byte(g), byte(b));
    return buf;
}

}  // namespace

bool Interaction::apply_range_value(html::Node* range, float x) {
    if (range == nullptr || host_.layout == nullptr) {
        return false;
    }
    layout::Box* box = host_.layout->principal_box(range);
    if (box == nullptr || box->geo.width <= 0.f) {
        return false;
    }
    float min_v = attr_number(range, "min", 0.f);
    float max_v = attr_number(range, "max", 100.f);
    if (max_v < min_v) {
        max_v = min_v;
    }
    const float span = max_v - min_v;
    float t = (x - box->geo.world_x) / box->geo.width;
    t = std::clamp(t, 0.f, 1.f);
    float value = min_v + t * span;
    const auto step_attr = range->attrs.find("step");
    const bool any_step = step_attr != range->attrs.end() && html::iequals(step_attr->second, "any");
    if (!any_step) {
        float step = attr_number(range, "step", 1.f);
        if (!(step > 0.f)) {
            step = 1.f;
        }
        if (step > 0.f && span > 0.f) {
            const float steps = std::round((value - min_v) / step);
            value = min_v + steps * step;
        }
    }
    value = std::clamp(value, min_v, max_v);
    const std::string text = format_scalar(value);
    if (text == range->value) {
        return false;
    }
    range->value = text;
    range->text = text;
    range->attrs["value"] = text;
    html::note_paint_change_at(range);
    return true;
}

bool Interaction::apply_color_value(html::Node* color, float x) {
    if (color == nullptr || host_.layout == nullptr) {
        return false;
    }
    layout::Box* box = host_.layout->principal_box(color);
    if (box == nullptr || box->geo.width <= 0.f) {
        return false;
    }
    float t = (x - box->geo.world_x) / box->geo.width;
    const std::string text = color_hex_at(t);
    if (text == color->value) {
        return false;
    }
    color->value = text;
    color->text = text;
    color->attrs["value"] = text;
    // A color input paints its hex as a text run, not a widget fill. Sync the run the way
    // App::set does, or the laid-out glyphs stay on the previous value.
    host_.layout->sync_text(color);
    host_.layout->mark_needs_layout(color);
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
    return true;
}

void Interaction::press_range_or_color(PressState& state, html::Node* click_node, float x,
                                       bool down_prevented) {
    if (down_prevented) {
        return;
    }
        if (html::Node* range = range_control(click_node)) {
            state.range = range;
            state.range_value0 = range->value;
            if (apply_range_value(range, x)) {
                fire(range, "input");
            }
        } else if (html::Node* color = color_control(click_node)) {
            state.color = color;
            state.color_value0 = color->value;
            if (apply_color_value(color, x)) {
                fire(color, "input");
            }
        }
}

}  // namespace input
}  // namespace zui
