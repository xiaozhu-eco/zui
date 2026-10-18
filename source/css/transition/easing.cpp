#include "css/transition/internal.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace css {
namespace transition_detail {
Easing bezier_ease(float x1, float y1, float x2, float y2) {
    Easing e;
    e.kind = EasingKind::Bezier;
    e.x1 = x1;
    e.y1 = y1;
    e.x2 = x2;
    e.y2 = y2;
    return e;
}

Easing steps_ease(int n, StepPos pos) {
    Easing e;
    e.kind = EasingKind::Steps;
    e.steps = n;
    e.step_pos = pos;
    return e;
}

StepPos parse_step_pos(std::string_view s) {
    if (s == "jump-start" || s == "start") {
        return StepPos::JumpStart;
    }
    if (s == "jump-none") {
        return StepPos::JumpNone;
    }
    if (s == "jump-both") {
        return StepPos::JumpBoth;
    }
    return StepPos::JumpEnd;  // jump-end / end / omitted
}

bool is_easing_token(std::string_view ls) {
    return ls == "linear" || ls.rfind("ease", 0) == 0 || ls.rfind("cubic-bezier", 0) == 0 ||
           ls == "step-start" || ls == "step-end" || ls.rfind("steps(", 0) == 0;
}

Easing parse_easing(std::string_view n) {
    std::string s = lower(trim(n));
    if (s == "linear") {
        return bezier_ease(0, 0, 1, 1);
    }
    if (s == "ease-in") {
        return bezier_ease(0.42f, 0, 1, 1);
    }
    if (s == "ease-out") {
        return bezier_ease(0, 0, 0.58f, 1);
    }
    if (s == "ease-in-out") {
        return bezier_ease(0.42f, 0, 0.58f, 1);
    }
    if (s == "step-start") {
        return steps_ease(1, StepPos::JumpStart);
    }
    if (s == "step-end") {
        return steps_ease(1, StepPos::JumpEnd);
    }
    if (s.rfind("steps(", 0) == 0) {
        const auto lp = s.find('(');
        const auto rp = s.rfind(')');
        if (lp != std::string::npos && rp > lp) {
            std::string inner = s.substr(lp + 1, rp - lp - 1);
            // steps(<integer> [, <step-position>]?)
            int n = 0;
            StepPos pos = StepPos::JumpEnd;
            size_t i = 0;
            while (i < inner.size() && std::isspace(static_cast<unsigned char>(inner[i]))) {
                ++i;
            }
            char* end = nullptr;
            const long steps = std::strtol(inner.c_str() + static_cast<int>(i), &end, 10);
            if (end != inner.c_str() + static_cast<int>(i)) {
                n = static_cast<int>(steps);
                i = static_cast<size_t>(end - inner.c_str());
                while (i < inner.size() &&
                       (inner[i] == ',' || std::isspace(static_cast<unsigned char>(inner[i])))) {
                    ++i;
                }
                if (i < inner.size()) {
                    pos = parse_step_pos(trim(std::string_view(inner).substr(i)));
                }
                // CSS Easing 1: steps > 0; jump-none requires steps > 1.
                if (n > 0 && !(pos == StepPos::JumpNone && n < 2)) {
                    return steps_ease(n, pos);
                }
            }
        }
        return bezier_ease(0.25f, 0.1f, 0.25f, 1);  // invalid steps() → ease
    }
    if (s.rfind("cubic-bezier", 0) == 0) {
        auto lp = s.find('(');
        auto rp = s.rfind(')');
        if (lp != std::string::npos && rp > lp) {
            std::string inner = s.substr(lp + 1, rp - lp - 1);
            float v[4] = {0, 0, 1, 1};
            int nvals = 0;
            size_t i = 0;
            while (i < inner.size() && nvals < 4) {
                while (i < inner.size() &&
                       (inner[i] == ',' || std::isspace(static_cast<unsigned char>(inner[i])))) {
                    ++i;
                }
                if (i >= inner.size()) {
                    break;
                }
                v[nvals++] = std::strtof(inner.c_str() + static_cast<int>(i), nullptr);
                while (i < inner.size() && inner[i] != ',') {
                    ++i;
                }
            }
            return bezier_ease(v[0], v[1], v[2], v[3]);
        }
    }
    return bezier_ease(0.25f, 0.1f, 0.25f, 1);  // ease
}

float bezier_x(float u, float x1, float x2) {
    const float mt = 1.0f - u;
    return 3.0f * mt * mt * u * x1 + 3.0f * mt * u * u * x2 + u * u * u;
}

float bezier_dx(float u, float x1, float x2) {
    const float mt = 1.0f - u;
    return 3.0f * mt * mt * x1 + 6.0f * mt * u * (x2 - x1) + 3.0f * u * u * (1.0f - x2);
}

float sample_bezier_curve(float t, float x1, float y1, float x2, float y2) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (x1 == y1 && x2 == y2) {
        return t;
    }
    float u = t;
    for (int i = 0; i < 8; ++i) {
        const float x = bezier_x(u, x1, x2) - t;
        const float dx = bezier_dx(u, x1, x2);
        if (std::fabs(dx) < 1e-6f) {
            break;
        }
        u = std::max(0.0f, std::min(1.0f, u - x / dx));
        if (std::fabs(x) < 1e-5f) {
            break;
        }
    }
    const float mt = 1.0f - u;
    return 3.0f * mt * mt * u * y1 + 3.0f * mt * u * u * y2 + u * u * u;
}

// CSS Easing 1 §2.3.1. Transitions sample the active interval with before_flag = false.
float sample_steps(float t, int steps, StepPos pos, bool before_flag) {
    t = std::max(0.0f, std::min(1.0f, t));
    int current = static_cast<int>(std::floor(static_cast<double>(t) * static_cast<double>(steps)));
    if (pos == StepPos::JumpStart || pos == StepPos::JumpBoth) {
        ++current;
    }
    if (before_flag) {
        const double scaled = static_cast<double>(t) * static_cast<double>(steps);
        if (std::fabs(scaled - std::round(scaled)) < 1e-9) {
            --current;
        }
    }
    if (t >= 0.f && current < 0) {
        current = 0;
    }
    int jumps = steps;
    if (pos == StepPos::JumpNone) {
        jumps = steps - 1;
    } else if (pos == StepPos::JumpBoth) {
        jumps = steps + 1;
    }
    if (t <= 1.f && current > jumps) {
        current = jumps;
    }
    if (jumps <= 0) {
        return t;
    }
    return static_cast<float>(current) / static_cast<float>(jumps);
}

float sample_easing(float t, const Easing& e) {
    if (e.kind == EasingKind::Steps) {
        return sample_steps(t, e.steps, e.step_pos, false);
    }
    return sample_bezier_curve(t, e.x1, e.y1, e.x2, e.y2);
}


}  // namespace transition_detail
}  // namespace css
}  // namespace zui
