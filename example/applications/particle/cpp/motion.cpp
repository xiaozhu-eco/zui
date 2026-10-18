#include "motion.h"

#include <cmath>

namespace particle {
namespace {

class Fountain : public Motion {
public:
    const char* id() const override { return "fountain"; }
    const char* label() const override { return "喷泉"; }
    bool advance_phase() const override { return false; }

    void seed_dot(Dot& dot, int index, float) const override {
        dot.x = 18.f + static_cast<float>(index % 8) * 7.f;
        dot.y = 72.f - static_cast<float>(index % 3) * 4.f;
    }

    void step_dot(Dot& dot, float, float speed, float spread) const override {
        dot.y -= speed * 8.f;
        dot.x += spread * 6.f;
    }
};

class Snow : public Motion {
public:
    const char* id() const override { return "snow"; }
    const char* label() const override { return "飘雪"; }
    bool advance_phase() const override { return false; }

    void seed_dot(Dot& dot, int index, float) const override {
        dot.x = 12.f + static_cast<float>(index % 8) * 9.f;
        dot.y = 8.f + static_cast<float>(index % 4) * 4.f;
    }

    void step_dot(Dot& dot, float, float speed, float spread) const override {
        dot.y += speed * 8.f;
        dot.x += spread * 6.f;
    }
};

class Orbit : public Motion {
public:
    const char* id() const override { return "orbit"; }
    const char* label() const override { return "环绕"; }
    bool advance_phase() const override { return true; }

    void seed_dot(Dot& dot, int, float spread) const override {
        const float angle = dot.phase;
        dot.x = 50.f + std::cos(angle) * (16.f + spread * 8.f);
        dot.y = 50.f + std::sin(angle) * (16.f + spread * 8.f);
    }

    void step_dot(Dot& dot, float phase, float, float spread) const override {
        const float angle = phase + dot.phase;
        dot.x = 50.f + std::cos(angle) * (16.f + spread * 10.f);
        dot.y = 50.f + std::sin(angle) * (16.f + spread * 10.f);
    }
};

}  // namespace

std::unique_ptr<Motion> make_motion(const std::string& id) {
    if (id == "snow") {
        return std::make_unique<Snow>();
    }
    if (id == "orbit") {
        return std::make_unique<Orbit>();
    }
    return std::make_unique<Fountain>();
}

}  // namespace particle
