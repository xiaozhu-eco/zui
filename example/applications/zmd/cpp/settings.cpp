#include "settings.h"

#include <cstdlib>
#include <string>

#include "dom.h"

namespace zmd {
namespace {

constexpr std::string_view kViews[] = {dom::Class::view_write, dom::Class::view_source,
                                       dom::Class::view_split, dom::Class::view_preview};

constexpr std::string_view kPanes[] = {"type", "look", "file", "export", "keys"};

/// The accent swatch's palette. `--w-accent` is a custom property, so the app picks the nearest
/// step and writes the hex back to the control; the class is named after that hex (`acc-4a6cf7`).
constexpr std::string_view kAccents[] = {
    "#e5484d", "#e0672b", "#d98200", "#a3a300", "#12a150", "#0d9bb5",
    "#3f78d8", "#4a6cf7", "#7c4dff", "#a855f7", "#e0398c", "#55606e",
};

/// One channel of a `#rrggbb` literal, or -1 when `hex` is not one.
int hex_channel(std::string_view hex, std::size_t at) {
    if (hex.size() != 7 || at + 2 > hex.size()) {
        return -1;
    }
    int value = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        const char c = hex[at + i];
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

/// `#rrggbb` → the palette entry closest to it. An unparsable value keeps @p fallback.
std::string nearest_accent(std::string_view hex, const std::string& fallback) {
    const int r = hex_channel(hex, 1);
    const int g = hex_channel(hex, 3);
    const int b = hex_channel(hex, 5);
    if (r < 0 || g < 0 || b < 0) {
        return fallback;
    }
    std::string best = fallback;
    long best_distance = -1;
    for (std::string_view candidate : kAccents) {
        const long dr = r - hex_channel(candidate, 1);
        const long dg = g - hex_channel(candidate, 3);
        const long db = b - hex_channel(candidate, 5);
        const long distance = dr * dr + dg * dg + db * db;
        if (best_distance < 0 || distance < best_distance) {
            best_distance = distance;
            best = std::string(candidate);
        }
    }
    return best;
}

template <typename T, std::size_t N>
void swap_class(zui::Document& page, std::string_view id, const T (&all)[N],
                std::string_view keep) {
    for (std::string_view name : all) {
        if (name == keep) {
            page.add_class(id, name);
        } else {
            page.remove_class(id, name);
        }
    }
}

int clamp_int(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

/// Snap @p value to a multiple of @p step inside [lo, hi].
int snap(int value, int lo, int hi, int step) {
    value = clamp_int(value, lo, hi);
    const int units = (value - lo + step / 2) / step;
    return clamp_int(lo + units * step, lo, hi);
}

void drop_prefix(zui::Document& page, std::string_view id, std::string_view prefix, int lo,
                 int hi, int step) {
    for (int value = lo; value <= hi; value += step) {
        page.remove_class(id, std::string(prefix) + std::to_string(value));
    }
}

std::string field(std::string_view text, std::string_view key) {
    const std::string needle = std::string(key) + "=";
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() >= needle.size() && line.substr(0, needle.size()) == needle) {
            return std::string(line.substr(needle.size()));
        }
        at = end + 1;
    }
    return {};
}

void mark_pressed(zui::Document& page, std::string_view id, bool on) {
    if (on) {
        page.add_class(id, "is-active");
    } else {
        page.remove_class(id, "is-active");
    }
    page.set(id, "aria-pressed", on ? "true" : "false");
}

}  // namespace

Settings::Settings(zui::Document& page) : page_(page) {}

std::string_view Settings::view_class() const {
    return kViews[static_cast<int>(view_)];
}

void Settings::paint_steps() {
    drop_prefix(page_, dom::Id::window, "sz", 14, 18, 1);
    drop_prefix(page_, dom::Id::window, "w", 520, 760, 10);
    drop_prefix(page_, dom::Id::window, "lh", 140, 220, 2);
    drop_prefix(page_, dom::Id::window, "rad", 0, 24, 4);
    for (std::string_view hex : kAccents) {
        page_.remove_class(dom::Id::window, "acc-" + std::string(hex.substr(1)));
    }
    page_.add_class(dom::Id::window, "sz" + std::to_string(font_px_));
    page_.add_class(dom::Id::window, "w" + std::to_string(measure_));
    page_.add_class(dom::Id::window, "lh" + std::to_string(line_height_));
    page_.add_class(dom::Id::window, "rad" + std::to_string(radius_));
    page_.add_class(dom::Id::window, "acc-" + accent_.substr(1));

    page_.remove_class(dom::Id::window, dom::Class::theme_light);
    page_.remove_class(dom::Id::window, dom::Class::theme_dark);
    page_.remove_class(dom::Id::window, dom::Class::theme_system);
    if (theme_ == Theme::Light) {
        page_.add_class(dom::Id::window, dom::Class::theme_light);
    } else if (theme_ == Theme::Dark) {
        page_.add_class(dom::Id::window, dom::Class::theme_dark);
    } else {
        page_.add_class(dom::Id::window, dom::Class::theme_system);
    }
}

void Settings::apply() {
    swap_class(page_, dom::Id::root, kViews, view_class());
    if (focus_) {
        page_.add_class(dom::Id::window, dom::Class::is_focusing);
    } else {
        page_.remove_class(dom::Id::window, dom::Class::is_focusing);
    }
    if (font_ == Font::Serif) {
        page_.add_class(dom::Id::window, dom::Class::serif);
    } else {
        page_.remove_class(dom::Id::window, dom::Class::serif);
    }
    paint_steps();
    sync_controls();
}

void Settings::sync_controls() {
    page_.set(dom::Id::range_size, "value", std::to_string(font_px_));
    page_.set(dom::Id::range_width, "value", std::to_string(measure_));
    const int whole = line_height_ / 100;
    const int frac = line_height_ % 100;
    std::string line = std::to_string(whole) + ".";
    if (frac < 10) {
        line.push_back('0');
    }
    line += std::to_string(frac);
    page_.set(dom::Id::range_line, "value", line);

    page_.set(dom::Id::size_val, "text", std::to_string(font_px_) + " px");
    page_.set(dom::Id::width_val, "text", std::to_string(measure_) + " px");
    page_.set(dom::Id::line_val, "text", line);

    // The swatch and the slider show the step the sheet applied, not the raw pick.
    page_.set(dom::Id::accent, "value", accent_);
    page_.set(dom::Id::range_radius, "value", std::to_string(radius_));
    page_.set(dom::Id::radius_val, "text", std::to_string(radius_) + " px");

    page_.set(dom::Id::sw_autosave, "checked", autosave_ ? "1" : "0");
    page_.set(dom::Id::sw_focus, "checked", focus_ ? "1" : "0");
    page_.set(dom::Id::ws_path, "value", workspace_);
    page_.set(dom::Id::assets_dir, "value", assets_);
    page_.set(dom::Id::window, "data-assets", assets_);

    mark_pressed(page_, dom::Id::font_sans, font_ == Font::Sans);
    mark_pressed(page_, dom::Id::font_serif, font_ == Font::Serif);
    mark_pressed(page_, dom::Id::theme_light, theme_ == Theme::Light);
    mark_pressed(page_, dom::Id::theme_dark, theme_ == Theme::Dark);
    mark_pressed(page_, dom::Id::theme_system, theme_ == Theme::System);
    mark_pressed(page_, dom::Id::new_root, new_in_root_);
    mark_pressed(page_, dom::Id::new_here, !new_in_root_);
    page_.set(dom::Id::focus_btn, "aria-pressed", focus_ ? "true" : "false");
}

void Settings::show_pane(std::string_view pane) {
    pane_ = std::string(pane);
    for (std::string_view name : kPanes) {
        const bool on = name == pane;
        const std::string tab = std::string("zmd-tab-") + std::string(name);
        const std::string body = std::string("zmd-pane-") + std::string(name);
        if (on) {
            page_.add_class(tab, "is-active");
            page_.add_class(body, "is-active");
        } else {
            page_.remove_class(tab, "is-active");
            page_.remove_class(body, "is-active");
        }
    }
}

void Settings::set_theme(Theme theme) {
    theme_ = theme;
    apply();
}

void Settings::set_view(View view) {
    view_ = view;
    apply();
}

void Settings::set_font_px(int px) {
    font_px_ = snap(px, 14, 18, 1);
    apply();
}

void Settings::set_font(Font font) {
    font_ = font;
    apply();
}

void Settings::set_focus(bool on) {
    focus_ = on;
    apply();
}

void Settings::set_typewriter(bool on) {
    typewriter_ = on;
    apply();
}

void Settings::set_measure(int px) {
    measure_ = snap(px, 520, 760, 10);
    apply();
}

void Settings::set_line_height(int percent) {
    line_height_ = snap(percent, 140, 220, 2);
    apply();
}

void Settings::set_accent(std::string hex) {
    accent_ = nearest_accent(hex, accent_);
    apply();
}

void Settings::set_radius(int px) {
    radius_ = snap(px, 0, 24, 4);
    apply();
}

void Settings::set_workspace_path(std::string path) {
    workspace_ = std::move(path);
    sync_controls();
}

void Settings::set_assets(std::string dir) {
    assets_ = std::move(dir);
    sync_controls();
}

void Settings::set_autosave(bool on) {
    autosave_ = on;
    sync_controls();
}

void Settings::set_new_in_root(bool on) {
    new_in_root_ = on;
    sync_controls();
}

void Settings::restore(std::string_view text) {
    if (text.empty()) {
        return;
    }
    const std::string font = field(text, "font");
    if (font == "serif") {
        font_ = Font::Serif;
    } else if (font == "sans") {
        font_ = Font::Sans;
    }
    const std::string size = field(text, "size");
    if (!size.empty()) {
        font_px_ = snap(std::atoi(size.c_str()), 14, 18, 1);
    }
    const std::string width = field(text, "width");
    if (!width.empty()) {
        measure_ = snap(std::atoi(width.c_str()), 520, 760, 10);
    }
    const std::string line = field(text, "line");
    if (!line.empty()) {
        line_height_ = snap(std::atoi(line.c_str()), 140, 220, 2);
    }
    const std::string radius = field(text, "radius");
    if (!radius.empty()) {
        radius_ = snap(std::atoi(radius.c_str()), 0, 24, 4);
    }
    const std::string accent = field(text, "accent");
    if (!accent.empty()) {
        accent_ = nearest_accent(accent, accent_);
    }
    const std::string typewriter = field(text, "typewriter");
    if (!typewriter.empty()) {
        typewriter_ = typewriter == "1";
    }
    const std::string focus = field(text, "focus");
    if (!focus.empty()) {
        focus_ = focus == "1";
    }
    const std::string theme = field(text, "theme");
    if (theme == "light") {
        theme_ = Theme::Light;
    } else if (theme == "dark") {
        theme_ = Theme::Dark;
    } else if (theme == "system") {
        theme_ = Theme::System;
    }
    const std::string autosave = field(text, "autosave");
    if (!autosave.empty()) {
        autosave_ = autosave == "1";
    }
    const std::string root = field(text, "new_in_root");
    if (!root.empty()) {
        new_in_root_ = root == "1";
    }
    const std::string assets = field(text, "assets");
    if (!assets.empty()) {
        assets_ = assets;
    }
}

std::string Settings::serialize() const {
    std::string out;
    out += "font=";
    out += font_ == Font::Serif ? "serif" : "sans";
    out += "\nsize=" + std::to_string(font_px_);
    out += "\nwidth=" + std::to_string(measure_);
    out += "\nline=" + std::to_string(line_height_);
    out += "\nradius=" + std::to_string(radius_);
    out += "\naccent=" + accent_;
    out += focus_ ? "\nfocus=1" : "\nfocus=0";
    out += "\ntheme=";
    out += theme_ == Theme::Light ? "light" : (theme_ == Theme::Dark ? "dark" : "system");
    out += autosave_ ? "\nautosave=1" : "\nautosave=0";
    out += new_in_root_ ? "\nnew_in_root=1" : "\nnew_in_root=0";
    out += typewriter_ ? "\ntypewriter=1" : "\ntypewriter=0";
    out += "\nassets=" + assets_ + "\n";
    return out;
}

}  // namespace zmd
