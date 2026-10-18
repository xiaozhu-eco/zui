#include "panels.h"

#include <cmath>

#include "dom.h"

namespace zmd {

Panels::Panels(zui::Document& page) : page_(page), later_(page) {}

void Panels::set_files_pinned(bool on) {
    files_pinned_ = on;
    if (on) {
        page_.add_class(dom::Id::window, dom::Class::pin_left);
    } else {
        page_.remove_class(dom::Id::window, dom::Class::pin_left);
    }
}

void Panels::set_outline_pinned(bool on) {
    outline_pinned_ = on;
    if (on) {
        page_.add_class(dom::Id::window, dom::Class::pin_right);
    } else {
        page_.remove_class(dom::Id::window, dom::Class::pin_right);
    }
}

void Panels::toggle_files() {
    set_files_pinned(!files_pinned_);
}

void Panels::toggle_outline() {
    set_outline_pinned(!outline_pinned_);
}

void Panels::set_outline(std::vector<Heading> rows) {
    outline_.set_rows(std::move(rows));
    page_.set_model(dom::Id::outline, outline_);
}

void Panels::set_current_heading(std::size_t row) {
    if (row == outline_.active() && outline_.row_count() > 0) {
        return;
    }
    outline_.set_active(row);
    // The click that selected the row is still on that button. Stamping now would
    // free it under the handler, so the class lands on the next frame.
    later_.run([this] { page_.set_model(dom::Id::outline, outline_); });
}

void Panels::select_heading(std::size_t row) {
    outline_.set_active(row);
    later_.run([this] { page_.set_model(dom::Id::outline, outline_); });
    page_.scroll_into_view("mdh" + std::to_string(row), zui::ScrollAlign::Start);
}

void Panels::set_progress(double ratio) {
    const double clamped = ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);
    const int step = static_cast<int>(std::lround(clamped * 20.0)) * 5;
    if (step == progress_step_) {
        return;
    }
    if (progress_step_ >= 0) {
        page_.remove_class(dom::Id::outline_bar, "prog" + std::to_string(progress_step_));
    }
    page_.add_class(dom::Id::outline_bar, "prog" + std::to_string(step));
    progress_step_ = step;
}

void Panels::scroll_top() {
    page_.set(dom::Id::scroll, "scroll_top", "0");
    if (outline_.row_count() > 0) {
        set_current_heading(0);
    }
}

}  // namespace zmd

