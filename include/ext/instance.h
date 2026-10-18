#ifndef ZUI_EXT_INSTANCE_H
#define ZUI_EXT_INSTANCE_H

#include <string>
#include <string_view>

namespace zui {

class InstanceService {
public:
    virtual ~InstanceService() = default;
    virtual bool claim(std::string_view identity) = 0;
    virtual bool signal_primary(std::string_view identity, std::string_view args) = 0;
    virtual std::string take_args(std::string_view identity) = 0;
};

}  // namespace zui

#endif
