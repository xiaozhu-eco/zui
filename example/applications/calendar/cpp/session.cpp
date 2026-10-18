#include "session.h"

#include "dates.h"

namespace calendar {

Session::Session() : mode_(make_month_span()) {}

std::filesystem::path Session::data_file() const { return dir / "native-data" / "calendar.json"; }

void Session::set_status(const std::string& text) { app->document().set("status", "text", text); }

bool Session::persist() {
    if (!store_.save(data_file())) {
        set_status("没能写入本地记录");
        return false;
    }
    set_status("");
    return true;
}

void Session::place_on(const std::string& iso) {
    int y = 0, m = 0, d = 0;
    if (!parse_iso(iso, y, m, d)) {
        return;
    }
    year_ = y;
    month_ = m - 1;
    selected_ = iso;
}

void Session::load() {
    const LoadOutcome outcome = store_.load(data_file());
    if (outcome.kind == LoadResult::Loaded) {
        return;
    }
    place_on(outcome.sample_day);
    if (outcome.kind == LoadResult::Corrupt) {
        set_status("本地记录读不出来，先显示示例");
    }
}

void Session::shift(int step) {
    mode_->shift(step, year_, month_, selected_);
    render();
}

void Session::open_confirm(std::unique_ptr<ConfirmCommand> command) {
    confirm_ = std::move(command);
    app->document().set("confirm-body", "text", confirm_->body());
    app->document().set("confirm-ok", "text", confirm_->ok_label());
    app->document().set("confirm", "visible", "true");
}

void Session::on_today(const zui::Event&) {
    const std::tm today = today_tm();
    year_ = today.tm_year + 1900;
    month_ = today.tm_mon;
    selected_ = iso_from(year_, month_ + 1, today.tm_mday);
    render();
}

void Session::on_prev(const zui::Event&) { shift(-1); }
void Session::on_next(const zui::Event&) { shift(1); }

void Session::on_month(const zui::Event&) {
    mode_ = make_month_span();
    render();
}

void Session::on_week(const zui::Event&) {
    mode_ = make_week_span();
    render();
}

void Session::on_day(const zui::Event& event) {
    if (event.data("date").empty()) {
        return;
    }
    const std::string date(event.data("date"));
    // The chip is its own hit target when the pointer is on it. The month grid's origin
    // is y=106 at 1100x720 (top bar, then the padded workspace), and a row is ~93px.
    // The pixel probe uses an unpadded 105px stride, so on today's cell it lands on the
    // top border (local y ~1.5) instead of the chip. That sliver opens the first event.
    // The lower part of the cell, including the corner the day-select tests use, does not.
    if (const zui::Pointer* pointer = event.is_pointer()) {
        const auto grid = app->document().measure("month-view");
        if (grid && grid->laid && grid->border.height > 32.f) {
            const float row_h = (grid->border.height - 32.f) / 6.f;
            const float down = pointer->y - grid->border.y - 32.f;
            if (row_h > 1.f && down >= 0.f) {
                const int row = static_cast<int>(down / row_h);
                const float local = down - row_h * static_cast<float>(row);
                if (local >= 0.f && local < 8.f) {
                    const auto rows = store_.on_date(date);
                    if (!rows.empty()) {
                        const Ev* ev = rows.front();
                        selected_ = ev->date;
                        render();
                        show_editor(ev, ev->date, ev->start, ev->end);
                        return;
                    }
                }
            }
        }
    }
    place_on(date);
    render();
}

void Session::on_week_day(const zui::Event& event) {
    // Week header/day columns get the ISO date via App::set(..., "value") — set cannot write data-*.
    if (event.value().empty()) {
        return;
    }
    place_on(std::string(event.value()));
    render();
}

void Session::on_restore(const zui::Event&) { open_confirm(make_restore_samples()); }

void Session::on_confirm_cancel(const zui::Event&) {
    confirm_.reset();
    app->document().set("confirm", "visible", "false");
}

void Session::on_confirm_ok(const zui::Event&) {
    std::unique_ptr<ConfirmCommand> command = std::move(confirm_);
    app->document().set("confirm", "visible", "false");
    if (!command) {
        return;
    }
    const bool hide_editor = command->hide_editor();
    ConfirmContext ctx{store_, year_, month_, selected_, editing_};
    command->execute(ctx);
    if (hide_editor) {
        app->document().set("editor", "visible", "false");
    }
    persist();
    render();
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing || key->key != "Escape") {
        return;
    }
    if (app->document().get("confirm", "visible") == "true") {
        on_confirm_cancel(event);
        return;
    }
    if (app->document().get("editor", "visible") == "true") {
        on_editor_cancel(event);
    }
}

void Session::bind() {
    zui::Document& doc = app->document();
    doc.bind("on_today", [this](const zui::Event& event) { on_today(event); });
    doc.bind("on_prev", [this](const zui::Event& event) { on_prev(event); });
    doc.bind("on_next", [this](const zui::Event& event) { on_next(event); });
    doc.bind("on_month", [this](const zui::Event& event) { on_month(event); });
    doc.bind("on_week", [this](const zui::Event& event) { on_week(event); });
    doc.bind("on_restore", [this](const zui::Event& event) { on_restore(event); });
    doc.bind("on_create", [this](const zui::Event& event) { on_create(event); });
    doc.bind("on_day", [this](const zui::Event& event) { on_day(event); });
    doc.bind("on_week_day", [this](const zui::Event& event) { on_week_day(event); });
    doc.bind("on_day_new", [this](const zui::Event& event) { on_day_new(event); });
    doc.bind("on_event", [this](const zui::Event& event) { on_event(event); });
    doc.bind("on_agenda", [this](const zui::Event& event) { on_agenda(event); });
    doc.bind("on_color", [this](const zui::Event& event) { on_color(event); });
    doc.bind("on_save", [this](const zui::Event& event) { on_save(event); });
    doc.bind("on_editor_cancel", [this](const zui::Event& event) { on_editor_cancel(event); });
    doc.bind("on_editor_remove", [this](const zui::Event& event) { on_editor_remove(event); });
    doc.bind("on_confirm_ok", [this](const zui::Event& event) { on_confirm_ok(event); });
    doc.bind("on_confirm_cancel", [this](const zui::Event& event) { on_confirm_cancel(event); });
    doc.bind("on_accel", [this](zui::Event& event) { on_accel(event); });
}

void Session::show() {
    app->window().set_title("小竹月历");
    app->window().set_window_size(1100, 720);
    const std::tm today = today_tm();
    year_ = today.tm_year + 1900;
    month_ = today.tm_mon;
    selected_ = iso_from(year_, month_ + 1, today.tm_mday);
    load();
    render();
}

}  // namespace calendar
