#include "image/svg/parser/path_data.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace zui {
namespace image {
namespace svg {

void PathData::move_to(float x, float y) {
    cx_ = x;
    cy_ = y;
    sx_ = x;
    sy_ = y;
    have_point_ = true;
    open_ = true;
    last_kind_ = 0;
}

void PathData::line_to(float x, float y) {
    if (!have_point_) {
        move_to(x, y);
        return;
    }
    Curve c;
    c.kind = 1;
    c.x0 = cx_;
    c.y0 = cy_;
    c.x1 = x;
    c.y1 = y;
    curves.push_back(c);
    cx_ = x;
    cy_ = y;
    last_kind_ = 1;
}

void PathData::quad_to(float x1, float y1, float x, float y) {
    Curve c;
    c.kind = 2;
    c.x0 = cx_;
    c.y0 = cy_;
    c.x1 = x1;
    c.y1 = y1;
    c.x2 = x;
    c.y2 = y;
    curves.push_back(c);
    last_cx_ = x1;
    last_cy_ = y1;
    cx_ = x;
    cy_ = y;
    have_point_ = true;
    open_ = true;
    last_kind_ = 2;
}

void PathData::cubic_to(float x1, float y1, float x2, float y2, float x, float y) {
    Curve c;
    c.kind = 3;
    c.x0 = cx_;
    c.y0 = cy_;
    c.x1 = x1;
    c.y1 = y1;
    c.x2 = x2;
    c.y2 = y2;
    c.x3 = x;
    c.y3 = y;
    curves.push_back(c);
    last_cx_ = x2;
    last_cy_ = y2;
    cx_ = x;
    cy_ = y;
    have_point_ = true;
    open_ = true;
    last_kind_ = 3;
}

void PathData::close() {
    if (open_ && (cx_ != sx_ || cy_ != sy_)) {
        line_to(sx_, sy_);
    }
    cx_ = sx_;
    cy_ = sy_;
    open_ = false;
    last_kind_ = 0;
}

void PathData::arc_to(float rx, float ry, float angle_deg, bool large, bool sweep, float x, float y) {
    if (!have_point_ || (rx == 0.f && ry == 0.f)) {
        line_to(x, y);
        return;
    }
    if (x == cx_ && y == cy_) {
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const float rad = angle_deg * 0.017453292519943295f;
    const float cos_a = std::cos(rad);
    const float sin_a = std::sin(rad);
    const float dx = (cx_ - x) * 0.5f;
    const float dy = (cy_ - y) * 0.5f;
    const float x1 = cos_a * dx + sin_a * dy;
    const float y1 = -sin_a * dx + cos_a * dy;
    float rx2 = rx * rx;
    float ry2 = ry * ry;
    const float x12 = x1 * x1;
    const float y12 = y1 * y1;
    const float lambda = x12 / rx2 + y12 / ry2;
    if (lambda > 1.f) {
        const float s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }
    float num = rx2 * ry2 - rx2 * y12 - ry2 * x12;
    float den = rx2 * y12 + ry2 * x12;
    if (den == 0.f) {
        line_to(x, y);
        return;
    }
    float coef = std::sqrt(std::max(0.f, num / den));
    if (large == sweep) {
        coef = -coef;
    }
    const float cxp = coef * rx * y1 / ry;
    const float cyp = coef * -ry * x1 / rx;
    const float cx = cos_a * cxp - sin_a * cyp + (cx_ + x) * 0.5f;
    const float cy = sin_a * cxp + cos_a * cyp + (cy_ + y) * 0.5f;
    auto angle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
        float ang = len == 0.f ? 0.f : std::acos(std::max(-1.f, std::min(1.f, dot / len)));
        if (ux * vy - uy * vx < 0.f) {
            ang = -ang;
        }
        return ang;
    };
    const float theta = angle(1.f, 0.f, (x1 - cxp) / rx, (y1 - cyp) / ry);
    float delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0.f) {
        delta -= 6.283185307179586f;
    } else if (sweep && delta < 0.f) {
        delta += 6.283185307179586f;
    }
    const int parts = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / 1.5707963267948966f)));
    const float step = delta / static_cast<float>(parts);
    for (int i = 0; i < parts; ++i) {
        const float t1 = theta + step * static_cast<float>(i);
        const float t2 = t1 + step;
        const float alpha = std::sin(step) * (std::sqrt(4.f + 3.f * std::pow(std::tan(step * 0.5f), 2.f)) - 1.f) / 3.f;
        const float cos1 = std::cos(t1);
        const float sin1 = std::sin(t1);
        const float cos2 = std::cos(t2);
        const float sin2 = std::sin(t2);
        const float p1x = cx + cos_a * rx * cos1 - sin_a * ry * sin1;
        const float p1y = cy + sin_a * rx * cos1 + cos_a * ry * sin1;
        const float p2x = cx + cos_a * rx * cos2 - sin_a * ry * sin2;
        const float p2y = cy + sin_a * rx * cos2 + cos_a * ry * sin2;
        const float e1x = -cos_a * rx * sin1 - sin_a * ry * cos1;
        const float e1y = -sin_a * rx * sin1 + cos_a * ry * cos1;
        const float e2x = -cos_a * rx * sin2 - sin_a * ry * cos2;
        const float e2y = -sin_a * rx * sin2 + cos_a * ry * cos2;
        cubic_to(p1x + alpha * e1x, p1y + alpha * e1y, p2x - alpha * e2x, p2y - alpha * e2y, p2x, p2y);
    }
}

bool PathData::parse(std::string_view d, std::string& error) {
    curves.clear();
    cx_ = cy_ = sx_ = sy_ = last_cx_ = last_cy_ = 0.f;
    last_kind_ = 0;
    have_point_ = false;
    open_ = false;
    std::size_t i = 0;
    auto skip = [&]() {
        while (i < d.size() && (d[i] == ' ' || d[i] == '\t' || d[i] == '\n' || d[i] == '\r' || d[i] == ',')) {
            ++i;
        }
    };
    auto is_num = [&]() {
        skip();
        if (i >= d.size()) {
            return false;
        }
        const char c = d[i];
        return c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9');
    };
    auto num = [&](float& out) {
        skip();
        if (i >= d.size()) {
            return false;
        }
        const std::string token(d.substr(i));
        char* end = nullptr;
        out = std::strtof(token.c_str(), &end);
        if (end == token.c_str()) {
            return false;
        }
        i += static_cast<std::size_t>(end - token.c_str());
        return true;
    };
    auto flag = [&](bool& out) {
        skip();
        if (i >= d.size() || (d[i] != '0' && d[i] != '1')) {
            return false;
        }
        out = d[i] == '1';
        ++i;
        return true;
    };
    char cmd = 0;
    while (true) {
        skip();
        if (i >= d.size()) {
            return true;
        }
        char next = d[i];
        if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
            cmd = next;
            ++i;
        } else if (cmd == 0) {
            error = "svg path command missing";
            return false;
        }
        const bool rel = cmd >= 'a' && cmd <= 'z';
        const char op = rel ? static_cast<char>(cmd - 'a' + 'A') : cmd;
        if (op == 'M') {
            float x = 0.f, y = 0.f;
            if (!num(x) || !num(y)) {
                error = "svg path moveto";
                return false;
            }
            if (rel && have_point_) {
                x += cx_;
                y += cy_;
            }
            move_to(x, y);
            while (is_num()) {
                if (!num(x) || !num(y)) {
                    error = "svg path moveto";
                    return false;
                }
                if (rel) {
                    x += cx_;
                    y += cy_;
                }
                line_to(x, y);
            }
        } else if (op == 'Z') {
            close();
        } else if (op == 'L') {
            while (is_num()) {
                float x = 0.f, y = 0.f;
                if (!num(x) || !num(y)) {
                    error = "svg path lineto";
                    return false;
                }
                if (rel) {
                    x += cx_;
                    y += cy_;
                }
                line_to(x, y);
            }
        } else if (op == 'H') {
            while (is_num()) {
                float x = 0.f;
                if (!num(x)) {
                    error = "svg path hlineto";
                    return false;
                }
                line_to(rel ? cx_ + x : x, cy_);
            }
        } else if (op == 'V') {
            while (is_num()) {
                float y = 0.f;
                if (!num(y)) {
                    error = "svg path vlineto";
                    return false;
                }
                line_to(cx_, rel ? cy_ + y : y);
            }
        } else if (op == 'C') {
            while (is_num()) {
                float x1, y1, x2, y2, x, y;
                if (!num(x1) || !num(y1) || !num(x2) || !num(y2) || !num(x) || !num(y)) {
                    error = "svg path curveto";
                    return false;
                }
                if (rel) {
                    x1 += cx_;
                    y1 += cy_;
                    x2 += cx_;
                    y2 += cy_;
                    x += cx_;
                    y += cy_;
                }
                cubic_to(x1, y1, x2, y2, x, y);
            }
        } else if (op == 'S') {
            while (is_num()) {
                float x2, y2, x, y;
                if (!num(x2) || !num(y2) || !num(x) || !num(y)) {
                    error = "svg path smooth curveto";
                    return false;
                }
                if (rel) {
                    x2 += cx_;
                    y2 += cy_;
                    x += cx_;
                    y += cy_;
                }
                float x1 = cx_;
                float y1 = cy_;
                if (last_kind_ == 3) {
                    x1 = 2.f * cx_ - last_cx_;
                    y1 = 2.f * cy_ - last_cy_;
                }
                cubic_to(x1, y1, x2, y2, x, y);
            }
        } else if (op == 'Q') {
            while (is_num()) {
                float x1, y1, x, y;
                if (!num(x1) || !num(y1) || !num(x) || !num(y)) {
                    error = "svg path quadratic";
                    return false;
                }
                if (rel) {
                    x1 += cx_;
                    y1 += cy_;
                    x += cx_;
                    y += cy_;
                }
                quad_to(x1, y1, x, y);
            }
        } else if (op == 'T') {
            while (is_num()) {
                float x, y;
                if (!num(x) || !num(y)) {
                    error = "svg path smooth quadratic";
                    return false;
                }
                if (rel) {
                    x += cx_;
                    y += cy_;
                }
                float x1 = cx_;
                float y1 = cy_;
                if (last_kind_ == 2) {
                    x1 = 2.f * cx_ - last_cx_;
                    y1 = 2.f * cy_ - last_cy_;
                }
                quad_to(x1, y1, x, y);
            }
        } else if (op == 'A') {
            while (is_num()) {
                float rx, ry, angle, x, y;
                bool large = false, sweep = false;
                if (!num(rx) || !num(ry) || !num(angle) || !flag(large) || !flag(sweep) || !num(x) ||
                    !num(y)) {
                    error = "svg path arc";
                    return false;
                }
                if (rel) {
                    x += cx_;
                    y += cy_;
                }
                arc_to(rx, ry, angle, large, sweep, x, y);
            }
        } else {
            error = "svg path command";
            return false;
        }
    }
}

}  // namespace svg
}  // namespace image
}  // namespace zui
