#ifndef ZUI_SOURCE_CSS_TRANSITION_INTERNAL_H
#define ZUI_SOURCE_CSS_TRANSITION_INTERNAL_H

// Internals shared by the transition translation units: the string helpers and tokenizers the
// parsers borrow, the easing model (`timing.cpp` / `easing.cpp`), the parsed value
// types, and the declared-but-not-defined entry points each property parser exposes.
//
//   timing.cpp        the transition shorthand and its timing list
//   color.cpp         <color> parsing and interpolation
//   transform.cpp     transform / opacity
//   value.cpp         per-property specified/used values and interpolation
//   transition.cpp    the `Transitions` object (sync / sample / lifecycle)

#include "css/transition/transition.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace css {
namespace transition_detail {

using StyleMap = ::zui::StyleMap;

std::string_view trim(std::string_view s);
std::string lower(std::string_view s);

/// `m->find(k)` / `node->computed` lookup that returns `nullptr` when the key is absent.
const std::string* map_get(const StyleMap* m, const char* k);
const std::string* node_get(const html::Node* n, const char* k);

/// Split on top-level commas / on whitespace, keeping function-call arguments together.
std::vector<std::string> split_tokens(std::string_view s);

// CSS Easing 1: cubic-bezier keywords / cubic-bezier(), or step easing (steps() / step-*).
enum class EasingKind { Bezier, Steps };
enum class StepPos { JumpStart, JumpEnd, JumpNone, JumpBoth };

struct Easing {
    EasingKind kind = EasingKind::Bezier;
    float x1 = 0.25f;
    float y1 = 0.1f;
    float x2 = 0.25f;
    float y2 = 1;
    int steps = 1;
    StepPos step_pos = StepPos::JumpEnd;
};

bool is_easing_token(std::string_view s);
Easing parse_easing(std::string_view n);
float sample_easing(float t, const Easing& e);

/// One `transition` / `transition-*` timing list (CSS Transitions 1 section 2).
struct Timing {
    std::vector<std::string> props;
    std::vector<float> durations;
    std::vector<float> delays;
    std::vector<Easing> eases;
};

Timing parse_timing(const StyleMap* m);
/// `true` when the declared transition-property list covers this longhand.
bool transition_prop_matches(std::string_view declared, std::string_view longhand);
/// Index into `t`'s per-item vectors for `name`, repeating the list per the spec rules.
int prop_index(const Timing& t, std::string_view name);
float at_or(const std::vector<float>& v, int i, float fallback);
Easing ease_at(const Timing& t, int i);

struct Rgba {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 255;
};

Rgba parse_rgba(std::string_view css);
Rgba lerp_rgba(Rgba a, Rgba b, float t);
bool rgba_eq(Rgba a, Rgba b);
/// The color from `key` on `map`, else the node's computed style, else transparent black.
Rgba color_from(const StyleMap* m, const html::Node* node, const char* key);

enum class Prop {
    Color,
    Background,
    BorderTop,
    BorderRight,
    BorderBottom,
    BorderLeft,
    Opacity,
    Transform,
    Width,
    Height,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft
};

struct Value {
    Rgba color;
    float opacity = 1;
    Xform xform;
    float length = 0;
    bool length_ok = false;       // absolute px ready to lerp / write
    bool length_percent = false;  // length holds the % number (50 for 50%)
    bool length_auto = false;     // intrinsic keyword; needs used size + interpolate-size
};

struct Record {
    html::Node* node = nullptr;
    Prop prop = Prop::Color;
    Value from;
    Value to;
    double start = 0;
    float duration = 0;
    float delay = 0;
    Easing ease;
    bool pending = false;  // % or auto endpoint still needs a layout basis
};

/// `true` for the properties whose used value is a length the layout pass consumes.
bool is_length_prop(Prop prop);
/// Parse a transition length endpoint: absolute, percentage, or `auto`.
bool parse_length_endpoint(const html::Node* node, const std::string* css, Value* out);
/// Resolve a still-`%` / `auto` endpoint against a layout basis (returns false while unknown).
bool try_resolve_length(Prop prop, Value& v, const LengthBasis& basis, bool allow_auto);
/// `interpolate-size: allow-keywords` on the node (CSS Sizing 2).
bool allow_keyword_size(const html::Node* node);

Value specified_value(const html::Node* node, const StyleMap* map, Prop prop);
void write_used(html::Node* node, Prop prop, const Value& v);
void clear_flag(html::Node* node, Prop prop);
Value lerp_value(Prop prop, const Value& a, const Value& b, float t);
bool value_eq(Prop prop, const Value& a, const Value& b);
float progress_of(const Record& r, double t);
const char* css_name(Prop p);

}  // namespace transition_detail
}  // namespace css
}  // namespace zui

#endif
