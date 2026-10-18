#include "plugins/tray/item.h"

namespace zui {
namespace tray_detail {

Item prepare(std::string_view tip) {
    Item item;
    item.tip.assign(tip.begin(), tip.end());
    if (item.tip.empty()) {
        item.reason = "tray tip is empty";
        return item;
    }
    item.id = "zui";
    for (unsigned char c : item.tip) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        item.id.push_back(ok ? static_cast<char>(c) : '-');
    }
    item.ready = true;
    return item;
}

}  // namespace tray_detail
}  // namespace zui
