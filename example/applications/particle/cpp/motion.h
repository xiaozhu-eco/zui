#ifndef PARTICLE_MOTION_H
#define PARTICLE_MOTION_H

#include <memory>
#include <string>

namespace particle {

struct Dot {
    float x = 50.f;
    float y = 50.f;
    float phase = 0.f;
};

/// Fountain, snow, and orbit place and move a dot differently. Clamping stays outside.
class Motion {
public:
    virtual ~Motion() = default;
    virtual const char* id() const = 0;
    virtual const char* label() const = 0;
    virtual void seed_dot(Dot& dot, int index, float spread) const = 0;
    virtual void step_dot(Dot& dot, float phase, float speed, float spread) const = 0;
    /// Orbit advances the shared phase before each step. Fountain and snow do not.
    virtual bool advance_phase() const = 0;
};

std::unique_ptr<Motion> make_motion(const std::string& id);

}  // namespace particle

#endif
