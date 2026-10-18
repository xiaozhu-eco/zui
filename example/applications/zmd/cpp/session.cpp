#include "session.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

#include "dom.h"
#include "util.h"

namespace zmd {
namespace {

constexpr std::string_view kAppTitle = "小竹 Markdown";

bool write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text;
}

std::string filename_utf8(const std::filesystem::path& path) {
    return path.filename().u8string();
}

/// Ctrl+1..4 (views) and Ctrl+Shift+1..2 (panels). With Shift the number row arrives as its
/// symbol on most layouts ("!" for 1), and the engine reports `KeyboardEvent.key`, so both
/// spellings have to mean the same digit.
int accelerator_digit(std::string_view name) {
    if (name.size() != 1) {
        return 0;
    }
    switch (name[0]) {
        case '1':
        case '!':
            return 1;
        case '2':
        case '@':
            return 2;
        case '3':
        case '#':
            return 3;
        case '4':
        case '$':
            return 4;
        default:
            return 0;
    }
}

/// Row buttons carry hd3 / fl7; the trailing number is the row index.
int row_index(std::string_view element_id, std::string_view prefix) {
    if (!util::starts_with(element_id, prefix)) {
        return -1;
    }
    std::string digits(element_id.substr(prefix.size()));
    if (digits.empty()) {
        return -1;
    }
    try {
        return std::stoi(digits);
    } catch (...) {
        return -1;
    }
}

/// Whether the character at @p at (a UTF-8 lead byte) reads as part of a word: ASCII letters,
/// digits and `_`, a CJK ideograph or kana, and any other letter-ish code point - the marks a word
/// breaks at (，。、！？（）【】and the general-punctuation block) stay out. The engine's own text
/// controls read the same rule; the paper is not one, so the writing view carries a copy.
bool source_word_char(const std::string& text, std::size_t at) {
    if (at >= text.size()) {
        return false;
    }
    const auto ascii = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) {
        return ascii(lead);
    }
    unsigned int code = 0;
    std::size_t need = 0;
    if ((lead & 0xE0) == 0xC0) {
        code = lead & 0x1Fu;
        need = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        code = lead & 0x0Fu;
        need = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        code = lead & 0x07u;
        need = 3;
    } else {
        return true;
    }
    for (std::size_t i = 0; i < need && at + 1 + i < text.size(); ++i) {
        code = (code << 6) | (static_cast<unsigned char>(text[at + 1 + i]) & 0x3Fu);
    }
    if (code >= 0x2000 && code <= 0x206F) {
        return false;
    }
    if (code >= 0x3000 && code <= 0x303F) {
        return false;
    }
    if (code >= 0xFE30 && code <= 0xFE4F) {
        return false;
    }
    if (code >= 0xFF01 && code <= 0xFF0F) {
        return false;
    }
    if (code >= 0xFF1A && code <= 0xFF20) {
        return false;
    }
    if (code >= 0xFF3B && code <= 0xFF40) {
        return false;
    }
    if (code >= 0xFF5B && code <= 0xFF65) {
        return false;
    }
    if (code >= 0x00A0 && code <= 0x00BF) {
        return false;
    }
    return true;
}

/// The double-click word around @p at in the source. A browser segments a CJK run as a word, so a
/// double click on Chinese text takes the run between its punctuation rather than one character.
void source_word_at(const std::string& text, std::size_t at, std::size_t& from, std::size_t& to) {
    from = 0;
    to = 0;
    if (text.empty()) {
        return;
    }
    const auto next = [&](std::size_t i) {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
            ++i;
        }
        return i;
    };
    const auto prev = [&](std::size_t i) {
        while (i > 0) {
            --i;
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
                break;
            }
        }
        return i;
    };
    // A click past the end of a line maps onto the newline that ends it; the word there is the
    // last one on the line, not the break.
    if (at >= text.size() || text[at] == '\n' || text[at] == '\r') {
        if (at == 0) {
            return;
        }
        at = prev(std::min(at, text.size()));
    }
    at = prev(next(at));
    if (source_word_char(text, at)) {
        from = at;
        while (from > 0) {
            const std::size_t begin = prev(from);
            if (!source_word_char(text, begin)) {
                break;
            }
            from = begin;
        }
        to = at;
        while (to < text.size() && source_word_char(text, to)) {
            to = next(to);
        }
        return;
    }
    from = at;
    to = next(at);
}

/// The newline-bounded line around @p at: what a triple click takes.
void source_line_at(const std::string& text, std::size_t at, std::size_t& from, std::size_t& to) {
    at = std::min(at, text.size());
    const std::size_t open = at > 0 ? text.rfind('\n', at - 1) : std::string::npos;
    from = open == std::string::npos ? 0 : open + 1;
    const std::size_t close = text.find('\n', at);
    to = close == std::string::npos ? text.size() : close;
}

}  // namespace

Session::Session(zui::App& app)
    : app_(app),
      later_(app.document()),
      workspace_(),
      document_(),
      editor_(app.document(), document_),
      chrome_(app.document()),
      panels_(app.document()),
      settings_(app.document()),
      palette_(app.document(),
               [this](std::string_view id) {
                   // A palette field takes the document selection when it is typed into, so a
                   // format command has to put the range the user had selected back first - and
                   // only while the source still reads the way it did when the range was taken.
                   const std::string& source = document_.source();
                   if (!palette_selection_.text.empty() &&
                       palette_selection_.end <= source.size() &&
                       source.substr(palette_selection_.start,
                                     palette_selection_.end - palette_selection_.start) ==
                           palette_selection_.text) {
                       editor_.set_selection(palette_selection_.start, palette_selection_.end);
                   }
                   palette_selection_ = {};
                   // A file row's id is the name the palette listed (Ctrl+P); the browser's row
                   // runs `selectTreeRow` on that entry (js/app.js `paletteFiles`), so the native
                   // opens the document - through `request_open`, which asks about unsaved work.
                   if (palette_.mode() == Palette::Mode::Files) {
                       const std::filesystem::path path = workspace_.path_of(std::string(id));
                       later([this, path] { request_open(path); });
                       return;
                   }
                   run_command(id);
               }),
      find_(app.document(), document_, editor_) {
    // The one writing surface the A-tier surface can serve is the source textarea:
    // it holds a value, a caret and a selection. The seamless article is
    // `contenteditable` and its content belongs to the renderer; zmd cannot write
    // a rendered subtree, so it is not bound here. See README.md.
    editor_.bind_surface(dom::Id::source);
}

Session::~Session() {
    for (const std::string& name : bound_) {
        page().unbind(name);
    }
    // The drag-scroll interval writes through this session.
    stop_write_scroll();
    // The save-dot pulse is a 1.2s timer that writes through this session's chrome.
    chrome_.reset();
}

void Session::bind(std::string_view name, zui::Document::Handler handler) {
    page().bind(name, std::move(handler));
    bound_.emplace_back(name);
}

void Session::later(std::function<void()> fn) {
    later_.run(std::move(fn));
}

bool Session::start(int argc, char** argv) {
    if (argv == nullptr || argc < 1 || argv[0] == nullptr) {
        return false;
    }
    // The page, the welcome sample and the preview fragment all sit beside the
    // executable: the build copies index.html, css/, js/ and samples/ next to it.
    // A relative "index.html" would resolve against the process working directory
    // instead, which is not where the app was installed.
    exe_dir_ = std::filesystem::absolute(argv[0]).parent_path();
    app_.host().set_app_id("zmd");
    if (!open_page((exe_dir_ / "index.html").string())) {
        return false;
    }
    app_.window().set_title(kAppTitle);

    const std::string data_dir = app_.host().user_data_dir();
    const std::filesystem::path data_path =
        data_dir.empty() ? exe_dir_ : std::filesystem::u8path(data_dir);
    std::error_code ec;
    std::filesystem::create_directories(data_path, ec);
    // The preview fragment is a scratch file. It must not land in the source tree
    // when a test points argv[0] at example/applications/zmd.
    preview_path_ = data_path / "_preview.html";
    load_settings();

    std::filesystem::path opened;
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        opened = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    }
    std::filesystem::path root = data_path / "notes";
    if (!opened.empty()) {
        std::error_code file_ec;
        if (std::filesystem::is_regular_file(opened, file_ec)) {
            root = opened.parent_path();
        } else if (std::filesystem::is_directory(opened, file_ec)) {
            root = opened;
            opened.clear();
        }
    }
    std::filesystem::create_directories(root, ec);
    workspace_.set_root(root);
    settings_.set_workspace_path(root.u8string());

    if (!opened.empty()) {
        if (!open_file(opened)) {
            open_welcome();
        }
    } else {
        open_welcome();
    }
    settings_.apply();
    apply_document_state();
    publish_files();
    return true;
}

bool Session::open_page(std::string_view html_path) {
    // A window that has already painted this page still has animation and hover
    // records aimed at those nodes (the caret blinks, the save dot pulses).
    // Loading index.html again frees the tree and the next frame walks the
    // records. A later start rebinds the live page and closes the overlays.
    const bool warm = page().get(dom::Id::app, "class").find("mdw") != std::string::npos;
    if (!warm) {
        if (!page().load(std::string(html_path))) {
            return false;
        }
    } else {
        page().blur();
        find_.close();
        palette_.close();
        page().add_class(dom::Id::app, dom::Class::set_closed);
        page().remove_class(dom::Id::app, dom::Class::pin_left);
        page().remove_class(dom::Id::app, dom::Class::pin_right);
        chrome_.reset();
        page().remove_class(dom::Id::app, dom::Class::is_blurred);
        page().remove_class(dom::Id::dialog_unsaved, "zmd-open");
        page().remove_class(dom::Id::dialog_link, "zmd-open");
        page().remove_class(dom::Id::dialog_image, "zmd-open");
        page().set(dom::Id::dialog_unsaved, "visible", "0");
        page().set(dom::Id::dialog_link, "visible", "0");
        page().set(dom::Id::dialog_image, "visible", "0");
        page().set(dom::Id::scroll, "scroll_top", "0");
        settings_.show_pane("type");
    }
    // Native host: 写作 shows the rendered preview. The browser has no such class
    // and keeps the contenteditable article.
    page().add_class(dom::Id::root, dom::Class::host);
    bind_all();
    build_commands();
    adopt_page_source();
    return true;
}

// ---------------------------------------------------------------- documents

void Session::adopt_page_source() {
    // The status row stays the page's own text until the first edit, on both sides:
    // js/zmd.js recomputes the counts only from the input handler. So this adopts the
    // document without writing the chrome.
    document_.reset("未命名.md", page().get(dom::Id::source, "value"), {});
    refresh_outline();
}

void Session::apply_source(std::string text, bool edited, bool write_field) {
    document_.set_source(std::move(text), edited);
    if (write_field) {
        page().set(editor_.surface(), "value", document_.source());
    }
    apply_document_state();
    refresh_outline();
    refresh_preview();
}

void Session::sync_from_input() {
    const std::string text = page().get(editor_.surface(), "value");
    if (text == document_.source()) {
        return;
    }
    document_.set_source(text, true);
    chrome_.pulse();
    // Welcome has no path, so an edit stays dirty. A document that already lives
    // on disk is stored immediately when 自动保存 is on.
    if (settings_.autosave() && !document_.path().empty()) {
        if (write_file(document_.path(), document_.source())) {
            document_.mark_saved();
        } else {
            document_.mark_error();
        }
    }
    apply_document_state();
    refresh_outline();
    later([this] { refresh_preview(); });
    // 打字机模式 keeps the block being written in the middle; the preview is re-stamped in this
    // turn, so the scroll waits for that (and for the layout the deferral causes).
    center_caret_block_later();
}

void Session::apply_document_state() {
    chrome_.set_title(document_.title());
    const auto [words, lines] = markdown_.counts(document_.source());
    chrome_.set_counts(words, lines);
    chrome_.set_save_state(document_.save_state());
}

bool Session::open_file(const std::filesystem::path& path) {
    // A folder, or a path that cannot be opened, is not a document. Reading through a stream
    // tells "cannot open" apart from "opened and empty", which `read_file` cannot: a folder used
    // to open as an empty buffer named after it, and the next save failed on it.
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    document_.reset(filename_utf8(path), text, path);
    page().set(editor_.surface(), "value", text);
    apply_document_state();
    refresh_outline();
    refresh_preview();
    publish_files();
    return true;
}

bool Session::open_welcome() {
    welcome_ = welcome_text(exe_dir_.empty() ? std::filesystem::current_path() : exe_dir_);
    document_.reset("欢迎.md", welcome_, {});
    page().set(editor_.surface(), "value", welcome_);
    apply_document_state();
    refresh_outline();
    refresh_preview();
    publish_files();
    return true;
}

void Session::create_untitled() {
    std::string name = "未命名.md";
    std::filesystem::path path;
    if (workspace_.valid()) {
        const std::filesystem::path dir = new_document_dir();
        name = unique_name_in(dir, "未命名", ".md");
        path = dir / std::filesystem::u8path(name);
        write_file(path, "");
    }
    document_.reset(name, "", path);
    page().set(editor_.surface(), "value", "");
    apply_document_state();
    refresh_outline();
    refresh_preview();
    publish_files();
    focus_source_later();
}

std::filesystem::path Session::new_document_dir() const {
    // Factory 新建位置 is the folder the file panel is showing. 工作目录根 is the
    // saved choice that sends new files and folders to the workspace root.
    return settings_.new_in_root() ? workspace_.root() : workspace_.dir();
}

void Session::request_new() {
    if (document_.dirty()) {
        pending_ = Pending::NewDoc;
        pending_path_.clear();
        open_dialog(dom::Id::dialog_unsaved);
        return;
    }
    create_untitled();
}

void Session::request_open(const std::filesystem::path& path) {
    if (path == document_.path()) {
        return;
    }
    if (document_.dirty()) {
        pending_ = Pending::OpenPath;
        pending_path_ = path;
        open_dialog(dom::Id::dialog_unsaved);
        return;
    }
    open_file(path);
}

void Session::request_welcome() {
    if (document_.dirty()) {
        pending_ = Pending::Welcome;
        pending_path_.clear();
        open_dialog(dom::Id::dialog_unsaved);
        return;
    }
    open_welcome();
}

void Session::open_doc_picker() {
    const zui::PickPaths picked = app_.host().pick_open();
    if (picked.status == zui::PickStatus::Ok && !picked.paths.empty()) {
        // The picker is a document opener like the file row and the drop: a dirty buffer asks
        // first. This used to call `open_file` directly and dropped unsaved edits on the floor.
        request_open(std::filesystem::u8path(picked.paths.front()));
    }
}

void Session::perform_pending() {
    const Pending kind = pending_;
    const std::filesystem::path path = pending_path_;
    pending_ = Pending::None;
    pending_path_.clear();
    if (kind == Pending::NewDoc) {
        create_untitled();
    } else if (kind == Pending::OpenPath) {
        open_file(path);
    } else if (kind == Pending::OpenPicker) {
        open_doc_picker();
    } else if (kind == Pending::Welcome) {
        open_welcome();
    } else if (kind == Pending::Quit) {
        app_.quit();
    }
}

void Session::new_doc() {
    request_new();
}

void Session::open_doc() {
    if (document_.dirty()) {
        pending_ = Pending::OpenPicker;
        pending_path_.clear();
        open_dialog(dom::Id::dialog_unsaved);
        return;
    }
    open_doc_picker();
}

void Session::save_doc() {
    if (document_.path().empty()) {
        save_doc_as();
        return;
    }
    if (write_file(document_.path(), document_.source())) {
        document_.mark_saved();
    } else {
        document_.mark_error();
    }
    apply_document_state();
}

void Session::save_doc_as() {
    const zui::PickPath picked = app_.host().pick_save();
    if (picked.status == zui::PickStatus::Ok && !picked.path.empty()) {
        document_.set_path(std::filesystem::u8path(picked.path));
        save_doc();
        publish_files();
        return;
    }
    // A headless host has no save dialog. A pathless document still has to land
    // somewhere when the user asked to save, the same way export does.
    if (picked.status != zui::PickStatus::Unsupported || !workspace_.valid()) {
        return;
    }
    const std::string name = workspace_.unique_name("未命名", ".md");
    document_.set_path(workspace_.path_of(name));
    save_doc();
    publish_files();
}

void Session::export_html() {
    write_preview();
    // The export has to stand on its own where it is written, so the sheets are inlined
    // instead of linked: a `css/…` href would point at a file that is not next to it.
    std::string style = read_file(exe_dir_.empty() ? std::filesystem::path()
                                                   : exe_dir_ / "css" / "app.css");
    if (style.empty()) {
        style = "body{max-width:40em;margin:0 auto;padding:2em;"
                "font-family:Georgia,\"Songti SC\",serif;line-height:1.7}";
    }
    const std::string page_text = "<!DOCTYPE html>\n<meta charset=\"utf-8\">\n<title>" +
                                  util::escape(document_.title()) +
                                  "</title>\n<style>\n" + style + "</style>\n" +
                                  "<body>\n<article class=\"mdw-doc\">\n" +
                                  markdown_.render(document_.source()) + "</article>\n";
    const zui::PickPath picked = app_.host().pick_save();
    std::filesystem::path dest;
    if (picked.status == zui::PickStatus::Ok && !picked.path.empty()) {
        dest = std::filesystem::u8path(picked.path);
    } else if (picked.status == zui::PickStatus::Unsupported) {
        // Headless hosts have no save dialog. Write beside the document instead.
        if (!document_.path().empty()) {
            dest = document_.path();
            dest.replace_extension(".html");
        } else if (workspace_.valid()) {
            dest = workspace_.root() / "export.html";
        }
    }
    if (!dest.empty()) {
        write_file(dest, page_text);
    }
}

bool Session::has_unsaved() const {
    return document_.dirty();
}

bool Session::confirm_leave() {
    return !document_.dirty();
}

// ------------------------------------------------------------------- view

void Session::refresh_outline() {
    panels_.set_outline(markdown_.headings(document_.source()));
}

void Session::write_preview(std::size_t caret) {
    if (preview_path_.empty()) {
        return;
    }
    if (caret == static_cast<std::size_t>(-2)) {
        caret = settings_.view() == Settings::View::Live ? editor_.caret()
                                                         : static_cast<std::size_t>(-1);
    }
    // The find bar counts hits; the writing view has to show them, the way the prototype marks
    // its hits. The hits are sorted and disjoint, which is what the renderer's search expects.
    MarkRanges marks;
    if (find_.is_open() && !find_.query().empty()) {
        for (std::size_t at : find_.hits()) {
            marks.emplace_back(at, at + find_.query().size());
        }
    }
    hit_sources_.clear();
    write_file(preview_path_, markdown_.render(document_.source(), caret, preview_base(),
                                               marks.empty() ? nullptr : &marks, &rendered_,
                                               &hit_sources_));
}

std::string Session::preview_base() const {
    if (!document_.path().empty()) {
        return document_.path().parent_path().u8string();
    }
    return scratch_dir().u8string();
}

void Session::sync_paper_caret() {
    if (settings_.view() != Settings::View::Live) {
        page().set_text_caret({}, std::nullopt);
        return;
    }
    const Editor::SelectionRange range = editor_.selection_range();
    if (range.valid && range.end > range.start) {
        page().set_text_caret({}, std::nullopt);
        return;
    }
    const std::size_t caret = editor_.caret();
    const auto parse_off = [](const std::string& raw) -> std::size_t {
        if (raw.empty()) {
            return 0;
        }
        try {
            return static_cast<std::size_t>(std::stoul(raw));
        } catch (...) {
            return 0;
        }
    };
    std::string best_id;
    std::size_t best_span = static_cast<std::size_t>(-1);
    std::size_t best_vis = 0;
    const int named = std::max(rendered_.headings, rendered_.blocks);
    for (int i = 0; i < named; ++i) {
        const std::string ids[] = {"mdh" + std::to_string(i), "mdb" + std::to_string(i)};
        for (const std::string& id : ids) {
            const std::size_t beg = parse_off(page().get(id, "data-beg"));
            const std::size_t end = parse_off(page().get(id, "data-at"));
            if (page().get(id, "data-at").empty() || end < beg || caret < beg || caret > end) {
                continue;
            }
            const std::size_t span = end - beg;
            if (span >= best_span) {
                continue;
            }
            best_span = span;
            best_id = id;
            const auto mapped = hit_sources_.find(id);
            if (mapped == hit_sources_.end()) {
                // A table (and anything else with no per-byte map) still has laid-out text.
                // The end of that text is where a caret in the block's second half belongs.
                best_vis = caret > beg + span / 2 ? static_cast<std::size_t>(-1) : 0;
                continue;
            }
            best_vis = mapped->second.at.size();
            for (std::size_t n = 0; n < mapped->second.at.size(); ++n) {
                if (mapped->second.at[n] >= caret) {
                    best_vis = n;
                    break;
                }
            }
        }
    }
    if (best_id.empty()) {
        page().set_text_caret({}, std::nullopt);
        return;
    }
    page().set_text_caret(best_id, best_vis);
    mark_current_block(best_id);
}

void Session::mark_current_block(const std::string& id) {
    if (paper_cur_ == id) {
        if (!id.empty()) {
            page().add_class(id, "mdw-cur");
        }
        return;
    }
    if (!paper_cur_.empty()) {
        page().remove_class(paper_cur_, "mdw-cur");
    }
    paper_cur_ = id;
    if (!id.empty()) {
        page().add_class(id, "mdw-cur");
    }
}

std::vector<Session::PaperBlock> Session::paper_blocks() {
    std::vector<PaperBlock> rows;
    const int named = std::max(rendered_.headings, rendered_.blocks);
    const auto parse_off = [](const std::string& raw) -> std::size_t {
        if (raw.empty()) {
            return 0;
        }
        try {
            return static_cast<std::size_t>(std::stoul(raw));
        } catch (...) {
            return 0;
        }
    };
    for (int i = 0; i < named; ++i) {
        const std::string ids[] = {"mdh" + std::to_string(i), "mdb" + std::to_string(i)};
        for (const std::string& id : ids) {
            if (page().get(id, "data-at").empty()) {
                continue;
            }
            PaperBlock block;
            block.id = id;
            block.beg = parse_off(page().get(id, "data-beg"));
            block.end = parse_off(page().get(id, "data-at"));
            const auto mapped = hit_sources_.find(id);
            block.vis_len = mapped == hit_sources_.end() ? 0 : mapped->second.at.size();
            rows.push_back(std::move(block));
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const PaperBlock& a, const PaperBlock& b) { return a.beg < b.beg; });
    std::size_t base = 0;
    for (PaperBlock& block : rows) {
        block.base = base;
        base += block.vis_len;
    }
    return rows;
}

bool Session::paper_point(std::size_t src, std::string& id, std::size_t& vis, std::size_t& flatten) {
    const std::vector<PaperBlock> rows = paper_blocks();
    const PaperBlock* picked = nullptr;
    std::size_t best_span = static_cast<std::size_t>(-1);
    for (const PaperBlock& block : rows) {
        if (src < block.beg || src > block.end) {
            continue;
        }
        const std::size_t span = block.end - block.beg;
        if (span >= best_span) {
            continue;
        }
        best_span = span;
        picked = &block;
    }
    if (picked == nullptr) {
        // A blank line, or the offset past the last block, emits no glyphs. The boundary sits on
        // the last emitted byte at or before it, so [0, gap) still highlights the paragraph and
        // an end of source.size() still reaches the last run.
        const PaperBlock* prev = nullptr;
        for (const PaperBlock& block : rows) {
            if (block.end < src) {
                prev = &block;
                continue;
            }
            break;
        }
        if (prev == nullptr) {
            if (rows.empty()) {
                return false;
            }
            id = rows.front().id;
            vis = 0;
            flatten = rows.front().base;
            return true;
        }
        id = prev->id;
        vis = prev->vis_len;
        flatten = prev->base + prev->vis_len;
        return true;
    }
    id = picked->id;
    vis = picked->vis_len;
    const auto mapped = hit_sources_.find(picked->id);
    if (mapped != hit_sources_.end()) {
        vis = mapped->second.at.size();
        for (std::size_t n = 0; n < mapped->second.at.size(); ++n) {
            if (mapped->second.at[n] >= src) {
                vis = n;
                break;
            }
        }
    }
    flatten = picked->base + vis;
    return true;
}

void Session::hide_format_strip() {
    if (!strip_open_ || preview_path_.empty()) {
        strip_open_ = false;
        return;
    }
    const std::filesystem::path path = preview_path_.parent_path() / "_sel.html";
    write_file(path, "<span hidden></span>");
    page().replace_fragment(path.string(), dom::Id::sel_host);
    strip_open_ = false;
}

void Session::place_format_strip(float x, float y, float height, float end_y, float end_h,
                                  float prev_glyph_bottom) {
    if (preview_path_.empty()) {
        return;
    }
    const std::optional<zui::ElementBox> root = page().measure(dom::Id::scroll);
    const float origin_x = root && root->laid ? root->border.x : 0.f;
    const float origin_y = root && root->laid ? root->border.y : 0.f;
    // The host is absolute inside the scroller. Paint places that frame at cb_offset - scroll,
    // and the caret rect is already in viewport space, so the stored top/left add the scroll
    // offset back. Without it the strip sits one scrollTop too high.
    const float scroll_y = scroll_top_value();
    float scroll_x = 0.f;
    try {
        scroll_x = std::stof(page().get(dom::Id::scroll, "scroll_left"));
    } catch (...) {
        scroll_x = 0.f;
    }
    constexpr float kStripW = 148.f;
    constexpr float kGap = 8.f;
    constexpr float kGuessH = 32.f;
    // `y` is the selected font-box top and `end_y` is the selection's font-box bottom.
    // The bubble (padding included) is what must clear the previous glyphs. Anchoring the
    // button instead left the padding over the line above, and a second correction put the
    // button back there even after the strip had flipped below the selection.
    const float sel_top = y;
    const float sel_bottom = std::max(end_y, y + height);
    const bool have_prev = prev_glyph_bottom > -1.0e8f;
    auto bubble_top = [&](float bubble_h) {
        const float above = sel_top - kGap - bubble_h;
        if (!have_prev || above >= prev_glyph_bottom - 0.5f) {
            return above;
        }
        return sel_bottom + kGap;
    };
    auto style_top = [&](float viewport_top) { return viewport_top - origin_y + scroll_y; };
    float top = style_top(bubble_top(kGuessH));
    float left = x - origin_x + scroll_x - kStripW * 0.5f;
    const float min_left = scroll_x + 6.f;
    const float limit =
        root && root->laid ? scroll_x + root->border.width - kStripW - 6.f : left;
    if (left < min_left) {
        left = min_left;
    }
    if (left > limit) {
        left = limit;
    }
    const auto px = [](float v) {
        return std::to_string(static_cast<int>(std::lround(v)));
    };
    std::string html;
    html += "<div class=\"mdw-bubble is-open\" id=\"zmd-sel-strip\" role=\"toolbar\" "
            "aria-label=\"\xe9\x80\x89\xe5\x8c\xba\xe6\xa0\xbc\xe5\xbc\x8f\" "
            "onpointerdown=\"on_sel_toolbar\" style=\"left:";
    html += px(left);
    html += "px;top:";
    html += px(top);
    html += "px\">";
    auto btn = [&](const char* id, const char* handler, const char* icon, const char* label) {
        html += "<button class=\"mdw-tbtn\" id=\"";
        html += id;
        html += "\" type=\"button\" aria-label=\"";
        html += label;
        html += "\" onclick=\"";
        html += handler;
        html += "\"><img class=\"mdw-ico-img\" src=\"icons/";
        html += icon;
        html += ".png\" width=\"15\" height=\"15\" alt=\"\"></button>";
    };
    btn("zmd-sel-bold", "on_format_bold", "bold", "\xe5\x8a\xa0\xe7\xb2\x97");
    btn("zmd-sel-italic", "on_format_italic", "italic", "\xe6\x96\x9c\xe4\xbd\x93");
    btn("zmd-sel-code", "on_format_code", "code", "\xe8\xa1\x8c\xe5\x86\x85\xe4\xbb\xa3\xe7\xa0\x81");
    html += "<span class=\"mdw-tsep\" aria-hidden=\"true\"></span>";
    btn("zmd-sel-link", "on_format_link", "link", "\xe9\x93\xbe\xe6\x8e\xa5");
    html += "</div>";
    const std::filesystem::path path = preview_path_.parent_path() / "_sel.html";
    auto publish = [&](float used_top) {
        const auto marker = html.find("top:");
        if (marker == std::string::npos) {
            return;
        }
        const auto end = html.find("px", marker);
        if (end == std::string::npos) {
            return;
        }
        html.replace(marker + 4, end - (marker + 4), px(used_top));
        write_file(path, html);
        page().replace_fragment(path.string(), dom::Id::sel_host);
    };
    publish(top);
    // The stored top and the painted bubble differ by the scroller's padding edge. Correct the
    // bubble (`zmd-sel-strip`), not the button inset inside it.
    if (const std::optional<zui::ElementBox> bubble = page().measure("zmd-sel-strip");
        bubble && bubble->laid) {
        const float want = bubble_top(bubble->border.height);
        const float delta = bubble->border.y - want;
        if (std::fabs(delta) > 0.5f) {
            publish(top - delta);
        }
    }
    strip_open_ = true;
}

void Session::sync_paper_view() {
    if (settings_.view() != Settings::View::Live) {
        page().set_text_caret({}, std::nullopt);
        hide_format_strip();
        mark_current_block({});
        return;
    }
    const Editor::SelectionRange range = editor_.selection_range();
    if (!range.valid || range.end <= range.start) {
        hide_format_strip();
        sync_paper_caret();
        return;
    }
    std::string start_id;
    std::string end_id;
    std::size_t start_vis = 0;
    std::size_t end_vis = 0;
    std::size_t start_flat = 0;
    std::size_t end_flat = 0;
    const bool whole = range.start == 0 && range.end >= document_.source().size();
    const bool have_start = paper_point(range.start, start_id, start_vis, start_flat);
    const bool have_end = paper_point(range.end, end_id, end_vis, end_flat);
    float anchor_x = 0.f;
    float anchor_y = 0.f;
    float anchor_h = 16.f;
    float end_y = 0.f;
    float end_h = 16.f;
    if (have_start) {
        page().set_text_caret(start_id, start_vis);
        if (const std::optional<zui::TextCaret> bar = page().text_caret_rect()) {
            anchor_x = bar->x;
            anchor_y = bar->y;
            anchor_h = bar->height;
            end_y = bar->y + bar->height;
            end_h = bar->height;
        }
        // `text_caret_rect` is the font box on the shaped run. The strip uses that line,
        // not the span's border box: an absorbed span's border is not the selected line.
    }
    if (have_end && (end_id != start_id || end_vis != start_vis)) {
        const std::size_t probe = end_vis == 0 ? 0 : end_vis - 1;
        page().set_text_caret(end_id, probe);
        if (const std::optional<zui::TextCaret> bar = page().text_caret_rect()) {
            end_y = bar->y + bar->height;
            end_h = bar->height;
        }
    }
    // Nearest block that ends above the selection. The caret bottom is that line's font box
    // (baseline + descent). This write is collapsed; the range is stored just below it.
    float prev_glyph_bottom = -1.0e9f;
    if (have_start) {
        const std::vector<PaperBlock> rows = paper_blocks();
        std::string prev_id;
        std::size_t prev_vis = 0;
        float prev_edge = -1.0e9f;
        for (const PaperBlock& block : rows) {
            if (block.id == start_id) {
                continue;
            }
            const std::optional<zui::ElementBox> box = page().measure(block.id);
            if (!box || !box->laid) {
                continue;
            }
            const float bottom = box->border.y + box->border.height;
            if (box->border.y <= anchor_y && bottom >= anchor_y) {
                continue;
            }
            if (bottom > anchor_y + 0.5f || bottom <= prev_edge) {
                continue;
            }
            prev_edge = bottom;
            prev_id = block.id;
            prev_vis = block.vis_len;
        }
        if (!prev_id.empty()) {
            page().set_text_caret(prev_id, prev_vis);
            if (const std::optional<zui::TextCaret> bar = page().text_caret_rect()) {
                if (bar->y + bar->height < anchor_y - 0.5f) {
                    prev_glyph_bottom = bar->y + bar->height;
                }
            }
        }
    }
    if (whole) {
        page().set(dom::Id::preview, "select", "0,1000000000");
    } else if (have_start && have_end && start_id == end_id) {
        page().set(start_id, "select",
                   std::to_string(std::min(start_vis, end_vis)) + "," +
                       std::to_string(std::max(start_vis, end_vis)));
    } else if (have_start && have_end) {
        page().set(dom::Id::preview, "select",
                   std::to_string(std::min(start_flat, end_flat)) + "," +
                       std::to_string(std::max(start_flat, end_flat)));
    }
    if (have_start) {
        place_format_strip(anchor_x, anchor_y, anchor_h, end_y, end_h, prev_glyph_bottom);
        mark_current_block(have_end ? end_id : start_id);
    }
}

void Session::refresh_preview() {
    write_preview();
    if (!preview_path_.empty()) {
        // Replace, not append: a `load_fragment` only takes the old nodes the new render still
        // names (`Markdown::render` names blocks by index), so a document that shrank kept the
        // blocks the new render no longer had - deleted paragraphs stayed painted.
        page().replace_fragment(preview_path_.string(), dom::Id::preview);
    }
    sync_paper_view();
}

void Session::focus_source_later() {
    later([this] {
        later([this] { page().focus(editor_.surface()); });
    });
}

void Session::remember_palette_selection() {
    const Editor::SelectionRange range = editor_.selection_range();
    const std::string& source = document_.source();
    if (!range.valid || range.start > range.end || range.end > source.size()) {
        palette_selection_ = {};
        return;
    }
    palette_selection_ = {range.start, range.end,
                          source.substr(range.start, range.end - range.start)};
}

void Session::toggle_palette(Palette::Mode mode) {
    remember_palette_selection();
    palette_.toggle(mode);
}

void Session::center_caret_block_later() {
    // 打字机模式 centres the block being written. Every other write keeps it merely in view - an
    // editor scrolls a caret back from the fold - and only 写作 paints the preview as the page; in
    // 分栏 the preview is a second pane beside the caret's own textarea, and 源码 has none at all.
    if (settings_.typewriter()) {
        scroll_block_of_caret_later(zui::ScrollAlign::Center);
    } else if (settings_.view() == Settings::View::Live) {
        scroll_block_of_caret_later(zui::ScrollAlign::Nearest);
    }
}

void Session::scroll_block_of_caret_later(zui::ScrollAlign align) {
    // A follow the reader overrules: the two frames this waits for are also two frames a wheel can
    // land in, and the scroll the reader made is the one to keep. The offset at the call is the
    // one the caret's own edit would not have moved yet (the re-stamp follows this turn).
    const float queued_top = scroll_top_value();
    later([this, align, queued_top] {
        // Two frames later, like `focus_source_later`: the preview fragment is re-stamped in this
        // turn, and the blocks it makes have no boxes until that frame has laid out.
        later([this, align, queued_top] {
            if (std::fabs(scroll_top_value() - queued_top) > 1.f) {
                return;
            }
            const std::size_t caret = editor_.caret();
            // The preview's blocks carry the source range they came from (`data-beg` / `data-at`,
            // written by `Markdown::render`). The block that starts closest to the caret at or
            // before it owns the caret; `scroll_into_view` does the arithmetic on the scroll
            // offset, and the alignment comes from the caller (centre for 打字机模式 and the find
            // jumps, "just keep it on screen" for the plain write).
            std::string owning;
            long owning_beg = -1;
            const int named = std::max(rendered_.headings, rendered_.blocks);
            for (int i = 0; i < named; ++i) {
                for (const char* prefix : {"mdh", "mdb"}) {
                    const std::string id = std::string(prefix) + std::to_string(i);
                    const std::string beg = page().get(id, "data-beg");
                    if (beg.empty()) {
                        continue;
                    }
                    const long at = std::strtol(beg.c_str(), nullptr, 10);
                    if (at >= 0 && static_cast<std::size_t>(at) <= caret && at > owning_beg) {
                        owning = id;
                        owning_beg = at;
                    }
                }
            }
            if (!owning.empty()) {
                page().scroll_into_view(owning, align);
            }
        });
    });
}

float Session::scroll_top_value() {
    try {
        return std::stof(page().get(dom::Id::scroll, "scroll_top"));
    } catch (...) {
        return 0.f;
    }
}

void Session::sync_reading_position() {
    const std::optional<zui::ElementBox> scroller = page().measure(dom::Id::scroll);
    if (!scroller || !scroller->laid) {
        return;
    }
    const float top = scroll_top_value();
    // The prototype's `syncOutline` takes the last heading whose `offsetTop` is at or before
    // `scrollTop + 64`. A world coordinate answers the same question: a heading's box sits at
    // `scroller.y + offsetTop - scrollTop`, so "offsetTop <= scrollTop + 64" is
    // "box.y <= scroller.y + 64".
    std::size_t active = 0;
    for (int i = 0; i < rendered_.headings; ++i) {
        const std::optional<zui::ElementBox> heading = page().measure("mdh" + std::to_string(i));
        if (!heading || !heading->laid) {
            continue;
        }
        if (heading->border.y <= scroller->border.y + 64.f) {
            active = static_cast<std::size_t>(i);
        }
    }
    panels_.set_current_heading(active);
    const float span = scroller->scroll_height - scroller->client_height;
    panels_.set_progress(span > 0.f ? static_cast<double>(top / span) : 0.0);
}

void Session::scroll_top() {
    panels_.scroll_top();
}

void Session::insert_snippet(std::string_view snippet) {
    editor_.insert_line(snippet);
    sync_from_input();
}

void Session::wrap_selection(std::string_view before, std::string_view after,
                             std::string_view placeholder) {
    editor_.wrap(before, after, placeholder);
    sync_from_input();
}

// ------------------------------------------------------------------ shell

void Session::open_dialog(std::string_view id) {
    // `visible` clears the hidden attribute. The sheet itself is shown by .zmd-open;
    // hiding through `visible` again drops the shell's box.
    page().set(id, "visible", "1");
    page().add_class(id, "zmd-open");
}

void Session::close_dialog(std::string_view id) {
    page().remove_class(id, "zmd-open");
    page().set(id, "visible", "0");
}

void Session::pick_image() {
    const zui::PickPaths picked = app_.host().pick_open();
    if (picked.status != zui::PickStatus::Ok || picked.paths.empty()) {
        return;
    }
    const std::filesystem::path source = std::filesystem::u8path(picked.paths.front());
    const std::string name = source.filename().u8string();
    std::error_code ec;
    std::filesystem::create_directories(asset_dir(), ec);
    ec.clear();
    std::filesystem::copy_file(source, asset_dir() / std::filesystem::u8path(name),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        // The copy did not land: the fields must not name the file the user just picked, or the
        // dialog would insert a picture that is not there.
        return;
    }
    page().set(dom::Id::image_url, "value", asset_rel_dir() + "/" + name);
    page().set(dom::Id::image_alt, "value", source.stem().u8string());
}

void Session::drop_paths(std::string_view path) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path dropped = std::filesystem::u8path(path);
    const std::string ext = util::lower(dropped.extension().string());
    if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
        // Opening a document is the guarded path: a dirty buffer has to ask first, the same way a
        // file row or the picker does.
        request_open(dropped);
        return;
    }
    std::error_code ec;
    if (std::filesystem::is_directory(dropped, ec)) {
        // A folder is not a picture. The browser puts a file chip on the page for one; the native
        // has no chip, and a link to the folder would render broken.
        return;
    }
    const std::string name = dropped.filename().u8string();
    std::filesystem::create_directories(asset_dir(), ec);
    ec.clear();
    std::filesystem::copy_file(dropped, asset_dir() / std::filesystem::u8path(name),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        // The copy did not land, so nothing may point at where it would have been.
        return;
    }
    insert_snippet("![" + dropped.stem().u8string() + "](" + asset_rel_dir() + "/" + name + ")");
}

std::filesystem::path Session::asset_dir() const {
    if (!workspace_.valid()) {
        return {};
    }
    return workspace_.root() / std::filesystem::u8path(asset_rel_dir());
}

std::string Session::asset_rel_dir() const {
    std::string dir = settings_.assets();
    for (char& c : dir) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (dir.rfind("./", 0) == 0) {
        dir.erase(0, 2);
    }
    while (!dir.empty() && dir.back() == '/') {
        dir.pop_back();
    }
    return dir.empty() ? "assets" : dir;
}

void Session::set_blurred(bool blurred) {
    chrome_.set_blurred(blurred);
}

void Session::run_command(std::string_view id) {
    if (id == "view.focus") {
        settings_.set_focus(!settings_.focus());
        save_settings();
    } else if (id == "view.typewriter") {
        settings_.set_typewriter(!settings_.typewriter());
        save_settings();
        center_caret_block_later();
    } else if (id == "view.live") {
        settings_.set_view(Settings::View::Live);
    } else if (id == "view.source") {
        settings_.set_view(Settings::View::Source);
        focus_source_later();
    } else if (id == "view.split") {
        settings_.set_view(Settings::View::Split);
        focus_source_later();
    } else if (id == "view.preview") {
        settings_.set_view(Settings::View::Preview);
    } else if (id == "file.new") {
        new_doc();
    } else if (id == "file.open") {
        open_doc();
    } else if (id == "file.save") {
        save_doc();
    } else if (id == "file.saveAs") {
        save_doc_as();
    } else if (id == "file.exportHtml") {
        export_html();
    } else if (id == "file.welcome") {
        request_welcome();
    } else if (id == "edit.undo") {
        std::string text;
        if (document_.undo(text)) {
            apply_source(std::move(text), false, true);
        }
    } else if (id == "edit.redo") {
        std::string text;
        if (document_.redo(text)) {
            apply_source(std::move(text), false, true);
        }
    } else if (id == "panel.files") {
        panels_.toggle_files();
    } else if (id == "panel.outline") {
        panels_.toggle_outline();
    } else if (id == "settings.open") {
        page().remove_class(dom::Id::app, dom::Class::set_closed);
    } else if (id == "top") {
        scroll_top();
    } else if (id == "format.bold") {
        wrap_selection("**", "**", "粗体");
    } else if (id == "format.italic") {
        wrap_selection("*", "*", "斜体");
    } else if (id == "format.code") {
        wrap_selection("`", "`", "代码");
    } else if (id == "format.mark") {
        wrap_selection("==", "==", "高亮");
    } else if (util::starts_with(id, "theme.")) {
        const std::string_view name = id.substr(6);
        if (name == "light") {
            settings_.set_theme(Settings::Theme::Light);
        } else if (name == "dark") {
            settings_.set_theme(Settings::Theme::Dark);
        } else if (name == "system") {
            settings_.set_theme(Settings::Theme::System);
        } else {
            return;
        }
        save_settings();
    }
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr) {
        return;
    }
    const std::string name = util::lower(key->key);
    const bool shift = key->shift;
    if (!key->ctrl) {
        if (name == "escape") {
            palette_.close();
            find_.close();
            page().add_class(dom::Id::app, dom::Class::set_closed);
            event.prevent_default();
            return;
        }
        // While the palette is open its list takes the arrow keys and Enter - the prototype wires
        // them on the palette input (js/app.js) - before the caret move refresh below.
        if (palette_.is_open()) {
            if (name == "arrowdown") {
                palette_.move(1);
            } else if (name == "arrowup") {
                palette_.move(-1);
            } else if (name == "enter") {
                palette_.pick(palette_.active());
            } else {
                return;
            }
            event.prevent_default();
            return;
        }
        if ((name == "arrowup" || name == "arrowdown") &&
            settings_.view() == Settings::View::Live && source_focused_) {
            // The keyboard proxy is 24px wide, so its own up/down walks a few source bytes and
            // the paper caret slides sideways. Step the paper's visual line instead.
            event.prevent_default();
            step_paper_line(name == "arrowdown" ? 1 : -1, shift);
            later([this] { sync_paper_view(); });
            center_caret_block_later();
            return;
        }
        if (name == "arrowleft" || name == "arrowright" || name == "arrowup" ||
            name == "arrowdown" || name == "home" || name == "end") {
            later([this] { sync_paper_view(); });
            // The caret moves after this dispatch (the text frame lands later); the deferred call
            // projects that caret onto the paper and scrolls its block into view.
            center_caret_block_later();
        } else if ((name == "pageup" || name == "pagedown") &&
                   settings_.view() == Settings::View::Live) {
            // 写作 paints the preview as the page, and its keyboard proxy is a 24px box of its own
            // that the engine's page key scrolls invisibly; the page the reader sees is the paper.
            // A browser's contenteditable scrolls the page for the same key. 源码 and 分栏 show the
            // textarea itself, which the engine scrolls.
            const std::optional<zui::ElementBox> scroller = page().measure(dom::Id::scroll);
            if (scroller && scroller->laid) {
                // A page, less a line's worth of overlap so the reader keeps their place.
                const float dy = std::max(1.f, scroller->border.height * 0.9f) *
                                 (name == "pagedown" ? 1.f : -1.f);
                page().set(dom::Id::scroll, "scroll_top",
                           std::to_string(scroll_top_value() + dy));
            }
        }
        return;
    }
    // The prototype's format / edit keys act only while the writing surface has focus
    // (js/app.js checks `document.activeElement`); the rest of the shortcuts are global.
    if ((name == "b" || name == "i" || name == "e" || name == "z" || name == "y" ||
         (name == "c" && shift)) &&
        !source_focused_) {
        return;
    }
    if (name == "s") {
        save_doc();
    } else if (name == "o") {
        run_command("file.open");
    } else if (name == "n") {
        run_command("file.new");
    } else if (name == "k" && shift) {
        open_dialog(dom::Id::dialog_link);
    } else if (name == "k") {
        toggle_palette(Palette::Mode::Commands);
    } else if (name == "p" && shift) {
        toggle_palette(Palette::Mode::Commands);
    } else if (name == "p") {
        toggle_palette(Palette::Mode::Files);
    } else if (name == "f" && shift) {
        run_command("view.focus");
    } else if (name == "f") {
        find_.open(false);
    } else if (name == "h") {
        find_.open(true);
    } else if (name == "c" && shift) {
        wrap_selection("`", "`", "代码");
    } else if (name == ",") {
        run_command("settings.open");
    } else if (const int digit = accelerator_digit(name); digit != 0) {
        // Ctrl+1..4 switch views and Ctrl+Shift+1..2 pin a panel; with Shift the key arrives as
        // the number row's symbol ("!" for 1), so both spellings mean the same digit.
        if (shift) {
            if (digit == 1) {
                run_command("panel.files");
            } else if (digit == 2) {
                run_command("panel.outline");
            } else {
                return;
            }
        } else if (digit == 1) {
            run_command("view.live");
        } else if (digit == 2) {
            run_command("view.source");
        } else if (digit == 3) {
            run_command("view.split");
        } else {
            run_command("view.preview");
        }
    } else if (name == "b") {
        wrap_selection("**", "**", "粗体");
    } else if (name == "i") {
        wrap_selection("*", "*", "斜体");
    } else if (name == "e") {
        wrap_selection("`", "`", "代码");
    } else if (name == "z") {
        // The palette advertises Ctrl+Z / Ctrl+Y, and the browser build gets them from the
        // textarea's own stack; the engine edits text without one, so the document's history has
        // to answer. Ctrl+Shift+Z is the other spelling of redo Chrome honors.
        run_command(shift ? "edit.redo" : "edit.undo");
    } else if (name == "y") {
        run_command("edit.redo");
    } else if (name == "a") {
        // The engine's own Ctrl+A selects the whole source after this handler. The paper
        // projects that range; it does not rebuild.
        later([this] { sync_paper_view(); });
        return;
    } else {
        return;
    }
    event.prevent_default();
}

// ---------------------------------------------------------------- bindings

void Session::bind_all() {
    bind_files();
    bind_editor();
    bind_chrome();
    bind_palette();
    bind_find();
}

void Session::bind_files() {
    this->bind("on_file_new", [this](zui::Event&) { run_command("file.new"); });
    this->bind("on_file_open", [this](zui::Event&) { run_command("file.open"); });
    this->bind("on_file_save", [this](zui::Event&) { run_command("file.save"); });
    this->bind("on_file_save_as", [this](zui::Event&) { run_command("file.saveAs"); });
    this->bind("on_file_export_html", [this](zui::Event&) { run_command("file.exportHtml"); });
    this->bind("on_file_welcome", [this](zui::Event&) { run_command("file.welcome"); });
    this->bind("on_file_print", [this](zui::Event&) { export_html(); });
    this->bind("on_folder_new", [this](zui::Event&) {
        if (!workspace_.valid()) {
            return;
        }
        // Same directory as 新建文件: the folder the panel is showing.
        std::error_code ec;
        const std::filesystem::path dir = new_document_dir();
        const std::string name = unique_name_in(dir, "\xe6\x96\xb0\xe5\xbb\xba\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9", "");
        std::filesystem::create_directories(dir / std::filesystem::u8path(name), ec);
        publish_files();
    });
    this->bind("on_file_pick", [this](zui::Event& event) {
        const int index = row_index(event.source(), "fl");
        const std::vector<FileEntry>& rows = workspace_.entries();
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) {
            return;
        }
        // The click still owns this stamped row. Restamping the list now would free it.
        const FileEntry row = rows[static_cast<std::size_t>(index)];
        if (row.folder) {
            later([this, name = row.name] {
                if (workspace_.enter(name)) {
                    publish_files();
                }
            });
            return;
        }
        const std::filesystem::path path = workspace_.path_of(row.name);
        later([this, path] { request_open(path); });
    });
    this->bind("on_folder_up", [this](zui::Event&) {
        if (workspace_.up()) {
            publish_files();
        }
    });
    this->bind("on_beforeunload", [this](zui::Event& event) {
        if (!has_unsaved()) {
            return;
        }
        // The close is the thing the dialog has to finish. Saving or discarding
        // then leaves; cancelling keeps this session.
        pending_ = Pending::Quit;
        pending_path_.clear();
        open_dialog(dom::Id::dialog_unsaved);
        event.prevent_default();
    });
    this->bind("on_drop", [this](zui::Event& event) {
        // A dropped document opens; anything else is copied into the assets folder and inserted
        // as a picture, which is what `drop_paths` does for one path.
        const zui::Drop* drop = event.is_drop();
        if (drop == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < drop->count; ++i) {
            drop_paths(drop->path(i));
        }
        // The engine reads this as "the drop was taken" (EVENT.md section 7).
        event.prevent_default();
    });
    this->bind("on_dialog_save", [this](zui::Event&) {
        close_dialog(dom::Id::dialog_unsaved);
        save_doc();
        if (document_.dirty()) {
            // The save did not land (the picker was cancelled). Stay with the text.
            if (pending_ == Pending::Quit) {
                pending_ = Pending::None;
                pending_path_.clear();
            }
            return;
        }
        perform_pending();
    });
    this->bind("on_dialog_discard", [this](zui::Event&) {
        close_dialog(dom::Id::dialog_unsaved);
        if (pending_ == Pending::Quit) {
            // Leave the discarded buffer as it is. Marking it saved would paint
            // a clean dot on text the user just threw away.
            pending_ = Pending::None;
            pending_path_.clear();
            app_.quit();
            return;
        }
        document_.mark_saved();
        apply_document_state();
        perform_pending();
    });
    this->bind("on_dialog_cancel", [this](zui::Event&) {
        close_dialog(dom::Id::dialog_unsaved);
        pending_ = Pending::None;
        pending_path_.clear();
    });
}

/// The writing surface takes clicks and drags only while no sheet covers it: the same gate the
/// paper's own `pointer-events` rule uses.
bool Session::write_surface_ready() {
    const std::string klass = this->page().get(dom::Id::app, "class");
    if (klass.find("set-closed") == std::string::npos ||
        klass.find("pal-closed") == std::string::npos) {
        return false;
    }
    return this->page().get(dom::Id::find, "visible") != "true";
}

std::size_t Session::write_offset_at(float x, float y) {
    // The preview's blocks carry the source range they came from (`data-beg` / `data-at`).
    // A soft-wrapped paragraph is one block and several visual lines: y picks the line, then x
    // maps through that line's share of the source. `measure` answers in pointer CSS px.
    std::size_t at = 0;
    float best = 1.0e9f;
    const auto parse_off = [](const std::string& raw) -> std::size_t {
        if (raw.empty()) {
            return 0;
        }
        try {
            return static_cast<std::size_t>(std::stoul(raw));
        } catch (...) {
            return 0;
        }
    };
    const auto utf8_floor = [](const std::string& text, std::size_t i) {
        if (i >= text.size()) {
            return text.size();
        }
        while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
            --i;
        }
        return i;
    };
    // Both id runs start at zero, so the longer of the two bounds the scan. The earlier walk
    // stopped after two empty index slots and never left the fixed 80, which hid every block of a
    // document longer than that.
    struct Band {
        std::string id;
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
        std::size_t beg = 0;
        std::size_t end = 0;
    };
    std::vector<Band> bands;
    const int named = std::max(rendered_.headings, rendered_.blocks);
    for (int i = 0; i < named; ++i) {
        const std::string ids[] = {"mdh" + std::to_string(i), "mdb" + std::to_string(i)};
        for (const std::string& id : ids) {
            const std::optional<zui::ElementBox> box = this->page().measure(id);
            if (!box || !box->laid) {
                continue;
            }
            // Convert x through the body span the render wraps the line in (`…l`): its box is
            // the text's own width. The block's box is the column's, so a short line - a heading,
            // a list item - put every click far too early in the line.
            const std::optional<zui::ElementBox> body = this->page().measure(id + "l");
            const zui::ElementBox::Rect& block_box = box->border;
            const zui::ElementBox::Rect& text_box =
                body && body->laid && body->border.width > 1.f ? body->border : block_box;
            // The inline body span can be only the first fragment tall. The block grows with
            // every wrapped line, and that is the box a click on the lower line is inside.
            const zui::ElementBox::Rect& b =
                block_box.height > text_box.height + 1.f ? block_box : text_box;
            Band band;
            band.id = id;
            band.x = b.x;
            band.y = b.y;
            band.w = b.width;
            band.h = b.height;
            band.beg = parse_off(this->page().get(id, "data-beg"));
            band.end = parse_off(this->page().get(id, "data-at"));
            bands.push_back(band);
        }
    }
    const Band* picked = nullptr;
    for (const Band& band : bands) {
        // A point inside a block beats the nearest centre. A wrapped paragraph's centre sits
        // between its lines, and a click on the lower line used to lose to that centre's x map.
        const float dist = (y >= band.y && y < band.y + band.h)
                               ? 0.f
                               : std::min(std::fabs(y - band.y), std::fabs(y - (band.y + band.h)));
        if (dist < best) {
            best = dist;
            picked = &band;
        }
    }
    if (picked == nullptr) {
        return std::min(at, document_.source().size());
    }
    // Glyph edge on the line under the point, then the source byte that emitted that edge.
    // A short last line maps a click past its period to the offset after that period. A
    // fraction of the column width landed near the start of the paragraph ("左边").
    const std::optional<std::size_t> vis = page().text_offset_at(picked->id, x, y);
    const auto mapped = hit_sources_.find(picked->id);
    if (vis && mapped != hit_sources_.end()) {
        const VisibleSource& src = mapped->second;
        if (src.at.empty() || *vis >= src.at.size()) {
            at = src.after;
        } else {
            at = src.at[*vis];
        }
        at = utf8_floor(document_.source(), at);
        return std::min(at, document_.source().size());
    }
    const float t = picked->w <= 1.f
                        ? 1.f
                        : std::max(0.f, std::min(1.f, (x - picked->x) / picked->w));
    const std::size_t span = picked->end > picked->beg ? picked->end - picked->beg : 0;
    at = picked->beg + static_cast<std::size_t>(t * static_cast<float>(span));
    at = utf8_floor(document_.source(), at);
    return std::min(at, document_.source().size());
}

void Session::step_paper_line(int dir, bool shift) {
    const std::optional<zui::TextCaret> caret = page().text_caret_rect();
    if (!caret || dir == 0) {
        return;
    }
    const float x = caret->x + std::min(1.f, caret->width * 0.5f);
    const std::size_t cur = editor_.caret();
    std::size_t landed = cur;
    // Probe past this line's box. A sample a few pixels off the caret centre is still the
    // same glyphs, so the first offset that differs is one character sideways — the motion
    // the 24px proxy produced. The next paper line is the first offset that moves.
    const float line = std::max(caret->height, 16.f);
    const float y_edge = dir > 0 ? caret->y + caret->height : caret->y;
    for (float extra = line * 0.35f; extra <= line * 3.f; extra += 4.f) {
        const float y = y_edge + static_cast<float>(dir) * extra;
        const std::size_t at = write_offset_at(x, y);
        if ((dir > 0 && at > cur) || (dir < 0 && at < cur)) {
            landed = at;
            break;
        }
    }
    if (shift) {
        const Editor::SelectionRange range = editor_.selection_range();
        const std::size_t anchor =
            range.valid && range.end > range.start ? (range.start == cur ? range.end : range.start)
                                                   : cur;
        editor_.set_selection(std::min(anchor, landed), std::max(anchor, landed));
    } else {
        editor_.set_selection(landed, landed);
    }
}

std::size_t Session::write_drag_offset_at(float x, float y) {
    // The paper's port, in the same world space `write_offset_at` measures in. A pointer past the
    // fold names the last visible line; the tick scrolls, and the mapping follows.
    const std::optional<zui::ElementBox> port = page().measure(dom::Id::scroll);
    if (port && port->laid && port->client_height > 4.f && port->border.width > 4.f) {
        const float left = port->border.x + 2.f;
        const float right = port->border.x + port->border.width - 2.f;
        const float top = port->border.y + 2.f;
        const float bottom = port->border.y + port->client_height - 2.f;
        x = std::max(left, std::min(x, right));
        y = std::max(top, std::min(y, bottom));
    }
    return write_offset_at(x, y);
}

void Session::start_write_scroll() {
    if (write_scroll_ == 0) {
        write_scroll_ = page().set_interval([this] { tick_write_scroll(); }, 40);
    }
}

void Session::stop_write_scroll() {
    if (write_scroll_ != 0) {
        page().clear_timer(write_scroll_);
        write_scroll_ = 0;
    }
}

void Session::tick_write_scroll() {
    if (!write_drag_.active || !write_surface_ready()) {
        stop_write_scroll();
        return;
    }
    const std::optional<zui::ElementBox> port = page().measure(dom::Id::scroll);
    if (!port || !port->laid || port->client_height <= 4.f) {
        return;
    }
    // A parked pointer keeps the paper moving, and a browser scrolls faster the further out it is.
    const float top = port->border.y;
    const float bottom = port->border.y + port->client_height;
    float dy = 0.f;
    if (write_drag_.y < top) {
        dy = -std::min(40.f, 4.f + (top - write_drag_.y) * 0.25f);
    } else if (write_drag_.y > bottom) {
        dy = std::min(40.f, 4.f + (write_drag_.y - bottom) * 0.25f);
    }
    if (dy == 0.f) {
        return;
    }
    this->page().set(dom::Id::scroll, "scroll_top",
                     std::to_string(scroll_top_value() + dy));
    const std::size_t at = write_drag_offset_at(write_drag_.x, write_drag_.y);
    if (at != write_drag_.focus) {
        write_drag_.focus = at;
        write_drag_.dragged = true;
        editor_.set_selection(write_drag_.anchor, at);
        // `set_selection` writes the textarea, and that replaces Document::selection.
        // Paint reads the one range after this handler returns. Project the paper now.
        // A defer runs at the next frame's start, and the next move overwrites it
        // before paint, so the highlight stayed invisible until mouse-up.
        sync_paper_view();
    }
}

void Session::bind_editor() {
    this->bind("on_source", [this](zui::Event&) {
        sync_from_input();
    });
    // The prototype's format / edit shortcuts act only while the writing surface has focus
    // (js/app.js checks `document.activeElement`). The native writing surface is the source
    // textarea, and these two events are what say whether it holds the focus.
    this->bind("on_source_focus", [this](zui::Event&) { source_focused_ = true; });
    this->bind("on_source_blur", [this](zui::Event&) { source_focused_ = false; });
    this->bind("on_write_click", [this](zui::Event& event) {
        if (!write_surface_ready()) {
            return;
        }
        // A click that ended a drag only hands the keyboard back: the drag chose the selection,
        // and collapsing it here would undo the gesture.
        const bool dragged = write_drag_.dragged;
        write_drag_.dragged = false;
        this->page().focus(editor_.surface());
        const zui::Pointer* ptr = event.is_pointer();
        if (dragged || ptr == nullptr) {
            return;
        }
        const std::size_t at = write_offset_at(ptr->x, ptr->y);
        // Shift+click extends what is selected instead of replacing it, the way it does in a text
        // control: the caret moves to the click and the range's other end stays the anchor. A
        // collapsed caret anchors itself, which is the first shift+click of a run.
        if (ptr->shift) {
            const Editor::SelectionRange range = editor_.selection_range();
            const std::size_t caret = editor_.caret();
            const std::size_t anchor =
                range.valid && range.end > range.start
                    ? (range.start == caret ? range.end : range.start)
                    : caret;
            editor_.set_selection(anchor, at);
            later([this] { sync_paper_view(); });
            return;
        }
        // A multi-click takes more than the point under the pointer: the paper maps a click onto a
        // source offset, so the word (double) and the line (triple) come from the source text.
        std::size_t from = at;
        std::size_t to = at;
        if (ptr->count >= 3) {
            source_line_at(document_.source(), at, from, to);
        } else if (ptr->count == 2) {
            source_word_at(document_.source(), at, from, to);
        }
        editor_.set_selection(from, to);
        later([this] { sync_paper_view(); });
    });
    // The paper is not an editing host. A drag maps glyph edges through the vis table onto
    // source offsets, writes that range on the parked textarea, and projects it as Document
    // Selection. The preview tree stays put.
    this->bind("on_write_press", [this](zui::Event& event) {
        if (!write_surface_ready()) {
            return;
        }
        const zui::Pointer* ptr = event.is_pointer();
        if (ptr == nullptr || ptr->button != "left") {
            return;
        }
        write_drag_.active = true;
        write_drag_.dragged = false;
        if (ptr->shift) {
            // Shift keeps the anchor: the press must not collapse the very range the click (or the
            // drag) is about to extend, so it anchors where the caret's far end already is.
            const Editor::SelectionRange range = editor_.selection_range();
            const std::size_t caret = editor_.caret();
            write_drag_.anchor =
                range.valid && range.end > range.start
                    ? (range.start == caret ? range.end : range.start)
                    : caret;
        } else {
            write_drag_.anchor = write_drag_offset_at(ptr->x, ptr->y);
            editor_.set_selection(write_drag_.anchor, write_drag_.anchor);
        }
        write_drag_.x = ptr->x;
        write_drag_.y = ptr->y;
        write_drag_.focus = write_drag_.anchor;
    });
    this->bind("on_write_drag", [this](zui::Event& event) {
        const zui::Pointer* ptr = event.is_pointer();
        if (!write_drag_.active || ptr == nullptr) {
            return;
        }
        // Only a pressed pointer extends the selection: a hover that follows a release outside the
        // paper must not keep dragging it.
        if (ptr->buttons == 0) {
            write_drag_.active = false;
            stop_write_scroll();
            return;
        }
        write_drag_.x = ptr->x;
        write_drag_.y = ptr->y;
        start_write_scroll();
        const std::size_t at = write_drag_offset_at(ptr->x, ptr->y);
        if (at == write_drag_.focus) {
            return;
        }
        write_drag_.focus = at;
        write_drag_.dragged = true;
        editor_.set_selection(write_drag_.anchor, at);
        // Same turn as the textarea write. See `tick_write_scroll`.
        sync_paper_view();
    });
    this->bind("on_write_release", [this](zui::Event&) {
        if (!write_drag_.active) {
            return;
        }
        write_drag_.active = false;
        stop_write_scroll();
        if (write_drag_.dragged) {
            later([this] { sync_paper_view(); });
        }
    });
    this->bind("on_beforeinput", [this](zui::Event& event) {
        if (editor_.apply_block_rule()) {
            sync_from_input();
            event.prevent_default();
        }
    });
    this->bind("on_format_bold", [this](zui::Event&) { wrap_selection("**", "**", "粗体"); });
    this->bind("on_format_italic", [this](zui::Event&) { wrap_selection("*", "*", "斜体"); });
    this->bind("on_format_strike", [this](zui::Event&) { wrap_selection("~~", "~~", "删除线"); });
    this->bind("on_format_code", [this](zui::Event&) { wrap_selection("`", "`", "代码"); });
    this->bind("on_format_mark", [this](zui::Event&) { wrap_selection("==", "==", "高亮"); });
    // The strip sits over the paper. Its pointerdown must not start a write-drag (that would
    // collapse the range the button is about to wrap).
    this->bind("on_sel_toolbar", [this](zui::Event& event) { event.prevent_default(); });
    this->bind("on_format_h1", [this](zui::Event&) { editor_.set_heading(1); sync_from_input(); });
    this->bind("on_format_h2", [this](zui::Event&) { editor_.set_heading(2); sync_from_input(); });
    this->bind("on_format_h3", [this](zui::Event&) { editor_.set_heading(3); sync_from_input(); });
    this->bind("on_format_ul", [this](zui::Event&) { editor_.toggle_line_prefix("- "); sync_from_input(); });
    this->bind("on_format_ol", [this](zui::Event&) { editor_.toggle_line_prefix("1. "); sync_from_input(); });
    this->bind("on_format_task", [this](zui::Event&) { editor_.toggle_task(); sync_from_input(); });
    this->bind("on_format_quote", [this](zui::Event&) { editor_.toggle_line_prefix("> "); sync_from_input(); });
    this->bind("on_format_codeblock", [this](zui::Event&) { insert_snippet("```"); });
    this->bind("on_format_table", [this](zui::Event&) {
        insert_snippet("| 元素 | 语法 | 快捷键 |\n| --- | --- | --- |\n|  |  |  |");
    });
    this->bind("on_format_hr", [this](zui::Event&) { insert_snippet("---"); });
    this->bind("on_format_link", [this](zui::Event&) { open_dialog(dom::Id::dialog_link); });
    this->bind("on_format_image", [this](zui::Event&) { open_dialog(dom::Id::dialog_image); });
    this->bind("on_format_pick_image", [this](zui::Event&) { pick_image(); });
    this->bind("on_format_apply_link", [this](zui::Event&) {
        const std::string text = this->page().get(dom::Id::link_text, "value");
        const std::string url = this->page().get(dom::Id::link_url, "value");
        close_dialog(dom::Id::dialog_link);
        if (!url.empty()) {
            editor_.insert("[" + (text.empty() ? url : text) + "](" + url + ")");
            sync_from_input();
        }
    });
    this->bind("on_format_apply_image", [this](zui::Event&) {
        const std::string alt = this->page().get(dom::Id::image_alt, "value");
        const std::string url = this->page().get(dom::Id::image_url, "value");
        close_dialog(dom::Id::dialog_image);
        if (!url.empty()) {
            editor_.insert("![" + alt + "](" + url + ")");
            sync_from_input();
        }
    });
    this->bind("on_edit_undo", [this](zui::Event&) { run_command("edit.undo"); });
    this->bind("on_edit_redo", [this](zui::Event&) { run_command("edit.redo"); });
}

void Session::bind_chrome() {
    this->bind("on_accel", [this](zui::Event& event) { on_accel(event); });
    // The prototype dims the sheet on window blur and undims it on focus (js/app.js). The host
    // folds focus loss, occlusion and minimize into one hidden state, so the event asks
    // `Window::visible()` where it went.
    this->bind("on_visibility", [this](zui::Event&) {
        set_blurred(!app_.window().visible());
    });
    this->bind("on_view_focus", [this](zui::Event&) { run_command("view.focus"); });
    this->bind("on_view_typewriter", [this](zui::Event&) { run_command("view.typewriter"); });
    this->bind("on_view_live", [this](zui::Event&) { run_command("view.live"); });
    this->bind("on_view_source", [this](zui::Event&) { run_command("view.source"); });
    this->bind("on_view_split", [this](zui::Event&) { run_command("view.split"); });
    this->bind("on_view_preview", [this](zui::Event&) { run_command("view.preview"); });
    this->bind("on_view_font_serif", [this](zui::Event&) {
        settings_.set_font(Settings::Font::Serif);
        save_settings();
    });
    this->bind("on_view_font_sans", [this](zui::Event&) {
        settings_.set_font(Settings::Font::Sans);
        save_settings();
    });
    this->bind("on_view_theme_light", [this](zui::Event&) { run_command("theme.light"); });
    this->bind("on_view_theme_dark", [this](zui::Event&) { run_command("theme.dark"); });
    this->bind("on_view_theme_system", [this](zui::Event&) { run_command("theme.system"); });
    this->bind("on_size_slide", [this](zui::Event&) {
        settings_.set_font_px(slider_number(dom::Id::range_size, false));
        save_settings();
    });
    this->bind("on_width_slide", [this](zui::Event&) {
        settings_.set_measure(slider_number(dom::Id::range_width, false));
        save_settings();
    });
    this->bind("on_line_slide", [this](zui::Event&) {
        settings_.set_line_height(slider_number(dom::Id::range_line, true));
        save_settings();
    });
    this->bind("on_radius_slide", [this](zui::Event&) {
        settings_.set_radius(slider_number(dom::Id::range_radius, false));
        save_settings();
    });
    this->bind("on_accent_pick", [this](zui::Event&) {
        settings_.set_accent(this->page().get(dom::Id::accent, "value"));
        save_settings();
    });
    this->bind("on_ws_edit", [this](zui::Event&) {
        const std::string path = this->page().get(dom::Id::ws_path, "value");
        std::error_code ec;
        const std::filesystem::path root = std::filesystem::u8path(path);
        if (!std::filesystem::is_directory(root, ec)) {
            return;
        }
        settings_.set_workspace_path(path);
        workspace_.set_root(root);
        publish_files();
        save_settings();
    });
    this->bind("on_assets_edit", [this](zui::Event&) {
        settings_.set_assets(this->page().get(dom::Id::assets_dir, "value"));
        save_settings();
    });
    this->bind("on_autosave_toggle", [this](zui::Event&) {
        // The engine flips checked before the click handler runs.
        settings_.set_autosave(this->page().get(dom::Id::sw_autosave, "checked") == "true");
        save_settings();
    });
    this->bind("on_focus_sw", [this](zui::Event&) {
        settings_.set_focus(!settings_.focus());
        save_settings();
    });
    this->bind("on_new_in_root", [this](zui::Event&) {
        settings_.set_new_in_root(true);
        save_settings();
    });
    this->bind("on_new_in_here", [this](zui::Event&) {
        settings_.set_new_in_root(false);
        save_settings();
    });
    this->bind("on_settings_open", [this](zui::Event&) {
        this->page().remove_class(dom::Id::app, dom::Class::set_closed);
    });
    this->bind("on_settings_close", [this](zui::Event&) {
        this->page().add_class(dom::Id::app, dom::Class::set_closed);
    });
    this->bind("on_set_type", [this](zui::Event&) { settings_.show_pane("type"); });
    this->bind("on_set_look", [this](zui::Event&) { settings_.show_pane("look"); });
    this->bind("on_set_file", [this](zui::Event&) { settings_.show_pane("file"); });
    this->bind("on_set_export", [this](zui::Event&) { settings_.show_pane("export"); });
    this->bind("on_set_keys", [this](zui::Event&) { settings_.show_pane("keys"); });
    this->bind("on_pin_files", [this](zui::Event&) { panels_.toggle_files(); });
    this->bind("on_pin_outline", [this](zui::Event&) { panels_.toggle_outline(); });
    this->bind("on_scroll_top", [this](zui::Event&) { scroll_top(); });
    this->bind("on_doc_scroll", [this](zui::Event&) { sync_reading_position(); });
    this->bind("on_heading", [this](zui::Event& event) {
        const int index = row_index(event.source(), "hd");
        if (index >= 0) {
            panels_.select_heading(static_cast<std::size_t>(index));
        }
    });
}

void Session::bind_palette() {
    this->bind("on_pal_open", [this](zui::Event&) { toggle_palette(Palette::Mode::Commands); });
    this->bind("on_pal_close", [this](zui::Event&) { palette_.close(); });
    this->bind("on_pal_input", [this](zui::Event&) {
        palette_.set_query(this->page().get(dom::Id::pal_input, "value"));
    });
    this->bind("on_pal_pick", [this](zui::Event& event) {
        const int index = row_index(event.source(), "cm");
        if (index >= 0) {
            palette_.pick(static_cast<std::size_t>(index));
        }
    });
}

void Session::bind_find() {
    this->bind("on_edit_find", [this](zui::Event&) { find_.open(false); });
    this->bind("on_edit_replace", [this](zui::Event&) { find_.open(true); });
    this->bind("on_find_input", [this](zui::Event&) {
        find_.set_query(this->page().get(dom::Id::find_input, "value"));
        later([this] { refresh_preview(); });
        if (find_.matches() > 0) {
            scroll_block_of_caret_later();
        }
    });
    this->bind("on_find_next", [this](zui::Event&) {
        if (find_.next()) {
            // A jump moves the caret onto the hit and selects it. Marks are already in the paper.
            later([this] { sync_paper_view(); });
            scroll_block_of_caret_later();
        }
    });
    this->bind("on_find_prev", [this](zui::Event&) {
        if (find_.previous()) {
            later([this] { sync_paper_view(); });
            scroll_block_of_caret_later();
        }
    });
    this->bind("on_find_replace", [this](zui::Event&) { find_.replace_one(); });
    this->bind("on_find_replace_all", [this](zui::Event&) { find_.replace_all(); });
    this->bind("on_find_close", [this](zui::Event&) {
        find_.close();
        later([this] { refresh_preview(); });
    });
    this->bind("on_dialog_close", [this](zui::Event&) {
        close_dialog(dom::Id::dialog_link);
        close_dialog(dom::Id::dialog_image);
    });
}

void Session::build_commands() {
    palette_.set_commands(command_list());
}

std::vector<Command> Session::command_list() const {
    return {
        {"view.focus", "聚焦模式", "Ctrl+Shift+F", "align-left"},
        {"view.typewriter", "打字机模式", "", "type"},
        {"settings.open", "打开设置", "", "settings"},
        {"theme.light", "主题：浅色", "", "sun"},
        {"theme.dark", "主题：深色", "", "moon"},
        {"theme.system", "主题：跟随系统", "", "sun"},
        {"view.live", "视图：写作", "Ctrl+1", "columns-2"},
        {"view.source", "视图：源码", "Ctrl+2", "code"},
        {"view.split", "视图：分栏", "Ctrl+3", "columns-2"},
        {"view.preview", "视图：预览", "Ctrl+4", "book"},
        {"panel.files", "固定 / 收起文件面板", "Ctrl+Shift+1", "pin"},
        {"panel.outline", "固定 / 收起目录面板", "Ctrl+Shift+2", "pin"},
        {"top", "回到顶部", "", "arrow-up"},
        {"format.bold", "加粗", "Ctrl+B", "bold"},
        {"format.italic", "斜体", "Ctrl+I", "italic"},
        {"format.code", "行内代码", "Ctrl+Shift+C", "code"},
        {"format.mark", "高亮", "", "highlighter"},
        {"file.new", "新建文档", "Ctrl+N", "file-plus"},
        {"file.open", "打开文档", "Ctrl+O", "folder-open"},
        {"file.save", "保存", "Ctrl+S", "download"},
        {"file.saveAs", "另存为", "", "download"},
        {"file.exportHtml", "导出 HTML", "", "download"},
        {"file.welcome", "打开欢迎稿", "", "file-text"},
        {"edit.undo", "撤销", "Ctrl+Z", "arrow-up"},
        {"edit.redo", "重做", "Ctrl+Y", "arrow-up"},
    };
}

std::vector<Command> Session::file_list() const {
    std::vector<Command> out;
    for (const FileEntry& entry : workspace_.entries()) {
        if (entry.folder) {
            continue;
        }
        out.push_back({entry.name, entry.name, "", "file-text"});
    }
    return out;
}

std::filesystem::path Session::scratch_dir() const {
    return workspace_.valid() ? workspace_.root() : std::filesystem::current_path();
}

void Session::publish_files() {
    workspace_.rescan();
    std::vector<FileEntry> rows = workspace_.entries();
    for (FileEntry& row : rows) {
        row.active = !row.folder && row.name == document_.title();
    }
    files_.set_rows(std::move(rows));
    page().set_model(dom::Id::files, files_);
    palette_.set_files(file_list());
    // The panel walks one folder at a time: its title is the folder it shows, and 返回上一级 is
    // only offered while there is one.
    page().set(dom::Id::folder_title, "text", workspace_.folder_name());
    page().set(dom::Id::folder_up, "visible", workspace_.folder().empty() ? "0" : "1");
}

std::filesystem::path Session::settings_file() const {
    const std::string dir = app_.host().user_data_dir();
    if (dir.empty()) {
        return {};
    }
    return std::filesystem::u8path(dir) / "settings.txt";
}

void Session::load_settings() {
    const std::filesystem::path path = settings_file();
    if (path.empty()) {
        return;
    }
    settings_.restore(read_file(path));
}

void Session::save_settings() {
    const std::filesystem::path path = settings_file();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    write_file(path, settings_.serialize());
}

int Session::slider_number(std::string_view id, bool line) {
    const std::string text = page().get(id, "value");
    if (text.empty()) {
        return 0;
    }
    const double value = std::strtod(text.c_str(), nullptr);
    // The line-height slider speaks 1.40..2.20. Stored steps are percent.
    if (line && value < 10.0) {
        return static_cast<int>(std::lround(value * 100.0));
    }
    return static_cast<int>(std::lround(value));
}

std::string welcome_text(const std::filesystem::path& dir) {
    const std::filesystem::path sample = dir / "samples" / "welcome.md";
    std::string text = read_file(sample);
    if (!text.empty()) {
        return text;
    }
    return "# 欢迎使用小竹 Markdown\n\n"
           "左侧悬停滑出文件树，右侧悬停滑出目录；Ctrl+K 打开命令面板。\n\n"
           "## 直接写\n\n"
           "- 行首打 `# ` 变成标题\n"
           "- 行首打 `- ` 变成列表\n"
           "- 选中文字后用顶部格式条\n";
}

}  // namespace zmd



