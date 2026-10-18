#include "image/svg/parser/shape_lowerer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

void ShapeLowerer::line(PathData& path, float x1, float y1, float x2, float y2) {
    path.move_to(x1, y1);
    path.line_to(x2, y2);
}

void ShapeLowerer::ellipse(PathData& path, float cx, float cy, float rx, float ry) {
    const float k = 0.5522847498f;
    path.move_to(cx + rx, cy);
    path.cubic_to(cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry);
    path.cubic_to(cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy);
    path.cubic_to(cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry);
    path.cubic_to(cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy);
    path.close();
}

void ShapeLowerer::rect(PathData& path, float x, float y, float w, float h, float rx, float ry) {
    rx = std::max(0.f, rx);
    ry = std::max(0.f, ry);
    if (w > 0.f) {
        rx = std::min(rx, w * 0.5f);
    }
    if (h > 0.f) {
        ry = std::min(ry, h * 0.5f);
    }
    if (rx <= 0.f && ry <= 0.f) {
        path.move_to(x, y);
        path.line_to(x + w, y);
        path.line_to(x + w, y + h);
        path.line_to(x, y + h);
        path.close();
        return;
    }
    const float k = 0.5522847498f;
    path.move_to(x + rx, y);
    path.line_to(x + w - rx, y);
    path.cubic_to(x + w - rx + k * rx, y, x + w, y + ry - k * ry, x + w, y + ry);
    path.line_to(x + w, y + h - ry);
    path.cubic_to(x + w, y + h - ry + k * ry, x + w - rx + k * rx, y + h, x + w - rx, y + h);
    path.line_to(x + rx, y + h);
    path.cubic_to(x + rx - k * rx, y + h, x, y + h - ry + k * ry, x, y + h - ry);
    path.line_to(x, y + ry);
    path.cubic_to(x, y + ry - k * ry, x + rx - k * rx, y, x + rx, y);
    path.close();
}

bool ShapeLowerer::points(PathData& path, std::string_view text, bool close, std::string& error) {
    std::vector<float> n;
    const std::string token(text);
    const char* cursor = token.c_str();
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' || *cursor == ',') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor) {
            error = "svg points";
            return false;
        }
        n.push_back(value);
        cursor = end;
    }
    if (n.size() < 4 || (n.size() % 2) != 0) {
        error = "svg points";
        return false;
    }
    path.move_to(n[0], n[1]);
    for (std::size_t i = 2; i < n.size(); i += 2) {
        path.line_to(n[i], n[i + 1]);
    }
    if (close) {
        path.close();
    }
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
