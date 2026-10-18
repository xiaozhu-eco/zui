#ifndef ZMD_EDITOR_H
#define ZMD_EDITOR_H

#include <string>
#include <string_view>

#include <zui/app.h>

namespace zmd {

class Document;
struct Heading;

/// The writing surface: caret and selection in UTF-8 bytes, the markdown input
/// rules, and the inline wraps a selection takes.
///
/// The surface is one text-entry element (the page's Markdown source textarea,
/// `dom::Id::source`); everything here goes through Document, so the editor never
/// touches the page directly. Only text entries carry a caret and a selection the
/// A-tier surface can read, which is why the `contenteditable` seamless surface is
/// not one of them.
class Editor {
public:
    Editor(zui::Document& page, Document& doc);

    void bind_surface(std::string_view id);
    const std::string& surface() const { return surface_; }

    std::string text() const;
    std::size_t caret() const;
    std::string selection() const;
    /// The source selection's byte range. `valid` is false when nothing is selected.
    struct SelectionRange {
        std::size_t start = 0;
        std::size_t end = 0;
        bool valid = false;
    };
    SelectionRange selection_range() const;
    /// Half-open UTF-8 byte range. An empty range collapses the caret.
    void set_selection(std::size_t start, std::size_t end);
    void insert(std::string_view text);

    /// Wraps the selection in @p before/@p after. An empty selection inserts
    /// @p placeholder and selects it, so the next keystroke lands inside.
    void wrap(std::string_view before, std::string_view after, std::string_view placeholder);

    /// The input rules: "# ", "## "… -> heading, "- "/"* " -> bullet, "1. " ->
    /// ordered, "> " -> quote. Called after every keystroke; returns true when it
    /// rewrote the line.
    bool apply_block_rule();

    void set_heading(int depth);
    /// Adds @p prefix when the caret's line lacks it, removes it when present.
    void toggle_line_prefix(std::string_view prefix);
    /// Task box: "- [ ] " on the caret's line.
    void toggle_task();
    void insert_line(std::string_view text);

private:
    std::string_view current_line(const std::string& text, std::size_t offset) const;
    void replace_line(std::string_view next);
    void commit(std::string next, std::size_t caret);

    zui::Document& page_;
    Document& doc_;
    /// Empty until Session binds it (`dom::Id::source`).
    std::string surface_;
};

}  // namespace zmd

#endif
