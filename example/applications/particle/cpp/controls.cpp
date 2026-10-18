#include "session.h"

#include "rows.h"
#include "shader.h"

#include <cmath>

namespace particle {

void Session::publish_log() {
    if (app == nullptr) {
        return;
    }
    FieldRows model;
    const std::size_t begin = log_.size() > 40 ? log_.size() - 40 : 0;
    for (std::size_t i = begin; i < log_.size(); ++i) {
        FieldRows::Row row;
        model.put(row, "line", log_[i]);
        model.put(row, "index", std::to_string(i));
        model.add(std::move(row));
    }
    app->document().set_model("log", model);
}

void Session::log_line(const std::string& line) {
    log_.push_back(line);
    publish_log();
}

void Session::publish_panel() {
    if (app == nullptr) {
        return;
    }
    app->document().set("count-value", "text", "数量 " + std::to_string(count_));
    app->document().set("speed-value", "text", "速度 " + num1(speed_));
    app->document().set("spread-value", "text", "散布 " + num1(spread_));
    const char* ids[] = {"effect-fountain", "effect-snow", "effect-orbit"};
    const char* names[] = {"fountain", "snow", "orbit"};
    for (int i = 0; i < 3; ++i) {
        if (motion_->id() == std::string(names[i])) {
            app->document().add_class(ids[i], "is-on");
        } else {
            app->document().remove_class(ids[i], "is-on");
        }
    }
}

void Session::apply_widths() {
    if (app == nullptr) {
        return;
    }
    if (tree_ < 0) {
        tree_ = 0;
    }
    if (tree_ > 8) {
        tree_ = 8;
    }
    if (param_ < 0) {
        param_ = 0;
    }
    if (param_ > 8) {
        param_ = 8;
    }
    for (int i = 0; i <= 8; ++i) {
        const std::string tree = "tree-" + std::to_string(i);
        const std::string param = "param-" + std::to_string(i);
        if (i == tree_) {
            app->document().add_class("bench", tree);
        } else {
            app->document().remove_class("bench", tree);
        }
        if (i == param_) {
            app->document().add_class("bench", param);
        } else {
            app->document().remove_class("bench", param);
        }
    }
}

void Session::on_effect(const zui::Event& event) {
    const std::string id(event.data("id"));
    if (id != "fountain" && id != "snow" && id != "orbit") {
        return;
    }
    motion_ = make_motion(id);
    reseed();
    publish_panel();
    log_line(std::string("效果 ") + motion_->label());
}

void Session::on_count_inc() {
    if (count_ < 48) {
        ++count_;
    }
    sync_count();
    push_uniforms();
    publish_panel();
    publish_dots();
    log_line("数量 " + std::to_string(count_));
}

void Session::on_count_dec() {
    if (count_ > 4) {
        --count_;
    }
    sync_count();
    push_uniforms();
    publish_panel();
    publish_dots();
    log_line("数量 " + std::to_string(count_));
}

void Session::on_speed_inc() {
    if (speed_ < 8.f) {
        speed_ += 1.f;
    }
    push_uniforms();
    publish_panel();
    log_line("速度 " + num1(speed_));
}

void Session::on_speed_dec() {
    if (speed_ > 1.f) {
        speed_ -= 1.f;
    }
    push_uniforms();
    publish_panel();
    log_line("速度 " + num1(speed_));
}

void Session::on_spread_inc() {
    if (spread_ < 3.f) {
        spread_ += 0.4f;
    }
    push_uniforms();
    publish_panel();
    log_line("散布 " + num1(spread_));
}

void Session::on_spread_dec() {
    if (spread_ > 0.4f) {
        spread_ -= 0.4f;
    }
    push_uniforms();
    publish_panel();
    log_line("散布 " + num1(spread_));
}

void Session::on_split_down(const zui::Event& event) {
    const zui::Pointer* pointer = event.is_pointer();
    if (pointer == nullptr) {
        return;
    }
    drag_ = true;
    which_ = std::string(event.source());
    x0_ = pointer->x;
    start_ = which_ == "split-param" ? param_ : tree_;
}

void Session::on_split_move(const zui::Event& event) {
    if (!drag_) {
        return;
    }
    const zui::Pointer* pointer = event.is_pointer();
    if (pointer == nullptr) {
        return;
    }
    const int steps = static_cast<int>(std::lround((pointer->x - x0_) / 40.0));
    int next = start_ + (which_ == "split-param" ? -steps : steps);
    if (next < 0) {
        next = 0;
    }
    if (next > 8) {
        next = 8;
    }
    if (which_ == "split-param") {
        param_ = next;
    } else {
        tree_ = next;
    }
    apply_widths();
}

void Session::on_split_up(const zui::Event&) { drag_ = false; }

}  // namespace particle
