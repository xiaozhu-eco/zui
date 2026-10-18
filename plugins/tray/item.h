#ifndef ZUI_PLUGIN_TRAY_ITEM_H
#define ZUI_PLUGIN_TRAY_ITEM_H

// Tray identity from the caller's tip. Platform files use this so an empty tip
// cannot report success and two tips stay distinct.

#include <string>
#include <string_view>

namespace zui {
namespace tray_detail {

struct Item {
    bool ready = false;
    std::string tip;
    std::string id;
    std::string reason;
};

Item prepare(std::string_view tip);

}  // namespace tray_detail
}  // namespace zui

#endif
