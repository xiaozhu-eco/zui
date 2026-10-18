#ifndef ZUI_IMAGE_SVG_PARSER_PARSER_H
#define ZUI_IMAGE_SVG_PARSER_PARSER_H

#include "image/svg/parser/document.h"

#include <string>
#include <string_view>

namespace zui {
namespace image {
namespace svg {

class Parser {
public:
    bool parse(std::string_view xml, Document& out, std::string& error) const;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
