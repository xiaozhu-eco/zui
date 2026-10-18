#ifndef ZUI_EXT_UPDATE_H
#define ZUI_EXT_UPDATE_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {

class UpdateService {
public:
    virtual ~UpdateService() = default;
    virtual plugin::UpdateReport apply(std::string_view manifest_path, std::string_view current_version,
                                       std::string_view dest_dir) = 0;
};

}  // namespace zui

#endif
