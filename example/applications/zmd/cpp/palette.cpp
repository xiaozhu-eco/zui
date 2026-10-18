#include "palette.h"

#include "dom.h"

namespace zmd {

Palette::Palette(zui::Document& page, Runner runner)
    : page_(page), later_(page), runner_(std::move(runner)) {}

void Palette::set_commands(std::vector<Command> commands) {
    commands_ = std::move(commands);
}

void Palette::set_files(std::vector<Command> files) {
    files_ = std::move(files);
}

void Palette::open(Mode mode) {
    mode_ = mode;
    active_ = 0;
    open_ = true;
    page_.remove_class(dom::Id::app, dom::Class::pal_closed);
    page_.set(dom::Id::pal_input, "value", "");
    // The prototype's openPalette spells the hint for the mode it opens (js/app.js): the file
    // list asks for a file name, the command list for a command.
    page_.set(dom::Id::pal_input, "placeholder",
              mode == Mode::Files ? "\xe8\xbe\x93\xe5\x85\xa5\xe6\x96\x87\xe4\xbb\xb6\xe5\x90\x8d"
                                    "\xe2\x80\xa6"
                                  : "\xe8\xbe\x93\xe5\x85\xa5\xe5\x91\xbd\xe4\xbb\xa4\xe2\x80\xa6");
    model_.set_items(mode == Mode::Commands ? commands_ : files_);
    model_.filter("");
    render();
    // The prototype defers this focus to the next tick; the native has to defer it a frame, or
    // the computed `display: none` from `pal-closed` still blocks `is_focusable`.
    later_.run([this] { page_.focus(dom::Id::pal_input); });
}

void Palette::close() {
    open_ = false;
    page_.add_class(dom::Id::app, dom::Class::pal_closed);
    page_.blur();
}

void Palette::toggle(Mode mode) {
    if (open_ && mode_ == mode) {
        close();
        return;
    }
    open(mode);
}

void Palette::set_query(std::string_view query) {
    model_.filter(query);
    active_ = 0;
    render();
}

void Palette::move(int delta) {
    const int count = static_cast<int>(model_.visible_count());
    if (count <= 0) {
        return;
    }
    int next = (static_cast<int>(active_) + delta + count) % count;
    active_ = static_cast<std::size_t>(next);
    render();
}

void Palette::pick(std::size_t row) {
    const Command* item = model_.at(row);
    if (item == nullptr) {
        return;
    }
    const std::string id = item->id;
    close();
    if (runner_) {
        runner_(id);
    }
}

void Palette::render() {
    model_.set_active(active_);
    page_.set_model(dom::Id::pal_list, model_);
    page_.set(dom::Id::pal_empty, "visible", model_.row_count() == 0 ? "1" : "0");
}

}  // namespace zmd
