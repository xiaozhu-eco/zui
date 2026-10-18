#ifndef ZMD_SESSION_H
#define ZMD_SESSION_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <zui/app.h>

#include "chrome.h"
#include "command.h"
#include "document.h"
#include "editor.h"
#include "file_list_model.h"
#include "find.h"
#include "later.h"
#include "markdown.h"
#include "palette.h"
#include "panels.h"
#include "settings.h"
#include "workspace.h"

namespace zmd {

/// Composition root. It owns every part, binds the page's handler names to
/// methods, and is the only class that knows about zui::App. The parts below it
/// stay independent of each other and of the shell.
class Session {
public:
    explicit Session(zui::App& app);
    /// Hands every handler name back to the page. A page outlives the session that drove it
    /// (`zui::App` keeps the tree across sessions), and a binding that outlived its session wrote
    /// into freed memory the next time the page dispatched - a later `open_page` blurs the focused
    /// field, which fires the dead session's `on_source_blur`.
    ~Session();

    /// Loads the page, wires the handlers, opens a document. False: no window.
    bool start(int argc, char** argv);
    /// Loads a page and wires every handler. start() uses it with index.html; tests
    /// point it at the source tree's page.
    bool open_page(std::string_view html_path);

    zui::App& app() { return app_; }
    zui::Document& page() { return app_.document(); }
    Markdown& markdown() { return markdown_; }
    Workspace& workspace() { return workspace_; }
    Document& document() { return document_; }
    Editor& editor() { return editor_; }
    Chrome& chrome() { return chrome_; }
    Panels& panels() { return panels_; }
    Settings& settings() { return settings_; }
    Palette& palette() { return palette_; }
    Find& find() { return find_; }

    // documents
    void apply_source(std::string text, bool edited, bool write_field);
    void sync_from_input();
    /// Takes whatever the page already shows as the open document, without writing
    /// the chrome: the status row keeps the page's own text until the first edit,
    /// which is what js/zmd.js does too.
    void adopt_page_source();
    bool open_file(const std::filesystem::path& path);
    bool open_welcome();
    void new_doc();
    void open_doc();
    void save_doc();
    void save_doc_as();
    void export_html();
    void insert_snippet(std::string_view snippet);
    void wrap_selection(std::string_view before, std::string_view after,
                        std::string_view placeholder);

    // view and panels
    void refresh_outline();
    void refresh_preview();
    /// The reading position after a scroll: the outline's current row (the prototype's
    /// `syncOutline` rule) and the panel's progress bar, both read from the scroller's own
    /// geometry. Wired to the scroller's `onscroll`.
    void sync_reading_position();
    void scroll_top();

    // shell
    void run_command(std::string_view id);
    void on_accel(zui::Event& event);
    void open_dialog(std::string_view id);
    void close_dialog(std::string_view id);
    void pick_image();
    void drop_paths(std::string_view path);
    void set_blurred(bool blurred);
    bool has_unsaved() const;
    /// False while the document has unsaved changes: the caller opens the dialog.
    bool confirm_leave();

private:
    /// Binds one page handler and remembers the name, so the destructor can unbind it.
    void bind(std::string_view name, zui::Document::Handler handler);
    /// `Document::defer`, guarded: a page outlives the session that armed the callback, so a
    /// callback still queued when the session dies would run against freed memory (the deferred
    /// follow below is two frames deep).
    void later(std::function<void()> fn);
    enum class Pending { None, NewDoc, OpenPath, OpenPicker, Welcome, Quit };

    void bind_all();
    void bind_files();
    void bind_editor();
    void bind_chrome();
    void bind_palette();
    void bind_find();
    void build_commands();
    std::vector<Command> command_list() const;
    std::vector<Command> file_list() const;
    std::filesystem::path scratch_dir() const;
    void write_preview(std::size_t caret = static_cast<std::size_t>(-2));
    /// Paint the paper caret at the editor offset. The preview text does not change.
    void sync_paper_caret();
    /// Project the parked source caret or selection onto the paper. A selection paints Document
    /// Selection; it does not rebuild the preview.
    void sync_paper_view();
    /// One named paper block, in source order. `base` is the innerText offset of its first
    /// visible byte inside `#mdc-a-preview`.
    struct PaperBlock {
        std::string id;
        std::size_t beg = 0;
        std::size_t end = 0;
        std::size_t base = 0;
        std::size_t vis_len = 0;
    };
    std::vector<PaperBlock> paper_blocks();
    /// Source byte → block id, innerText offset in that block, and the same offset inside the
    /// preview. An offset in a blank line, or past the last block, clamps to the end of the
    /// preceding run. False only when the paper has no named block.
    bool paper_point(std::size_t src, std::string& id, std::size_t& vis, std::size_t& flatten);
    void mark_current_block(const std::string& id);
    void hide_format_strip();
    /// Place the format strip from caret geometry. Coordinates are viewport CSS px.
    /// `prev_glyph_bottom` is the previous line's font-box bottom (baseline + descent), or a
    /// large negative value when the selection has no line above it.
    void place_format_strip(float x, float y, float height, float end_y, float end_h,
                            float prev_glyph_bottom);
    std::string paper_cur_;
    bool strip_open_ = false;
    /// The writing surface takes a click or a drag only while no sheet covers the paper, which is
    /// the same gate the sheet's own `pointer-events` rule uses.
    bool write_surface_ready();
    /// The source offset a point over the paper names. The block under the point is chosen by
    /// y, then the caret's glyph hit on that block is mapped back through the bytes the
    /// preview actually painted. Shared by the writing-view click and the drag that selects
    /// text on the paper.
    std::size_t write_offset_at(float x, float y);
    /// One paper line up or down from the planted caret, keeping its column. @p dir is -1 or +1.
    void step_paper_line(int dir, bool shift);
    /// The paper scroller's current offset; 0 when it has none to report.
    float scroll_top_value();
    /// One drag in the writing view. The press anchors a source offset, each move extends the
    /// parked textarea's selection, and the paper projects that range as Document Selection.
    /// `dragged` tells the click that follows to leave that selection alone.
    struct WriteDrag {
        bool active = false;
        bool dragged = false;
        std::size_t anchor = 0;
        std::size_t focus = 0;
        /// The pointer's latest point, so the scroll tick knows where the drag is parked.
        float x = 0.f;
        float y = 0.f;
    };
    WriteDrag write_drag_;
    /// The interval that scrolls the paper while a drag's pointer parks outside its port.
    zui::TimerId write_scroll_ = 0;
    void start_write_scroll();
    void stop_write_scroll();
    /// One step of 写作's drag scroll (see `start_write_scroll`).
    void tick_write_scroll();
    /// The source offset a drag point names: the pointer held inside the paper's visible port, so
    /// a pointer past the fold selects the last visible line rather than the document's end.
    std::size_t write_drag_offset_at(float x, float y);
    /// What a relative image or link target in the document resolves against: the document's own
    /// folder, or the workspace while the document has no path yet (the welcome sample).
    std::string preview_base() const;
    /// Two frames later. `focus` in the same turn as `load_fragment` SEHs.
    void focus_source_later();
    /// 打字机模式: keeps the preview block that holds the caret in the middle of the viewport.
    /// Deferred because the preview fragment is re-stamped in this turn.
    void center_caret_block_later();
    /// Scrolls the preview to the block that owns the editor caret. Shared by 打字机模式 and the
    /// find jumps, which both move the caret to text the reader wants to see. The find jumps
    /// centre the hit; the writing view keeps the caret's block in view, and 打字机模式 asks for
    /// the middle.
    void scroll_block_of_caret_later(zui::ScrollAlign align = zui::ScrollAlign::Center);
    void apply_document_state();
    void publish_files();
    void create_untitled();
    /// Where 新建位置 sends a new document: the workspace root, or the folder the file panel
    /// shows while 当前文件夹 is selected.
    std::filesystem::path new_document_dir() const;
    void request_new();
    void request_open(const std::filesystem::path& path);
    void request_welcome();
    void open_doc_picker();
    void perform_pending();
    void load_settings();
    void save_settings();
    std::filesystem::path settings_file() const;
    int slider_number(std::string_view id, bool line);
    /// The source selection the palette was opened over. `Document::selection` belongs to the
    /// whole document, and typing in the palette's field replaces it, so the range (and the text
    /// it covered) is remembered here and put back before the palette runs a command. The toolbar
    /// never needs this: a click keeps the selection.
    struct PaletteSelection {
        std::size_t start = 0;
        std::size_t end = 0;
        std::string text;
    };
    void remember_palette_selection();
    void toggle_palette(Palette::Mode mode);
    PaletteSelection palette_selection_;
    /// The 图片存放 row as a directory under the workspace ("assets" when the row is empty).
    std::filesystem::path asset_dir() const;
    /// The same directory as the document spells it: no trailing separator, forward slashes.
    std::string asset_rel_dir() const;

    zui::App& app_;
    /// Every handler name `bind()` took, in binding order; the destructor hands them back.
    std::vector<std::string> bound_;
    /// This session's deferred callbacks; the token inside dies with the session (see `later.h`).
    Later later_;
    FileListModel files_;
    Pending pending_ = Pending::None;
    std::filesystem::path pending_path_;
    Markdown markdown_;
    /// What the last preview render named: how far the `mdh{index}` / `mdb{index}` scans can go.
    /// The ids run from zero, so a stale node from a longer render is out of range, not in it.
    Markdown::RenderCounts rendered_;
    /// Visible byte -> source byte for each preview block the last render named.
    VisibleSources hit_sources_;
    Workspace workspace_;
    Document document_;
    Editor editor_;
    Chrome chrome_;
    Panels panels_;
    Settings settings_;
    Palette palette_;
    Find find_;
    /// The editing keys answer only while the source textarea holds the focus; `on_source_focus`
    /// / `on_source_blur` maintain it. The prototype checks `document.activeElement` the same way.
    bool source_focused_ = false;
    std::string welcome_;
    /// The directory start() was launched from: page, samples and preview fragment.
    std::filesystem::path exe_dir_;
    std::filesystem::path preview_path_;
};

/// The welcome document, read from beside the executable when present.
std::string welcome_text(const std::filesystem::path& dir);

}  // namespace zmd

#endif


