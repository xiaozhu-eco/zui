#ifndef ZMD_SETTINGS_H
#define ZMD_SETTINGS_H

#include <string>
#include <string_view>

#include <zui/app.h>

namespace zmd {

/// Everything the settings sheet edits. View, type, and theme become classes the
/// shared sheet already selects. Page width, type size, line height, accent, and
/// corner radius are discrete class steps (`w660`, `sz16`, `lh172`, `acc-4a6cf7`,
/// `rad12`) because the app cannot write a custom property. The file under the host
/// user-data directory is plain text.
class Settings {
public:
    enum class Theme { Light, Dark, System };
    enum class View { Live, Source, Split, Preview };
    enum class Font { Serif, Sans };

    explicit Settings(zui::Document& page);

    void apply();
    void sync_controls();
    void show_pane(std::string_view pane);

    void set_theme(Theme theme);
    void set_view(View view);
    void set_font_px(int px);
    void set_font(Font font);
    void set_focus(bool on);
    void set_typewriter(bool on);
    void set_measure(int px);
    void set_line_height(int percent);
    /// Snaps the picked colour to the nearest entry of the accent palette.
    void set_accent(std::string hex);
    /// Snaps to a 4px step of `--w-radius`.
    void set_radius(int px);
    void set_workspace_path(std::string path);
    void set_assets(std::string dir);
    void set_autosave(bool on);
    void set_new_in_root(bool on);

    /// `key=value` lines. Unknown keys are ignored. Empty text is the defaults.
    void restore(std::string_view text);
    std::string serialize() const;

    Theme theme() const { return theme_; }
    View view() const { return view_; }
    Font font() const { return font_; }
    int font_px() const { return font_px_; }
    bool focus() const { return focus_; }
    bool typewriter() const { return typewriter_; }
    int measure() const { return measure_; }
    int line_height() const { return line_height_; }
    const std::string& accent() const { return accent_; }
    int radius() const { return radius_; }
    const std::string& workspace_path() const { return workspace_; }
    const std::string& assets() const { return assets_; }
    bool autosave() const { return autosave_; }
    bool new_in_root() const { return new_in_root_; }

private:
    std::string_view view_class() const;
    void paint_steps();

    zui::Document& page_;
    Theme theme_ = Theme::System;
    View view_ = View::Live;
    Font font_ = Font::Sans;
    int font_px_ = 16;
    bool focus_ = false;
    bool typewriter_ = false;
    int measure_ = 660;
    int line_height_ = 172;
    std::string accent_{"#4a6cf7"};
    int radius_ = 12;
    std::string workspace_;
    std::string assets_{"assets/"};
    bool autosave_ = true;
    // Factory 新建位置 is the folder the file panel is showing. 工作目录根 is an
    // explicit choice that still sends new files and folders to the workspace root.
    bool new_in_root_ = false;
    std::string pane_{"type"};
};

}  // namespace zmd

#endif
