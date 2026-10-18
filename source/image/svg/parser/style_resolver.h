#ifndef ZUI_IMAGE_SVG_PARSER_STYLE_RESOLVER_H
#define ZUI_IMAGE_SVG_PARSER_STYLE_RESOLVER_H

#include "image/svg/parser/paint.h"

#include <string_view>

namespace zui {
namespace image {
namespace svg {

class XmlScanner;

class StyleResolver {
public:
    Paint fill{};
    Paint stroke{};
    float fill_opacity = 1.f;
    float stroke_opacity = 1.f;
    float stroke_width = 1.f;
    int fill_rule = 0;

    static StyleResolver initial();
    StyleResolver applied(const XmlScanner& tag) const;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
