#ifndef ZUI_IMAGE_SVG_PARSER_XML_SCANNER_H
#define ZUI_IMAGE_SVG_PARSER_XML_SCANNER_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace image {
namespace svg {

class XmlScanner {
public:
    enum class Kind { End, Open, Close, Empty, Error };

    explicit XmlScanner(std::string_view xml);

    Kind next();
    std::string_view element() const { return element_; }
    bool has(std::string_view key) const;
    std::string_view attr(std::string_view key) const;
    const std::string& error() const { return error_; }

private:
    std::string_view xml_;
    std::size_t i_ = 0;
    std::string element_;
    std::string error_;
    std::vector<std::pair<std::string, std::string>> attrs_;

    void skip_ws();
    bool skip_comment();
    bool skip_until(char end);
    bool read_name(std::string& out);
    bool read_attr_value(std::string& out);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
