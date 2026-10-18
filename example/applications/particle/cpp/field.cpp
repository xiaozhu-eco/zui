#include "session.h"

#include "rows.h"
#include "shader.h"

#include <algorithm>

namespace particle {

void Session::publish_dots() {
    if (app == nullptr) {
        return;
    }
    FieldRows model;
    for (std::size_t i = 0; i < dots_.size(); ++i) {
        FieldRows::Row row;
        model.put(row, "index", std::to_string(i));
        model.put(row, "x", num1(dots_[i].x));
        model.put(row, "y", num1(dots_[i].y));
        model.add(std::move(row));
    }
    app->document().set_model("field", model);
}

void Session::reseed() {
    dots_.clear();
    phase_ = 0.2f;
    for (int i = 0; i < count_; ++i) {
        Dot dot;
        dot.phase = 0.35f * static_cast<float>(i);
        motion_->seed_dot(dot, i, spread_);
        dots_.push_back(dot);
    }
    publish_dots();
}

void Session::sync_count() {
    if (count_ < 4) {
        count_ = 4;
    }
    if (count_ > 48) {
        count_ = 48;
    }
    if (static_cast<int>(dots_.size()) > count_) {
        dots_.resize(static_cast<std::size_t>(count_));
    }
    while (static_cast<int>(dots_.size()) < count_) {
        Dot dot;
        const int i = static_cast<int>(dots_.size());
        dot.phase = 0.35f * static_cast<float>(i);
        dot.x = 10.f + static_cast<float>(i % 8) * 8.f;
        dot.y = 40.f;
        dots_.push_back(dot);
    }
}

void Session::step() {
    sync_count();
    if (motion_->advance_phase()) {
        phase_ += speed_ * 0.45f;
    }
    for (Dot& dot : dots_) {
        motion_->step_dot(dot, phase_, speed_, spread_);
        if (dot.x > 96.f) {
            dot.x = 96.f;
        }
        if (dot.y > 96.f) {
            dot.y = 96.f;
        }
        if (dot.x < 2.f) {
            dot.x = 2.f;
        }
        if (dot.y < 2.f) {
            dot.y = 2.f;
        }
    }
    publish_dots();
}

void Session::on_tick() { step(); }

}  // namespace particle
