#ifndef ZMD_DOM_H
#define ZMD_DOM_H

#include <string_view>

namespace zmd::dom {

/// The ids the shipped page really publishes. The page is the prototype's markup
/// copied verbatim, so the prototype's `mdc-a-*` ids are the live ones. Nothing
/// here is invented: `Document::set` on an unknown id is a silent no-op, so a
/// wrong name would look like working code that paints nothing.
///
/// zmd adds its own `zmd-*` ids on the same screen for the controls the prototype
/// script only addressed by `data-set` (the settings controls, the find bar, the
/// dialogs). List rows are `<template>` hosts: the model stamps `fl{{index}}`,
/// `hd{{index}}`, and `cm{{index}}`.
struct Id {
    /// The prototype's screen element. It is also where the view classes
    /// (`view-live` / `view-source` / `view-split` / `view-preview`) live:
    /// js/zmd.js toggles them here and css/zmd.css selects `#md-editor-mock.view-*`.
    static constexpr std::string_view root = "md-editor-mock";
    /// The window element. It carries the prototype's `mdw mdw-a` classes and is
    /// where every state class below is written.
    static constexpr std::string_view window = "mdc-a";
    static constexpr std::string_view app = window;

    // the prototype's screen
    static constexpr std::string_view title = "mdc-a-name";
    /// The save dot. Its state is the `data-state` attribute (`saved` / `typing` /
    /// `error`), matched by `[data-state=…]` in the sheet, not a class.
    static constexpr std::string_view save = "mdc-a-save";
    static constexpr std::string_view counts = "mdc-a-stats";
    /// The seamless writing surface: a `contenteditable` article whose content the
    /// renderer owns. zmd cannot write it (no subtree authoring); see README.md.
    static constexpr std::string_view live = "mdc-a-doc";
    static constexpr std::string_view outline = "mdc-a-outline";
    static constexpr std::string_view files = "mdc-a-files";
    static constexpr std::string_view scroll = "mdc-a-scroll";
    static constexpr std::string_view status = "mdc-a-status";
    static constexpr std::string_view sheet = "mdc-a-set";
    static constexpr std::string_view pal_input = "mdc-a-pal-input";
    static constexpr std::string_view pal_list = "mdc-a-pal-list";
    static constexpr std::string_view pal_empty = "zmd-pal-empty";

    // zmd's own layer, written into the same screen
    /// The Markdown source. It is the app's writing surface: a textarea is the one
    /// element the A-tier surface can read a caret and a selection from.
    static constexpr std::string_view source = "mdc-a-source";
    static constexpr std::string_view preview = "mdc-a-preview";
    /// Format strip for a writing-view selection. A sibling of the paper, not a child of the
    /// selected run: the paper's HTML stays the markdown projection.
    static constexpr std::string_view sel_host = "zmd-sel-host";
    static constexpr std::string_view find = "zmd-find";
    static constexpr std::string_view find_input = "zmd-find-q";
    static constexpr std::string_view find_count = "zmd-find-count";
    static constexpr std::string_view dialog_unsaved = "zmd-dlg-unsaved";
    static constexpr std::string_view dialog_link = "zmd-dlg-link";
    static constexpr std::string_view dialog_image = "zmd-dlg-image";
    static constexpr std::string_view link_text = "zmd-link-text";
    static constexpr std::string_view link_url = "zmd-link-url";
    static constexpr std::string_view image_alt = "zmd-image-alt";
    static constexpr std::string_view image_url = "zmd-image-url";
    static constexpr std::string_view open_file = "zmd-open-file";
    static constexpr std::string_view image_file = "zmd-image-file";

    // The settings controls. The prototype script addressed them through `data-set`
    // alone; zmd's own ids sit on the same controls, next to the `data-set` the
    // browser script still reads.
    static constexpr std::string_view range_size = "zmd-size";
    static constexpr std::string_view range_width = "zmd-width";
    static constexpr std::string_view range_line = "zmd-line";
    static constexpr std::string_view size_val = "zmd-size-val";
    static constexpr std::string_view width_val = "zmd-width-val";
    static constexpr std::string_view line_val = "zmd-line-val";
    static constexpr std::string_view sw_autosave = "zmd-autosave";
    static constexpr std::string_view sw_focus = "zmd-focus-sw";
    static constexpr std::string_view focus_btn = "zmd-focus";
    static constexpr std::string_view ws_path = "zmd-ws";
    static constexpr std::string_view assets_dir = "zmd-assets";
    static constexpr std::string_view font_sans = "zmd-font-sans";
    static constexpr std::string_view font_serif = "zmd-font-serif";
    static constexpr std::string_view theme_light = "zmd-theme-light";
    static constexpr std::string_view theme_dark = "zmd-theme-dark";
    static constexpr std::string_view theme_system = "zmd-theme-system";
    static constexpr std::string_view new_root = "zmd-new-root";
    static constexpr std::string_view new_here = "zmd-new-here";
    static constexpr std::string_view scrim = "zmd-scrim";
    /// The file panel's title (the folder it shows) and its 返回上一级 button.
    static constexpr std::string_view folder_title = "zmd-folder-title";
    static constexpr std::string_view folder_up = "zmd-folder-up";
    /// The outline panel's reading-progress bar. The sheet's `.mdw-outline-bar` gets the
    /// `prog0` … `prog100` classes; its height reads the `--w-progress` step.
    static constexpr std::string_view outline_bar = "zmd-outline-bar";
    /// The look pane's accent swatch (`input[type=color]`) and corner radius slider.
    static constexpr std::string_view accent = "zmd-accent";
    static constexpr std::string_view range_radius = "zmd-radius";
    static constexpr std::string_view radius_val = "zmd-radius-val";
};

/// Class tokens the sheet (css/app.css) really matches. Every entry is a selector
/// fragment in app.css; a token that is not there would be an invisible write.
struct Class {
    /// `#md-editor-mock .mdw-a.is-focusing .mdw-doc > :not(.mdw-cur)`
    static constexpr std::string_view is_focusing = "is-focusing";
    /// `#md-editor-mock .mdw-serif .mdw-doc`
    static constexpr std::string_view serif = "mdw-serif";
    /// `#md-editor-mock .mdw-a.is-typing …` (the typing veil)
    static constexpr std::string_view is_typing = "is-typing";
    /// `#md-editor-mock .mdw-a.is-blurred …`
    static constexpr std::string_view is_blurred = "is-blurred";
    /// `#md-editor-mock .mdw-a.set-closed .mdw-set`
    static constexpr std::string_view set_closed = "set-closed";
    /// `#md-editor-mock .mdw-a.pal-closed .mdw-pal`
    static constexpr std::string_view pal_closed = "pal-closed";
    /// `#md-editor-mock .mdw-a.pin-l .mdw-float-l`
    static constexpr std::string_view pin_left = "pin-l";
    /// `#md-editor-mock .mdw-a.pin-r .mdw-float-r`
    static constexpr std::string_view pin_right = "pin-r";
    static constexpr std::string_view theme_light = "theme-light";
    static constexpr std::string_view theme_dark = "theme-dark";
    static constexpr std::string_view theme_system = "theme-system";
    /// Native host. css/zmd.css shows the rendered preview in 写作 when this is set.
    static constexpr std::string_view host = "zmd-host";

    // The view classes go on Id::root, not on the window.
    static constexpr std::string_view view_write = "view-live";
    static constexpr std::string_view view_source = "view-source";
    static constexpr std::string_view view_split = "view-split";
    static constexpr std::string_view view_preview = "view-preview";
};

/// The `data-state` values the save dot takes.
struct SaveState {
    static constexpr std::string_view saved = "saved";
    static constexpr std::string_view typing = "typing";
    static constexpr std::string_view error = "error";
};

// File rows, outline rows, palette rows, and the settings controls are real ids
// on the page. `data-act` / `data-fmt` / `data-set` stay so the browser script
// still runs; zui binds the matching `on*` value. See README.md.

}  // namespace zmd::dom

#endif
