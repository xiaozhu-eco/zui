#include "session.h"

#include "shader.h"

namespace particle {

Session::Session() : motion_(make_motion("fountain")) {}

void Session::show_shader() {
    if (app == nullptr) {
        return;
    }
    quiet_ = true;
    app->document().set("shader-source", "value", source_);
    quiet_ = false;
}

void Session::pull_uniforms() {
    float count = static_cast<float>(count_);
    float speed = speed_;
    float spread = spread_;
    const bool has_count = read_define(source_, "U_COUNT", count);
    const bool has_speed = read_define(source_, "U_SPEED", speed);
    const bool has_spread = read_define(source_, "U_SPREAD", spread);
    if (has_count) {
        count_ = static_cast<int>(count + 0.5f);
    }
    if (has_speed && speed > 0.f) {
        speed_ = speed;
    }
    if (has_spread && spread > 0.f) {
        spread_ = spread;
    }
    sync_count();
    publish_panel();
    publish_dots();
}

void Session::push_uniforms() {
    write_define(source_, "U_COUNT", static_cast<float>(count_));
    write_define(source_, "U_SPEED", speed_);
    write_define(source_, "U_SPREAD", spread_);
    show_shader();
}

void Session::on_shader_input(const zui::Event& event) {
    if (quiet_) {
        return;
    }
    source_ = std::string(event.value());
    pull_uniforms();
}

void Session::on_shader_import() {
    if (app == nullptr) {
        return;
    }
    const std::string text = trim(app->document().get("shader-path", "value"));
    if (text.empty()) {
        log_line("导入失败：没有路径");
        return;
    }
    const std::string bytes = read_file(std::filesystem::path(text));
    if (bytes.empty()) {
        log_line("导入失败：读不到文件");
        return;
    }
    source_ = bytes;
    pull_uniforms();
    reseed();
    show_shader();
    log_line("导入着色器");
}

void Session::on_shader_export() {
    if (app == nullptr) {
        return;
    }
    const std::string text = trim(app->document().get("shader-out", "value"));
    if (text.empty()) {
        log_line("导出失败：没有路径");
        return;
    }
    if (!write_file(std::filesystem::path(text), source_)) {
        log_line("导出失败：写不了文件");
        return;
    }
    log_line("导出着色器");
}

void Session::bind() {
    if (app == nullptr) {
        return;
    }
    zui::Document& doc = app->document();
    doc.bind("on_effect", [this](zui::Event& event) { on_effect(event); });
    doc.bind("on_count_inc", [this](zui::Event&) { on_count_inc(); });
    doc.bind("on_count_dec", [this](zui::Event&) { on_count_dec(); });
    doc.bind("on_speed_inc", [this](zui::Event&) { on_speed_inc(); });
    doc.bind("on_speed_dec", [this](zui::Event&) { on_speed_dec(); });
    doc.bind("on_spread_inc", [this](zui::Event&) { on_spread_inc(); });
    doc.bind("on_spread_dec", [this](zui::Event&) { on_spread_dec(); });
    doc.bind("on_split_down", [this](zui::Event& event) { on_split_down(event); });
    doc.bind("on_split_move", [this](zui::Event& event) { on_split_move(event); });
    doc.bind("on_split_up", [this](zui::Event& event) { on_split_up(event); });
    doc.bind("on_shader_input", [this](zui::Event& event) { on_shader_input(event); });
    doc.bind("on_shader_import", [this](zui::Event&) { on_shader_import(); });
    doc.bind("on_shader_export", [this](zui::Event&) { on_shader_export(); });
}

void Session::stop() {
    if (app != nullptr && timer_ != 0) {
        app->document().clear_timer(timer_);
        timer_ = 0;
    }
}

void Session::show() {
    dots_.clear();
    log_.clear();
    motion_ = make_motion("fountain");
    count_ = 12;
    speed_ = 1.f;
    spread_ = 0.6f;
    phase_ = 0.f;
    tree_ = 3;
    param_ = 3;
    drag_ = false;
    source_ = default_shader();
    pull_uniforms();
    reseed();
    apply_widths();
    publish_panel();
    show_shader();
    log_line("工作台就绪");
    if (app != nullptr) {
        stop();
        timer_ = app->document().set_interval([this] { on_tick(); }, 0);
    }
}

}  // namespace particle
