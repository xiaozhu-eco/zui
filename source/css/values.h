#ifndef ZUI_CSS_VALUES_H
#define ZUI_CSS_VALUES_H

#include <zui/host_surface.h>
#include <zui/host_types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace zui {
namespace css {

/// Resolved `<color>` bytes. No paint::Color — css must not depend on paint.
struct CssRgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/// Parses a CSS `<color>` (hex, rgb/a, hsl/a, hwb, lab/lch, oklab/oklch, color(), color-mix(),
/// CSS Color 4 named colours, transparent). Recognises system colours `Highlight` (`#0078D7`)
/// and `HighlightText` (`#FFFFFF`). `currentcolor` and unknown values return nullopt — callers
/// resolve currentcolor against the originating element. `color-mix()` interpolates in srgb,
/// srgb-linear, display-p3, lab, lch, oklab, oklch, hsl, hwb, xyz, xyz-d65, or xyz-d50.
std::optional<CssRgba> parse_css_color(std::string_view css);

/// Viewport and user-agent facts media queries and env() are resolved against.
/// Host fields (`hover`, `pointer`, `reduced_motion`, `color_scheme`, safe-area / keyboard
/// insets) come from App/backends via `HostMediaFacts` — css does not call OS APIs.
struct MediaState {
    float width = 0;
    float height = 0;
    /// Primary pointing device can conveniently hover (`hover: hover`).
    bool hover = true;
    /// At least one input can hover (`any-hover: hover`).
    bool any_hover = true;
    /// Primary pointing-device accuracy (`pointer`).
    PointerAccuracy pointer = PointerAccuracy::Fine;
    /// Most-capable pointing device (`any-pointer`).
    PointerAccuracy any_pointer = PointerAccuracy::Fine;
    /// User asked the system to minimize non-essential motion.
    bool reduced_motion = false;
    ColorSchemePreference color_scheme = ColorSchemePreference::Light;
    /// CSS-px display cutout / safe-area insets (`env(safe-area-inset-*)`).
    float safe_top = 0;
    float safe_right = 0;
    float safe_bottom = 0;
    float safe_left = 0;
    /// Virtual keyboard height in CSS px (`env(keyboard-inset-bottom)`).
    float keyboard_bottom = 0;
    /// Device pixels per CSS pixel (`resolution`). `dppx` and `x` are this value; `dpi` is
    /// 96 times it. The default matches the difftest browser, which pins deviceScaleFactor to 1.
    float resolution_dppx = 1.f;
    /// Set while a container size condition is evaluated. `resolution` is not a size feature.
    bool size_query = false;
};

/// CSS Conditional Rules and Media Queries. An empty query matches.
/// An unparsable query does not.
bool media_matches(std::string_view query, const MediaState& media);

/// CSS Conditional Rules `@supports` condition. Empty matches; unparsable does not.
/// Supports `(prop: value)`, `not`, `and`, `or`, nested parens, and `selector()`.
/// `selector()` is true when the selector parses.
bool supports_matches(std::string_view condition);

/// Split a CSS Containment 3 `@container` prelude into an optional name and the size condition.
/// Returns false when the prelude is unparsable. Empty prelude → empty name + empty condition.
bool parse_container_prelude(std::string_view prelude, std::string& name,
                             std::string_view& condition);

/// Evaluate a `@container` size condition. `width` is the container's content inline size and
/// `height` is its content block size, which is how layout publishes them. Features: those
/// axes and their min-/max- variants, `inline-size` / `block-size` (the same two axes; not
/// media features), orientation (portrait when block size >= inline size), aspect-ratio, and
/// Media Queries 4 ranges, with `not` / `and` / `or`. `resolution`, `color`, `monochrome`,
/// `scripting`, `display-mode`, `update`, `overflow-block`, `overflow-inline`, and `grid`
/// are not size features and do not match. Empty condition matches; unparsable does not.
bool container_condition_matches(std::string_view condition, float width, float height);

using CustomLookup = std::function<std::optional<std::string>(std::string_view name)>;

/// A computed `<length-percentage>` after the relative units were resolved.
struct ResolvedLength {
    bool valid = false;
    /// The value stayed a percentage; `px` then holds the percentage number, not pixels.
    bool is_percent = false;
    float px = 0;
};

/// Used-value bases for the font-relative units (CSS Values §6.1.1). Negative means "not
/// measured": `ch`/`ex` then fall back to `0.5em`, and `lh` falls back to `1em`. Callers that
/// can open the element's face should fill `ch` (advance of U+0030), `ex` (x-height) and `lh`
/// (used line-height).
struct FontRelativeUnits {
    float ch = -1.f;
    float ex = -1.f;
    float lh = -1.f;
};

/// Resolve a computed `<length-percentage>`: absolute units, `em` (against `font_size`), `rem`
/// (against `root_font_size`), `ch` / `ex` / `lh` (against @p font_rel or the fallbacks above),
/// and `calc()` / `min()` / `max()` / `clamp()` over those. A percentage has no basis here and
/// stays a percentage; a sum that mixes `%` with a length cannot be resolved without one, so it
/// comes back invalid and the caller keeps its own initial value.
ResolvedLength resolve_length(std::string_view text, float font_size, float root_font_size,
                              const FontRelativeUnits& font_rel = {});

/// The same, with the initial containing block known: `vw` / `vh` / `vmin` / `vmax` (and their
/// `d`/`s`/`l` variants, plus the logical `vi` / `vb`) resolve against it (CSS Values section 5.1).
/// A zero viewport means the caller does not know it, and viewport units are then invalid rather
/// than silently zero.
ResolvedLength resolve_length(std::string_view text, float font_size, float root_font_size,
                              float viewport_width, float viewport_height,
                              const FontRelativeUnits& font_rel = {});

/// Like `resolve_length`, but a percentage is resolved against `percent_basis` (the
/// containing block size). `calc(100% - 80px)`, `min(120px, 50%)` and `clamp` then
/// become a used length. A negative basis leaves percentages unresolved.
ResolvedLength resolve_length_basis(std::string_view text, float font_size, float root_font_size,
                                    float viewport_width, float viewport_height,
                                    float percent_basis,
                                    const FontRelativeUnits& font_rel = {});

/// A declaration the style interner kept because its math mixes `%` with lengths, and it baked the
/// relative units into px while interning (see `layout::intern_style`). Only the percentage basis
/// is left, so this is `resolve_length_basis` without the font and viewport facts.
ResolvedLength resolve_length_percent(std::string_view text, float percent_basis);

/// Substitute var() and env() at computed-value time.
/// @p media supplies known `env()` names (safe-area / keyboard / titlebar-area; defaults 0px).
/// Unknown names use the comma fallback when present; otherwise the declaration is invalid.
/// nullopt means the declaration is invalid at computed-value time.
std::optional<std::string> substitute(std::string_view value, const CustomLookup& lookup,
                                      const MediaState& media = {});

bool contains_substitution(std::string_view value);

bool is_custom_property(std::string_view name);

}  // namespace css
}  // namespace zui

#endif
