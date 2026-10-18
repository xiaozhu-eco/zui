#ifndef ZUI_EXT_OPENER_H
#define ZUI_EXT_OPENER_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {

/// Choosable opener. Query and unregister take the same `OpenerSpec` registration used.
class OpenerService {
public:
    virtual ~OpenerService() = default;
    virtual plugin::OpenerReport register_opener(const plugin::OpenerSpec& spec) = 0;
    virtual plugin::OpenerReport query_opener(const plugin::OpenerSpec& spec) = 0;
    virtual plugin::OpenerReport unregister_opener(const plugin::OpenerSpec& spec) = 0;
    virtual plugin::OpenerReport launch_opener(const plugin::OpenerSpec& spec,
                                               std::string_view file_path) = 0;
};

}  // namespace zui

#endif
