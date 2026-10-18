#ifndef ZUI_IMAGE_SVG_PARSER_PATH_DATA_H
#define ZUI_IMAGE_SVG_PARSER_PATH_DATA_H

#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace image {
namespace svg {

struct Curve {
    int kind = 1;  // 1 line, 2 quadratic, 3 cubic
    float x0 = 0.f;
    float y0 = 0.f;
    float x1 = 0.f;
    float y1 = 0.f;
    float x2 = 0.f;
    float y2 = 0.f;
    float x3 = 0.f;
    float y3 = 0.f;
};

class PathData {
public:
    std::vector<Curve> curves;

    bool parse(std::string_view d, std::string& error);
    void move_to(float x, float y);
    void line_to(float x, float y);
    void quad_to(float x1, float y1, float x, float y);
    void cubic_to(float x1, float y1, float x2, float y2, float x, float y);
    void close();

private:
    void arc_to(float rx, float ry, float angle_deg, bool large, bool sweep, float x, float y);
    float cx_ = 0.f;
    float cy_ = 0.f;
    float sx_ = 0.f;
    float sy_ = 0.f;
    float last_cx_ = 0.f;
    float last_cy_ = 0.f;
    int last_kind_ = 0;
    bool have_point_ = false;
    bool open_ = false;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
