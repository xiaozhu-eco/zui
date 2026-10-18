#ifndef ZUI_PLUGIN_ASSOCIATION_TEXT_H
#define ZUI_PLUGIN_ASSOCIATION_TEXT_H

#include <string>
#include <string_view>

namespace zui {
namespace association_detail {

class AssociationText {
public:
    static std::string ios_plist(std::string_view extension, std::string_view mime, std::string_view scheme);
    static std::string android_manifest(std::string_view extension, std::string_view mime, std::string_view scheme);
};

}  // namespace association_detail
}  // namespace zui

#endif
