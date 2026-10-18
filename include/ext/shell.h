#ifndef ZUI_EXT_SHELL_H
#define ZUI_EXT_SHELL_H

#include <ext/platform.h>

#include <string>
#include <string_view>

namespace zui {

class ShellService {
public:
    virtual ~ShellService() = default;
    virtual bool open_uri(std::string_view uri) = 0;
    virtual std::string user_data_dir(std::string_view app_id) const = 0;
    virtual std::string cache_dir(std::string_view app_id) const = 0;
};

}  // namespace zui

#endif
