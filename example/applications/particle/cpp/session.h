#ifndef PARTICLE_SESSION_H
#define PARTICLE_SESSION_H

#include "motion.h"

#include <zui/app.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace particle {

class Session {
public:
    Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    zui::App* app = nullptr;
    std::filesystem::path dir;

    void bind();
    void show();
    void stop();

private:
    void publish_log();
    void log_line(const std::string& line);
    void publish_dots();
    void publish_panel();
    void show_shader();
    void reseed();
    void sync_count();
    void step();
    void pull_uniforms();
    void push_uniforms();
    void apply_widths();
    void on_tick();
    void on_effect(const zui::Event& event);
    void on_count_inc();
    void on_count_dec();
    void on_speed_inc();
    void on_speed_dec();
    void on_spread_inc();
    void on_spread_dec();
    void on_split_down(const zui::Event& event);
    void on_split_move(const zui::Event& event);
    void on_split_up(const zui::Event&);
    void on_shader_input(const zui::Event& event);
    void on_shader_import();
    void on_shader_export();

    std::vector<Dot> dots_;
    std::vector<std::string> log_;
    std::unique_ptr<Motion> motion_;
    std::string source_;
    int count_ = 12;
    float speed_ = 1.f;
    float spread_ = 0.6f;
    float phase_ = 0.f;
    int tree_ = 3;
    int param_ = 3;
    bool drag_ = false;
    std::string which_;
    float x0_ = 0.f;
    int start_ = 3;
    zui::TimerId timer_ = 0;
    bool quiet_ = false;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }
inline void stop(Session& session) { session.stop(); }

}  // namespace particle

#endif
