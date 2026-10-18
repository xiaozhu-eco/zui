#include "image/svg/parser/length.h"

#include <cstdlib>
#include <string>

namespace zui {
namespace image {
namespace svg {

bool Length::px(std::string_view text, float& out) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    const std::string token(text);
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) {
        return false;
    }
    std::string_view rest(end);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.remove_prefix(1);
    }
    if (rest.size() >= 2 && (rest[0] == 'p' || rest[0] == 'P') && (rest[1] == 'x' || rest[1] == 'X')) {
        rest.remove_prefix(2);
    }
    if (!rest.empty()) {
        return false;
    }
    out = value;
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
