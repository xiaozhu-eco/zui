#include "image/svg/parser/transform.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == ',' ||
                              text.front() == '\n' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    return text;
}

bool read_numbers(std::string_view body, std::vector<float>& out) {
    const std::string token(body);
    const char* cursor = token.c_str();
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == '\n' ||
               *cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor) {
            return false;
        }
        out.push_back(value);
        cursor = end;
    }
    return true;
}

Transform translate(float x, float y) {
    Transform t;
    t.e = x;
    t.f = y;
    return t;
}

Transform scale(float x, float y) {
    Transform t;
    t.a = x;
    t.d = y;
    return t;
}

Transform rotate(float degrees) {
    const float rad = degrees * 0.017453292519943295f;
    Transform t;
    t.a = std::cos(rad);
    t.b = std::sin(rad);
    t.c = -std::sin(rad);
    t.d = std::cos(rad);
    return t;
}

}  // namespace

Transform Transform::mul(const Transform& right) const {
    Transform o;
    o.a = a * right.a + c * right.b;
    o.b = b * right.a + d * right.b;
    o.c = a * right.c + c * right.d;
    o.d = b * right.c + d * right.d;
    o.e = a * right.e + c * right.f + e;
    o.f = b * right.e + d * right.f + f;
    return o;
}

void Transform::apply(float x, float y, float& ox, float& oy) const {
    ox = a * x + c * y + e;
    oy = b * x + d * y + f;
}

bool Transform::parse(std::string_view text, Transform& out) {
    out = Transform{};
    text = trim(text);
    if (text.empty()) {
        return true;
    }
    while (!text.empty()) {
        text = trim(text);
        const std::size_t paren = text.find('(');
        if (paren == std::string_view::npos) {
            return false;
        }
        std::string_view name = text.substr(0, paren);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.remove_suffix(1);
        }
        const std::size_t close = text.find(')', paren);
        if (close == std::string_view::npos) {
            return false;
        }
        std::vector<float> n;
        if (!read_numbers(text.substr(paren + 1, close - paren - 1), n)) {
            return false;
        }
        Transform step;
        if (name == "matrix") {
            if (n.size() != 6) {
                return false;
            }
            step.a = n[0];
            step.b = n[1];
            step.c = n[2];
            step.d = n[3];
            step.e = n[4];
            step.f = n[5];
        } else if (name == "translate") {
            if (n.empty() || n.size() > 2) {
                return false;
            }
            step = translate(n[0], n.size() > 1 ? n[1] : 0.f);
        } else if (name == "scale") {
            if (n.empty() || n.size() > 2) {
                return false;
            }
            step = scale(n[0], n.size() > 1 ? n[1] : n[0]);
        } else if (name == "rotate") {
            if (n.empty() || (n.size() != 1 && n.size() != 3)) {
                return false;
            }
            if (n.size() == 1) {
                step = rotate(n[0]);
            } else {
                step = translate(n[1], n[2]).mul(rotate(n[0])).mul(translate(-n[1], -n[2]));
            }
        } else if (name == "skewX" || name == "skewY") {
            if (n.size() != 1) {
                return false;
            }
            const float t = std::tan(n[0] * 0.017453292519943295f);
            step = Transform{};
            if (name == "skewX") {
                step.c = t;
            } else {
                step.b = t;
            }
        } else {
            return false;
        }
        out = out.mul(step);
        text.remove_prefix(close + 1);
    }
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
